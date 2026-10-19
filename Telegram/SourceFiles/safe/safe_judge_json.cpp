/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_judge_json.h"

#include <cmath>
#include <cstdint>

namespace Safe::JudgeJson {
namespace {

constexpr char kBase64[]
	= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char kHex[] = "0123456789abcdef";
constexpr std::uint32_t kReplacement = 0xFFFD;

void AppendUtf8(std::string &out, std::uint32_t code) {
	if (code < 0x80) {
		out.push_back(char(code));
	} else if (code < 0x800) {
		out.push_back(char(0xC0 | (code >> 6)));
		out.push_back(char(0x80 | (code & 0x3F)));
	} else if (code < 0x10000) {
		out.push_back(char(0xE0 | (code >> 12)));
		out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
		out.push_back(char(0x80 | (code & 0x3F)));
	} else {
		out.push_back(char(0xF0 | (code >> 18)));
		out.push_back(char(0x80 | ((code >> 12) & 0x3F)));
		out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
		out.push_back(char(0x80 | (code & 0x3F)));
	}
}

// Decodes one UTF-8 sequence at `i`, advancing past it. Overlong forms,
// surrogates, values past U+10FFFF and cut sequences read as U+FFFD, one
// byte at a time.
std::uint32_t NextCodePoint(std::string_view s, std::size_t &i) {
	const auto byte = std::uint8_t(s[i]);
	if (byte < 0x80) {
		++i;
		return byte;
	}
	auto length = 0;
	auto code = std::uint32_t(0);
	auto minimum = std::uint32_t(0);
	if ((byte & 0xE0) == 0xC0) {
		length = 2;
		code = byte & 0x1F;
		minimum = 0x80;
	} else if ((byte & 0xF0) == 0xE0) {
		length = 3;
		code = byte & 0x0F;
		minimum = 0x800;
	} else if ((byte & 0xF8) == 0xF0) {
		length = 4;
		code = byte & 0x07;
		minimum = 0x10000;
	} else {
		++i;
		return kReplacement;
	}
	if (i + length > s.size()) {
		++i;
		return kReplacement;
	}
	for (auto k = 1; k < length; ++k) {
		const auto next = std::uint8_t(s[i + k]);
		if ((next & 0xC0) != 0x80) {
			++i;
			return kReplacement;
		}
		code = (code << 6) | (next & 0x3F);
	}
	if (code < minimum
		|| code > 0x10FFFF
		|| (code >= 0xD800 && code <= 0xDFFF)) {
		++i;
		return kReplacement;
	}
	i += length;
	return code;
}

void AppendEscaped(std::string &out, std::uint32_t code) {
	out.append("\\u");
	out.push_back(kHex[(code >> 12) & 0xF]);
	out.push_back(kHex[(code >> 8) & 0xF]);
	out.push_back(kHex[(code >> 4) & 0xF]);
	out.push_back(kHex[code & 0xF]);
}

class Reader final {
public:
	explicit Reader(std::string_view text) : _s(text) {
	}

	[[nodiscard]] bool value(Value &out, int depth) {
		if (depth > kMaxDepth) {
			return false;
		}
		skipSpace();
		if (done()) {
			return false;
		}
		switch (_s[_i]) {
		case '{': return object(out, depth);
		case '[': return array(out, depth);
		case '"': {
			auto text = std::string();
			if (!string(text)) {
				return false;
			}
			out.data = std::move(text);
			return true;
		}
		case 't':
			out.data = true;
			return literal("true");
		case 'f':
			out.data = false;
			return literal("false");
		case 'n':
			out.data = Null();
			return literal("null");
		default: return number(out);
		}
	}

	void skipSpace() {
		while (!done()) {
			const auto c = _s[_i];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				++_i;
			} else {
				break;
			}
		}
	}

	[[nodiscard]] bool done() const {
		return _i >= _s.size();
	}

private:
	[[nodiscard]] bool object(Value &out, int depth) {
		++_i; // '{'
		auto members = Object();
		skipSpace();
		if (!done() && _s[_i] == '}') {
			++_i;
			out.data = std::move(members);
			return true;
		}
		while (true) {
			skipSpace();
			auto key = std::string();
			if (done() || _s[_i] != '"' || !string(key)) {
				return false;
			}
			skipSpace();
			if (done() || _s[_i] != ':') {
				return false;
			}
			++_i;
			auto item = Value();
			if (!value(item, depth + 1)) {
				return false;
			}
			auto replaced = false;
			for (auto &[name, existing] : members) {
				if (name == key) {
					existing = std::move(item); // a repeated key keeps the last
					replaced = true;
					break;
				}
			}
			if (!replaced) {
				members.emplace_back(std::move(key), std::move(item));
			}
			skipSpace();
			if (done()) {
				return false;
			}
			const auto c = _s[_i++];
			if (c == '}') {
				out.data = std::move(members);
				return true;
			} else if (c != ',') {
				return false;
			}
		}
	}

