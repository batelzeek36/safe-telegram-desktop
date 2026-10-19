/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_trust.h"

// Invite links whose chat Telegram does not name before the join (added by
// r2a next to the shared contract in safe_trust.h, for the join gate).
// Claude answers an AskInvite() request with a signed approval of the invite
// hash; the join gate then lets that one join through and reports the chat
// it led to, which gets the approved tier until Claude names its id.

namespace Safe {

// The tier Claude approved for this invite hash, or nullopt (not answered,
// or the hash is not a valid invite hash). Trusted / Allowed = the join may
// go ahead; Off = Claude said no.
[[nodiscard]] std::optional<Tier> InviteTier(const QString &hash);

// An AskInvite() request for this hash is waiting for an answer.
[[nodiscard]] bool InviteRequested(const QString &hash);

// Call after a join through `hash` succeeded, with the joined chat. Only an
// invite approved Trusted or Allowed binds; anything else is ignored.
void JoinedByInvite(const QString &hash, not_null<PeerData*> peer);

} // namespace Safe
