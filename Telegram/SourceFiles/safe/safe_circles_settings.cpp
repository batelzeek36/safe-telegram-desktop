/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_settings.h"

#include "safe/safe_calm_files.h"
#include "safe/safe_calm_leaf.h"
#include "safe/safe_circles_paint.h"
#include "settings/settings_common.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include "styles/style_layers.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Safe::Circles {
namespace {

const auto kFileName = u"safe_circles.json"_q;
constexpr auto kGlyphSize = 30;

struct Store {
	bool loaded = false;
	rpl::variable<Layout> layout = Layout::List;
};

[[nodiscard]] Store &Instance() {
	static auto result = Store();
	return result;
}

void Load(Store &store) {
	if (store.loaded) {
		return;
	}
	store.loaded = true;
	const auto bytes = Calm::ReadPrivateFile(kFileName);
	if (bytes.isEmpty()) {
		return;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Safe Circles: %1 did not parse, the list.").arg(kFileName));
		return;
	}
	const auto value = document.object().value(u"layout"_q);
	if (!value.isString()) {
		return;
	}
	const auto word = value.toString().toStdString();
	store.layout = ParseLayout(word);
}

void Save(Layout layout) {
	auto root = QJsonObject();
	root.insert(
		u"layout"_q,
		QString::fromUtf8(LayoutKey(layout).data(), LayoutKey(layout).size()));
	Calm::WritePrivateFile(
		kFileName,
		QJsonDocument(root).toJson(QJsonDocument::Compact));
}

[[nodiscard]] std::array<Layout, 3> AllLayouts() {
	return { Layout::List, Layout::Crown, Layout::Orbit };
}

// A small drawing of each layout, in the row's own text color with the
// focused chat in jade: three rows, a ring round a hub, a tilted orbit.
void PaintGlyph(QPainter &p, QRectF rect, Layout layout, QColor fg) {
	auto hq = PainterHighQualityEnabler(p);
	const auto jade = Calm::JadeColor();
	const auto c = rect.center();
	const auto s = rect.width();
	auto line = QPen(fg, s * 0.06, Qt::SolidLine, Qt::RoundCap);
	const auto dot = [&](QPointF center, double r, QColor color) {
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawEllipse(center, r, r);
	};
	switch (layout) {
	case Layout::List: {
		for (auto i = 0; i != 3; ++i) {
			const auto y = c.y() + (i - 1) * s * 0.3;
			dot(QPointF(c.x() - s * 0.3, y), s * 0.1, i ? fg : jade);
			p.setPen(line);
			p.drawLine(
				QPointF(c.x() - s * 0.12, y),
				QPointF(c.x() + s * 0.42, y));
		}
	} break;
	case Layout::Crown: {
		p.setPen(QPen(fg, s * 0.04));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(c, s * 0.18, s * 0.18);
		for (auto i = 0; i != 8; ++i) {
			const auto a = (-90. + i * 45.) * M_PI / 180.;
			const auto at = QPointF(
				c.x() + std::cos(a) * s * 0.38,
				c.y() + std::sin(a) * s * 0.38);
			dot(at, i ? s * 0.07 : s * 0.1, i ? fg : jade);
		}
	} break;
	case Layout::Orbit: {
		p.setPen(QPen(fg, s * 0.04));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(c, s * 0.42, s * 0.2);
		Calm::PaintLeaf(
			p,
			QRectF(c.x() - s * 0.13, c.y() - s * 0.2, s * 0.26, s * 0.26),
			s * 0.26,
			jade);
		dot(QPointF(c.x(), c.y() + s * 0.2), s * 0.12, jade);
		dot(QPointF(c.x() - s * 0.36, c.y() + s * 0.1), s * 0.08, fg);
		dot(QPointF(c.x() + s * 0.36, c.y() + s * 0.1), s * 0.08, fg);
		dot(QPointF(c.x() - s * 0.2, c.y() - s * 0.18), s * 0.05, fg);
		dot(QPointF(c.x() + s * 0.2, c.y() - s * 0.18), s * 0.05, fg);
	} break;
	}
}

// The glyphs stand in one column at the section's right edge, as far in
// from it as the radio buttons are from the left.
void AddGlyph(
		not_null<Ui::RpWidget*> container,
		not_null<Ui::RpWidget*> row,
		Layout layout) {
	const auto glyph = Ui::CreateChild<Ui::RpWidget>(container.get());
	const auto size = style::ConvertScale(kGlyphSize);
	glyph->resize(size, size);
	glyph->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		row->geometryValue(),
		container->widthValue()
	) | rpl::on_next([=](QRect geometry, int width) {
		glyph->move(
			width - st::settingsSendTypePadding.left() - size,
			geometry.y() + (geometry.height() - size) / 2);
		glyph->raise();
	}, glyph->lifetime());
	glyph->paintOn([=](QPainter &p) {
		auto color = st::windowSubTextFg->c;
		PaintGlyph(p, QRectF(glyph->rect()), layout, color);
	});
}

} // namespace

