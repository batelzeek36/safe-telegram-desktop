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
class PopupMenu;
} // namespace Ui

// Calm mode's boxes and menu items: the settings (Settings > Chat Settings
// > Calm mode, laid out like docs/previews/calm/js/settings.js), one chat's
// rule (the chat's menu), "What you typed" on a softened message he sent
// and "Suggest a reply" on a message (next-suggest-ui.js messageMenu).
namespace Safe::Calm {

void SettingsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session);

void ChatRuleBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer);

[[nodiscard]] QString ChatRuleLabel(not_null<PeerData*> peer);

// "Second look" / "Autopilot" / "Off", following the settings.
[[nodiscard]] rpl::producer<QString> SendModeLabel();

// Empty when the item is not one of his softened messages.
void WhatYouTypedBox(
	not_null<Ui::GenericBox*> box,
	not_null<HistoryItem*> item);

// The message's menu: "Suggest a reply" with the leaf, when suggestions
// are on in its chat and it has words to answer.
void AddSuggestReplyAction(
	not_null<Ui::PopupMenu*> menu,
	not_null<HistoryItem*> item,
	Fn<void()> callback);

} // namespace Safe::Calm
