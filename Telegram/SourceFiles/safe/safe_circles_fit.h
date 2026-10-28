/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <vector>

// Words in a circle (the Crown's hub) and on a line (names, the Orbit's
// card), d35-circles. A hub's lines are as wide as the circle at their own
// height (it is widest in the middle), so a name or a message is laid out
// line by line against those widths, and the whole block is moved up or
// down to where the words fit best. Rules (design-evaluator pass 2):
// a name wraps to a second line before it is cut; a word under
// kWholeWordLetters letters is never cut inside ("Objectivity",
// "youtube.com" show whole or the layout gives way elsewhere); when the
// text goes on past the last line an ellipsis follows the last whole word
// with its trailing punctuation trimmed (never "youtube.…").
// Plain C++20, no Qt: tools/desktopcheck checks it.
namespace Safe::Circles {

// How wide a circle `radius` across its middle is over a horizontal band
// from `top` to `bottom` (both measured from its center, y down): the
// narrower end of the band decides. 0 for a band reaching past the circle.
[[nodiscard]] double ChordWidth(double radius, double top, double bottom);

inline constexpr int kWholeWordLetters = 14;

struct FitWord {
	int width = 0; // the word with its punctuation
	int trimmed = 0; // without trailing punctuation (an ellipsis follows)
	int space = 0; // the space between it and the next word
	int letters = 0; // letters and digits, for the "never cut" rule
};

struct FitLine {
	int from = 0; // the first word on the line
	int till = 0; // past the last word
	bool ellipsis = false; // the text goes on: "…" ends the line
	bool cutWord = false; // the last word itself is cut (a long word)
};

struct WordFit {
	std::vector<FitLine> lines; // only the lines the words need
	bool whole = true; // every word shows, none cut
	bool valid = true; // no word shorter than the limit was cut
	int shown = 0; // words shown whole
};

// Lays `words` greedily on lines `widths` wide, one line after another.
// Words left over after the last line: the last line keeps the words that
// fit before an `ellipsis` wide mark. A word wider than a whole empty line
// is cut there (and the text ends with it) when it has `wholeLetters`
// letters or more; a shorter one makes the fit invalid.
[[nodiscard]] WordFit FitWords(
	const std::vector<FitWord> &words,
	const std::vector<int> &widths,
	int ellipsis,
	int wholeLetters = kWholeWordLetters);

// ---- The Crown's hub ----

struct HubPart {
	std::vector<FitWord> words;
	int lineHeight = 0;
	int ellipsis = 0;
	int maxLines = 2;
};

struct HubMeta {
	int width = 0;
	int height = 0;
};

struct HubInput {
	double radius = 0.; // the room for words: the hub less its padding
	HubPart name;
	HubPart text;
	// The time row, as it may be drawn in order of preference (time and
	// badge, the time alone); leaving it out is the last resort.
	std::vector<HubMeta> metas;
	int gapText = 0; // between the name and the message
	int gapMeta = 0; // above the time row
};

struct HubPlan {
	int height = 0; // of the whole block
	int nameTop = 0; // every top is measured from the circle's center
	std::vector<int> nameWidths;
	WordFit name;
	int textTop = 0;
	std::vector<int> textWidths;
	WordFit text;
	int meta = -1; // the HubInput::metas index drawn, -1 = none
	int metaTop = 0;
};

// The name (up to two lines), the message (up to two) and the time row,
// stacked and centered as near the middle as the words allow, each line
// as wide as the circle at its height. The name comes first (whole when
// any layout shows it whole, else as much of it as can show), then the
// message whole, then some of the message, then the time row, then more of
// the message, then the badge beside the time; the block may sit off
// center when that lets a longer line through the wide middle.
[[nodiscard]] HubPlan PlanHub(const HubInput &input);

} // namespace Safe::Circles
