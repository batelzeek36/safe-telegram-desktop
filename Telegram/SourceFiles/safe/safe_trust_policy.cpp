/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_trust_policy.h"

#include <limits>
#include <utility>

namespace Safe::TrustRules {
namespace {

class JsonReader final {
public:
	JsonReader(std::string_view text, std::string_view what)
	: _s(text)
	, _what(what) {
	}

	[[nodiscard]] JsonResult parseDocument() {
		auto result = JsonResult();
		skipWhitespace();
		if (_s.substr(_pos, 3) == "\xEF\xBB\xBF") {
			_pos += 3;
			skipWhitespace();
		}
		auto value = parseValue(0);
		if (!_error.empty()) {
			result.error = std::move(_error);
			return result;
		}
		skipWhitespace();
		if (_pos != _s.size()) {
			fail("trailing data");
			result.error = std::move(_error);
			return result;
		}
		result.value = std::move(value);
		return result;
	}

private:
	[[nodiscard]] JsonValue parseValue(int depth) {
		if (depth > kMaxJsonDepth) {
			fail("nested too deep");
			return {};
		}
		skipWhitespace();
		if (_pos >= _s.size()) {
			fail("unexpected end");
			return {};
		}
		const auto c = _s[_pos];
		switch (c) {
		case '{': return parseObject(depth);
		case '[': return parseArray(depth);
		case '"': {
			auto result = JsonValue();
			result.type = JsonValue::Type::String;
			result.string = parseString();
			return result;
		}
		case 't': return parseWord("true", JsonValue::Type::Bool, true);
		case 'f': return parseWord("false", JsonValue::Type::Bool, false);
		case 'n': return parseWord("null", JsonValue::Type::Null, false);
		}
		if (c == '-' || (c >= '0' && c <= '9')) {
			return parseInteger();
		}
		fail("unexpected character");
		return {};
	}

	[[nodiscard]] JsonValue parseObject(int depth) {
		auto result = JsonValue();
		result.type = JsonValue::Type::Object;
		++_pos;
		skipWhitespace();
		if (peek() == '}') {
			++_pos;
			return result;
		}
		while (_error.empty()) {
			skipWhitespace();
			if (peek() != '"') {
				fail("expected a key");
				break;
			}
			auto key = parseString();
			if (!_error.empty()) {
				break;
			}
			skipWhitespace();
			if (peek() != ':') {
				fail("expected ':'");
				break;
			}
			++_pos;
			auto value = parseValue(depth + 1);
			if (!_error.empty()) {
				break;
			}
			if (result.find(key)) {
				fail("duplicate key");
				break;
			}
			result.object.push_back({ std::move(key), std::move(value) });
			skipWhitespace();
			const auto next = peek();
			++_pos;
			if (next == ',') {
				continue;
			} else if (next == '}') {
				return result;
			}
			fail("expected ',' or '}'");
		}
		return {};
	}

	[[nodiscard]] JsonValue parseArray(int depth) {
		auto result = JsonValue();
		result.type = JsonValue::Type::Array;
		++_pos;
		skipWhitespace();
		if (peek() == ']') {
			++_pos;
			return result;
		}
		while (_error.empty()) {
			auto value = parseValue(depth + 1);
			if (!_error.empty()) {
				break;
			}
			result.array.push_back(std::move(value));
			skipWhitespace();
			const auto next = peek();
			++_pos;
			if (next == ',') {
				continue;
			} else if (next == ']') {
				return result;
			}
			fail("expected ',' or ']'");
		}
		return {};
	}

	[[nodiscard]] static int HexValue(char c) {
		if (c >= '0' && c <= '9') {
			return c - '0';
		} else if (c >= 'a' && c <= 'f') {
			return c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			return c - 'A' + 10;
		}
		return -1;
	}

	[[nodiscard]] int readHex4() {
		if (_pos + 4 > _s.size()) {
			fail("short unicode escape");
			return -1;
		}
		auto result = 0;
		for (auto i = 0; i != 4; ++i) {
			const auto digit = HexValue(_s[_pos + i]);
			if (digit < 0) {
				fail("bad unicode escape");
				return -1;
			}
			result = result * 16 + digit;
		}
		_pos += 4;
		return result;
	}

