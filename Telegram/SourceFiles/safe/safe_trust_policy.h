/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// The trust tiers as pure logic: standard C++ only (no Qt, no tdesktop types),
// so the control repo's tools/trustcheck-desktop compiles and checks this exact
// file on a plain Mac. Same rules as the phone (Android SafeTrustPolicy.java).
//
// The base list is the "android" block of Zach's private trust.json, built into
// the app: {"trusted_dialogs": [...], "allowed_dialogs": [...],
// "known_dialogs": [...]}. Dialog ids: a user is +id, a basic group or a
// channel is -id. known_dialogs = every chat the account had when the list was
// made.
//
// Tiers for a dialog id, first match wins:
//   1. the account's own user (Saved Messages) and Telegram's service account
//      777000 are Trusted, always;
//   2. an override (a signed approval, see safe_trust_approvals.h) wins over
//      the base list;
//   3. trusted_dialogs -> Trusted, allowed_dialogs -> Allowed;
//   4. known_dialogs but in neither list -> Off;
//   5. not known: a person -> New; a group, supergroup or channel this
//      account created (PeerKind::OwnChat) -> Trusted; any other group,
//      channel, bot, or a user whose kind is not known yet -> Off.
// Fail closed: a missing or broken base list gives empty lists, and overrides
// may then only turn chats Off, never open them (the own-chat rule is off
// too: which of his chats he turned Off is not known then).
//
// Own chats (Zach, 2026-10-19: "the groups i make should show automatically"):
// the creator flag is read live by the caller, so a group made today is
// Trusted at once. It is only the default: an override or the base list still
// decides a chat it names (a chat named Off stays Off, an Allowed pick stays
// Allowed), and a chat handed to someone else is plain Chat again and falls
// back to its listed tier.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Safe::TrustRules {

inline constexpr std::int64_t kTelegramServiceId = 777000;
inline constexpr std::size_t kMaxFileBytes = std::size_t(1) << 20;
inline constexpr int kMaxJsonDepth = 32;

inline constexpr std::string_view kKeyTrusted = "trusted_dialogs";
inline constexpr std::string_view kKeyAllowed = "allowed_dialogs";
inline constexpr std::string_view kKeyKnown = "known_dialogs";

enum class Tier : unsigned char {
	Trusted,
	Allowed,
	New,
	Off,
};

enum class Status : unsigned char {
	Trusted,
	Allowed,
	New,
	OffKnown,
	OffNew,
	OffRequested,
};

enum class PeerKind : unsigned char {
	User, // A person (not a bot).
	Bot,
	Chat, // A basic group, supergroup or channel.
	Unknown, // A user whose object is not loaded yet: treated as a bot.
	OwnChat, // A basic group, supergroup or channel this account created.
};

[[nodiscard]] Tier TierOfStatus(Status status);
[[nodiscard]] bool IsOpen(Tier tier);

// "trusted" | "allowed" | "off" -> the tier; anything else -> nullopt.
[[nodiscard]] std::optional<Tier> TierFromWord(std::string_view word);
// The stored word for a tier: "trusted", "allowed", "off" ("new" for New).
[[nodiscard]] std::string_view WordOf(Tier tier);

// A dialog id written as a decimal string: optional '-', digits, no leading
// zero, not 0, fits in 64 bits. Nothing else (no '+', no spaces).
[[nodiscard]] std::optional<std::int64_t> ParseDialogKey(std::string_view key);
[[nodiscard]] std::string DialogKey(std::int64_t dialogId);

// ---- a strict, small JSON reader ----
// Objects, arrays, strings, integers, true / false / null. Numbers with a
// fraction or an exponent are rejected, so are duplicate keys, nesting deeper
// than kMaxJsonDepth and trailing data. Error messages never quote content.

struct JsonMember;

struct JsonValue {
	enum class Type : unsigned char {
		Null,
		Bool,
		Integer,
		String,
		Array,
		Object,
	};
	Type type = Type::Null;
	bool boolean = false;
	std::int64_t integer = 0;
	std::string string;
	std::vector<JsonValue> array;
	std::vector<JsonMember> object;

	[[nodiscard]] const JsonValue *find(std::string_view key) const;
};

struct JsonMember {
	std::string key;
	JsonValue value;
};

