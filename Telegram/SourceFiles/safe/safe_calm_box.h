/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

// Calm mode's boxes: the settings (Settings > Chat Settings > Calm mode,
// laid out like docs/previews/calm/js/settings.js), one chat's rule (the
// chat's menu) and "What you typed" on a softened message he sent.
namespace Safe::Calm {

void SettingsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

void ChatRuleBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer);

[[nodiscard]] QString ChatRuleLabel(not_null<PeerData*> peer);

// Empty when the item is not one of his softened messages.
void WhatYouTypedBox(
	not_null<Ui::GenericBox*> box,
	not_null<HistoryItem*> item);

} // namespace Safe::Calm