	static void AppendUtf8(std::string &to, std::uint32_t code) {
		if (code < 0x80) {
			to.push_back(char(code));
		} else if (code < 0x800) {
			to.push_back(char(0xC0 | (code >> 6)));
			to.push_back(char(0x80 | (code & 0x3F)));
		} else if (code < 0x10000) {
			to.push_back(char(0xE0 | (code >> 12)));
			to.push_back(char(0x80 | ((code >> 6) & 0x3F)));
			to.push_back(char(0x80 | (code & 0x3F)));
		} else {
			to.push_back(char(0xF0 | (code >> 18)));
			to.push_back(char(0x80 | ((code >> 12) & 0x3F)));
			to.push_back(char(0x80 | ((code >> 6) & 0x3F)));
			to.push_back(char(0x80 | (code & 0x3F)));
		}
	}

	[[nodiscard]] std::string parseString() {
		auto result = std::string();
		++_pos;
		while (_pos < _s.size()) {
			const auto c = _s[_pos++];
			if (c == '"') {
				return result;
			} else if (static_cast<unsigned char>(c) < 0x20) {
				fail("control character in a string");
				return {};
			} else if (c != '\\') {
				result.push_back(c);
				continue;
			} else if (_pos >= _s.size()) {
				break;
			}
			const auto e = _s[_pos++];
			switch (e) {
			case '"':
			case '\\':
			case '/': result.push_back(e); break;
			case 'b': result.push_back('\b'); break;
			case 'f': result.push_back('\f'); break;
			case 'n': result.push_back('\n'); break;
			case 'r': result.push_back('\r'); break;
			case 't': result.push_back('\t'); break;
			case 'u': {
				const auto high = readHex4();
				if (high < 0) {
					return {};
				}
				auto code = std::uint32_t(high);
				if (high >= 0xD800 && high <= 0xDBFF) {
					if (_s.substr(_pos, 2) != "\\u") {
						fail("bad unicode escape");
						return {};
					}
					_pos += 2;
					const auto low = readHex4();
					if (low < 0xDC00 || low > 0xDFFF) {
						fail("bad unicode escape");
						return {};
					}
					code = 0x10000
						+ ((std::uint32_t(high) - 0xD800) << 10)
						+ (std::uint32_t(low) - 0xDC00);
				} else if (high >= 0xDC00 && high <= 0xDFFF) {
					fail("bad unicode escape");
					return {};
				}
				AppendUtf8(result, code);
			} break;
			default:
				fail("bad escape");
				return {};
			}
		}
		fail("unterminated string");
		return {};
	}

	[[nodiscard]] JsonValue parseInteger() {
		const auto negative = (peek() == '-');
		if (negative) {
			++_pos;
		}
		const auto digitsStart = _pos;
		while (_pos < _s.size() && _s[_pos] >= '0' && _s[_pos] <= '9') {
			++_pos;
		}
		const auto digits = _pos - digitsStart;
		if (!digits) {
			fail("expected digits");
			return {};
		} else if (digits > 1 && _s[digitsStart] == '0') {
			fail("leading zero");
			return {};
		} else if (_pos < _s.size()
			&& (_s[_pos] == '.' || _s[_pos] == 'e' || _s[_pos] == 'E')) {
			fail("ids must be whole numbers");
			return {};
		}
		// Accumulate as unsigned: the limit is 2^63 - 1, or 2^63 negated.
		const auto limit = negative
			? std::uint64_t(std::numeric_limits<std::int64_t>::max()) + 1
			: std::uint64_t(std::numeric_limits<std::int64_t>::max());
		auto magnitude = std::uint64_t(0);
		for (auto i = digitsStart; i != _pos; ++i) {
			const auto digit = std::uint64_t(_s[i] - '0');
			if (magnitude > (limit - digit) / 10) {
				fail("integer out of range");
				return {};
			}
			magnitude = magnitude * 10 + digit;
		}
		auto result = JsonValue();
		result.type = JsonValue::Type::Integer;
		result.integer = negative
			? (magnitude == limit
				? std::numeric_limits<std::int64_t>::min()
				: -std::int64_t(magnitude))
			: std::int64_t(magnitude);
		return result;
	}

