/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"

class PeerData;

// Calm mode's choices (Settings > Chat Settings > Calm mode, docs/CALM.md
// "Modes"), kept in safe_calm.json in the app's data folder. Defaults are
// Zach's picks of 2026-10-25: Second look, leaf on, their side on.
namespace Safe::Calm {

enum class SendMode : uchar {
	Off,
	Review, // A: Second look
	Autopilot, // C
};

enum class ChatRule : uchar {
	Default,
	Always,
	Off,
};

struct ChatEntry {
	ChatRule rule = ChatRule::Default;
	TimeId since = 0; // when the rule was set
};

struct Settings {
	SendMode send = SendMode::Review;
	bool typing = true; // B: Leaf in the bar
	bool incoming = true; // D: Their side
	TimeId incomingSince = 0; // when their side was last switched on
	base::flat_map<PeerId, ChatEntry> chats;
};

// What applies in one chat after its rule and the chat kind are counted.
struct Modes {
	SendMode send = SendMode::Off;
	bool typing = false;
	bool incoming = false;
	TimeId incomingSince = 0;

	[[nodiscard]] bool outgoing() const {
		return (send != SendMode::Off) || typing;
	}
};

[[nodiscard]] const Settings &Current();
[[nodiscard]] rpl::producer<> Changes();
void Update(Fn<void(Settings&)> change);

[[nodiscard]] ChatRule RuleFor(PeerId peer);
void SetRule(PeerId peer, ChatRule rule);

// Default rule: people and groups only (not bots, not channels, not Saved
// Messages or the service account); Always turns everything on in that chat
// (Second look when Send is Off), Off turns everything off.
[[nodiscard]] Modes ModesFor(not_null<PeerData*> peer);

} // namespace Safe::Calm
