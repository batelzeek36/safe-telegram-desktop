/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

// The chat circles in the main chat list (d35-circles). Dialogs::Widget
// attaches once and calls Sync whenever what it shows may have changed
// (geometry, search, folders, forums); the circles cover the list (and the
// folder tabs) while Crown or Orbit is picked and the plain chat list would
// show, and step aside for search, an open folder or forum, or a narrow
// column. With List picked nothing is created.
namespace Safe::Circles {

struct Host {
	// The main list in its default state: no search, no open folder, forum
	// or community, not narrowed to photos only.
	Fn<bool()> shown;
	// Where the chat list and Telegram's folder tabs sit.
	Fn<QRect()> area;
	// Telegram's folder tabs hide while the circles show (the circle names
	// replace them) and come back after.
	Fn<void(bool hidden)> hideFolderTabs;
	// Widgets that must stay above the circles (the connecting bar).
	Fn<void()> raiseOverlays;
	// Open a chat as if it was chosen in the list.
	Fn<void(not_null<History*>)> open;
};

void Attach(
	not_null<Ui::RpWidget*> list,
	not_null<Window::SessionController*> controller,
	Host host);
void Sync(not_null<QWidget*> list);

// Keys for the circles while they show (arrows, Return, Page Up / Down,
// Home); false leaves the key to the list.
[[nodiscard]] bool HandleKey(not_null<QWidget*> list, not_null<QKeyEvent*> e);

} // namespace Safe::Circles
