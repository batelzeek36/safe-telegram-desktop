/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_circles_core.h"

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {
struct HighlightEntry;
} // namespace Settings

// Which chat list he sees: List (Telegram's own), Crown or Orbit. Picked from
// the main menu ("Chat list") and in Settings > Chat Settings > Chat list;
// List until he picks another. Kept in safe_circles.json in the app's data
// folder (owner-only); an unreadable file means List.
namespace Safe::Circles {

[[nodiscard]] Layout CurrentLayout();
[[nodiscard]] rpl::producer<Layout> LayoutValue();
void SetLayout(Layout layout);

[[nodiscard]] QString LayoutName(Layout layout);
[[nodiscard]] QString LayoutAbout(Layout layout);

// The main menu row: "Chat list" with the current layout on the right; a
// click opens a small menu of the three, the current one checked. Picking
// one closes the main menu so the chat list shows it at once.
void AddMainMenuRow(
	not_null<Ui::VerticalLayout*> menu,
	not_null<Window::SessionController*> controller);

// Settings > Chat Settings: the "Chat list" section, three radio rows with a
// small drawing of each layout and a line on what Crown and Orbit do.
void SetupSettingsSection(
	not_null<Window::SessionController*> controller,
	not_null<Ui::VerticalLayout*> container);

} // namespace Safe::Circles
