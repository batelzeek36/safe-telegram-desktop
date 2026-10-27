/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <vector>

// Where a message shouts (d31-caps-italic, Zach 2026-10-26: "Anytime i TYPE
// IN ALL CAPS, i would like the softener to send it as italics, otherwise it
// reads like im yelling"). Plain C++, no Qt: tools/desktopcheck runs it.
namespace Safe::Caps {

// One UTF-16 unit of the message, as the Qt side classifies it.
enum class CharKind : unsigned char {
	Other, // spaces, punctuation, emoji: between words, never ends a run
	Upper, // a capital letter
	Lower, // a small letter: its word is not shouting
	Uncased, // part of a word without case: digits, apostrophes in DON'T
	Break, // inside a link, mention, hashtag or code: never touched
};

struct Range {
	int from = 0;
	int till = 0; // one past the last unit

	friend inline bool operator==(const Range &, const Range &) = default;
};

// Words whose letters are all capitals, next to each other with nothing but
// spaces, punctuation or emoji between them. A run counts when it has two or
// more such words and at least kMinCapitals capitals: a single capitals word
// (OMG, LOL, USA, a name) stays as typed. A word with any small letter, or a
// Break, ends the run. Each range starts at its first capital word and ends
// after its last one.
inline constexpr auto kMinWords = 2;
inline constexpr auto kMinCapitals = 4;

[[nodiscard]] std::vector<Range> ShoutRuns(const std::vector<CharKind> &kinds);

} // namespace Safe::Caps
