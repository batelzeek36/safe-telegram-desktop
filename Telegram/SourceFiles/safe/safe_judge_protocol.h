/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// The picture check's request and answer. Standard C++ only (no Qt, no
// tdesktop types), so tools/judgecheck-desktop checks this exact file on a
// plain Mac and compares its request bodies byte for byte with the phone's.
// A port of SafeJudgeProtocol.java (Android M3b): one POST /v1/messages per
// picture, Claude Haiku 5.5, the rubric (safe_judge_rubric.h, generated from
// the phone's file) as the system prompt, the cover and up to 3 frames as
// base64 image blocks, the answer held to {"verdict":"show"|"hide","reason"}
// by structured outputs, thinking off at effort "low".
//
// Never sent, because Haiku 5.5 refuses them: temperature, top_p, top_k,
// budget_tokens, an assistant prefill, fallbacks.
//
// Reading an answer: stop_reason first ("refusal" = Flag: fail closed,
// "max_tokens" = no verdict), then the content blocks by type. Anything that
// is not a clean verdict is a failure with a short reason from a fixed
// vocabulary (safe to log); the model's own words stay in modelReason, which
// the app passes nowhere.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Safe::JudgeProtocol {

inline constexpr std::string_view kEndpoint
	= "https://api.anthropic.com/v1/messages";
inline constexpr std::string_view kModel = "claude-haiku-5-5";
inline constexpr std::string_view kApiVersion = "2023-06-01";
inline constexpr int kMaxTokens = 256;
inline constexpr std::string_view kEffort = "low";

// The rubric's identity, stored with every answer: bump it with the phone's
// SafeJudge.NAME whenever the rubric changes (tools/judge_rubric.py checks).
inline constexpr std::string_view kJudgeName = "haiku55-1";

inline constexpr std::string_view kVerdictShow = "show";
inline constexpr std::string_view kVerdictHide = "hide";

// Dollars per million tokens at prompts of 100K tokens or less.
inline constexpr double kInputPerMTok = 0.10;
inline constexpr double kOutputPerMTok = 0.50;

inline constexpr int kMaxFrames = 3;
inline constexpr int kMaxSide = 256;
inline constexpr int kStrippedMaxSide = 64;
inline constexpr std::size_t kMaxTextUnits = 1024;
inline constexpr std::size_t kMaxPictureBytes = 1024 * 1024;
inline constexpr std::size_t kMaxResponseBytes = 256 * 1024;
inline constexpr int kMaxInFlight = 2;
inline constexpr int kTimeoutMs = 20 * 1000;

// What the picture is, in the phone's words ("Kind: photo.").
enum class Kind : unsigned char {
	Photo,
	Video,
	Gif,
	Sticker,
	Other,
};

[[nodiscard]] std::string_view KindName(Kind kind);
[[nodiscard]] Kind KindFor(bool document, std::string_view mime);

struct Request {
	Kind kind = Kind::Other;
	std::string cover; // Picture file bytes (the Mac sends JPEG).
	bool coverStripped = false; // Only Telegram's tiny inline preview.
	std::vector<std::string> frames;
	std::string caption; // UTF-8, untrusted.
	std::string fileName; // UTF-8, untrusted.
	std::string mime; // UTF-8, untrusted.
};

enum class Verdict : unsigned char {
	None,
	Clear,
	Flag,
};

struct Result {
	Verdict verdict = Verdict::None;
	std::string failure; // Fixed vocabulary, safe to log. Empty = verdict.
	std::string modelReason; // Picture-derived: never logged, never stored.
	int inputTokens = 0;
	int outputTokens = 0;

	[[nodiscard]] double cost() const;
};

[[nodiscard]] Result Failed(
	std::string_view failure,
	int inputTokens = 0,
	int outputTokens = 0);

// The media type the API needs for these bytes, or nothing for bytes that
// are not a JPEG, PNG, GIF or WebP file (those are never sent).
[[nodiscard]] std::optional<std::string_view> MediaTypeOf(
	std::string_view bytes);

// The request body, or nothing when there is no picture to send.
[[nodiscard]] std::optional<std::string> RequestBody(const Request &request);

// The one text block: what the picture is, then the untrusted texts.
[[nodiscard]] std::string UserText(const Request &request, int frames);

// `status` is the HTTP status, `body` the response text.
[[nodiscard]] Result Parse(int status, std::string_view body);

// Keeps a logged word to [A-Za-z0-9_-], at most 32 characters.
[[nodiscard]] std::string Clean(std::string_view word);

// The key format the phone build accepts: [A-Za-z0-9_-]{16,200}.
[[nodiscard]] bool KeyLooksValid(std::string_view key);

// Telegram's inline preview is about 40 px: say so to the model.
[[nodiscard]] bool LooksStripped(int width, int height);

} // namespace Safe::JudgeProtocol
