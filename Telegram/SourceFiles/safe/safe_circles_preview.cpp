/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_preview.h"

#include "history/history.h"
#include "safe/safe_circles_model.h"
#include "safe/safe_circles_paint.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"

#include "styles/style_basic.h"
#include "styles/style_dialogs.h"
#include "styles/style_safe_circles.h"

namespace Safe::Circles {
namespace {

const auto kEllipsis = QString(QChar(0x2026));
// A one-line label cut between words keeps at least this share of its room.
constexpr auto kWordCutShare = 0.6;

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

// Where a cut text ends: before the spaces and punctuation it would leave
// in front of the ellipsis ("youtube.com." gives "youtube.com…", never
// "youtube.com.…").
[[nodiscard]] int TrimmedEnd(const QString &text, int from, int till) {
	while (till > from
		&& (text[till - 1].isSpace() || text[till - 1].isPunct())) {
		--till;
	}
	return till;
}

[[nodiscard]] int Letters(const QString &text, int from, int till) {
	auto result = 0;
	for (auto i = from; i != till; ++i) {
		result += text[i].isLetterOrNumber() ? 1 : 0;
	}
	return result;
}

[[nodiscard]] Ui::Text::String Line(
		const style::TextStyle &st,
		const TextWithEntities &text) {
	return Ui::Text::String(st, text, kMarkupTextOptions);
}

[[nodiscard]] int WidthOf(
		const TextWithEntities &text,
		const style::TextStyle &st,
		int from,
		int till) {
	return (till > from)
		? Line(st, Ui::Text::Mid(text, from, till - from)).maxWidth()
		: 0;
}

[[nodiscard]] TextWithEntities Cut(
		const TextWithEntities &text,
		int from,
		int till) {
	auto result = Ui::Text::Mid(text, from, std::max(till - from, 0));
	result.append(kEllipsis);
	return result;
}

} // namespace

MeasuredWords MeasureWords(
		const TextWithEntities &text,
		const style::TextStyle &st) {
	auto result = MeasuredWords{ .text = text };
	const auto &s = text.text;
	const auto size = int(s.size());
	const auto space = st.font->spacew;
	auto i = 0;
	while (i < size) {
		while (i < size && s[i].isSpace()) {
			++i;
		}
		if (i >= size) {
			break;
		}
		auto j = i;
		while (j < size && !s[j].isSpace()) {
			++j;
		}
		const auto width = WidthOf(text, st, i, j);
		const auto end = TrimmedEnd(s, i, j);
		result.ranges.emplace_back(i, j);
		result.words.push_back({
			.width = width,
			.trimmed = (end == j) ? width : WidthOf(text, st, i, end),
			.space = space,
			.letters = Letters(s, i, j),
		});
		i = j;
	}
	result.ellipsis = st.font->width(kEllipsis);
	return result;
}

std::vector<Ui::Text::String> FittedLines(
		const MeasuredWords &measured,
		const WordFit &fit,
		const std::vector<int> &widths,
		const style::TextStyle &st) {
	auto result = std::vector<Ui::Text::String>();
	const auto &text = measured.text;
	const auto &s = text.text;
	for (auto i = 0; i != int(fit.lines.size()); ++i) {
		const auto &line = fit.lines[i];
		if (line.till <= line.from
			|| line.till > int(measured.ranges.size())) {
			break;
		}
		const auto from = measured.ranges[line.from].first;
		if (line.cutWord) {
			// The longest start of the long word that fits before "…".
			const auto width = (i < int(widths.size())) ? widths[i] : 0;
			const auto [wordFrom, wordTill] = measured.ranges[line.till - 1];
			const auto fits = [&](int keep) {
				const auto end = TrimmedEnd(s, from, wordFrom + keep);
				return Line(st, Cut(text, from, end)).maxWidth() <= width;
			};
			auto low = 0;
			auto high = wordTill - wordFrom;
			while (low < high) {
				const auto middle = (low + high + 1) / 2;
				if (fits(middle)) {
					low = middle;
				} else {
					high = middle - 1;
				}
			}
			auto end = wordFrom + low;
			if (end > wordFrom && end < int(s.size()) && s[end].isLowSurrogate()) {
				--end;
			}
			result.push_back(Line(st, Cut(text, from, TrimmedEnd(s, from, end))));
		} else if (line.ellipsis) {
			const auto end = TrimmedEnd(
				s,
				from,
				measured.ranges[line.till - 1].second);
			result.push_back(Line(st, Cut(text, from, end)));
		} else {
			const auto end = measured.ranges[line.till - 1].second;
			result.push_back(Line(st, Ui::Text::Mid(text, from, end - from)));
		}
	}
	return result;
}

const Ui::Text::String &LineCache::line(
		not_null<History*> history,
		const TextWithEntities &text,
		const style::TextStyle &st,
		int width) {
	auto &entry = _entries[history];
	if (entry.width == width && entry.st == &st && entry.text == text.text) {
		return entry.line;
	}
	entry.width = width;
	entry.st = &st;
	entry.text = text.text;
	auto full = Line(st, text);
	if (full.maxWidth() <= width) {
		entry.line = std::move(full);
		return entry.line;
	}
	const auto measured = MeasureWords(text, st);
	auto fit = FitWords(measured.words, { width }, measured.ellipsis);
	if (!fit.valid) {
		fit = FitWords(measured.words, { width }, measured.ellipsis, 0);
	} else if (!fit.lines.empty()
		&& !fit.lines.front().cutWord
		&& fit.lines.front().till < int(measured.words.size())) {
		// A label cut after its first short word would say almost nothing
		// (an emoji and the ellipsis): it is cut inside the next word.
		const auto &line = fit.lines.front();
		auto used = measured.ellipsis;
		for (auto i = line.from; i != line.till; ++i) {
			const auto &word = measured.words[i];
			used += (i + 1 == line.till) ? word.trimmed : (word.width + word.space);
		}
		if (used < width * kWordCutShare) {
			fit.lines = { FitLine{ line.from, line.till + 1, true, true } };
		}
	}
	auto lines = FittedLines(measured, fit, { width }, st);
	entry.line = lines.empty() ? Ui::Text::String() : std::move(lines.front());
	return entry.line;
}

void HubPreview::prepare(
		Model &model,
		not_null<History*> history,
		int radius) {
	const auto name = model.name(history).toString();
	const auto &marked = model.previewMarked(history);
	const auto time = model.time(history);
	const auto badges = model.badges(history);
	if (_history == history
		&& _radius == radius
		&& _name == name
		&& _text == marked.text
		&& _time == time
		&& _badges == badges) {
		return;
	}
	_history = history;
	_radius = radius;
	_name = name;
	_text = marked.text;
	_time = time;
	_badges = badges;

	const auto &nameSt = st::circlesHubName;
	const auto &textSt = st::circlesHubText;
	const auto names = MeasureWords({ name }, nameSt);
	const auto texts = MeasureWords(marked, textSt);
	_timeWidth = time.isEmpty() ? 0 : st::circlesHubTimeFont->width(time);
	_badge = BadgeSize(badges);

	auto input = HubInput{
		.radius = double(radius),
		.name = {
			.words = names.words,
			.lineHeight = LineHeightOf(nameSt),
			.ellipsis = names.ellipsis,
			.maxLines = 2,
		},
		.text = {
			.words = texts.words,
			.lineHeight = LineHeightOf(textSt),
			.ellipsis = texts.ellipsis,
			.maxLines = 2,
		},
		.gapText = S(2),
		.gapMeta = S(4),
	};
	const auto timeHeight = st::circlesHubTimeFont->height;
	if (_timeWidth && !_badge.isEmpty()) {
		input.metas.push_back({
			_timeWidth + S(6) + _badge.width(),
			std::max(timeHeight, _badge.height()),
		});
		input.metas.push_back({ _timeWidth, timeHeight });
	} else if (_timeWidth) {
		input.metas.push_back({ _timeWidth, timeHeight });
	} else if (!_badge.isEmpty()) {
		input.metas.push_back({ _badge.width(), _badge.height() });
	}
	_plan = PlanHub(input);
	_nameLines = FittedLines(names, _plan.name, _plan.nameWidths, nameSt);
	_textLines = FittedLines(texts, _plan.text, _plan.textWidths, textSt);
	if (_plan.meta < 0) {
		_timeWidth = 0;
		_badge = QSize();
	} else if (_timeWidth && !_badge.isEmpty() && _plan.meta == 1) {
		_badge = QSize();
	}
}

void HubPreview::paint(
		Painter &p,
		Model &model,
		not_null<History*> history,
		QPointF center,
		double radius,
		double alpha) {
	prepare(model, history, int(std::floor(radius)));
	if (alpha <= 0.01) {
		return;
	}
	const auto was = p.opacity();
	p.setOpacity(was * alpha);
	const auto draw = [&](
			const std::vector<Ui::Text::String> &lines,
			const std::vector<int> &widths,
			int top,
			int lineHeight,
			QColor color) {
		p.setPen(color);
		const auto count = std::min(lines.size(), widths.size());
		for (auto i = 0; i != int(count); ++i) {
			const auto width = widths[i];
			lines[i].drawElided(
				p,
				int(std::round(center.x() - width / 2.)),
				int(std::round(center.y() + top + i * lineHeight)),
				width,
				1,
				style::al_center);
		}
	};
	draw(
		_nameLines,
		_plan.nameWidths,
		_plan.nameTop,
		LineHeightOf(st::circlesHubName),
		TextColor());
	draw(
		_textLines,
		_plan.textWidths,
		_plan.textTop,
		LineHeightOf(st::circlesHubText),
		SubTextColor());
	if (_plan.meta >= 0 && (_timeWidth || !_badge.isEmpty())) {
		const auto &font = st::circlesHubTimeFont;
		const auto height = std::max(
			_timeWidth ? font->height : 0,
			_badge.height());
		const auto between = (_timeWidth && !_badge.isEmpty()) ? S(6) : 0;
		const auto full = _timeWidth + between + _badge.width();
		auto x = center.x() - full / 2.;
		const auto top = center.y() + _plan.metaTop;
		if (_timeWidth) {
			p.setFont(font);
			p.setPen(st::dialogsDateFg);
			p.drawText(
				QPointF(x, top + (height - font->height) / 2. + font->ascent),
				_time);
			x += _timeWidth + between;
		}
		if (!_badge.isEmpty()) {
			PaintBadgeAt(
				p,
				_badges,
				QPointF(x + _badge.width() / 2., top + height / 2.),
				1.);
		}
	}
	p.setOpacity(was);
}

void CardPreview::paint(
		Painter &p,
		Model &model,
		not_null<History*> history,
		QRect rect,
		double alpha) {
	if (alpha <= 0.01) {
		return;
	}
	const auto was = p.opacity();
	p.setOpacity(was * alpha);
	const auto padding = S(14);
	const auto photo = rect.height() - 2 * S(12);
	{
		auto hq = PainterHighQualityEnabler(p);
		const auto ratio = style::DevicePixelRatio();
		const auto pixels = int(std::ceil(photo * ratio / 8.) * 8);
		p.drawImage(
			QRect(
				rect.x() + S(12),
				rect.y() + (rect.height() - photo) / 2,
				photo,
				photo),
			model.face(history, pixels));
	}
	const auto inner = rect.marginsRemoved({
		S(12) + photo + S(12),
		0,
		padding,
		0,
	});
	const auto &name = model.name(history);
	const auto &text = model.previewMarked(history);
	const auto time = model.time(history);
	const auto state = model.badges(history);
	const auto badge = BadgeSize(state);
	const auto &timeFont = st::circlesCardTimeFont;
	const auto nameLine = LineHeightOf(st::circlesCardName);
	const auto textLine = LineHeightOf(st::circlesCardText);
	const auto gap = S(3);
	const auto skip = S(10);
	const auto top = rect.y() + (rect.height() - nameLine - gap - textLine) / 2;
	const auto lineTop = top + nameLine + gap;

	// The name runs to the time; one that cannot gets the whole line and
	// the time goes down beside the message.
	const auto timeWidth = time.isEmpty() ? 0 : timeFont->width(time);
	const auto besideTime = inner.width() - (timeWidth ? (timeWidth + skip) : 0);
	const auto timeOnTop = (name.maxWidth() <= besideTime);
	const auto nameWidth = timeOnTop ? besideTime : inner.width();
	p.setPen(TextColor());
	_names.line(
		history,
		{ name.toString() },
		st::circlesCardName,
		nameWidth
	).drawElided(
		p,
		inner.x(),
		top,
		nameWidth,
		1);
	const auto drawTime = [&](int right, int lineY, int lineHeight) {
		p.setFont(timeFont);
		p.setPen(st::dialogsDateFg);
		p.drawText(
			right - timeWidth,
			lineY + (lineHeight - timeFont->height) / 2 + timeFont->ascent,
			time);
	};
	auto right = inner.x() + inner.width();
	if (timeWidth && timeOnTop) {
		drawTime(right, top, nameLine);
	}
	if (!badge.isEmpty()) {
		PaintBadgeAt(
			p,
			state,
			QPointF(right - badge.width() / 2., lineTop + textLine / 2.),
			1.);
		right -= badge.width() + S(8);
	}
	if (timeWidth && !timeOnTop) {
		drawTime(right, lineTop, textLine);
		right -= timeWidth + S(8);
	}
	const auto textWidth = right - inner.x();
	if (!text.text.isEmpty() && textWidth > 0) {
		p.setPen(SubTextColor());
		_texts.line(
			history,
			text,
			st::circlesCardText,
			textWidth
		).drawElided(
			p,
			inner.x(),
			lineTop,
			textWidth,
			1);
	}
	p.setOpacity(was);
}

} // namespace Safe::Circles
