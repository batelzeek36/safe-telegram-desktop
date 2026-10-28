/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <string_view>
#include <utility>
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
// Pass 3: a message line may cut inside a word of kMessageCutLetters or
// more, and fills with one rather than stopping a short way in ("Pele…");
// a lone dash stays with the word before it. Pass 4: the time row is never
// given up for the message ("TRANSMIT - / STAY… / Voice mes…" lost its
// time): the message shortens first, and the time row may tuck up under it.
// Pass 5: a word cut inside drops kCutDropMin letters at least (never
// "throug…"), and Telegram's own labels ("Voice message", "Photo", "GIF")
// show whole or not at all (never "Voice mess…").
// Plain C++20, no Qt: tools/desktopcheck checks it.
namespace Safe::Circles {

// How wide a circle `radius` across its middle is over a horizontal band
// from `top` to `bottom` (both measured from its center, y down): the
// narrower end of the band decides. 0 for a band reaching past the circle.
[[nodiscard]] double ChordWidth(double radius, double top, double bottom);

inline constexpr int kWholeWordLetters = 14;
// A message line may cut inside a word this long (a name never).
inline constexpr int kMessageCutLetters = 6;
// A message's last line cut between words keeps at least this share of
// its room, else it is cut inside the next word.
inline constexpr double kMessageFillShare = 0.6;

// Where the words of `text` are, [from, till) each: runs between spaces,
// with a lone mark ("-", "–", "|", "/", "&") kept on the word before it so
// no line starts with one ("TRANSMIT -" / "STAY", never "TRANSMIT" /
// "- STAY").
[[nodiscard]] std::vector<std::pair<int, int>> SplitWords(
	std::u16string_view text);

// Where a separator kept on the word [from, till) begins (a lone dash, bar
// or bullet after a space: "TRANSMIT -"), with the spaces before it; `till`
// when the word has none. Where a line breaks after such a word the break
// is the separator, as in display type: it is not drawn ("TRANSMIT" /
// "STAY", the room it took goes to the line).
[[nodiscard]] int SeparatorStart(
	std::u16string_view text,
	int from,
	int till);

// Whether a colored run of a message line is one of Telegram's own labels
// (what a message is when it has no words: "Photo", "Voice message",
// "Sticker 😀", "3 photos"): a few words, no sender ("You:", "Amberelle:"
// end in a colon) and no link or address. A label is never cut: it shows
// whole or not at all.
inline constexpr int kLabelWordsMax = 3;
inline constexpr int kLabelLengthMax = 24; // UTF-16 units, spaces trimmed
[[nodiscard]] bool IsLabelRun(std::u16string_view run);

// A word cut inside drops this many letters at least ("sent throug…" says
// "through" was nearly there and was not shown), and after other words it
// shows kCutLettersMin at least.
inline constexpr int kCutDropMin = 3;
inline constexpr int kCutLettersMin = 3;

// How many of its `letters` letters a word cut at the end of a line shows
// when `fits` of them fit before the ellipsis: `fits` when that drops
// kCutDropMin or more (and, after words with letters, shows kCutLettersMin
// or more); else after such words none (0: the line ends on them, "sent…");
// a word that starts its line, or follows only marks and emoji ("🌿
// Autopilot"), is cut deeper instead, to drop kCutDropMin ("🌿 Autopi…").
[[nodiscard]] int CutShown(int letters, int fits, bool lettersBefore);

// Where a word cut at the end of a line ends, counted in its units (its
// graphemes, an emoji however many code points it takes being one, as
// Telegram draws it): `letter[i]` = unit i is a letter or digit, `fits` =
// how many units fit before the ellipsis. While the cut falls inside the
// word's letters CutShown decides; once every letter fits (or the word has
// none: a row of hearts) the cut falls between whole emoji and every unit
// that fits shows ("❤️❤️❤️❤️…", "yes🎉🎉…"; design-evaluator pass 6: a row
// of hearts counted no letters and its line was a bare "…"). 0 = none of it
// (the line ends on the words before it).
[[nodiscard]] int CutUnits(
	const std::vector<bool> &letter,
	int fits,
	bool lettersBefore);

struct FitWord {
	int width = 0; // the word with its punctuation
	int trimmed = 0; // without trailing punctuation (an ellipsis follows)
	int space = 0; // the space between it and the next word
	int letters = 0; // letters and digits, for the "never cut" rule
	int bare = 0; // without its separator (SeparatorStart), 0 = none
	// The label (IsLabelRun) it belongs to, the same number for each of its
	// words; 0 = none. A label is never cut inside, never ended inside with
	// an ellipsis, and starts a new line rather than break across one.
	int label = 0;
	// A word with no letters (a row of hearts): its first emoji's width; it
	// may be cut between its emoji when that one fits. 0 = not such a word
	// (cut only by the letters rule).
	int unit = 0;
};

struct FitLine {
	int from = 0; // the first word on the line
	int till = 0; // past the last word
	bool ellipsis = false; // the text goes on: "…" ends the line
	bool cutWord = false; // the last word itself is cut (a long word)
	bool bare = false; // a line broken after a separator: it is not drawn
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
// letters or more; a shorter one makes the fit invalid. With `fillShare`,
// a last line whose whole words and ellipsis take less than that share of
// it is cut inside the next word instead, when that word may be cut.
// A label's words (FitWord::label) are never cut and never split by the
// ellipsis, whatever `wholeLetters` says. A word with no letters (a row of
// hearts, FitWord::unit) may be cut between its emoji wherever its first
// one fits (CutUnits): the "never cut a short word" rule is about letters.
[[nodiscard]] WordFit FitWords(
	const std::vector<FitWord> &words,
	const std::vector<int> &widths,
	int ellipsis,
	int wholeLetters = kWholeWordLetters,
	double fillShare = 0.);

// ---- The Crown's hub ----

struct HubPart {
	std::vector<FitWord> words;
	int lineHeight = 0;
	int ellipsis = 0;
	int maxLines = 2;
	int wholeLetters = kWholeWordLetters; // see FitWords
	double fillShare = 0.;
};

// Glyphs fill a line's middle, not its leading: a band is measured this
// share of its height in from each edge.
inline constexpr double kBandInset = 0.125;
// The time row is digits and capitals: they sit in the middle half of the
// row.
inline constexpr double kTimeRowInset = 0.25;

struct HubMeta {
	int width = 0;
	int height = 0;
	double inset = kBandInset; // see kBandInset
};

struct HubInput {
	double radius = 0.; // the room for words: the hub less its padding
	HubPart name;
	HubPart text;
	// The time row, as it may be drawn in order of preference; leaving it
	// out is the last resort, after every message line.
	std::vector<HubMeta> metas;
	int gapText = 0; // between the name and the message
	int gapMeta = 0; // above the time row
	// Above the time row when the usual gap leaves no room for it or for a
	// message line: the row tucks up into the message's leading (-1 = no).
	int gapMetaLeast = -1;
	// The most room the words may take (the hub less a thinner padding),
	// only when `radius` would cut the name or leave the message out
	// ("🌺 Amber's" / "Garden Gate" / the time had no room for a line of
	// Amber's community); 0 = none.
	double reach = 0.;
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
// time row, then the message whole, then some of it, then more of it,
// then the usual gap over the time row. The block may sit off center when
// that lets a longer line through the wide middle.
[[nodiscard]] HubPlan PlanHub(const HubInput &input);

} // namespace Safe::Circles
