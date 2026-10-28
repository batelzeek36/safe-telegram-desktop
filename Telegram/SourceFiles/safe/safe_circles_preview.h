/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "dialogs/dialogs_common.h"
#include "safe/safe_circles_fit.h"
#include "ui/text/text.h"

#include <array>
#include <map>

class History;
class Painter;

// The focused (or pointed at) chat's preview: in the Crown's hub, fitted
// line by line to the circle, and in the Orbit's card as a list row. The
// words are laid out with safe_circles_fit (no short word cut, the ellipsis
// after the last whole word) and drawn as one-line strings that keep the
// message's colors and weights.
namespace Safe::Circles {

class Model;

// A text's words as FitWords measures them, in `st`.
struct MeasuredWords {
	TextWithEntities text;
	// Left to right when the text has any left-to-right letters (a name
	// mixing Hebrew and English), else the text's own.
	Qt::LayoutDirection direction = Qt::LayoutDirectionAuto;
	std::vector<std::pair<int, int>> ranges; // each word's [from, till)
	std::vector<FitWord> words;
	int ellipsis = 0;
};
[[nodiscard]] MeasuredWords MeasureWords(
	const TextWithEntities &text,
	const style::TextStyle &st);

// The lines `fit` chose as one-line strings in `st`; a line the text goes
// on past ends in "…" after its last whole word, trailing punctuation
// trimmed, or inside a long word cut to `widths`.
[[nodiscard]] std::vector<Ui::Text::String> FittedLines(
	const MeasuredWords &measured,
	const WordFit &fit,
	const std::vector<int> &widths,
	const style::TextStyle &st);

// A chat's text on one line `width` wide, cut the same way, kept until
// the text or the width changes; a text no more than `slack` too wide stays
// whole (the caller finds the room). A returned line stays valid while
// other chats' lines are added (a dial keeps several while it paints).
class LineCache final {
public:
	[[nodiscard]] const Ui::Text::String &line(
		not_null<History*> history,
		const TextWithEntities &text,
		const style::TextStyle &st,
		int width,
		int slack = 0);

private:
	struct Entry {
		QString text;
		const style::TextStyle *st = nullptr;
		int width = -1;
		int slack = 0;
		Ui::Text::String line;
	};
	std::map<not_null<History*>, Entry> _entries;

};

// The Crown's hub: name (13 pt semibold, up to two lines), message (12 pt,
// up to two) and time (11 pt; the unread badge stays on the face, never
// repeated here), each line as wide as the circle `radius` round `center`
// is at its height. Two chats' layouts are kept, so one can fade out while
// the next fades in.
class HubPreview final {
public:
	void paint(
		Painter &p,
		Model &model,
		not_null<History*> history,
		QPointF center,
		double radius,
		double alpha);

private:
	struct Prepared {
		History *history = nullptr;
		QString name;
		QString text;
		QString time;
		int radius = -1;

		HubPlan plan;
		std::vector<Ui::Text::String> nameLines;
		std::vector<Ui::Text::String> textLines;
		int timeWidth = 0;
	};

	// The slot holding `history`'s layout, laid out again if it changed.
	[[nodiscard]] int prepare(
		Model &model,
		not_null<History*> history,
		int radius);

	std::array<Prepared, 2> _slots;
	int _recent = 0;

};

// The Orbit's card: the photo, the name (14 pt semibold) running to the
// time, the message under it, the unread badge after it when `badge` (not
// while the chat's face wears the count itself). A name that does not fit
// beside the time gets the whole line and the time moves down beside the
// message.
class CardPreview final {
public:
	void paint(
		Painter &p,
		Model &model,
		not_null<History*> history,
		QRect rect,
		double alpha,
		bool badge);

private:
	LineCache _names;
	LineCache _texts;

};

} // namespace Safe::Circles
