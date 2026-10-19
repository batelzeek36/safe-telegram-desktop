/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_peer_id.h"

class PeerData;

namespace Safe {

// Trust tiers, the same rules as the phone build (Android SafeTrustPolicy):
// Trusted = everything shows. Allowed = the chat opens, pictures from senders
// who are not Trusted go through the picture check. New = a person never
// listed (the chat opens, pictures never show). Off = hidden, cannot be opened.
enum class Tier : uchar {
	Trusted,
	Allowed,
	New,
	Off,
};

// The tier plus why an Off chat is off (the Ask to allow box words it).
enum class Status : uchar {
	Trusted,
	Allowed,
	New,
	OffKnown, // In the build's list as off, or turned off by an approval.
	OffNew, // Never listed, never asked for.
	OffRequested, // Ask to allow sent, not approved yet.
};

[[nodiscard]] Tier TierFromStatus(Status status);

[[nodiscard]] inline bool IsOpenTier(Tier tier) {
	return (tier == Tier::Trusted) || (tier == Tier::Allowed);
}

// The id space trust.json uses (Android dialog ids): a user is +id, a basic
// group or a channel is -id. Zero for anything else.
[[nodiscard]] int64 DialogIdOf(PeerId peer);

// Saved Messages (the session's own user) and Telegram's service account 777000
// are always Trusted. A user whose object is not loaded yet counts as a bot
// (Off unless listed). A channel's discussion group is Allowed while its channel
// is Trusted or Allowed, unless the group itself was turned off.
[[nodiscard]] Tier TierOf(not_null<PeerData*> peer);
[[nodiscard]] Status StatusOf(not_null<PeerData*> peer);

[[nodiscard]] inline bool IsOff(not_null<PeerData*> peer) {
	return TierOf(peer) == Tier::Off;
}

[[nodiscard]] inline bool IsOpen(not_null<PeerData*> peer) {
	return IsOpenTier(TierOf(peer));
}

// Ask to allow: queues a request that Claude approves later (signed approvals,
// see safe_trust.cpp). False when the same request is already queued.
// `reason` is one short word: "join", "invite", "link", "open", "bot",
// "miniapp", "game", "payment", "inline", "call".
bool Ask(not_null<PeerData*> peer, const QString &reason);

// An invite link whose chat id is unknown until it is joined.
bool AskInvite(
	const QString &hash,
	const QString &title,
	const QString &reason);

// Fires on the main thread after the lists change (new approvals loaded).
// Anything that caches tiers (chat list membership, counters, search results)
// refreshes on it.
[[nodiscard]] rpl::producer<> Changes();

} // namespace Safe