	[[nodiscard]] JsonValue parseWord(
			std::string_view word,
			JsonValue::Type type,
			bool value) {
		if (_s.substr(_pos, word.size()) != word) {
			fail("unexpected word");
			return {};
		}
		_pos += word.size();
		auto result = JsonValue();
		result.type = type;
		result.boolean = value;
		return result;
	}

	[[nodiscard]] char peek() const {
		return (_pos < _s.size()) ? _s[_pos] : '\0';
	}

	void skipWhitespace() {
		while (_pos < _s.size()) {
			const auto c = _s[_pos];
			if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
				break;
			}
			++_pos;
		}
	}

	void fail(std::string_view reason) {
		if (_error.empty()) {
			_error = std::string(_what)
				+ ": "
				+ std::string(reason)
				+ " at offset "
				+ std::to_string(_pos);
		}
	}

	std::string_view _s;
	std::string_view _what;
	std::size_t _pos = 0;
	std::string _error;

};

// One id list of the base file. Empty optional + `error` on anything wrong.
[[nodiscard]] std::optional<std::set<std::int64_t>> IdSet(
		const JsonValue &root,
		std::string_view key,
		std::string &error) {
	const auto value = root.find(key);
	if (!value || value->type != JsonValue::Type::Array) {
		error = "trust file: "
			+ std::string(key)
			+ " is missing or not a list";
		return std::nullopt;
	}
	auto result = std::set<std::int64_t>();
	for (const auto &entry : value->array) {
		if (entry.type != JsonValue::Type::Integer) {
			error = "trust file: "
				+ std::string(key)
				+ " holds a value that is not an integer id";
			return std::nullopt;
		} else if (!entry.integer) {
			error = "trust file: " + std::string(key) + " holds id 0";
			return std::nullopt;
		}
		result.insert(entry.integer);
	}
	return result;
}

} // namespace

Tier TierOfStatus(Status status) {
	switch (status) {
	case Status::Trusted: return Tier::Trusted;
	case Status::Allowed: return Tier::Allowed;
	case Status::New: return Tier::New;
	case Status::OffKnown:
	case Status::OffNew:
	case Status::OffRequested: return Tier::Off;
	}
	return Tier::Off;
}

bool IsOpen(Tier tier) {
	return (tier == Tier::Trusted) || (tier == Tier::Allowed);
}

std::optional<Tier> TierFromWord(std::string_view word) {
	if (word == "trusted") {
		return Tier::Trusted;
	} else if (word == "allowed") {
		return Tier::Allowed;
	} else if (word == "off") {
		return Tier::Off;
	}
	return std::nullopt;
}

std::string_view WordOf(Tier tier) {
	switch (tier) {
	case Tier::Trusted: return "trusted";
	case Tier::Allowed: return "allowed";
	case Tier::New: return "new";
	case Tier::Off: return "off";
	}
	return "off";
}

std::optional<std::int64_t> ParseDialogKey(std::string_view key) {
	if (key.empty() || key.size() > 20) {
		return std::nullopt;
	}
	auto json = JsonReader(key, "key").parseDocument();
	if (!json.value
		|| json.value->type != JsonValue::Type::Integer
		|| !json.value->integer
		|| key.front() == ' '
		|| key.back() == ' ') {
		return std::nullopt;
	}
	for (const auto c : key) {
		if (c != '-' && (c < '0' || c > '9')) {
			return std::nullopt;
		}
	}
	return json.value->integer;
}

std::string DialogKey(std::int64_t dialogId) {
	return std::to_string(dialogId);
}

const JsonValue *JsonValue::find(std::string_view key) const {
	for (const auto &member : object) {
		if (member.key == key) {
			return &member.value;
		}
	}
	return nullptr;
}

