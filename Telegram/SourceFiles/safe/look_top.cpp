/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_top.h"

#include "lang/lang_keys.h"
#include "safe/look_icons.h"
#include "safe/look_style.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPlainTextEdit>
#include <QtGui/QShortcut>
#include <QtWidgets/QTextEdit>

#include "styles/style_basic.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

namespace Safe::Look {
namespace {

// Measured on the native app (night theme, 380pt window): the title row
// and search field take 89pt above the hairline; the field is 30pt tall,
// 10pt from the sides and 9pt above the hairline, outlined in a hairline
// a shade lighter than the list.
constexpr auto kTitleAdded = 36;
constexpr auto kSearchHeight = 30;
constexpr auto kSearchBelow = 9;
constexpr auto kTitleFontSize = 15;
constexpr auto kComposeGlyph = 22;
constexpr auto kComposeButton = 40;
constexpr auto kComposeRight = 9;
constexpr auto kSearchGlyph = 14;
constexpr auto kSearchGlyphGap = 6;
constexpr auto kPressedOpacity = 0.55;

[[nodiscard]] QString TitleName() {
	return u"safe_look_title"_q;
}

[[nodiscard]] QString ComposeName() {
	return u"safe_look_compose"_q;
}

[[nodiscard]] QString SearchGlyphName() {
	return u"safe_look_search_glyph"_q;
}

[[nodiscard]] QString ShortcutName() {
	return u"safe_look_search_shortcut"_q;
}

[[nodiscard]] bool IsTextInput(QWidget *widget) {
	return qobject_cast<QTextEdit*>(widget)
		|| qobject_cast<QPlainTextEdit*>(widget)
		|| qobject_cast<QLineEdit*>(widget);
}

// Cmd+K (Qt's Ctrl is the command key on macOS) focuses the chat list
// search the way the native app does. It lives on the window, so it works
// while a chat has the keyboard, and is switched off inside text boxes,
// where Telegram Desktop's own Cmd+K makes a link. It goes away with the
// search controls (an account switch builds new ones).
void InstallSearchShortcut(
		not_null<Ui::RpWidget*> controls,
		not_null<Window::SessionController*> controller,
		not_null<Ui::InputField*> search) {
	const auto window = controller->widget();
	if (window->findChild<QShortcut*>(
			ShortcutName(),
			Qt::FindDirectChildrenOnly)) {
		return;
	}
	const auto shortcut = controls->lifetime().make_state<
		base::unique_qptr<QShortcut>
	>(base::make_unique_q<QShortcut>(
		QKeySequence(Qt::CTRL | Qt::Key_K),
		window.get()));
	const auto raw = shortcut->get();
	raw->setObjectName(ShortcutName());
	raw->setContext(Qt::WindowShortcut);
	raw->setEnabled(!IsTextInput(QApplication::focusWidget()));
	QObject::connect(
		qApp,
		&QApplication::focusChanged,
		raw,
		[=](QWidget *old, QWidget *now) {
			raw->setEnabled(!IsTextInput(now));
		});
	const auto weak = QPointer<Ui::InputField>(search.get());
	QObject::connect(raw, &QShortcut::activated, raw, [=] {
		const auto field = weak.data();
		if (!field) {
			return;
		}
		if (!field->isVisible()) {
			// One column with a chat open: back to the list first.
			controller->clearSectionStack();
		}
		field->setFocus();
		field->selectAll();
	});
}

[[nodiscard]] int SearchGlyphSpace() {
	return style::ConvertScale(kSearchGlyph + kSearchGlyphGap);
}

[[nodiscard]] const style::font &TitleFont() {
	static const auto result = style::font(
		style::ConvertScale(kTitleFontSize),
		st::semiboldFont->flags(),
		st::semiboldFont->family());
	return result;
}

template <typename Type>
[[nodiscard]] Type *FindChild(not_null<QWidget*> parent, const QString &name) {
	// Found by name: these widgets have no meta object of their own, and only
	// this file creates children with these names.
	return static_cast<Type*>(parent->findChild<QWidget*>(
		name,
		Qt::FindDirectChildrenOnly));
}

// "Chats" centered over the field and the hairline under the whole area.
class TitleRow final : public Ui::RpWidget {
public:
	explicit TitleRow(QWidget *parent);

