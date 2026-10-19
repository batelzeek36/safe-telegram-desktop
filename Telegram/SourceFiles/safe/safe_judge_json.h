/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// The little JSON the picture check needs. Standard C++ only (no Qt, no
// tdesktop types), so the control repo's tools/judgecheck-desktop compiles and
// checks this exact file on a plain Mac. A port of the phone's SafeJson.java
// (Android M3b, src/saferelease):
// - writing: AppendString() quotes and escapes one UTF-8 value the way the
//   phone does (U+2028 / U+2029 escaped too); broken UTF-8 becomes U+FFFD so
//   the body is always valid JSON. Base64() encodes the picture bytes.
// - reading: Parse() is strict: no comments, no trailing commas, no single
//   quotes, no NaN, no leading zeros, nothing after the top-level value, a
//   depth limit and a length limit. Anything it does not like is no value,
//   which every caller treats as "no verdict". A repeated key keeps the last
//   value. JSON null is a value of its own, so a present-but-null field is
//   not mistaken for a missing one. Numbers are read without the C locale
//   (the app may run under a comma-decimal locale).

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace Safe::JudgeJson {

inline constexpr int kMaxDepth = 24;
inline constexpr std::size_t kMaxText = 4 * 1024 * 1024;

struct Value;
struct Null {
};
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value {
	std::variant<Null, bool, double, std::string, Array, Object> data;
};

// The parsed value, or nothing when the text is not strict JSON.
[[nodiscard]] std::optional<Value> Parse(std::string_view text);

// Lookups that never fail: a wrong type or a missing key is nullptr.
[[nodiscard]] const Value *Field(const Value *value, std::string_view key);
[[nodiscard]] const std::string *String(const Value *value);
[[nodiscard]] const std::string *StringField(
	const Value *value,
	std::string_view key);
[[nodiscard]] std::optional<double> Number(const Value *value);
[[nodiscard]] const Array *ArrayOf(const Value *value);
[[nodiscard]] bool IsNull(const Value *value);

// Appends `utf8` as a quoted, escaped JSON string.
void AppendString(std::string &out, std::string_view utf8);

// Standard base64 with padding and no line breaks.
[[nodiscard]] std::string Base64(std::string_view bytes);

} // namespace Safe::JudgeJson
