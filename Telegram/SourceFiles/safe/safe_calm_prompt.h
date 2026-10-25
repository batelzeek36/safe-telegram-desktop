/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// What one calm check sends and what comes back, for both ways of asking:
// the runner on MAINGEAR (POST /v1/check, calm-runner/calm_runner.py in the
// control repo) and the Messages API fallback, which carries the runner's own
// system prompt and schema (calm-runner/judge.py: keep the two in step).
namespace Safe::Calm {

enum class Direction : uchar {
	Out, // a draft Zach is about to send
	In, // a message someone sent to him
};

struct ContextLine {
	QString from;
	QString text;
	QString ago; // how long before now it was sent: "now", "4m", "3h", "2d"
};

struct Request {
	Direction direction = Direction::Out;
	QString text;
	QString sender;
	QString recipient;
	std::vector<ContextLine> context;
};

struct Verdict {
	bool soften = false;
	bool toThem = false;
	bool ifSeen = false;
	int heat = 0; // 0 when fine, 1 a bit sharp .. 3 a full crash-out
	QStringList phrases; // exact substrings of the original (out only)
	QStringList notes; // one or two lines to Zach about his draft (out only)
	QString text; // the calm version; the exact original when fine
	QString via; // "warm" / "cold" (runner) or "api"
};

struct Result {
	std::optional<Verdict> verdict;
	QString failure; // why there is no verdict ("timeout", "http 429", ...)
};

inline constexpr auto kRunnerTimeoutMs = 9000;
inline constexpr auto kApiTimeoutMs = 14000;
inline constexpr auto kMaxResponseBytes = 64 * 1024;
inline constexpr auto kMaxTextLength = 4000; // the runner's MAX_TEXT
inline constexpr auto kMaxContext = 50; // the runner's MAX_CONTEXT
inline constexpr auto kMaxContextText = 1500;
inline constexpr auto kMaxAgo = 16;

inline constexpr auto kApiEndpoint = "https://api.anthropic.com/v1/messages";
inline constexpr auto kApiVersion = "2023-06-01";
inline constexpr auto kApiModel = "claude-sonnet-5-5";

[[nodiscard]] QByteArray RunnerBody(const Request &request);
[[nodiscard]] QByteArray ApiBody(const Request &request);

[[nodiscard]] Result ParseRunner(
	int status,
	const QByteArray &body,
	const Request &request);
[[nodiscard]] Result ParseApi(
	int status,
	const QByteArray &body,
	const Request &request);

// The runner's judge.normalize(): bounds and cleans an answer, "fine" always
// hands back the exact original, a rewrite equal to it counts as fine, and
// their side (In) never carries phrases or notes quoted from the original.
[[nodiscard]] std::optional<Verdict> Normalize(
	const QJsonObject &answer,
	const Request &request);

} // namespace Safe::Calm
