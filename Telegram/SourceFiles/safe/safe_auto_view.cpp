/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_auto_view.h"

namespace Safe::Auto {

int MinutesLeft(std::int64_t now, std::int64_t until) {
	const auto seconds = until - now;
	if (seconds <= 0) {
		return 0;
	}
	const auto minutes = (seconds + 59) / 60;
	return (minutes > 100000) ? 100000 : int(minutes);
}

ViewOutput Decide(const ViewInput &in) {
	auto result = ViewOutput();
	if (!in.known || !in.enabled) {
		return result;
	}
	result.unreachable = in.lastFailed
		&& (in.now - in.lastAnswer >= kUnreachableAfter);
	result.night = (in.quiet == QuietKind::Night);

	// What needs him comes first: a hand-back he has not answered or put
	// aside, then a pause, then the Max login being out.
	if (in.handback && !in.handbackHeld) {
		result.kind = BarKind::Handback;
	} else if (in.pausedUntil > in.now) {
		result.kind = BarKind::Paused;
		result.minutesLeft = MinutesLeft(in.now, in.pausedUntil);
	} else if (in.quiet == QuietKind::MaxOut) {
		result.kind = BarKind::Quiet;
	} else if (in.handlingSince > 0
		&& !in.handback
		&& (in.now - in.handlingSince) < kHandlingLimit
		&& in.lastOutgoing < in.handlingSince) {
		result.kind = BarKind::Handling;
	} else {
		result.kind = BarKind::On;
	}
	return result;
}

} // namespace Safe::Auto
