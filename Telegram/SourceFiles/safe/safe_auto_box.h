/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

namespace Window {
class SessionController;
} // namespace Window

// The chat menu's Full-auto row and its dialog (mocks: docs/previews/auto/,
// M and P): On / Off and the pace, stored on the runner so the actor and
// every app agree. Only in chats where full-auto has been on: the actor is
// trained per chat, so it is never offered for a chat it does not know.
namespace Safe::Auto {

void AddMenuAction(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Ui::Menu::MenuCallback &addAction);

void SettingsBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer);

} // namespace Safe::Auto
