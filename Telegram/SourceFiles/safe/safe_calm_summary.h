/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_calm_prompt.h"

// One message instead of a run (Zach 10-27: "the AI should detect when im
// mass-sending messages that could overwhelm the reader, and it should
// offer to delete and summarize what i said in my voice"; then "my mother
// is going to read what i just said as pressure": the one message is calmer
// too, and his GIFs, stickers and photos stay where they are). The runner's
// POST /v1/summarize only (calm-runner/summarize.py): never the paid API,
// no answer = no offer (docs/desktop/STATE-SUMMARIZE.md).
namespace Safe::Calm {

enum class RunMedia : uchar {
	None, // a text message
	Gif,
	Sticker,
	Photo,
	Video,
};

struct RunLine {
	QString text; // a text message; empty for media
	RunMedia media = RunMedia::None;
	QString caption; // media only, read-only context for the model
	QString ago;
};

struct SummaryRequest {
	QString recipient;
	QString chat; // Telegram's marked chat id, for the runner's voice profile
	std::vector<ContextLine> context; // the messages just before the run
	std::vector<RunLine> run; // oldest first
};

struct SummaryResult {
	QString text; // the one message; empty on failure
	int count = 0; // how many of his text messages it replaces
	QString via;
	QString failure;
};

inline constexpr auto kSummaryTimeoutMs = 60000; // the runner's own is 50 s
inline constexpr auto kSummaryContext = 12;
inline constexpr auto kMaxRunLines = 60; // the runner's MAX_RUN

[[nodiscard]] QByteArray SummaryRunnerBody(const SummaryRequest &request);
[[nodiscard]] SummaryResult ParseSummaryRunner(
	int status,
	const QByteArray &body,
	const SummaryRequest &request);

} // namespace Safe::Calm