JsonResult ParseJson(std::string_view text, std::string_view what) {
	if (text.size() > kMaxFileBytes) {
		auto result = JsonResult();
		result.error = std::string(what) + ": too large";
		return result;
	}
	return JsonReader(text, what).parseDocument();
}

void AppendJsonString(std::string &to, std::string_view value) {
	static constexpr char kHex[] = "0123456789abcdef";
	to.push_back('"');
	for (const auto c : value) {
		const auto u = static_cast<unsigned char>(c);
		if (c == '"' || c == '\\') {
			to.push_back('\\');
			to.push_back(c);
		} else if (u < 0x20 || u == 0x7F) {
			to += "\\u00";
			to.push_back(kHex[u >> 4]);
			to.push_back(kHex[u & 0x0F]);
		} else {
			to.push_back(c);
		}
	}
	to.push_back('"');
}

Policy::Policy(
	std::shared_ptr<const Lists> lists,
	Overrides overrides,
	std::set<std::int64_t> requested)
: _lists(std::move(lists))
, _overrides(std::move(overrides))
, _requested(std::move(requested)) {
}

Policy Policy::FailClosed(Overrides overrides) {
	return Policy(
		std::make_shared<const Lists>(),
		std::move(overrides),
		{});
}

PolicyResult Policy::Parse(std::string_view json, Overrides overrides) {
	auto result = PolicyResult();
	if (json.size() > kMaxFileBytes) {
		result.error = "trust file too large";
		return result;
	}
	const auto parsed = ParseJson(json, "trust file");
	if (!parsed.value) {
		result.error = parsed.error;
		return result;
	} else if (parsed.value->type != JsonValue::Type::Object) {
		result.error = "trust file is not a JSON object";
		return result;
	}
	auto trusted = IdSet(*parsed.value, kKeyTrusted, result.error);
	if (!trusted) {
		return result;
	}
	auto allowed = IdSet(*parsed.value, kKeyAllowed, result.error);
	if (!allowed) {
		return result;
	}
	auto known = IdSet(*parsed.value, kKeyKnown, result.error);
	if (!known) {
		return result;
	}
	auto lists = std::make_shared<Lists>();
	lists->loaded = true;
	lists->trusted = std::move(*trusted);
	lists->allowed = std::move(*allowed);
	lists->known = std::move(*known);
	result.policy = Policy(std::move(lists), std::move(overrides), {});
	return result;
}

Policy Policy::withOverrides(Overrides overrides) const {
	return Policy(_lists, std::move(overrides), _requested);
}

Policy Policy::withRequests(std::set<std::int64_t> requested) const {
	return Policy(_lists, _overrides, std::move(requested));
}

bool Policy::loaded() const {
	return _lists->loaded;
}

std::size_t Policy::trustedCount() const {
	return _lists->trusted.size();
}

std::size_t Policy::allowedCount() const {
	return _lists->allowed.size();
}

std::size_t Policy::knownCount() const {
	return _lists->known.size();
}

std::size_t Policy::overrideCount() const {
	return _overrides.size();
}

std::size_t Policy::requestCount() const {
	return _requested.size();
}

Tier Policy::tierOf(
		std::int64_t dialogId,
		PeerKind kind,
		std::int64_t selfUserId) const {
	if (!dialogId) {
		return Tier::Off;
	} else if (IsFixed(dialogId, selfUserId)) {
		return Tier::Trusted;
	}
	const auto override = _overrides.find(dialogId);
	if (override != end(_overrides)
		&& (_lists->loaded || override->second == Tier::Off)) {
		return override->second;
	} else if (_lists->trusted.contains(dialogId)) {
		return Tier::Trusted;
	} else if (_lists->allowed.contains(dialogId)) {
		return Tier::Allowed;
	} else if (_lists->known.contains(dialogId)) {
		return Tier::Off;
	} else if (kind == PeerKind::OwnChat) {
		// His own group or channel, named nowhere (never while failed closed).
		return (_lists->loaded && dialogId < 0) ? Tier::Trusted : Tier::Off;
	}
	return (kind == PeerKind::User && dialogId > 0) ? Tier::New : Tier::Off;
}

