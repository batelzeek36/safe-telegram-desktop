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

// Glyphs fill a line's middle, not its leading: a band is measured this
// share in from each of its edges.
constexpr auto kBandInset = 0.125;

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

struct Candidate {
	HubPlan plan;
	// Compared as a whole: the name whole first, then as much of it as
	// can show, the message whole, some of the message, the time row, more
	// of the message, the badge beside the time (the face has it too), the
	// block nearer the middle.
	std::tuple<bool, int, bool, bool, bool, int, int, int> score;
};

[[nodiscard]] std::optional<Candidate> Best(
		const HubInput &input,
		int wholeLetters) {
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
	for (auto meta = 0; meta <= metaCount; ++meta) {
		const auto hasMeta = (meta < metaCount);
		for (auto textLines = textMax; textLines >= 0; --textLines) {
			for (auto nameLines = std::min(nameMax, 1); nameLines <= nameMax; ++nameLines) {
				const auto height = nameLines * name.lineHeight
					+ (textLines
						? (((nameLines > 0) ? input.gapText : 0)
							+ textLines * text.lineHeight)
						: 0)
					+ (hasMeta
						? (input.gapMeta + input.metas[meta].height)
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
						wholeLetters);
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
							wholeLetters)
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
							+ input.gapMeta;
						const auto &row = input.metas[meta];
						const auto inset = row.height * kBandInset;
						const auto room = ChordWidth(
							r,
							plan.metaTop + inset,
							plan.metaTop + row.height - inset);
						if (row.width > room) {
							continue;
						}
						plan.meta = meta;
					}
					auto score = std::make_tuple(
						plan.name.whole,
						plan.name.shown,
						plan.text.whole,
						!plan.text.lines.empty(),
						hasMeta,
						plan.text.shown,
						hasMeta ? (metaCount - meta) : 0,
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

} // namespace

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
		int wholeLetters) {
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
		if (!last && index > from) {
			result.lines.push_back({ from, index, false, false });
			result.shown += index - from;
			continue;
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
		if (keep > from) {
			result.lines.push_back({ from, keep, true, false });
			result.shown += keep - from;
			return result;
		}
		if (words[from].letters < wholeLetters) {
			result.valid = false;
			return result;
		}
		result.lines.push_back({ from, from + 1, true, true });
		return result;
	}
	return result;
}

HubPlan PlanHub(const HubInput &input) {
	if (auto best = Best(input, kWholeWordLetters)) {
		return std::move(best->plan);
	}
	// Nothing keeps every short word whole: cut words rather than show none.
	if (auto best = Best(input, 0)) {
		return std::move(best->plan);
	}
	return HubPlan();
}

} // namespace Safe::Circles
