/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Signed approvals: how the Mac's lists change after the build (the Mac has no
// DNS Lock, so only Claude's signed file can change them). Standard C++ plus
// OpenSSL (Ed25519), no Qt, checked by the control repo's tools/desktopcheck.
//
// The file `approvals.json` in the app's data folder, written only by the
// control repo's tools/mac_approve.py:
//   {"format": "safe-telegram-approvals/1",
//    "payload": "<base64 of the payload bytes>",
//    "signature": "<base64 of the Ed25519 signature>"}
// The signature covers "safe-telegram-approvals/1\n" + the payload bytes.
// Payload (UTF-8 JSON):
//   {"serial": 7, "issued": "2026-10-18T22:00:00Z",
//    "dialogs": {"<dialog id>": "trusted" | "allowed" | "off", ...},
//    "invites": {"<invite hash>": "trusted" | "allowed" | "off", ...}}
// Every file holds the whole override layer (not a diff), and its serial only
// goes up. Anything malformed rejects the whole file.

#include "safe/safe_trust_policy.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Safe::TrustRules {

inline constexpr std::string_view kApprovalsFormat
	= "safe-telegram-approvals/1";
inline constexpr std::size_t kPublicKeySize = 32;
inline constexpr std::size_t kSignatureSize = 64;
inline constexpr std::size_t kMaxInviteHash = 64;

struct Approvals {
	std::int64_t serial = 0;
	std::string issued;
	Overrides dialogs;
	std::map<std::string, Tier> invites;
};

struct ApprovalsResult {
	std::optional<Approvals> approvals;
	std::string error; // Never quotes content.
};

struct Envelope {
	std::string payload;
	std::string signature;
};

// Invite hashes as Telegram writes them in t.me/+<hash> links: letters,
// digits, '_' and '-', 1 to 64 characters.
[[nodiscard]] bool ValidInviteHash(std::string_view hash);

// Strict base64 (standard alphabet, padded, no whitespace).
[[nodiscard]] std::optional<std::string> Base64Decode(std::string_view text);

// The bytes the signature covers.
[[nodiscard]] std::string SignedMessage(std::string_view payload);

[[nodiscard]] std::optional<Envelope> ParseEnvelope(
	std::string_view file,
	std::string &error);
[[nodiscard]] ApprovalsResult ParseApprovalsPayload(std::string_view payload);

// Ed25519 through OpenSSL. False on any error.
[[nodiscard]] bool VerifyEd25519(
	const unsigned char (&publicKey)[kPublicKeySize],
	std::string_view message,
	std::string_view signature);

// The whole check: envelope, signature, payload.
[[nodiscard]] ApprovalsResult ReadSignedApprovals(
	std::string_view file,
	const unsigned char (&publicKey)[kPublicKeySize]);

// What stays in force, given the app's copy of the last file it accepted
// (`applied`, re-verified at every load) and the current approvals.json:
// - both good: the higher serial wins; an equal serial keeps the copy, a
//   lower one is ignored (an old file put back changes nothing);
// - only one good: that one (the copy survives a deleted, broken or unsigned
//   approvals.json; the current file is used when there is no good copy);
// - neither: no approvals at all (the base list alone, fail closed).
struct ApprovalsChoice {
	enum class Source : unsigned char {
		None,
		Applied,
		Current,
	};
	Source source = Source::None;
	bool currentIgnored = false; // A readable current file that lost.
	bool currentBroken = false; // A current file that did not verify.
};
[[nodiscard]] ApprovalsChoice ChooseApprovals(
	const ApprovalsResult &applied,
	bool currentPresent,
	const ApprovalsResult &current);

} // namespace Safe::TrustRules