bool Policy::isOff(
		std::int64_t dialogId,
		PeerKind kind,
		std::int64_t selfUserId) const {
	return tierOf(dialogId, kind, selfUserId) == Tier::Off;
}

Status Policy::statusOf(
		std::int64_t dialogId,
		PeerKind kind,
		std::int64_t selfUserId) const {
	return statusFor(dialogId, tierOf(dialogId, kind, selfUserId));
}

Status Policy::statusFor(std::int64_t dialogId, Tier tier) const {
	switch (tier) {
	case Tier::Trusted: return Status::Trusted;
	case Tier::Allowed: return Status::Allowed;
	case Tier::New: return Status::New;
	case Tier::Off: break;
	}
	if (_requested.contains(dialogId)) {
		return Status::OffRequested;
	} else if (_lists->known.contains(dialogId)
		|| overrideOf(dialogId) == Tier::Off) {
		return Status::OffKnown;
	}
	return Status::OffNew;
}

Tier Policy::discussionTier(
		std::int64_t groupId,
		std::int64_t channelId,
		std::int64_t selfUserId) const {
	return discussionTier(
		groupId,
		PeerKind::Chat,
		channelId,
		PeerKind::Chat,
		selfUserId);
}

Tier Policy::discussionTier(
		std::int64_t groupId,
		PeerKind groupKind,
		std::int64_t channelId,
		PeerKind channelKind,
		std::int64_t selfUserId) const {
	const auto own = tierOf(groupId, groupKind, selfUserId);
	if (own != Tier::Off
		|| groupId >= 0
		|| channelId >= 0
		|| channelId == groupId) {
		return own;
	} else if (overrideOf(groupId) == Tier::Off) {
		return Tier::Off;
	}
	return IsOpen(tierOf(channelId, channelKind, selfUserId))
		? Tier::Allowed
		: Tier::Off;
}

Status Policy::discussionStatus(
		std::int64_t groupId,
		std::int64_t channelId,
		std::int64_t selfUserId) const {
	return statusFor(groupId, discussionTier(groupId, channelId, selfUserId));
}

Status Policy::discussionStatus(
		std::int64_t groupId,
		PeerKind groupKind,
		std::int64_t channelId,
		PeerKind channelKind,
		std::int64_t selfUserId) const {
	return statusFor(groupId, discussionTier(
		groupId,
		groupKind,
		channelId,
		channelKind,
		selfUserId));
}

bool Policy::isUnlisted(std::int64_t dialogId) const {
	return !_overrides.contains(dialogId) && !baseTier(dialogId).has_value();
}

std::vector<std::int64_t> Policy::listedIds() const {
	auto all = std::set<std::int64_t>();
	all.insert(begin(_lists->trusted), end(_lists->trusted));
	all.insert(begin(_lists->allowed), end(_lists->allowed));
	all.insert(begin(_lists->known), end(_lists->known));
	for (const auto &[id, tier] : _overrides) {
		all.insert(id);
	}
	all.erase(0);
	return { begin(all), end(all) };
}

std::optional<Tier> Policy::baseTier(std::int64_t dialogId) const {
	if (_lists->trusted.contains(dialogId)) {
		return Tier::Trusted;
	} else if (_lists->allowed.contains(dialogId)) {
		return Tier::Allowed;
	} else if (_lists->known.contains(dialogId)) {
		return Tier::Off;
	}
	return std::nullopt;
}

std::optional<Tier> Policy::overrideOf(std::int64_t dialogId) const {
	const auto i = _overrides.find(dialogId);
	return (i != end(_overrides))
		? std::make_optional(i->second)
		: std::nullopt;
}

bool Policy::requested(std::int64_t dialogId) const {
	return _requested.contains(dialogId);
}

bool Policy::IsFixed(std::int64_t dialogId, std::int64_t selfUserId) {
	return (dialogId == kTelegramServiceId)
		|| (selfUserId != 0 && dialogId == selfUserId);
}

} // namespace Safe::TrustRules
