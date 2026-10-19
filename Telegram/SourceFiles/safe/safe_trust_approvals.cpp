/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_trust_approvals.h"

#include <openssl/evp.h>

#include <memory>

namespace Safe::TrustRules {
namespace {

[[nodiscard]] int Base64Value(char c) {
	if (c >= 'A' && c <= 'Z') {
		return c - 'A';
	} else if (c >= 'a' && c <= 'z') {
		return c - 'a' + 26;
	} else if (c >= '0' && c <= '9') {
		return c - '0' + 52;
	} else if (c == '+') {
		return 62;
	} else if (c == '/') {
		return 63;
	}
	return -1;
}

struct PkeyDeleter {
	void operator()(EVP_PKEY *value) const {
		EVP_PKEY_free(value);
	}
};

struct MdCtxDeleter {
	void operator()(EVP_MD_CTX *value) const {
		EVP_MD_CTX_free(value);
	}
};

// One string member of an object that must hold exactly `keys`.
[[nodiscard]] bool OnlyKeys(
		const JsonValue &object,
		std::initializer_list<std::string_view> keys) {
	for (const auto &member : object.object) {
		auto found = false;
		for (const auto key : keys) {
			if (member.key == key) {
				found = true;
				break;
			}
		}
		if (!found) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] const std::string *StringMember(
		const JsonValue &object,
		std::string_view key) {
	const auto value = object.find(key);
	return (value && value->type == JsonValue::Type::String)
		? &value->string
		: nullptr;
}

} // namespace

bool ValidInviteHash(std::string_view hash) {
	if (hash.empty() || hash.size() > kMaxInviteHash) {
		return false;
	}
	for (const auto c : hash) {
		const auto ok = (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| c == '_'
			|| c == '-';
		if (!ok) {
			return false;
		}
	}
	return true;
}

std::optional<std::string> Base64Decode(std::string_view text) {
	if (text.empty() || text.size() % 4) {
		return std::nullopt;
	}
	auto result = std::string();
	result.reserve(text.size() / 4 * 3);
	for (auto i = std::size_t(0); i != text.size(); i += 4) {
		const auto last = (i + 4 == text.size());
		int values[4] = { 0 };
		auto padding = 0;
		for (auto k = 0; k != 4; ++k) {
			const auto c = text[i + k];
			if (c == '=' && last && k >= 2) {
				++padding;
				values[k] = 0;
				continue;
			} else if (padding) {
				return std::nullopt; // Data after '='.
			}
			values[k] = Base64Value(c);
			if (values[k] < 0) {
				return std::nullopt;
			}
		}
		const auto bits = (std::uint32_t(values[0]) << 18)
			| (std::uint32_t(values[1]) << 12)
			| (std::uint32_t(values[2]) << 6)
			| std::uint32_t(values[3]);
		result.push_back(char((bits >> 16) & 0xFF));
		if (padding < 2) {
			result.push_back(char((bits >> 8) & 0xFF));
		} else if (bits & 0xFFFF) {
			return std::nullopt; // Non-canonical trailing bits.
		}
		if (padding < 1) {
			result.push_back(char(bits & 0xFF));
		} else if (padding == 1 && (bits & 0xFF)) {
			return std::nullopt;
		}
	}
	return result;
}

std::string SignedMessage(std::string_view payload) {
	auto result = std::string(kApprovalsFormat);
	result.push_back('\n');
	result.append(payload);
	return result;
}

std::optional<Envelope> ParseEnvelope(
		std::string_view file,
		std::string &error) {
	const auto parsed = ParseJson(file, "approvals");
	if (!parsed.value) {
		error = parsed.error;
		return std::nullopt;
	}
	const auto &root = *parsed.value;
	if (root.type != JsonValue::Type::Object
		|| !OnlyKeys(root, { "format", "payload", "signature" })) {
		error = "approvals: not an approvals envelope";
		return std::nullopt;
	}
	const auto format = StringMember(root, "format");
	const auto payload = StringMember(root, "payload");
	const auto signature = StringMember(root, "signature");
	if (!format || *format != kApprovalsFormat) {
		error = "approvals: unknown format";
		return std::nullopt;
	} else if (!payload || !signature) {
		error = "approvals: payload or signature missing";
		return std::nullopt;
	}
	auto result = Envelope();
	auto decodedPayload = Base64Decode(*payload);
	auto decodedSignature = Base64Decode(*signature);
	if (!decodedPayload || !decodedSignature) {
		error = "approvals: bad base64";
		return std::nullopt;
	} else if (decodedSignature->size() != kSignatureSize) {
		error = "approvals: signature has the wrong size";
		return std::nullopt;
	}
	result.payload = std::move(*decodedPayload);
	result.signature = std::move(*decodedSignature);
	return result;
}

ApprovalsResult ParseApprovalsPayload(std::string_view payload) {
	auto result = ApprovalsResult();
	const auto parsed = ParseJson(payload, "approvals payload");
	if (!parsed.value) {
		result.error = parsed.error;
		return result;
	}
	const auto &root = *parsed.value;
	if (root.type != JsonValue::Type::Object
		|| !OnlyKeys(root, { "serial", "issued", "dialogs", "invites" })) {
		result.error = "approvals payload: unknown shape";
		return result;
	}
	const auto serial = root.find("serial");
	const auto dialogs = root.find("dialogs");
	const auto invites = root.find("invites");
	const auto issued = root.find("issued");
	if (!serial
		|| serial->type != JsonValue::Type::Integer
		|| serial->integer < 1) {
		result.error = "approvals payload: serial missing or not positive";
		return result;
	} else if (!dialogs || dialogs->type != JsonValue::Type::Object) {
		result.error = "approvals payload: dialogs missing";
		return result;
	} else if (!invites || invites->type != JsonValue::Type::Object) {
		result.error = "approvals payload: invites missing";
		return result;
	} else if (issued && issued->type != JsonValue::Type::String) {
		result.error = "approvals payload: issued is not a string";
		return result;
	}
	auto approvals = Approvals();
	approvals.serial = serial->integer;
	if (issued) {
		approvals.issued = issued->string;
	}
	for (const auto &member : dialogs->object) {
		const auto id = ParseDialogKey(member.key);
		const auto tier = (member.value.type == JsonValue::Type::String)
			? TierFromWord(member.value.string)
			: std::nullopt;
		if (!id || !tier) {
			result.error = "approvals payload: a dialog entry is malformed";
			return result;
		}
		approvals.dialogs.emplace(*id, *tier);
	}
	for (const auto &member : invites->object) {
		const auto tier = (member.value.type == JsonValue::Type::String)
			? TierFromWord(member.value.string)
			: std::nullopt;
		if (!ValidInviteHash(member.key) || !tier) {
			result.error = "approvals payload: an invite entry is malformed";
			return result;
		}
		approvals.invites.emplace(member.key, *tier);
	}
	result.approvals = std::move(approvals);
	return result;
}

bool VerifyEd25519(
		const unsigned char (&publicKey)[kPublicKeySize],
		std::string_view message,
		std::string_view signature) {
	if (signature.size() != kSignatureSize) {
		return false;
	}
	const auto key = std::unique_ptr<EVP_PKEY, PkeyDeleter>(
		EVP_PKEY_new_raw_public_key(
			EVP_PKEY_ED25519,
			nullptr,
			publicKey,
			kPublicKeySize));
	const auto context = std::unique_ptr<EVP_MD_CTX, MdCtxDeleter>(
		EVP_MD_CTX_new());
	if (!key
		|| !context
		|| EVP_DigestVerifyInit(
			context.get(),
			nullptr,
			nullptr,
			nullptr,
			key.get()) != 1) {
		return false;
	}
	return EVP_DigestVerify(
		context.get(),
		reinterpret_cast<const unsigned char*>(signature.data()),
		signature.size(),
		reinterpret_cast<const unsigned char*>(message.data()),
		message.size()) == 1;
}

ApprovalsResult ReadSignedApprovals(
		std::string_view file,
		const unsigned char (&publicKey)[kPublicKeySize]) {
	auto result = ApprovalsResult();
	const auto envelope = ParseEnvelope(file, result.error);
	if (!envelope) {
		return result;
	} else if (!VerifyEd25519(
			publicKey,
			SignedMessage(envelope->payload),
			envelope->signature)) {
		result.error = "approvals: the signature does not match";
		return result;
	}
	return ParseApprovalsPayload(envelope->payload);
}

ApprovalsChoice ChooseApprovals(
		const ApprovalsResult &applied,
		bool currentPresent,
		const ApprovalsResult &current) {
	using Source = ApprovalsChoice::Source;
	auto result = ApprovalsChoice();
	result.currentBroken = currentPresent && !current.approvals;
	if (applied.approvals && current.approvals) {
		if (current.approvals->serial > applied.approvals->serial) {
			result.source = Source::Current;
		} else {
			result.source = Source::Applied;
			result.currentIgnored
				= (current.approvals->serial < applied.approvals->serial);
		}
	} else if (applied.approvals) {
		result.source = Source::Applied;
	} else if (current.approvals) {
		result.source = Source::Current;
	}
	return result;
}

} // namespace Safe::TrustRules
