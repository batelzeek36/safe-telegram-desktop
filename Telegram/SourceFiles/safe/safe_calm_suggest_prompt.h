/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_calm_prompt.h"

// Reply suggestions (Zach 10-25: "if idk what to say or i just dont want to
// put energy into thinking of what to say"): three replies in his voice for
// the chat's next message, or for one message he picked. The runner's
// POST /v1/suggest first, then the Messages API with the runner's own prompt
// (calm-runner/suggest.py: keep the two in step).
namespace Safe::Calm {

struct SuggestRequest {
	QString recipient;
	std::vector<ContextLine> context;
	std::optional<ContextLine> replyTo; // the message he picked to answer
};

struct Suggestion {
	QString text;
	QString angle; // "warm", "plan", "question back", ...
};

struct SuggestResult {
	std::vector<Suggestion> list;
	QString via; // "warm" / "cold" (runner) or "api"
	QString failure; // why the list is empty
};

inline constexpr auto kSuggestCount = 3;

[[nodiscard]] QByteArray SuggestRunnerBody(const SuggestRequest &request);
[[nodiscard]] QByteArray SuggestApiBody(const SuggestRequest &request);
[[nodiscard]] SuggestResult ParseSuggestRunner(int status, const QByteArray &body);
[[nodiscard]] SuggestResult ParseSuggestApi(int status, const QByteArray &body);

} // namespace Safe::Calm
