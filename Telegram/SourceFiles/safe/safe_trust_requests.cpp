/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_trust_requests.h"

#include <algorithm>
#include <array>

namespace Safe::TrustRules {
namespace {

constexpr auto kKinds = std::array<std::string_view, 5>{
	"person", "bot", "group", "channel", "chat" };
constexpr auto kReasons = std::array<std::string_view, 11>{
	"join", "invite", "link", "open", "bot", "miniapp", "game", "payment",
	"inline", "call", "other" };

template <std::size_t N>
[[nodiscard]] bool OneOf(
		std::string_view value,
		const std::array<std::string_view, N> &words) {
	return std::find(begin(words), end(words), value) != end(words);
}

[[nodiscard]] std::string StringOr(
		const JsonValue &object,
		std::string_view key,
		std::string_view fallback) {
	const auto value = object.find(key);
	return (value && value->type == JsonValue::Type::String)
		? value->string
		: std::string(fallback);
}

[[nodiscard]] std::int64_t IntegerOr(
		const JsonValue &object,
		std::string_view key,
		std::int64_t fallback) {
	const auto value = object.find(key);
	return (value && value->type == JsonValue::Type::Integer)
		? value->integer
		: fallback;
}

// Length of the UTF-8 sequence at `i`, 0 when it is not valid UTF-8.
[[nodiscard]] std::size_t Utf8Length(std::string_view s, std::size_t i) {
	const auto lead = static_cast<unsigned char>(s[i]);
	const auto length = (lead < 0x80) ? 1
		: ((lead & 0xE0) == 0xC0 && lead >= 0xC2) ? 2
		: ((lead & 0xF0) == 0xE0) ? 3
		: ((lead & 0xF8) == 0xF0 && lead <= 0xF4) ? 4
		: 0;
	if (!length || i + length > s.size()) {
		return 0;
	}
	for (auto k = std::size_t(1); k != std::size_t(length); ++k) {
		if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
			return 0;
		}
	}
	return std::size_t(length);
}

void AppendField(
		std::string &to,
		std::string_view key,
		std::string_view value,
		bool first = false) {
	if (!first) {
		to.push_back(',');
	}
	AppendJsonString(to, key);
	to.push_back(':');
	AppendJsonString(to, value);
}

void AppendField(std::string &to, std::string_view key, std::int64_t value) {
	to.push_back(',');
	AppendJsonString(to, key);
	to.push_back(':');
	to += std::to_string(value);
}

[[nodiscard]] const JsonValue *ListOf(
		std::string_view text,
		std::string_view what,
		std::string_view key,
		std::optional<JsonValue> &holder,
		std::string &error) {
	auto parsed = ParseJson(text, what);
	if (!parsed.value) {
		error = parsed.error;
		return nullptr;
	}
	holder = std::move(parsed.value);
	const auto list = (holder->type == JsonValue::Type::Object)
		? holder->find(key)
		: nullptr;
	if (!list || list->type != JsonValue::Type::Array) {
		error = std::string(what) + ": not the expected shape";
		return nullptr;
	}
	return list;
}

} // namespace

std::string KeyForDialog(std::int64_t dialogId) {
	return DialogKey(dialogId);
}

std::optional<std::string> KeyForInvite(std::string_view hash) {
	if (!ValidInviteHash(hash)) {
		return std::nullopt;
	}
	return std::string(kInvitePrefix) + std::string(hash);
}

std::optional<std::string> InviteHashOf(std::string_view key) {
	if (!key.starts_with(kInvitePrefix)) {
		return std::nullopt;
	}
	const auto hash = key.substr(kInvitePrefix.size());
	return ValidInviteHash(hash)
		? std::make_optional(std::string(hash))
		: std::nullopt;
}

std::string CleanTitle(std::string_view title) {
	auto result = std::string();
	auto characters = std::size_t(0);
	for (auto i = std::size_t(0); i < title.size()
			&& characters < kMaxTitle; ++characters) {
		const auto length = Utf8Length(title, i);
		if (!length) {
			result.push_back('?');
			++i;
			continue;
		}
		const auto c = static_cast<unsigned char>(title[i]);
		if (length == 1 && (c < 0x20 || c == 0x7F)) {
			result.push_back(' ');
		} else {
			result.append(title.substr(i, length));
		}
		i += length;
	}
	return result;
}