Layout CurrentLayout() {
	auto &store = Instance();
	Load(store);
	return store.layout.current();
}

rpl::producer<Layout> LayoutValue() {
	auto &store = Instance();
	Load(store);
	return store.layout.value();
}

void SetLayout(Layout layout) {
	auto &store = Instance();
	Load(store);
	if (store.layout.current() == layout) {
		return;
	}
	store.layout = layout;
	Save(layout);
}

QString LayoutName(Layout layout) {
	switch (layout) {
	case Layout::List: return u"List"_q;
	case Layout::Crown: return u"Crown"_q;
	case Layout::Orbit: return u"Orbit"_q;
	}
	return u"List"_q;
}

QString LayoutAbout(Layout layout) {
	switch (layout) {
	case Layout::List: return u"Your chats in a column, newest first."_q;
	case Layout::Crown: return u"A ring of faces round the chat in focus."_q;
	case Layout::Orbit: return u"Your chats circle the leaf; the front one is in focus."_q;
	}
	return QString();
}

namespace {

// A hairline round the popup in the circles' raised-surface edge color: it
// opens over the main menu, both paint the menu background, and a dark
// theme's shadow barely shows, so without it the two run together.
void AddPopupEdge(not_null<Ui::PopupMenu*> popup) {
	const auto edge = Ui::CreateChild<Ui::RpWidget>(popup.get());
	edge->setAttribute(Qt::WA_TransparentForMouseEvents);
	popup->sizeValue(
	) | rpl::on_next([=](QSize size) {
		edge->setGeometry(QRect(QPoint(), size));
	}, edge->lifetime());
	edge->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(edge);
		auto hq = PainterHighQualityEnabler(p);
		const auto half = st::lineWidth / 2.;
		const auto rect = QRectF(popup->inner()).marginsRemoved(
			{ half, half, half, half });
		const auto radius = st::popupMenuWithIcons.radius;
		p.setPen(QPen(SurfaceEdgeColor(), st::lineWidth));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(rect, radius, radius);
	}, edge->lifetime());
	edge->show();
	edge->raise();
}

} // namespace

void AddMainMenuRow(
		not_null<Ui::VerticalLayout*> menu,
		not_null<Window::SessionController*> controller) {
	const auto button = Settings::AddButtonWithLabel(
		menu,
		rpl::single(u"Chat list"_q),
		LayoutValue() | rpl::map(LayoutName),
		st::mainMenuButton,
		{ &st::menuIconChats });
	const auto popup = button->lifetime().make_state<
		base::unique_qptr<Ui::PopupMenu>>();
	button->setClickedCallback([=] {
		*popup = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		const auto current = CurrentLayout();
		for (const auto layout : AllLayouts()) {
			(*popup)->addAction(LayoutName(layout), [=] {
				// The main menu (and this popup) closes; not from inside it.
				crl::on_main(controller, [=] {
					SetLayout(layout);
					controller->window().hideSettingsAndLayer();
				});
			}, (layout == current) ? &st::mediaPlayerMenuCheck : nullptr);
		}
		AddPopupEdge(popup->get());
		// Upward from the row's value, so it opens over the menu, inside
		// the window, not hanging below it.
		(*popup)->setForcedOrigin(Ui::PanelAnimation::Origin::BottomRight);
		const auto at = button->mapToGlobal(QPoint(
			button->width() - st::mainMenuButton.padding.right(),
			0));
		(*popup)->popup(at);
	});
}

void SetupSettingsSection(
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container) {
	Ui::AddDivider(container);
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(u"Chat list"_q));

	const auto group = std::make_shared<Ui::RadioenumGroup<Layout>>(
		CurrentLayout());
	group->setChangedCallback([=](Layout value) {
		SetLayout(value);
	});
	LayoutValue() | rpl::on_next([=](Layout value) {
		if (group->current() != value) {
			group->setValue(value);
		}
	}, container->lifetime());

	auto rows = std::vector<std::pair<not_null<Ui::RpWidget*>, Layout>>();
	for (const auto layout : AllLayouts()) {
		const auto row = container->add(
			object_ptr<Ui::Radioenum<Layout>>(
				container,
				group,
				layout,
				LayoutName(layout),
				st::settingsSendType),
			st::settingsSendTypePadding);
		rows.emplace_back(row, layout);
	}
	for (const auto &[row, layout] : rows) {
		AddGlyph(container, row, layout);
	}
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(u"Crown and Orbit put your chats on a dial you can spin, "
			"flick or turn with the arrow keys, and a click on a face opens "
			"that chat. Each folder is a circle of its own, and a sideways "
			"swipe with two fingers moves to the next one."_q));
}

} // namespace Safe::Circles
