/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Dialogs {
class Widget;
} // namespace Dialogs

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Safe::Look {

// The native app's bottom tab bar under the chat list: Contacts, Calls,
// Chats (selected, with the unread count) and Settings, over a hairline.
// Called from Dialogs::Widget::updateControlsGeometry before the bottom
// buttons are stacked: creates the bar on first use as a child of the list
// widget, puts it at the very bottom and returns the height it takes, which
// the caller adds to its bottom skip. Returns 0 (bar hidden) for a list that
// is not the main one and while the column is narrow (photos only).
[[nodiscard]] int PlaceTabBar(
	not_null<Dialogs::Widget*> list,
	not_null<Window::SessionController*> controller,
	bool mainList,
	int width,
	float64 narrowRatio);

// Called by every Settings page (Info::Settings::Widget) when it is created:
// while at least one is alive in a window, that window's tab bar shows the
// Settings tab selected instead of Chats, and Chats closes the Settings pages.
void TrackSettingsShown(
	not_null<Ui::RpWidget*> page,
	not_null<Window::SessionController*> controller);

} // namespace Safe::Look
