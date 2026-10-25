/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Safe::Calm {

class Client;

// The one client every Calm check goes through (safe_calm.cpp).
[[nodiscard]] Client &SharedClient();

// The name the runner knows Zach by (calm-runner/judge.py OWNER).
[[nodiscard]] QString OwnerName();

// How a chat or a sender is named to the model: the short name of a person.
[[nodiscard]] QString DisplayName(not_null<PeerData*> peer);

// Telegram's marked chat id ("5666554655", "-1002882936986"), as Telethon's
// get_peer_id and the Bot API write it: the runner picks its per-chat voice
// profile by it. Sent to the runner only, never to the API fallback.
[[nodiscard]] QString ChatKey(not_null<PeerData*> peer);

} // namespace Safe::Calm
