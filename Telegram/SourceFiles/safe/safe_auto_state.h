/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_auto_view.h"

class PeerData;

// Full-auto's state for one chat, as the runner on MAINGEAR keeps it
// (calm-runner/auto/api.py, GET and POST /v1/auto/state): parsed and
// checked here, the one place the runner's answer enters the app.
namespace Safe::Auto {

enum class Pace : uchar {
	LikeMe,
	Calmer,
	Slow,
};

struct Handback {
	QString kind;
	QString reason;
	int64 msg = 0; // the message it is about, 0 when unknown

	friend inline bool operator==(
		const Handback &,
		const Handback &) = default;
};

struct State {
	int64 chat = 0; // Bot API form: -100... for groups
	bool enabled = false;
	Pace pace = Pace::Calmer;
	int64 pausedUntil = 0;
	std::optional<Handback> handback;
	QuietKind quiet = QuietKind::None;

	friend inline bool operator==(const State &, const State &) = default;
};

// What a POST asks for (one of them per tap).
struct Change {
	std::optional<bool> enabled;
	std::optional<Pace> pace;
	bool resume = false; // Let full-auto handle it
	bool unpause = false; // Resume now: a pause ends early
	int pauseMinutes = 0; // Pause 30 min: on again by itself after that
};

// The answer for `chat`, or nothing when it is not a well formed state for
// that chat (anything unexpected counts as no answer).
[[nodiscard]] std::optional<State> ParseState(
	const QByteArray &json,
	int64 chat);
[[nodiscard]] QByteArray ChangeBody(int64 chat, const Change &change);

[[nodiscard]] QString PaceKey(Pace pace);
[[nodiscard]] QString PaceLine(Pace pace); // "Calmer pace"

// The chat id the runner knows a chat by (Bot API form) and back.
[[nodiscard]] int64 RunnerChatId(not_null<PeerData*> peer);
[[nodiscard]] PeerId PeerFromChatId(int64 chat);

} // namespace Safe::Auto
