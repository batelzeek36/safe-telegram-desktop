/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstdint>

// Which face the full-auto bar shows for a chat (docs/FULL-AUTO.md,
// "Controls"; mocks docs/previews/auto/). Plain C++, no Qt and no Telegram
// types, so tools/desktopcheck checks it without the app.
namespace Safe::Auto {

enum class BarKind : unsigned char {
	Hidden, // full-auto is off here, or nothing is known yet
	On, // "Full-auto on" with the pace, Pause 30 min
	Handling, // on, answering the hand-back he let it handle
	Handback, // the reason, Reply yourself / Let full-auto handle it
	Paused, // paused_until: "Paused · back at 2:45", Resume now
	Quiet, // the Max login is out: nothing sends, Pause 30 min
};

enum class QuietKind : unsigned char {
	None,
	Night, // quiet hours: nothing sends until the morning
	MaxOut, // the Max login is out
};

struct ViewInput {
	bool known = false; // the runner answered for this chat at least once
	bool enabled = false;
	std::int64_t now = 0; // unix seconds
	std::int64_t pausedUntil = 0; // 0 = no pause
	bool handback = false;
	bool handbackHeld = false; // he chose Reply yourself on this one
	QuietKind quiet = QuietKind::None;
	std::int64_t handlingSince = 0; // he tapped Let full-auto handle it
	std::int64_t lastOutgoing = 0; // his newest message in the chat
	std::int64_t lastAnswer = 0; // the runner's last good answer
	bool lastFailed = false; // the newest ask got no good answer
};

struct ViewOutput {
	BarKind kind = BarKind::Hidden;
	int minutesLeft = 0; // Paused: whole minutes, rounded up, at least 1
	bool night = false; // On and Handling during quiet hours
	bool unreachable = false; // no word from the runner for a while
};

// The bar's quick button pauses, never switches off (Zach 10-27: "when i
// shut full-auto off it should pause temporarily, not shut off forever.
// Maybe a 30-min pause"); the chat menu's dialog keeps the real Off.
inline constexpr int kPauseMinutes = 30;

// How long "Answering <name> lightly" stays without his next message.
inline constexpr std::int64_t kHandlingLimit = 30 * 60;
// How long without a good answer before the bar says so.
inline constexpr std::int64_t kUnreachableAfter = 3 * 60;

[[nodiscard]] ViewOutput Decide(const ViewInput &in);
[[nodiscard]] int MinutesLeft(std::int64_t now, std::int64_t until);

} // namespace Safe::Auto