struct JsonResult {
	std::optional<JsonValue> value;
	std::string error;
};

// `what` starts every error message ("trust file", "approvals", ...).
[[nodiscard]] JsonResult ParseJson(
	std::string_view text,
	std::string_view what);

// Appends `value` as a JSON string (quotes and escapes included).
void AppendJsonString(std::string &to, std::string_view value);

// ---- the policy ----

using Overrides = std::map<std::int64_t, Tier>;

struct PolicyResult;

class Policy final {
public:
	// The policy used when the base list is missing or broken.
	[[nodiscard]] static Policy FailClosed(Overrides overrides = {});
	[[nodiscard]] static PolicyResult Parse(
		std::string_view json,
		Overrides overrides = {});

	[[nodiscard]] Policy withOverrides(Overrides overrides) const;
	[[nodiscard]] Policy withRequests(std::set<std::int64_t> requested) const;

	[[nodiscard]] bool loaded() const;
	[[nodiscard]] std::size_t trustedCount() const;
	[[nodiscard]] std::size_t allowedCount() const;
	[[nodiscard]] std::size_t knownCount() const;
	[[nodiscard]] std::size_t overrideCount() const;
	[[nodiscard]] std::size_t requestCount() const;

	[[nodiscard]] Tier tierOf(
		std::int64_t dialogId,
		PeerKind kind,
		std::int64_t selfUserId) const;
	[[nodiscard]] bool isOff(
		std::int64_t dialogId,
		PeerKind kind,
		std::int64_t selfUserId) const;
	// The tier and, for Off, why: known (or turned off), asked for, or new.
	[[nodiscard]] Status statusOf(
		std::int64_t dialogId,
		PeerKind kind,
		std::int64_t selfUserId) const;

	// A channel's discussion group (both dialog ids): the group's own tier,
	// raised to Allowed while the channel is Trusted or Allowed, unless an
	// override turned the group itself off.
	[[nodiscard]] Tier discussionTier(
		std::int64_t groupId,
		std::int64_t channelId,
		std::int64_t selfUserId) const;
	[[nodiscard]] Status discussionStatus(
		std::int64_t groupId,
		std::int64_t channelId,
		std::int64_t selfUserId) const;
	// The same with both chats' kinds (either may be OwnChat): a group he
	// created is Trusted on its own, a group under his own channel is raised
	// to Allowed like under any open channel. The forms above = both Chat.
	[[nodiscard]] Tier discussionTier(
		std::int64_t groupId,
		PeerKind groupKind,
		std::int64_t channelId,
		PeerKind channelKind,
		std::int64_t selfUserId) const;
	[[nodiscard]] Status discussionStatus(
		std::int64_t groupId,
		PeerKind groupKind,
		std::int64_t channelId,
		PeerKind channelKind,
		std::int64_t selfUserId) const;

	// No override and no base list entry names `dialogId`: its tier is the
	// rule-5 default (a person New, his own chat Trusted, anything else Off).
	[[nodiscard]] bool isUnlisted(std::int64_t dialogId) const;

	// Every id the base list or the override layer names, sorted.
	[[nodiscard]] std::vector<std::int64_t> listedIds() const;
	// The base list's own tier (no override): Trusted, Allowed, Off (known).
	[[nodiscard]] std::optional<Tier> baseTier(std::int64_t dialogId) const;
	[[nodiscard]] std::optional<Tier> overrideOf(std::int64_t dialogId) const;
	[[nodiscard]] bool requested(std::int64_t dialogId) const;

	// Saved Messages and Telegram's service account: always Trusted.
	[[nodiscard]] static bool IsFixed(
		std::int64_t dialogId,
		std::int64_t selfUserId);

private:
	struct Lists {
		bool loaded = false;
		std::set<std::int64_t> trusted;
		std::set<std::int64_t> allowed;
		std::set<std::int64_t> known;
	};

	Policy(
		std::shared_ptr<const Lists> lists,
		Overrides overrides,
		std::set<std::int64_t> requested);

	[[nodiscard]] Status statusFor(std::int64_t dialogId, Tier tier) const;

	std::shared_ptr<const Lists> _lists;
	Overrides _overrides;
	std::set<std::int64_t> _requested;

};

struct PolicyResult {
	std::optional<Policy> policy;
	std::string error;
};

} // namespace Safe::TrustRules