	[[nodiscard]] bool array(Value &out, int depth) {
		++_i; // '['
		auto items = Array();
		skipSpace();
		if (!done() && _s[_i] == ']') {
			++_i;
			out.data = std::move(items);
			return true;
		}
		while (true) {
			auto item = Value();
			if (!value(item, depth + 1)) {
				return false;
			}
			items.push_back(std::move(item));
			skipSpace();
			if (done()) {
				return false;
			}
			const auto c = _s[_i++];
			if (c == ']') {
				out.data = std::move(items);
				return true;
			} else if (c != ',') {
				return false;
			}
		}
	}

	[[nodiscard]] bool hex4(std::uint32_t &out) {
		if (_i + 4 > _s.size()) {
			return false;
		}
		auto n = std::uint32_t(0);
		for (auto k = 0; k < 4; ++k) {
			const auto c = _s[_i + k];
			auto d = 0;
			if (c >= '0' && c <= '9') {
				d = c - '0';
			} else if (c >= 'a' && c <= 'f') {
				d = c - 'a' + 10;
			} else if (c >= 'A' && c <= 'F') {
				d = c - 'A' + 10;
			} else {
				return false;
			}
			n = n * 16 + std::uint32_t(d);
		}
		_i += 4;
		out = n;
		return true;
	}

	[[nodiscard]] bool string(std::string &out) {
		++_i; // '"'
		while (true) {
			if (done()) {
				return false;
			}
			const auto c = _s[_i++];
			if (c == '"') {
				return true;
			} else if (std::uint8_t(c) < 0x20) {
				return false;
			} else if (c != '\\') {
				out.push_back(c);
				continue;
			}
			if (done()) {
				return false;
			}
			switch (_s[_i++]) {
			case '"': out.push_back('"'); break;
			case '\\': out.push_back('\\'); break;
			case '/': out.push_back('/'); break;
			case 'b': out.push_back('\b'); break;
			case 'f': out.push_back('\f'); break;
			case 'n': out.push_back('\n'); break;
			case 'r': out.push_back('\r'); break;
			case 't': out.push_back('\t'); break;
			case 'u': {
				auto code = std::uint32_t(0);
				if (!hex4(code)) {
					return false;
				}
				if (code >= 0xD800 && code <= 0xDBFF) {
					// A high surrogate pairs with a following low one.
					auto low = std::uint32_t(0);
					const auto mark = _i;
					if (_i + 1 < _s.size()
						&& _s[_i] == '\\'
						&& _s[_i + 1] == 'u') {
						_i += 2;
						if (!hex4(low)) {
							return false;
						}
						if (low >= 0xDC00 && low <= 0xDFFF) {
							code = 0x10000
								+ ((code - 0xD800) << 10)
								+ (low - 0xDC00);
						} else {
							_i = mark;
							code = kReplacement;
						}
					} else {
						code = kReplacement;
					}
				} else if (code >= 0xDC00 && code <= 0xDFFF) {
					code = kReplacement;
				}
				AppendUtf8(out, code);
			} break;
			default: return false;
			}
		}
	}

	// One or more digits, appended to `mantissa`; `leading` refuses a
	// leading zero in a longer integer part.
	[[nodiscard]] bool digits(bool leading, double &mantissa, int &count) {
		const auto start = _i;
		while (!done() && _s[_i] >= '0' && _s[_i] <= '9') {
			mantissa = mantissa * 10. + double(_s[_i] - '0');
			++_i;
		}
		count = int(_i - start);
		if (count == 0) {
			return false;
		}
		return !(leading && count > 1 && _s[start] == '0');
	}

