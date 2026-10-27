/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_caps_core.h"

namespace Safe::Caps {
namespace {

struct Word {
	int from = 0;
	int till = 0;
	int upper = 0;
	int lower = 0;
	bool broken = false;
};

[[nodiscard]] bool InWord(CharKind kind) {
	return (kind == CharKind::Upper)
		|| (kind == CharKind::Lower)
		|| (kind == CharKind::Uncased);
}

} // namespace

std::vector<Range> ShoutRuns(const std::vector<CharKind> &kinds) {
	auto result = std::vector<Range>();
	auto run = Range();
	auto runWords = 0;
	auto runCapitals = 0;
	const auto close = [&] {
		if (runWords >= kMinWords && runCapitals >= kMinCapitals) {
			result.push_back(run);
		}
		run = Range();
		runWords = runCapitals = 0;
	};
	const auto size = int(kinds.size());
	for (auto i = 0; i < size;) {
		if (kinds[i] == CharKind::Break) {
			close();
			++i;
			continue;
		} else if (!InWord(kinds[i])) {
			++i;
			continue;
		}
		auto word = Word{ .from = i };
		for (; i < size && InWord(kinds[i]); ++i) {
			if (kinds[i] == CharKind::Upper) {
				++word.upper;
			} else if (kinds[i] == CharKind::Lower) {
				++word.lower;
			}
		}
		word.till = i;
		if (word.lower > 0) {
			close(); // a word with small letters is not shouting
		} else if (word.upper > 0) {
			if (!runWords) {
				run.from = word.from;
			}
			run.till = word.till;
			++runWords;
			runCapitals += word.upper;
		}
		// a word without case (digits) neither starts nor ends a run
	}
	close();
	return result;
}

} // namespace Safe::Caps
