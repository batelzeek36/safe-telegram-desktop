/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_fit.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

namespace Safe::Circles {
namespace {


[[nodiscard]] int Span(
		const std::vector<FitWord> &words,
		int from,
		int till,
		bool trimLast) {
	auto result = 0;
	for (auto i = from; i != till; ++i) {
		const auto last = (i + 1 == till);
		result += (last && trimLast) ? words[i].trimmed : words[i].width;
		if (!last) {
			result += words[i].space;
		}
	}
	return result;
}

[[nodiscard]] std::vector<int> BandWidths(
		double radius,
		int top,
		int lines,
		int lineHeight) {
	auto result = std::vector<int>();
	result.reserve(lines);
	const auto inset = lineHeight * kBandInset;
	for (auto i = 0; i != lines; ++i) {
		const auto from = top + i * lineHeight + inset;
		const auto till = top + (i + 1) * lineHeight - inset;
		result.push_back(int(std::floor(ChordWidth(radius, from, till))));
	}
	return result;
}

[[nodiscard]] bool IsSpace(char16_t c) {
	switch (c) {
	case u' ': case u'\t': case u'\n': case u'\r': case u'\f': case u'\v':
	case u'\u0085': case u'\u00A0': case u'\u1680': case u'\u2028':
	case u'\u2029': case u'\u202F': case u'\u205F': case u'\u3000':
		return true;
	}
	return (c >= u'\u2000' && c <= u'\u200A');
}

// Marks that close or join a phrase: on a line of their own's start they
// read as a stray ("- STAY").
[[nodiscard]] bool IsMark(char16_t c) {
	switch (c) {
	case u'-': case u'|': case u'/': case u'\\': case u':': case u';':
	case u',': case u'.': case u'!': case u'?': case u'&': case u'+':
	case u'=': case u'~': case u'*': case u'>': case u')': case u']':
	case u'}': case u'\u00B7': case u'\u00BB': case u'\u2022':
	case u'\u2026': case u'\u203A': case u'\u2212':
		return true;
	}
	return (c >= u'\u2010' && c <= u'\u2015'); // the dashes
}

// The marks that only separate (a line break can stand in for them).
[[nodiscard]] bool IsSeparator(char16_t c) {
	switch (c) {
	case u'-': case u'|': case u'\u00B7': case u'\u2022': case u'\u2212':
		return true;
	}
	return (c >= u'\u2010' && c <= u'\u2015');
}

// A break before word `at` falls inside a label (its words either side).
[[nodiscard]] bool InsideLabel(const std::vector<FitWord> &words, int at) {
	return (at > 0)
		&& (at < int(words.size()))
		&& (words[at].label != 0)
		&& (words[at - 1].label == words[at].label);
}

// The first word of the label a break before `at` falls inside, not before
// `from`.
[[nodiscard]] int LabelFrom(
		const std::vector<FitWord> &words,
		int from,
		int at) {
	const auto label = words[at].label;
	while (at > from && words[at - 1].label == label) {
		--at;
	}
	return at;
}

struct Candidate {
	HubPlan plan;
	// Compared as a whole: the name whole first, then as much of it as
	// can show, the time row, the message whole, some of the message, more
	// of it, the preferred time row, the usual gap over it, the block
	// nearer the middle.
	std::tuple<bool, int, bool, bool, bool, int, int, bool, int> score;
};

// `strict`: each part keeps its words of HubPart::wholeLetters letters
// whole; otherwise any word may be cut (the last resort).
[[nodiscard]] std::optional<Candidate> Best(
		const HubInput &input,
		bool strict) {
	auto result = std::optional<Candidate>();
	const auto r = input.radius;
	if (r <= 0.) {
		return result;
	}
	const auto &name = input.name;
	const auto &text = input.text;
	const auto metaCount = int(input.metas.size());
	const auto textMax = text.words.empty()
		? 0
		: std::max(text.maxLines, 0);
	const auto nameMax = name.words.empty()
		? 0
		: std::max(name.maxLines, 1);
	// Each way the time row may go (or none), with the usual gap over it
	// and then, when allowed, tucked up under the message.
	struct Row {
		int meta = -1;
		int gap = 0;
		bool tight = false;
	};
	auto rows = std::vector<Row>();
	for (auto meta = 0; meta != metaCount; ++meta) {
		rows.push_back({ meta, input.gapMeta, false });
		if (input.gapMetaLeast >= 0 && input.gapMetaLeast < input.gapMeta) {
			rows.push_back({ meta, input.gapMetaLeast, true });
		}
	}
	rows.push_back({});
	for (const auto &row : rows) {
		const auto meta = row.meta;
		const auto hasMeta = (meta >= 0);
		for (auto textLines = textMax; textLines >= 0; --textLines) {
			for (auto nameLines = std::min(nameMax, 1); nameLines <= nameMax; ++nameLines) {
				const auto height = nameLines * name.lineHeight
					+ (textLines
						? (((nameLines > 0) ? input.gapText : 0)
							+ textLines * text.lineHeight)
						: 0)
					+ (hasMeta
						? (row.gap + input.metas[meta].height)
						: 0);
				if (height <= 0 || height > 2. * r) {
					continue;
				}
				const auto centered = -height / 2;
				const auto reach = int(std::floor(r - height / 2.));
				for (auto step = 0; step <= 2 * reach; ++step) {
					// 0, +1, -1, +2, -2 ...: the middle first.
					const auto shift = (step % 2) ? ((step + 1) / 2) : -(step / 2);
					auto plan = HubPlan();
					plan.height = height;
					plan.nameTop = centered + shift;
					if (plan.nameTop < -r || plan.nameTop + height > r) {
						continue;
					}
					plan.nameWidths = BandWidths(
						r,
						plan.nameTop,
						nameLines,
						name.lineHeight);
					plan.name = FitWords(
						name.words,
						plan.nameWidths,
						name.ellipsis,
						strict ? name.wholeLetters : 0,
						name.fillShare);
					if (!plan.name.valid
						|| int(plan.name.lines.size()) < nameLines) {
						continue;
					}
					plan.textTop = plan.nameTop
						+ nameLines * name.lineHeight
						+ ((textLines && nameLines) ? input.gapText : 0);
					plan.textWidths = BandWidths(
						r,
						plan.textTop,
						textLines,
						text.lineHeight);
					plan.text = textLines
						? FitWords(
							text.words,
							plan.textWidths,
							text.ellipsis,
							strict ? text.wholeLetters : 0,
							text.fillShare)
						: WordFit{
							.whole = text.words.empty(),
							.valid = true,
						};
					if (!plan.text.valid
						|| int(plan.text.lines.size()) < textLines) {
						continue;
					}
					if (hasMeta) {
						plan.metaTop = plan.textTop
							+ textLines * text.lineHeight
							+ row.gap;
						const auto &time = input.metas[meta];
						const auto inset = time.height * time.inset;
						const auto room = ChordWidth(
							r,
							plan.metaTop + inset,
							plan.metaTop + time.height - inset);
						if (time.width > room) {
							continue;
						}
						plan.meta = meta;
					}
					const auto lines = int(plan.text.lines.size());
					auto score = std::make_tuple(
						plan.name.whole,
						plan.name.shown,
						hasMeta,
						plan.text.whole,
						lines > 0,
						plan.text.shown,
						hasMeta ? (metaCount - meta) : 0,
						!row.tight,
						-std::abs(shift));
					if (!result || score > result->score) {
						result = Candidate{ std::move(plan), score };
					}
				}
			}
		}
	}
	return result;
}

[[nodiscard]] std::optional<Candidate> Choose(const HubInput &input) {
	if (auto best = Best(input, true)) {
		return best;
	}
	// Nothing keeps every short word whole: cut words rather than show none.
	return Best(input, false);
}

} // namespace

std::vector<std::pair<int, int>> SplitWords(std::u16string_view text) {
	auto result = std::vector<std::pair<int, int>>();
	const auto size = int(text.size());
	auto i = 0;
	while (i < size) {
		while (i < size && IsSpace(text[i])) {
			++i;
		}
		if (i >= size) {
			break;
		}
		auto j = i;
		auto marks = true;
		while (j < size && !IsSpace(text[j])) {
			marks = marks && IsMark(text[j]);
			++j;
		}
		if (marks && !result.empty()) {
			result.back().second = j;
		} else {
			result.emplace_back(i, j);
		}
		i = j;
	}
	return result;
}

int SeparatorStart(std::u16string_view text, int from, int till) {
	till = std::min(till, int(text.size()));
	auto i = till;
	while (i > from && IsSeparator(text[i - 1])) {
		--i;
	}
	if (i == till || i == from || !IsSpace(text[i - 1])) {
		return till;
	}
	while (i > from && IsSpace(text[i - 1])) {
		--i;
	}
	return (i > from) ? i : till;
}

bool IsLabelRun(std::u16string_view run) {
	auto from = std::size_t(0);
	auto till = run.size();
	while (from < till && IsSpace(run[from])) {
		++from;
	}
	while (till > from && IsSpace(run[till - 1])) {
		--till;
	}
	if (from == till || int(till - from) > kLabelLengthMax) {
		return false;
	}
	auto words = 0;
	auto inWord = false;
	for (auto i = from; i != till; ++i) {
		switch (run[i]) {
		case u':': case u'：': case u'.': case u'/': case u'@':
			return false;
		}
		const auto space = IsSpace(run[i]);
		if (!space && !inWord) {
			++words;
		}
		inWord = !space;
	}
	return (words <= kLabelWordsMax);
}

int CutShown(int letters, int fits, bool lettersBefore) {
	letters = std::max(letters, 0);
	fits = std::clamp(fits, 0, letters);
	const auto most = letters - kCutDropMin;
	if (fits <= most && (!lettersBefore || fits >= kCutLettersMin)) {
		return fits;
	} else if (lettersBefore) {
		return 0;
	}
	return std::max(std::min(fits, most), std::min(fits, 1));
}

double ChordWidth(double radius, double top, double bottom) {
	const auto far = std::max(std::abs(top), std::abs(bottom));
	if (radius <= 0. || far >= radius) {
		return 0.;
	}
	return 2. * std::sqrt(radius * radius - far * far);
}

WordFit FitWords(
		const std::vector<FitWord> &words,
		const std::vector<int> &widths,
		int ellipsis,
		int wholeLetters,
		double fillShare) {
	auto result = WordFit();
	const auto count = int(words.size());
	if (!count) {
		return result;
	}
	if (widths.empty()) {
		result.whole = false;
		return result;
	}
	auto index = 0;
	for (auto line = 0; line != int(widths.size()); ++line) {
		const auto width = widths[line];
		const auto last = (line + 1 == int(widths.size()));
		const auto from = index;
		auto used = 0;
		while (index < count) {
			const auto add = ((index > from) ? words[index - 1].space : 0)
				+ words[index].width;
			if (used + add > width) {
				break;
			}
			used += add;
			++index;
		}
		if (index == count) {
			result.lines.push_back({ from, index, false, false });
			result.shown += index - from;
			return result;
		}
		if (!last) {
			// The line breaks here: a separator on its last word goes, and
			// a word that fits without its separator ends the line.
			const auto gap = (index > from) ? words[index - 1].space : 0;
			if (words[index].bare > 0 && used + gap + words[index].bare <= width) {
				++index;
			}
			// A label goes down to the next line whole ("Voice" / "message"
			// only when it is wider than a line).
			if (InsideLabel(words, index)) {
				if (const auto start = LabelFrom(words, from, index); start > from) {
					index = start;
				}
			}
			if (index > from) {
				const auto bare = (words[index - 1].bare > 0);
				result.lines.push_back({ from, index, false, false, bare });
				result.shown += index - from;
				if (index == count) {
					return result;
				}
				continue;
			}
		}
		// The words stop here: on the last line, or at a word too wide for
		// a line of its own. The next word may still fit with its trailing
		// punctuation given up for the ellipsis.
		result.whole = false;
		auto keep = std::min(index + 1, count);
		while (keep > from
			&& Span(words, from, keep, true) + ellipsis > width) {
			--keep;
		}
		if (InsideLabel(words, keep)) {
			keep = LabelFrom(words, from, keep);
		}
		if (keep > from) {
			// Stopping a short way into the line says little ("Pele…"):
			// the next word, when it may be cut, fills the line instead.
			const auto fill = (fillShare > 0.)
				&& (keep < count)
				&& !words[keep].label
				&& (words[keep].letters >= wholeLetters)
				&& (Span(words, from, keep, true) + ellipsis
					< width * fillShare);
			result.lines.push_back({ from, fill ? (keep + 1) : keep, true, fill });
			result.shown += keep - from;
			return result;
		}
		if (words[from].label || words[from].letters < wholeLetters) {
			result.valid = false;
			return result;
		}
		result.lines.push_back({ from, from + 1, true, true });
		return result;
	}
	return result;
}

HubPlan PlanHub(const HubInput &input) {
	auto best = Choose(input);
	const auto cramped = !best
		|| !best->plan.name.whole
		|| (best->plan.text.lines.empty() && !input.text.words.empty());
	if (cramped && input.reach > input.radius) {
		auto roomy = input;
		roomy.radius = input.reach;
		roomy.reach = 0.;
		// Only for more of the name or a message line, never to move the
		// words about in a hub they already fit.
		const auto key = [](const Candidate &candidate) {
			const auto &score = candidate.score;
			return std::make_tuple(
				std::get<0>(score),
				std::get<1>(score),
				std::get<2>(score),
				std::get<3>(score),
				std::get<4>(score));
		};
		auto other = Choose(roomy);
		if (other && (!best || key(*other) > key(*best))) {
			best = std::move(other);
		}
	}
	return best ? std::move(best->plan) : HubPlan();
}

} // namespace Safe::Circles
