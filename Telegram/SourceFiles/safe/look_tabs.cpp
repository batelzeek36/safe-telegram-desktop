/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_tabs.h"

#include "boxes/peer_list_controllers.h"
#include "calls/calls_box_controller.h"
#include "data/data_session.h"
#include "dialogs/dialogs_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "safe/look_icons.h"
#include "safe/look_style.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"
#include "window/window_session_controller.h"

#include "styles/style_basic.h"
#include "styles/style_dialogs.h"

namespace Safe::Look {
namespace {

// Measured on the native app: a 50pt bar (hairline included), 28pt glyphs
// centered 26pt below the hairline, the count badge up and to the right.
// The chats glyph is drawn 10% larger (its bubbles span ~27pt there).
constexpr auto kBarHeight = 50;
constexpr auto kGlyphSize = 28;
constexpr auto kChatsGlyphSize = 31;
constexpr auto kGlyphCenter = 26;
constexpr auto kBadgeShiftX = 11;
constexpr auto kBadgeShiftY = -12;
constexpr auto kPressedOpacity = 0.55;

[[nodiscard]] QString BarName() {
	return u"safe_look_tab_bar"_q;
}

// How many Settings pages each window shows right now (a section, a layer),
// and a ping for a window whose count changed.
struct SettingsShownState {
	base::flat_map<not_null<Window::SessionController*>, int> counts;
	rpl::event_stream<not_null<Window::SessionController*>> changes;
};

[[nodiscard]] SettingsShownState &SettingsShown() {
	static auto result = SettingsShownState();
	return result;
}

[[nodiscard]] bool SettingsShownIn(
		not_null<Window::SessionController*> controller) {
	return SettingsShown().counts.contains(controller);
}

[[nodiscard]] int GlyphSize(Glyph glyph) {
	return style::ConvertScale((glyph == Glyph::Chats)
		? kChatsGlyphSize
		: kGlyphSize);
}

class Tab final : public Ui::AbstractButton {
public:
	Tab(QWidget *parent, Glyph glyph, QString name, bool selected);

	void setBadge(int count, bool muted);
	void setSelected(bool selected);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QString accessibilityName() override;

private:
	void paintBadge(QPainter &p, QPoint center);

