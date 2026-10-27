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
	On, // "Full-auto on" with the pace, Turn off
	Handling, // on, answering the hand-back he let it handle
	Handback, // the reason, Reply yourself / Let full-auto handle it
	Paused, // a pause left over from before 10-26 (paused_until), Resume
	Quiet, // the Max login is out: nothing sends, Turn off
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

// How long "Answering <name> lightly" stays without his next message.
inline constexpr std::int64_t kHandlingLimit = 30 * 60;
// How long without a good answer before the bar says so.
inline constexpr std::int64_t kUnreachableAfter = 3 * 60;

[[nodiscard]] ViewOutput Decide(const ViewInput &in);
[[nodiscard]] int MinutesLeft(std::int64_t now, std::int64_t until);

} // namespace Safe::Auto
