/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace style {
struct InputField;
} // namespace style

namespace Ui {
class RpWidget;
class InputField;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// The top of the chat list the way Telegram for macOS (the Swift app) has
// it: a title row ("Chats" centered, the main menu as a small icon on the
// left, a compose button on the right), a full width rounded search field
// with a centered magnifier and placeholder, and a hairline under it.
// Dialogs::Widget::updateControlsGeometry asks for the extra height and the
// field position through these calls, then PlaceTitle adds the pieces.
namespace Safe::Look {

// The search field style: Telegram Desktop's chat list field made 30pt tall,
// outlined instead of filled, with the placeholder centered.
[[nodiscard]] const style::InputField &SearchFieldStyle();

// The height the title row adds above the search field: 0 for a list that
// does not show it (child list, an open folder, forum or community), shrinking
// to 0 while the column animates to its narrow (photos only) width.
[[nodiscard]] int TitleHeight(bool mainList, float64 narrowRatio);

// Where the search field goes once the title row is there: full width
// (left edge) and at the bottom of the taller area (top edge). With no
// title row both return what Telegram Desktop computed.
[[nodiscard]] int SearchLeft(int computed, int titleHeight);
[[nodiscard]] int SearchTop(
	int computed,
	int areaHeight,
	int searchHeight,
	int titleHeight);

// Creates (first call) and places the title row, the compose button and the
// magnifier inside the search field. `controls` is the search area widget,
// `lock` the passcode lock button (the compose button moves left of it when
// it is shown).
void PlaceTitle(
	not_null<Ui::RpWidget*> controls,
	not_null<Window::SessionController*> controller,
	not_null<Ui::InputField*> search,
	QWidget *lock,
	int titleHeight);

} // namespace Safe::Look