	const Glyph _glyph;
	const QString _name;
	bool _selected = false;
	int _badge = 0;
	bool _badgeMuted = false;

};

class TabBar final : public Ui::RpWidget {
public:
	TabBar(
		QWidget *parent,
		not_null<Dialogs::Widget*> list,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void setupTabs();
	void layoutTabs(int width);
	void showChats();
	void updateSelected();

	const not_null<Dialogs::Widget*> _list;
	const not_null<Window::SessionController*> _controller;
	std::vector<not_null<Tab*>> _tabs;
	Tab *_chats = nullptr;
	Tab *_settings = nullptr;

};

Tab::Tab(QWidget *parent, Glyph glyph, QString name, bool selected)
: AbstractButton(parent)
, _glyph(glyph)
, _name(std::move(name))
, _selected(selected) {
}

void Tab::setBadge(int count, bool muted) {
	if (_badge == count && _badgeMuted == muted) {
		return;
	}
	_badge = count;
	_badgeMuted = muted;
	accessibilityNameChanged();
	update();
}

void Tab::setSelected(bool selected) {
	if (_selected == selected) {
		return;
	}
	_selected = selected;
	update();
}

void Tab::onStateChanged(State was, StateChangeSource source) {
	update();
}

QString Tab::accessibilityName() {
	return (_badge > 0)
		? (_name + u", "_q + QString::number(_badge))
		: _name;
}

void Tab::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (isDown()) {
		p.setOpacity(kPressedOpacity);
	}
	const auto size = GlyphSize(_glyph);
	const auto center = QPoint(
		width() / 2,
		style::ConvertScale(kGlyphCenter));
	const auto area = QRect(
		center.x() - size / 2,
		center.y() - size / 2,
		size,
		size);
	PaintGlyph(
		p,
		_glyph,
		area,
		size,
		_selected ? st::windowActiveTextFg->c : GlyphColor());
	if (_badge > 0) {
		p.setOpacity(1.);
		paintBadge(
			p,
			center + QPoint(
				style::ConvertScale(kBadgeShiftX),
				style::ConvertScale(kBadgeShiftY)));
	}
}

void Tab::paintBadge(QPainter &p, QPoint center) {
	const auto text = (_badge > 99)
		? u"99+"_q
		: QString::number(_badge);
	const auto &font = st::dialogsUnreadFont;
	const auto height = st::dialogsUnreadHeight;
	const auto width = std::max(
		height,
		font->width(text) + 2 * st::dialogsUnreadPadding);
	const auto rect = QRect(
		center.x() - height / 2,
		center.y() - height / 2,
		width,
		height);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(_badgeMuted
		? st::dialogsUnreadBgMuted
		: st::attentionButtonFg);
	const auto radius = height / 2.;
	p.drawRoundedRect(rect, radius, radius);
	p.setFont(font);
	p.setPen(st::dialogsUnreadFg);
	p.drawText(rect, text, style::al_center);
}

TabBar::TabBar(
	QWidget *parent,
	not_null<Dialogs::Widget*> list,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _list(list)
, _controller(controller) {
	setObjectName(BarName());
	setupTabs();
}

void TabBar::setupTabs() {
	const auto controller = _controller;
	const auto add = [&](
			Glyph glyph,
			QString name,
			bool selected,
			Fn<void()> callback) {
		const auto tab = Ui::CreateChild<Tab>(
			this,
			glyph,
			std::move(name),
			selected);
		tab->setClickedCallback(std::move(callback));
		_tabs.push_back(tab);
		return tab;
	};
	add(Glyph::Contacts, tr::lng_menu_contacts(tr::now), false, [=] {
		controller->show(PrepareContactsBox(controller));
	});
	add(Glyph::Calls, tr::lng_menu_calls(tr::now), false, [=] {
		::Calls::ShowCallsBox(controller);
	});
	const auto chats = add(Glyph::Chats, ChatsTitle(), true, [=] {
		showChats();
	});
	_chats = chats;
	_settings = add(
		Glyph::Settings,
		tr::lng_menu_settings(tr::now),
		false,
		[=] { controller->showSettings(); });

	// Settings is the selected tab while a Settings page is shown in this
	// window, Chats otherwise (the native app's tabs work that way).
	rpl::single(
		controller
	) | rpl::then(
		SettingsShown().changes.events()
	) | rpl::filter([=](not_null<Window::SessionController*> which) {
		return (which == controller);
	}) | rpl::on_next([=] {
		updateSelected();
	}, lifetime());

	const auto data = &controller->session().data();
	rpl::single(rpl::empty) | rpl::then(
		data->unreadBadgeChanges()
	) | rpl::on_next([=] {
		chats->setBadge(data->unreadBadge(), data->unreadBadgeMuted());
	}, lifetime());

	sizeValue() | rpl::on_next([=](QSize size) {
		layoutTabs(size.width());
	}, lifetime());
}

void TabBar::layoutTabs(int width) {
	const auto count = int(_tabs.size());
	if (!count) {
		return;
	}
	const auto top = st::lineWidth;
	for (auto i = 0; i != count; ++i) {
		const auto left = (width * i) / count;
		const auto right = (width * (i + 1)) / count;
		_tabs[i]->setGeometry(left, top, right - left, height() - top);
	}
}

void TabBar::updateSelected() {
	const auto settings = SettingsShownIn(_controller);
	_settings->setSelected(settings);
	_chats->setSelected(!settings);
}

void TabBar::showChats() {
	if (SettingsShownIn(_controller)) {
		// Back to the chats: close the Settings pages in the main column.
		_controller->clearSectionStack();
	} else if (_controller->shownForum().current()) {
		_controller->closeForum();
	} else if (_controller->openedFolder().current()) {
		_controller->closeFolder();
	} else {
		_list->jumpToTop();
	}
}

void TabBar::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::dialogsBg);
	p.fillRect(0, 0, width(), st::lineWidth, SeparatorColor());
}

} // namespace

void TrackSettingsShown(
		not_null<Ui::RpWidget*> page,
		not_null<Window::SessionController*> controller) {
	auto &state = SettingsShown();
	++state.counts[controller];
	state.changes.fire_copy(controller);
	page->lifetime().add([=] {
		auto &state = SettingsShown();
		const auto i = state.counts.find(controller);
		if (i == end(state.counts)) {
			return;
		} else if (--i->second <= 0) {
			state.counts.erase(i);
		}
		state.changes.fire_copy(controller);
	});
}

int PlaceTabBar(
		not_null<Dialogs::Widget*> list,
		not_null<Window::SessionController*> controller,
		bool mainList,
		int width,
		float64 narrowRatio) {
	// Found by name: TabBar has no meta object of its own, and only this
	// file creates a child with that name.
	auto bar = static_cast<TabBar*>(list->findChild<QWidget*>(
		BarName(),
		Qt::FindDirectChildrenOnly));
	const auto shown = mainList && (narrowRatio <= 0.);
	if (!shown) {
		if (bar) {
			bar->hide();
		}
		return 0;
	}
	if (!bar) {
		bar = Ui::CreateChild<TabBar>(list.get(), list, controller);
	}
	const auto height = style::ConvertScale(kBarHeight);
	bar->setGeometry(0, list->height() - height, width, height);
	bar->show();
	bar->raise();
	return height;
}

} // namespace Safe::Look