	void setTitleCenter(int y);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	int _titleCenter = 0;

};

class ComposeButton final : public Ui::AbstractButton {
public:
	ComposeButton(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QString accessibilityName() override;

private:
	void showMenu();

	const not_null<Window::SessionController*> _controller;
	base::unique_qptr<Ui::PopupMenu> _menu;

};

// The magnifier left of the centered placeholder, shown while the field is
// empty (the placeholder fades out as soon as there is text).
class SearchGlyph final : public Ui::RpWidget {
public:
	SearchGlyph(QWidget *parent, not_null<Ui::InputField*> field);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const not_null<Ui::InputField*> _field;

};

TitleRow::TitleRow(QWidget *parent)
: RpWidget(parent) {
	setObjectName(TitleName());
	setAttribute(Qt::WA_TransparentForMouseEvents);
}

void TitleRow::setTitleCenter(int y) {
	if (_titleCenter != y) {
		_titleCenter = y;
		update();
	}
}

void TitleRow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto &font = TitleFont();
	p.setFont(font);
	p.setPen(st::dialogsNameFg);
	const auto text = ChatsTitle();
	const auto left = (width() - font->width(text)) / 2;
	p.drawText(
		left,
		_titleCenter - font->height / 2 + font->ascent,
		text);
	p.fillRect(
		0,
		height() - st::lineWidth,
		width(),
		st::lineWidth,
		SeparatorColor());
}

ComposeButton::ComposeButton(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractButton(parent)
, _controller(controller) {
	setObjectName(ComposeName());
	resize(
		style::ConvertScale(kComposeButton),
		style::ConvertScale(kComposeButton));
	setClickedCallback([=] { showMenu(); });
}

void ComposeButton::onStateChanged(State was, StateChangeSource source) {
	update();
}

QString ComposeButton::accessibilityName() {
	return tr::lng_create_group_title(tr::now)
		+ u", "_q
		+ tr::lng_create_channel_title(tr::now);
}

void ComposeButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	if (isDown() || _menu) {
		p.setOpacity(kPressedOpacity);
	}
	PaintGlyph(
		p,
		Glyph::Compose,
		rect(),
		style::ConvertScale(kComposeGlyph),
		st::windowActiveTextFg->c);
}

void ComposeButton::showMenu() {
	if (_menu) {
		return;
	}
	const auto controller = _controller;
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_menu->addAction(tr::lng_create_group_title(tr::now), [=] {
		controller->showNewGroup();
	}, &st::menuIconGroups);
	_menu->addAction(tr::lng_create_channel_title(tr::now), [=] {
		controller->showNewChannel();
	}, &st::menuIconChannel);
	_menu->setDestroyedCallback(crl::guard(this, [=] {
		update();
	}));
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
	_menu->popup(mapToGlobal(QPoint(width(), height())));
	update();
}

SearchGlyph::SearchGlyph(QWidget *parent, not_null<Ui::InputField*> field)
: RpWidget(parent)
, _field(field) {
	setObjectName(SearchGlyphName());
	setAttribute(Qt::WA_TransparentForMouseEvents);
	field->sizeValue() | rpl::on_next([=](QSize size) {
		setGeometry(QRect(QPoint(), size));
	}, lifetime());
	field->changes() | rpl::on_next([=] {
		update();
	}, lifetime());
}

void SearchGlyph::paintEvent(QPaintEvent *e) {
	if (!_field->getLastText().isEmpty()) {
		return;
	}
	const auto &st = SearchFieldStyle();
	const auto placeholder = SearchHint();
	const auto textWidth = st.placeholderFont->width(placeholder);
	const auto margins = st.textMargins + st.placeholderMargins;
	const auto available = width() - margins.left() - margins.right();
	const auto textLeft = margins.left() + (available - textWidth) / 2;
	const auto size = style::ConvertScale(kSearchGlyph);
	const auto left = textLeft - SearchGlyphSpace();
	auto p = QPainter(this);
	PaintGlyph(
		p,
		Glyph::Search,
		QRect(left, (height() - size) / 2, size, size),
		size,
		st.placeholderFg->c);
}

} // namespace

rpl::producer<QString> SearchPlaceholder() {
	return rpl::single(SearchHint());
}

const style::InputField &SearchFieldStyle() {
	static const auto result = [] {
		auto st = st::dialogsFilter;
		const auto height = style::ConvertScale(kSearchHeight);
		const auto side = st.textMargins.left();
		const auto right = st.textMargins.right();
		st.heightMin = height;
		st.borderRadius = height / 2;
		st.border = 2 * st::lineWidth;
		st.borderDenominator = 2;
		st.textBg = st::dialogsBg;
		st.borderFg = st::dialogsBgOver;
		// The native placeholder grey (also the magnifier's color).
		st.placeholderFg = PlaceholderFg();
		st.placeholderFgActive = PlaceholderFg();
		// Keep the text where Telegram Desktop has it, 5pt less padding.
		const auto shrink = st::dialogsFilter.heightMin - height;
		const auto top = std::max(
			st.textMargins.top() - (shrink * 3 + 2) / 5,
			0);
		const auto bottom = std::max(
			st.textMargins.bottom() - (shrink - (shrink * 3 + 2) / 5),
			0);
		st.textMargins = QMargins(side, top, right, bottom);
		// Center [magnifier gap placeholder] in the whole field: the text
		// rectangle is lopsided (room for the clear button on the right).
		st.placeholderAlign = style::al_top;
		st.placeholderMargins = QMargins(
			(right - side) + SearchGlyphSpace(),
			st.placeholderMargins.top(),
			0,
			st.placeholderMargins.bottom());
		return st;
	}();
	return result;
}

int TitleHeight(bool mainList, float64 narrowRatio) {
	if (!mainList || narrowRatio >= 1.) {
		return 0;
	}
	return anim::interpolate(
		style::ConvertScale(kTitleAdded),
		0,
		narrowRatio);
}

int SearchLeft(int computed, int titleHeight) {
	// The same margin Telegram Desktop keeps on the right of the field.
	return (titleHeight > 0)
		? (st::dialogsFilterSkip + st::dialogsFilterPadding.x())
		: computed;
}

int SearchTop(
		int computed,
		int areaHeight,
		int searchHeight,
		int titleHeight) {
	return (titleHeight > 0)
		? (areaHeight - searchHeight - style::ConvertScale(kSearchBelow))
		: computed;
}

void PlaceTitle(
		not_null<Ui::RpWidget*> controls,
		not_null<Window::SessionController*> controller,
		not_null<Ui::InputField*> search,
		QWidget *lock,
		int titleHeight) {
	if (!FindChild<SearchGlyph>(search, SearchGlyphName())) {
		Ui::CreateChild<SearchGlyph>(search.get(), search)->show();
	}
	auto title = FindChild<TitleRow>(controls, TitleName());
	auto compose = FindChild<ComposeButton>(controls, ComposeName());
	if (titleHeight <= 0) {
		if (title) {
			title->hide();
		}
		if (compose) {
			compose->hide();
		}
		return;
	}
	if (!title) {
		title = Ui::CreateChild<TitleRow>(controls.get());
	}
	if (!compose) {
		compose = Ui::CreateChild<ComposeButton>(controls.get(), controller);
	}
	InstallSearchShortcut(controls, controller, search);
	// The row keeps Telegram Desktop's top bar center, where the main menu
	// button already sits.
	const auto center = st::topBarHeight / 2;
	title->setGeometry(0, 0, controls->width(), controls->height());
	title->setTitleCenter(center);
	title->show();
	title->lower();

	const auto lockShown = lock && !lock->isHidden();
	const auto right = lockShown
		? lock->x()
		: (controls->width() - style::ConvertScale(kComposeRight));
	compose->moveToLeft(
		right - compose->width(),
		center - compose->height() / 2);
	compose->show();
	compose->raise();
}

} // namespace Safe::Look
