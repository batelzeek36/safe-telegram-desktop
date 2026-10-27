/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"
#include "ui/text/text_entity.h"

class PeerData;

namespace Window {
class SessionController;
} // namespace Window

namespace Ui::Menu {
struct MenuCallback;
} // namespace Ui::Menu

// ALL CAPS goes out as italics (d31-caps-italic, Zach 2026-10-26: "Anytime i
// TYPE IN ALL CAPS, i would like the softener to send it as italics,
// otherwise it reads like im yelling" and "should be toggle-able per chat").
// A shouting run (safe_caps_core.h) is lowercased and set in italics before
// the message is sent, so it reads as emphasis. On in every chat until he
// turns it off in that chat's menu; the chats he turned off are kept in
// safe_caps.json in the app's data folder.
namespace Safe::Caps {

[[nodiscard]] bool EnabledFor(PeerId peer);
void SetEnabled(PeerId peer, bool enabled);

// The transform alone: every shouting run lowercased and italic. Links,
// mentions, hashtags, bot commands and code are never touched.
[[nodiscard]] TextWithEntities Soften(TextWithEntities text);

// Called on the outgoing text right before it is cut into messages and
// sent (ApiWrap::sendMessage); does nothing in a chat he turned it off in.
void Apply(PeerId peer, TextWithEntities &text);

// The chat menu's "ALL CAPS as italics: On / Off" row: a click flips it for
// that chat and says so in a toast.
void AddMenuAction(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Ui::Menu::MenuCallback &addAction);

} // namespace Safe::Caps
