/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// The Ask to allow queue and the invite joins, as pure logic (standard C++,
// checked by the control repo's tools/desktopcheck).
//
// `safe_requests.json` in the app's data folder (the app writes it, the
// control repo's tools/mac_approve.py reads it):
//   {"requests": [{"key": "-1234" | "invite:<hash>", "dialog_id": -1234,
//     "peer": "user" | "chat" | "channel" | "invite",
//     "kind": "person" | "bot" | "group" | "channel" | "chat",
//     "title": "...", "username": "...", "reason": "join",
//     "first_seen": <ms since 1970>, "serial": <approvals serial then>}]}
// A request is answered once an approvals file with a higher serial than the
// one it was asked under names its key (a dialog id or an invite hash),
// whatever the answer: allowed, trusted or off (rejected).
//
// `safe_invite_joins.json`: which chat an approved invite led to, so the
// joined chat gets the invite's approved tier before Claude names its id:
//   {"joins": [{"hash": "...", "dialog_id": -1234, "peer": "channel",
//     "title": "...", "time": <ms>}]}

#include "safe/safe_trust_approvals.h"
#include "safe/safe_trust_policy.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Safe::TrustRules {

inline constexpr std::string_view kInvitePrefix = "invite:";
inline constexpr std::size_t kMaxTitle = 128; // In characters.
inline constexpr std::size_t kMaxUsername = 64;
inline constexpr std::size_t kMaxRequests = 500;
inline constexpr std::size_t kMaxJoins = 500;

struct Request {
	std::string key;
	std::int64_t dialogId = 0; // 0 for an invite.
	std::string peer;
	std::string kind;
	std::string title;
	std::string username;
	std::string reason;
	std::int64_t firstSeen = 0;
	std::int64_t serial = 0;
};

struct InviteJoin {
	std::string hash;
	std::int64_t dialogId = 0;
	std::string peer;
	std::string title;
	std::int64_t time = 0;
};

[[nodiscard]] std::string KeyForDialog(std::int64_t dialogId);
[[nodiscard]] std::optional<std::string> KeyForInvite(std::string_view hash);
// The hash of an "invite:<hash>" key, or nullopt.
[[nodiscard]] std::optional<std::string> InviteHashOf(std::string_view key);

// Every text field cleaned: kind / peer / reason one of the known words
// (else "chat" / "user" or "invite" / "other"), the title cut to kMaxTitle
// characters with control characters made spaces, the username without '@'
// and only [A-Za-z0-9_] up to kMaxUsername (else empty), times not negative.
// Nullopt when the key does not match the dialog id or the invite hash.
[[nodiscard]] std::optional<Request> CleanRequest(Request request);

[[nodiscard]] std::string CleanTitle(std::string_view title);
[[nodiscard]] std::string CleanUsername(std::string_view username);

[[nodiscard]] std::string EncodeRequests(const std::vector<Request> &list);
// The file is parsed strictly; a broken file gives an empty list and an
// error. Entries that do not clean up are dropped, duplicates keep the first.
[[nodiscard]] std::vector<Request> DecodeRequests(
	std::string_view text,
	std::string &error);

[[nodiscard]] bool Answered(
	const Request &request,
	const std::optional<Approvals> &approvals);

// Adds `request` unless the same key is pending or the queue is full.
// Answered requests with the same key are replaced. True when added.
bool AddRequest(
	std::vector<Request> &list,
	Request request,
	const std::optional<Approvals> &approvals);

// Removes answered requests. True when something was removed.
bool DropAnswered(
	std::vector<Request> &list,
	const std::optional<Approvals> &approvals);

[[nodiscard]] std::set<std::int64_t> PendingDialogIds(
	const std::vector<Request> &list,
	const std::optional<Approvals> &approvals);
[[nodiscard]] bool InvitePending(
	const std::vector<Request> &list,
	std::string_view hash,
	const std::optional<Approvals> &approvals);

[[nodiscard]] std::string EncodeJoins(const std::vector<InviteJoin> &list);
[[nodiscard]] std::vector<InviteJoin> DecodeJoins(
	std::string_view text,
	std::string &error);
// Records (or moves) the chat an invite led to. False when nothing changed.
bool AddJoin(std::vector<InviteJoin> &list, InviteJoin join);

// The override layer in force: the approvals' dialogs, plus each joined
// chat of an approved invite that the approvals do not name themselves.
[[nodiscard]] Overrides EffectiveOverrides(
	const std::optional<Approvals> &approvals,
	const std::vector<InviteJoin> &joins);

} // namespace Safe::TrustRules