std::string CleanUsername(std::string_view username) {
	if (username.starts_with('@')) {
		username.remove_prefix(1);
	}
	if (username.empty() || username.size() > kMaxUsername) {
		return {};
	}
	for (const auto c : username) {
		const auto ok = (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| c == '_';
		if (!ok) {
			return {};
		}
	}
	return std::string(username);
}

std::optional<Request> CleanRequest(Request request) {
	if (request.dialogId) {
		if (request.key != KeyForDialog(request.dialogId)) {
			return std::nullopt;
		}
		const auto person = (request.peer == "user");
		const auto chat = (request.peer == "chat")
			|| (request.peer == "channel");
		if ((person && request.dialogId < 0)
			|| (chat && request.dialogId > 0)
			|| (!person && !chat)) {
			return std::nullopt;
		}
	} else {
		if (!InviteHashOf(request.key)) {
			return std::nullopt;
		}
		request.peer = "invite";
	}
	if (!OneOf(request.kind, kKinds)) {
		request.kind = "chat";
	}
	if (!OneOf(request.reason, kReasons)) {
		request.reason = "other";
	}
	request.title = CleanTitle(request.title);
	request.username = CleanUsername(request.username);
	request.firstSeen = std::max<std::int64_t>(request.firstSeen, 0);
	request.serial = std::max<std::int64_t>(request.serial, 0);
	return request;
}

std::string EncodeRequests(const std::vector<Request> &list) {
	auto result = std::string("{\"requests\":[");
	auto first = true;
	for (const auto &request : list) {
		if (!first) {
			result.push_back(',');
		}
		first = false;
		result.push_back('\n');
		result.push_back('{');
		AppendField(result, "key", request.key, true);
		AppendField(result, "dialog_id", request.dialogId);
		AppendField(result, "peer", request.peer);
		AppendField(result, "kind", request.kind);
		AppendField(result, "title", request.title);
		AppendField(result, "username", request.username);
		AppendField(result, "reason", request.reason);
		AppendField(result, "first_seen", request.firstSeen);
		AppendField(result, "serial", request.serial);
		result.push_back('}');
	}
	result += "\n]}\n";
	return result;
}

std::vector<Request> DecodeRequests(
		std::string_view text,
		std::string &error) {
	auto holder = std::optional<JsonValue>();
	const auto list = ListOf(text, "requests", "requests", holder, error);
	if (!list) {
		return {};
	}
	auto result = std::vector<Request>();
	auto keys = std::set<std::string>();
	for (const auto &entry : list->array) {
		if (entry.type != JsonValue::Type::Object
			|| result.size() >= kMaxRequests) {
			continue;
		}
		auto request = Request();
		request.key = StringOr(entry, "key", "");
		request.dialogId = IntegerOr(entry, "dialog_id", 0);
		request.peer = StringOr(entry, "peer", "");
		request.kind = StringOr(entry, "kind", "chat");
		request.title = StringOr(entry, "title", "");
		request.username = StringOr(entry, "username", "");
		request.reason = StringOr(entry, "reason", "other");
		request.firstSeen = IntegerOr(entry, "first_seen", 0);
		request.serial = IntegerOr(entry, "serial", 0);
		auto clean = CleanRequest(std::move(request));
		if (clean && keys.insert(clean->key).second) {
			result.push_back(std::move(*clean));
		}
	}
	return result;
}

bool Answered(
		const Request &request,
		const std::optional<Approvals> &approvals) {
	if (!approvals || approvals->serial <= request.serial) {
		return false;
	} else if (request.dialogId) {
		return approvals->dialogs.contains(request.dialogId);
	}
	const auto hash = InviteHashOf(request.key);
	return hash && approvals->invites.contains(*hash);
}

bool AddRequest(
		std::vector<Request> &list,
		Request request,
		const std::optional<Approvals> &approvals) {
	auto clean = CleanRequest(std::move(request));
	if (!clean) {
		return false;
	}
	const auto i = std::find_if(begin(list), end(list), [&](
			const Request &existing) {
		return existing.key == clean->key;
	});
	if (i != end(list)) {
		if (!Answered(*i, approvals)) {
			return false;
		}
		list.erase(i);
	}
	if (list.size() >= kMaxRequests) {
		return false;
	}
	list.push_back(std::move(*clean));
	return true;
}

bool DropAnswered(
		std::vector<Request> &list,
		const std::optional<Approvals> &approvals) {
	const auto was = list.size();
	list.erase(std::remove_if(begin(list), end(list), [&](
			const Request &request) {
		return Answered(request, approvals);
	}), end(list));
	return list.size() != was;
}

std::set<std::int64_t> PendingDialogIds(
		const std::vector<Request> &list,
		const std::optional<Approvals> &approvals) {
	auto result = std::set<std::int64_t>();
	for (const auto &request : list) {
		if (request.dialogId && !Answered(request, approvals)) {
			result.insert(request.dialogId);
		}
	}
	return result;
}

bool InvitePending(
		const std::vector<Request> &list,
		std::string_view hash,
		const std::optional<Approvals> &approvals) {
	const auto key = KeyForInvite(hash);
	if (!key) {
		return false;
	}
	for (const auto &request : list) {
		if (request.key == *key) {
			return !Answered(request, approvals);
		}
	}
	return false;
}

std::string EncodeJoins(const std::vector<InviteJoin> &list) {
	auto result = std::string("{\"joins\":[");
	auto first = true;
	for (const auto &join : list) {
		if (!first) {
			result.push_back(',');
		}
		first = false;
		result += "\n{";
		AppendField(result, "hash", join.hash, true);
		AppendField(result, "dialog_id", join.dialogId);
		AppendField(result, "peer", join.peer);
		AppendField(result, "title", join.title);
		AppendField(result, "time", join.time);
		result.push_back('}');
	}
	result += "\n]}\n";
	return result;
}

std::vector<InviteJoin> DecodeJoins(
		std::string_view text,
		std::string &error) {
	auto holder = std::optional<JsonValue>();
	const auto list = ListOf(text, "invite joins", "joins", holder, error);
	if (!list) {
		return {};
	}
	auto result = std::vector<InviteJoin>();
	for (const auto &entry : list->array) {
		if (entry.type != JsonValue::Type::Object) {
			continue;
		}
		auto join = InviteJoin();
		join.hash = StringOr(entry, "hash", "");
		join.dialogId = IntegerOr(entry, "dialog_id", 0);
		join.peer = StringOr(entry, "peer", "");
		join.title = StringOr(entry, "title", "");
		join.time = IntegerOr(entry, "time", 0);
		AddJoin(result, std::move(join));
	}
	return result;
}

bool AddJoin(std::vector<InviteJoin> &list, InviteJoin join) {
	// An invite always leads to a group or a channel: a negative id.
	if (!ValidInviteHash(join.hash)
		|| join.dialogId >= 0
		|| (join.peer != "chat" && join.peer != "channel")) {
		return false;
	}
	join.title = CleanTitle(join.title);
	join.time = std::max<std::int64_t>(join.time, 0);
	const auto i = std::find_if(begin(list), end(list), [&](
			const InviteJoin &existing) {
		return existing.hash == join.hash;
	});
	if (i != end(list)) {
		if (i->dialogId == join.dialogId && i->peer == join.peer) {
			return false;
		}
		*i = std::move(join);
		return true;
	} else if (list.size() >= kMaxJoins) {
		return false;
	}
	list.push_back(std::move(join));
	return true;
}

Overrides EffectiveOverrides(
		const std::optional<Approvals> &approvals,
		const std::vector<InviteJoin> &joins) {
	if (!approvals) {
		return {};
	}
	auto result = approvals->dialogs;
	auto fromJoins = Overrides();
	for (const auto &join : joins) {
		const auto i = approvals->invites.find(join.hash);
		if (i == end(approvals->invites)
			|| approvals->dialogs.contains(join.dialogId)) {
			continue;
		}
		// Two invites into one chat: the stricter answer wins.
		auto &tier = fromJoins.try_emplace(join.dialogId, i->second)
			.first->second;
		tier = std::max(tier, i->second);
	}
	result.insert(begin(fromJoins), end(fromJoins));
	return result;
}

} // namespace Safe::TrustRules