	[[nodiscard]] bool number(Value &out) {
		auto negative = false;
		if (_s[_i] == '-') {
			negative = true;
			++_i;
		}
		auto mantissa = 0.;
		auto count = 0;
		if (!digits(true, mantissa, count)) {
			return false;
		}
		auto scale = 0;
		if (!done() && _s[_i] == '.') {
			++_i;
			if (!digits(false, mantissa, count)) {
				return false;
			}
			scale -= count;
		}
		if (!done() && (_s[_i] == 'e' || _s[_i] == 'E')) {
			++_i;
			auto exponentNegative = false;
			if (!done() && (_s[_i] == '+' || _s[_i] == '-')) {
				exponentNegative = (_s[_i] == '-');
				++_i;
			}
			auto exponent = 0.;
			if (!digits(false, exponent, count)) {
				return false;
			}
			if (exponent > 100000.) {
				exponent = 100000.;
			}
			scale += int(exponentNegative ? -exponent : exponent);
		}
		auto result = mantissa;
		if (scale != 0 && mantissa != 0.) {
			result = mantissa * std::pow(10., double(scale));
		}
		if (std::isnan(result) || std::isinf(result)) {
			return false;
		}
		out.data = negative ? -result : result;
		return true;
	}

	[[nodiscard]] bool literal(std::string_view word) {
		if (_s.substr(_i, word.size()) != word) {
			return false;
		}
		_i += word.size();
		return true;
	}

	std::string_view _s;
	std::size_t _i = 0;

};

} // namespace

std::optional<Value> Parse(std::string_view text) {
	if (text.empty() || text.size() > kMaxText) {
		return std::nullopt;
	}
	auto reader = Reader(text);
	auto result = Value();
	if (!reader.value(result, 0)) {
		return std::nullopt;
	}
	reader.skipSpace();
	if (!reader.done()) {
		return std::nullopt;
	}
	return result;
}

const Value *Field(const Value *value, std::string_view key) {
	if (!value) {
		return nullptr;
	}
	const auto object = std::get_if<Object>(&value->data);
	if (!object) {
		return nullptr;
	}
	for (const auto &[name, item] : *object) {
		if (name == key) {
			return &item;
		}
	}
	return nullptr;
}

const std::string *String(const Value *value) {
	return value ? std::get_if<std::string>(&value->data) : nullptr;
}

const std::string *StringField(const Value *value, std::string_view key) {
	return String(Field(value, key));
}

std::optional<double> Number(const Value *value) {
	const auto number = value ? std::get_if<double>(&value->data) : nullptr;
	return number ? std::make_optional(*number) : std::nullopt;
}

const Array *ArrayOf(const Value *value) {
	return value ? std::get_if<Array>(&value->data) : nullptr;
}

bool IsNull(const Value *value) {
	return value && std::holds_alternative<Null>(value->data);
}

void AppendString(std::string &out, std::string_view utf8) {
	out.push_back('"');
	auto i = std::size_t(0);
	while (i < utf8.size()) {
		const auto code = NextCodePoint(utf8, i);
		switch (code) {
		case '"': out.append("\\\""); break;
		case '\\': out.append("\\\\"); break;
		case '\n': out.append("\\n"); break;
		case '\r': out.append("\\r"); break;
		case '\t': out.append("\\t"); break;
		case '\b': out.append("\\b"); break;
		case '\f': out.append("\\f"); break;
		default:
			if (code < 0x20 || code == 0x7F || code == 0x2028 || code == 0x2029) {
				AppendEscaped(out, code);
			} else {
				AppendUtf8(out, code);
			}
		}
	}
	out.push_back('"');
}

std::string Base64(std::string_view bytes) {
	auto out = std::string();
	out.reserve(((bytes.size() + 2) / 3) * 4);
	auto i = std::size_t(0);
	const auto at = [&](std::size_t index) {
		return std::uint32_t(std::uint8_t(bytes[index]));
	};
	for (; i + 3 <= bytes.size(); i += 3) {
		const auto n = (at(i) << 16) | (at(i + 1) << 8) | at(i + 2);
		out.push_back(kBase64[(n >> 18) & 63]);
		out.push_back(kBase64[(n >> 12) & 63]);
		out.push_back(kBase64[(n >> 6) & 63]);
		out.push_back(kBase64[n & 63]);
	}
	const auto rest = bytes.size() - i;
	if (rest == 1) {
		const auto n = at(i) << 16;
		out.push_back(kBase64[(n >> 18) & 63]);
		out.push_back(kBase64[(n >> 12) & 63]);
		out.append("==");
	} else if (rest == 2) {
		const auto n = (at(i) << 16) | (at(i + 1) << 8);
		out.push_back(kBase64[(n >> 18) & 63]);
		out.push_back(kBase64[(n >> 12) & 63]);
		out.push_back(kBase64[(n >> 6) & 63]);
		out.push_back('=');
	}
	return out;
}

} // namespace Safe::JudgeJson
