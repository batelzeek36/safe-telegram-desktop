/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_ideas.h"

#include "safe/safe_calm_leaf.h"
#include "safe/safe_calm_marks.h"
#include "safe/safe_calm_thought.h"
#include "ui/painter.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

namespace Safe::Calm {
namespace {

// Logical pixels, after next-suggest.css; the panel shares the measures of
// the approved card and sheet (safe_calm_panels).
constexpr auto kSide = 12;
constexpr auto kGap = 8;
constexpr auto kMaxWidth = 440;
constexpr auto kShadow = 8;
constexpr auto kRadius = 16;
constexpr auto kPadTop = 11;
constexpr auto kPadSide = 12;
constexpr auto kPadBottom = 12;
constexpr auto kHead = 24;
constexpr auto kLeafHead = 16;
constexpr auto kListTop = 10;
constexpr auto kIdeaGap = 6;
constexpr auto kIdeaPadX = 11;
constexpr auto kIdeaPadTop = 6;
constexpr auto kIdeaPadBottom = 7;
constexpr auto kLabelGap = 2;
constexpr auto kIdeaShare = 0.92;
constexpr auto kGhostHeight = 50;
constexpr auto kBubbleRadius = 17;
constexpr auto kTailRadius = 5;
constexpr auto kAngleSize = 11;
constexpr auto kAngleMax = 110;

constexpr auto kChipHeight = 34;
constexpr auto kChipLines = 2;
constexpr auto kChipPad = 12;
constexpr auto kChipLabelGap = 6;
constexpr auto kChipMin = 60;
constexpr auto kRowGap = 5;
constexpr auto kStackTop = 6;
constexpr auto kStackBottom = 8;
constexpr auto kMoreHeight = 30;
constexpr auto kMorePadLeft = 9;
constexpr auto kMorePadRight = 11;
constexpr auto kMoreGap = 8;
constexpr auto kMoreIndex = -2;
constexpr auto kThoughtIndex = -3;
constexpr auto kMinChat = 96; // never pushed below this by a long note

constexpr auto kShowPanelMs = crl::time(220);
constexpr auto kShowChipsMs = crl::time(280);
constexpr auto kLandMs = 520.;
constexpr auto kLandStagger = 70.;
constexpr auto kPickMs = crl::time(140);

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] const style::font &AngleFont() {
	static const auto result = style::font(
		S(kAngleSize),
		st::semiboldFont->flags(),
		st::semiboldFont->family());
	return result;
}

[[nodiscard]] const style::font &MoreFont() {
	return st::semiboldFont;
}

[[nodiscard]] const style::TextStyle &IdeaStyle() {
	return st::messageTextStyle;
}

[[nodiscard]] QString MoreText() {
	return u"More"_q;
}

[[nodiscard]] QString ListeningText() {
	return u"listening…"_q;
}

// His outgoing bubble with its tail corner (bottom right) drawn small.
[[nodiscard]] QPainterPath BubblePath(QRectF r, float64 radius) {
	radius = std::min(radius, std::min(r.width(), r.height()) / 2.);
	const auto tail = std::min(float64(S(kTailRadius)), radius);
	auto path = QPainterPath();
	path.moveTo(r.left() + radius, r.top());
	path.lineTo(r.right() - radius, r.top());
	path.arcTo(
		QRectF(r.right() - 2 * radius, r.top(), 2 * radius, 2 * radius),
		90,
		-90);
	path.lineTo(r.right(), r.bottom() - tail);
	path.arcTo(
		QRectF(r.right() - 2 * tail, r.bottom() - 2 * tail, 2 * tail, 2 * tail),
		0,
		-90);
	path.lineTo(r.left() + radius, r.bottom());
	path.arcTo(
		QRectF(r.left(), r.bottom() - 2 * radius, 2 * radius, 2 * radius),
		270,
		-90);
	path.lineTo(r.left(), r.top() + radius);
	path.arcTo(
		QRectF(r.left(), r.top(), 2 * radius, 2 * radius),
		180,
		-90);
	path.closeSubpath();
	return path;
}

[[nodiscard]] QColor IdeaFill(bool chip, bool over) {
	if (chip) {
		auto result = anim::color(
			st::windowBg,
			st::msgOutBg,
			over ? 0.7 : 0.55);
		result.setAlphaF(0.94);
		return result;
	}
	return anim::color(st::boxBg, st::msgOutBg, over ? 0.58 : 0.42);
}

[[nodiscard]] float64 EaseOut(float64 t) {
	const auto left = 1. - std::clamp(t, 0., 1.);
	return 1. - left * left * left * left;
}

} // namespace

Ideas::Ideas(QWidget *parent, Args &&args)
: RpWidget(parent)
, _pick(std::move(args.pick))
, _more(std::move(args.more))
, _changed(std::move(args.changed))
, _ask(std::move(args.ask))
, _thoughtCancelled(std::move(args.thoughtCancelled))
, _faded(std::move(args.faded))
, _ticker([=](crl::time) {
	update();
	return true;
})
, _pickTimer([=] {
	if (_picked >= 0 && _picked < int(_items.size())) {
		_pick(_items[_picked].raw);
	}
}) {
	setMouseTracking(true);
	hide();
}

Ideas::~Ideas() = default;

void Ideas::setThoughtOffered(bool offered) {
	_thought = nullptr;
	if (offered) {
		_thought = std::make_unique<Thought>(Thought::Args{
			.parent = this,
			.ask = _ask,
			.resized = [=] { thoughtResized(); },
			.cancelled = _thoughtCancelled,
		});
	}
}

QString Ideas::thought() const {
	return _thought ? _thought->words() : QString();
}

bool Ideas::thoughtOpen() const {
	return _thought && _thought->opened();
}

bool Ideas::thoughtFocused() const {
	return _thought && _thought->focused();
}

void Ideas::openThought() {
	if (_thought) {
		_thought->open();
	}
}

void Ideas::foldThought() {
	if (_thought) {
		_thought->fold();
	}
}

bool Ideas::focusThought() {
	return showing() && _thought && _thought->focus();
}

void Ideas::thoughtResized() {
	if (isHidden()) {
		return;
	}
	const auto was = _reserved;
	relayout();
	if (_reserved != was) {
		notifyChanged();
	}
}

bool Ideas::showing() const {
	return !isHidden() && !_hiding;
}

Ideas::Look Ideas::look() const {
	return _look;
}

int Ideas::reservedHeight() const {
	return isHidden() ? 0 : _reserved;
}

void Ideas::showLoading(Look look) {
	const auto fresh = !showing() || (_look != look);
	_look = look;
	_loading = true;
	_listening = 0;
	_hiding = false;
	_items.clear();
	_landedAt = 0;
	_picked = _pressed = -1;
	_pickTimer.cancel();
	setOver(-1);
	relayout();
	show();
	raise();
	if (fresh) {
		_shown.start(
			[=] { update(); },
			0.,
			1.,
			(look == Look::Panel) ? kShowPanelMs : kShowChipsMs,
			anim::easeOutCirc);
	}
	_ticker.start();
	update();
	notifyChanged();
}

void Ideas::setListening(int notes) {
	notes = std::max(notes, 0);
	if (!_loading || _hiding || isHidden() || _listening == notes) {
		return;
	}
	const auto was = listening();
	_listening = notes;
	if (listening() != was) {
		const auto reserved = _reserved;
		relayout();
		if (_reserved != reserved) {
			notifyChanged();
		}
	}
}

bool Ideas::listening() const {
	return _loading && (_listening > 0);
}

QString Ideas::slotText() const {
	return listening() ? ListeningText() : MoreText();
}

void Ideas::showList(const std::vector<Suggestion> &list) {
	if (isHidden() || _hiding) {
		return;
	}
	_loading = false;
	_listening = 0;
	_items.clear();
	for (const auto &suggestion : list) {
		auto item = Item{ .raw = suggestion.text };
		const auto shown = (_look == Look::Chips)
			? QString(suggestion.text).replace(QChar('\n'), QChar(' '))
			: suggestion.text;
		item.text.setText(IdeaStyle(), shown);
		item.angle = AngleFont()->elided(
			suggestion.angle.toLower(),
			S(kAngleMax));
		_items.push_back(std::move(item));
	}
	_landedAt = crl::now();
	relayout();
	_ticker.start();
	update();
	notifyChanged();
}

void Ideas::hideAnimated() {
	if (!showing()) {
		return;
	}
	_hiding = true;
	_pickTimer.cancel();
	setOver(-1);
	if (_thought) {
		_thought->freeze();
	}
	_shown.start([=] {
		update();
		if (!_shown.animating()) {
			finishHiding();
		}
	}, 1., 0., kShowPanelMs, anim::easeOutCirc);
}

void Ideas::hideFast() {
	_shown.stop();
	if (!isHidden()) {
		finishHiding();
	}
}

void Ideas::finishHiding() {
	_hiding = false;
	_loading = false;
	_items.clear();
	_ghosts.clear();
	_picked = _pressed = -1;
	_ticker.stop();
	_pickTimer.cancel();
	_thought = nullptr; // his note goes with the ideas
	hide();
	if (_faded) {
		_faded();
	}
	notifyChanged();
}

void Ideas::notifyChanged() {
	if (!_changed) {
		return;
	} else if (_painting) {
		crl::on_main(this, [=] { _changed(); });
	} else {
		_changed();
	}
}

void Ideas::setAnchor(QRect compose) {
	if (_anchor == compose) {
		return;
	}
	_anchor = compose;
	if (!isHidden()) {
		const auto was = _reserved;
		relayout();
		if (_reserved != was) {
			notifyChanged();
		}
	}
}

void Ideas::relayout() {
	if (_anchor.width() <= 2 * S(kSide)) {
		return;
	} else if (_look == Look::Panel) {
		layoutPanel();
	} else {
		layoutChips();
	}
	update();
}

void Ideas::layoutPanel() {
	const auto width = std::min(S(kMaxWidth), _anchor.width() - 2 * S(kSide));
	const auto inner = width - 2 * S(kPadSide);
	const auto left = S(kShadow) + S(kPadSide);
	auto top = S(kShadow) + S(kPadTop);
	_headTop = top;
	top += S(kHead) + S(kListTop);
	_ghosts.clear();
	if (_loading) {
		for (const auto share : { 0.72, 0.48, 0.84 }) {
			const auto w = int(inner * share);
			_ghosts.push_back(
				QRect(left + inner - w, top, w, S(kGhostHeight)));
			top += S(kGhostHeight) + S(kIdeaGap);
		}
	} else {
		const auto most = int(inner * kIdeaShare);
		for (auto &item : _items) {
			const auto natural = 2 * S(kIdeaPadX) + std::max(
				AngleFont()->width(item.angle),
				item.text.maxWidth());
			const auto w = std::min(most, natural);
			const auto textHeight = item.text.countHeight(
				std::max(w - 2 * S(kIdeaPadX), 1));
			const auto h = S(kIdeaPadTop)
				+ AngleFont()->height
				+ S(kLabelGap)
				+ textHeight
				+ S(kIdeaPadBottom);
			item.rect = QRect(left + inner - w, top, w, h);
			item.textTop = top
				+ S(kIdeaPadTop)
				+ AngleFont()->height
				+ S(kLabelGap);
			top += h + S(kIdeaGap);
		}
	}
	if (_thought) {
		_thought->setRowLimit(_anchor.y()
			- S(kMinChat)
			- S(kGap)
			- (top + S(kPadBottom) - S(kShadow)));
		const auto row = _thought->height();
		_thought->moveTo(QRect(left, top, inner, row));
		top += row + S(kIdeaGap);
	}
	top += S(kPadBottom) - S(kIdeaGap);
	const auto height = top - S(kShadow);
	const auto moreWidth = S(8)
		+ S(kLeafHead)
		+ S(5)
		+ MoreFont()->width(slotText())
		+ S(4);
	_moreRect = QRect(left + inner - moreWidth, _headTop, moreWidth, S(kHead));
	_box = QRect(S(kShadow), S(kShadow), width, height);
	_reserved = S(kGap) + height;
	const auto x = _anchor.x() + _anchor.width() - S(kSide) - width;
	const auto y = _anchor.y() - S(kGap) - height;
	setGeometry(
		x - S(kShadow),
		y - S(kShadow),
		width + 2 * S(kShadow),
		height + 2 * S(kShadow));
}

void Ideas::layoutChips() {
	const auto right = _anchor.x() + _anchor.width() - S(kSide);
	const auto cap = std::max(
		std::min(_anchor.width() - 2 * S(kSide), st::msgMaxWidth),
		S(kChipMin));
	auto naturals = std::vector<int>();
	if (_loading) {
		naturals = { S(150), S(214), S(188) };
	} else {
		for (const auto &item : _items) {
			naturals.push_back(2 * S(kChipPad)
				+ AngleFont()->width(item.angle)
				+ S(kChipLabelGap)
				+ item.text.maxWidth());
		}
	}
	// More sits beside the shortest idea, where the row has the most room;
	// "listening…" in the same place while voice notes are transcribed.
	const auto moreRow = (_loading && !listening())
		? -1
		: int(std::min_element(begin(naturals), end(naturals))
			- begin(naturals));
	const auto moreWidth = S(kMorePadLeft)
		+ S(15)
		+ S(5)
		+ MoreFont()->width(slotText())
		+ S(kMorePadRight);
	auto rects = std::vector<QRect>();
	auto textTops = std::vector<int>();
	auto more = QRect();
	auto top = S(kStackTop);
	auto minLeft = right;
	for (auto i = 0; i != int(naturals.size()); ++i) {
		const auto limit = cap
			- ((i == moreRow) ? (moreWidth + S(kMoreGap)) : 0);
		const auto w = std::min(naturals[i], std::max(limit, S(kChipMin)));
		// An idea too long for one line wraps to a second under itself (the
		// angle stays a label column on the first); never cut to one line.
		auto h = S(kChipHeight);
		auto textTop = top + (h - IdeaStyle().font->height) / 2;
		if (!_loading) {
			const auto &item = _items[i];
			const auto column = w
				- 2 * S(kChipPad)
				- AngleFont()->width(item.angle)
				- S(kChipLabelGap);
			const auto line = item.text.countHeight(
				std::max(item.text.maxWidth(), 1));
			const auto text = std::min(
				item.text.countHeight(std::max(column, 1)),
				kChipLines * line);
			h = S(kChipHeight) - line + text;
			textTop = top + (h - text) / 2;
		}
		rects.push_back(QRect(right - w, top, w, h));
		textTops.push_back(textTop);
		minLeft = std::min(minLeft, right - w);
		if (i == moreRow) {
			more = QRect(
				right - w - S(kMoreGap) - moreWidth,
				top + (h - S(kMoreHeight)) / 2,
				moreWidth,
				S(kMoreHeight));
			minLeft = std::min(minLeft, more.x());
		}
		top += h + S(kRowGap);
	}
	auto thoughtTop = 0;
	if (_thought) {
		// Its own place on the controls side, at the column's left edge, so
		// it does not move when the chips under the shimmer land.
		minLeft = std::min(minLeft, right - cap);
		thoughtTop = top;
		_thought->setRowLimit(_anchor.y()
			- S(kMinChat)
			- (top + S(kStackBottom)));
		top += _thought->height() + S(kRowGap);
	}
	top += S(kStackBottom) - S(kRowGap);
	const auto height = top;
	const auto shift = QPoint(-minLeft, 0);
	_ghosts.clear();
	if (_loading) {
		for (const auto &rect : rects) {
			_ghosts.push_back(rect.translated(shift));
		}
	} else {
		for (auto i = 0; i != int(_items.size()); ++i) {
			_items[i].rect = rects[i].translated(shift);
			_items[i].textTop = textTops[i];
		}
	}
	_moreRect = more.isEmpty() ? QRect() : more.translated(shift);
	_box = QRect();
	_reserved = height;
	setGeometry(minLeft, _anchor.y() - height, right - minLeft, height);
	if (_thought) {
		_thought->moveTo(QRect(
			right - cap - minLeft,
			thoughtTop,
			cap,
			_thought->height()));
	}
}

float64 Ideas::landing(int index) const {
	if (!_landedAt || anim::Disabled()) {
		return 1.;
	}
	const auto elapsed = crl::now() - _landedAt - index * kLandStagger;
	return EaseOut(elapsed / kLandMs);
}

void Ideas::paintEvent(QPaintEvent *e) {
	if (_items.empty() && _ghosts.empty() && !_thought) {
		return;
	}
	_painting = true;
	const auto guard = gsl::finally([&] { _painting = false; });
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto shown = _shown.value(_hiding ? 0. : 1.);
	if (shown < 1.) {
		if (_look == Look::Panel) {
			const auto origin = QPointF(_box.right(), _box.bottom());
			const auto scale = 0.96 + 0.04 * shown;
			p.translate(origin);
			p.scale(scale, scale);
			p.translate(-origin);
		} else {
			p.translate(0., S(6) * (1. - shown));
		}
		p.setOpacity(shown);
	}
	if (_look == Look::Panel) {
		paintBox(p);
		paintHead(p);
	}
	for (const auto &ghost : _ghosts) {
		paintGhost(p, ghost);
	}
	for (auto i = 0; i != int(_items.size()); ++i) {
		paintIdea(p, i);
	}
	if (!_moreRect.isEmpty()) {
		paintMore(p);
	}
	if (_thought) {
		_thought->paint(p);
	}
	const auto landed = (landing(int(_items.size())) >= 1.);
	if (!_loading && landed) {
		_ticker.stop();
	}
}

void Ideas::paintBox(QPainter &p) {
	const auto radius = S(kRadius);
	p.setPen(Qt::NoPen);
	for (auto i = 3; i != 0; --i) {
		p.setBrush(QColor(0, 0, 0, 14 * (4 - i)));
		const auto spread = S(2) * i;
		p.drawRoundedRect(
			QRectF(_box).adjusted(-spread, -spread + S(3), spread, spread + S(3)),
			radius + spread,
			radius + spread);
	}
	p.setBrush(st::boxBg);
	p.setPen(QPen(JadeColor(0.24), st::lineWidth));
	p.drawRoundedRect(
		QRectF(_box).adjusted(0.5, 0.5, -0.5, -0.5),
		radius,
		radius);
}

void Ideas::paintHead(QPainter &p) {
	const auto left = _box.x() + S(kPadSide);
	const auto leaf = QRectF(
		left,
		_headTop + (S(kHead) - S(kLeafHead)) / 2.,
		S(kLeafHead),
		S(kLeafHead));
	PaintLeaf(p, leaf, S(kLeafHead), JadeColor());
	p.setFont(MoreFont());
	p.setPen(TickColor(st::windowFg->c));
	p.drawText(
		left + S(kLeafHead) + S(7),
		_headTop + (S(kHead) - MoreFont()->height) / 2 + MoreFont()->ascent,
		u"Reply ideas"_q);
}

void Ideas::paintMore(QPainter &p) {
	const auto chip = (_look == Look::Chips);
	const auto status = listening(); // "listening…": not a button
	const auto enabled = !_loading && (_picked < 0);
	const auto over = enabled && (_over == kMoreIndex);
	const auto opacity = p.opacity();
	const auto e = chip ? landing(int(_items.size())) : 1.;
	if (e <= 0.) {
		return;
	}
	p.save();
	if (e < 1.) {
		const auto corner = QPointF(_moreRect.right(), _moreRect.bottom());
		p.translate(S(10) * (1. - e), S(12) * (1. - e));
		p.translate(corner);
		p.scale(0.9 + 0.1 * e, 0.9 + 0.1 * e);
		p.translate(-corner);
	}
	p.setOpacity(opacity * e * ((enabled || status) ? 1. : 0.4));
	const auto rect = QRectF(_moreRect);
	auto text = QColor();
	if (chip) {
		const auto radius = rect.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgServiceBg);
		p.drawRoundedRect(rect, radius, radius);
		text = TickColor(st::msgServiceFg->c);
		if (over) {
			auto lift = text;
			lift.setAlphaF(0.12);
			p.setBrush(lift);
			p.drawRoundedRect(rect, radius, radius);
		}
	} else {
		if (over) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(rect, S(8), S(8));
		}
		text = TickColor(st::windowFg->c);
	}
	const auto icon = chip ? S(15) : S(kLeafHead);
	auto x = rect.x() + (chip ? S(kMorePadLeft) : S(8));
	const auto iconRect = QRectF(
		x,
		rect.y() + (rect.height() - icon) / 2.,
		icon,
		icon);
	if (status) {
		PaintLeaf(p, iconRect, icon, JadeColor());
	} else {
		PaintRefresh(p, iconRect, text);
	}
	x += icon + S(5);
	p.setFont(MoreFont());
	p.setPen(text);
	p.drawText(
		QPointF(
			x,
			rect.y()
				+ (rect.height() - MoreFont()->height) / 2.
				+ MoreFont()->ascent),
		slotText());
	p.restore();
}

void Ideas::paintIdea(QPainter &p, int index) {
	const auto &item = _items[index];
	const auto e = landing(index);
	if (e <= 0.) {
		return;
	}
	const auto chip = (_look == Look::Chips);
	const auto picked = (_picked == index);
	const auto over = (_over == index) && (_picked < 0);
	const auto rect = QRectF(item.rect);
	p.save();
	if (e < 1.) {
		const auto corner = QPointF(rect.right(), rect.bottom());
		p.translate(S(10) * (1. - e), S(12) * (1. - e));
		p.translate(corner);
		p.scale(0.9 + 0.1 * e, 0.9 + 0.1 * e);
		p.translate(-corner);
		p.setOpacity(p.opacity() * e);
	}
	const auto path = BubblePath(
		rect.adjusted(0.5, 0.5, -0.5, -0.5),
		S(kBubbleRadius));
	p.setBrush(picked ? st::msgOutBg->c : IdeaFill(chip, over));
	p.setPen(picked
		? QPen(Qt::NoPen)
		: QPen(JadeColor(0.24), st::lineWidth));
	p.drawPath(path);

	const auto angle = TickColor(st::historyTextOutFg->c);
	const auto padX = chip ? S(kChipPad) : S(kIdeaPadX);
	const auto left = item.rect.x() + padX;
	p.setFont(AngleFont());
	p.setPen(angle);
	if (chip) {
		// The angle sits on the first line of the idea.
		const auto line = IdeaStyle().font->height;
		p.drawText(
			left,
			item.textTop + (line - AngleFont()->height) / 2 + AngleFont()->ascent,
			item.angle);
		const auto textLeft = left
			+ AngleFont()->width(item.angle)
			+ S(kChipLabelGap);
		p.setPen(st::historyTextOutFg);
		item.text.draw(p, {
			.position = { textLeft, item.textTop },
			.outerWidth = width(),
			.availableWidth = std::max(
				item.rect.x() + item.rect.width() - padX - textLeft,
				1),
			.elisionLines = kChipLines,
		});
	} else {
		p.drawText(
			left,
			item.rect.y() + S(kIdeaPadTop) + AngleFont()->ascent,
			item.angle);
		p.setPen(st::historyTextOutFg);
		item.text.draw(p, {
			.position = { left, item.textTop },
			.outerWidth = width(),
			.availableWidth = std::max(item.rect.width() - 2 * padX, 1),
		});
	}
	p.restore();
}

void Ideas::paintGhost(QPainter &p, QRect rect) {
	const auto chip = (_look == Look::Chips);
	p.setBrush(IdeaFill(chip, false));
	p.setPen(QPen(JadeColor(0.24), st::lineWidth));
	p.drawPath(BubblePath(
		QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5),
		S(kBubbleRadius)));
	const auto base = st::historyTextOutFg->c;
	if (chip) {
		PaintShimmerBar(
			p,
			QRectF(
				rect.x() + S(kChipPad),
				rect.y() + (rect.height() - S(8)) / 2.,
				rect.width() - 2 * S(kChipPad),
				S(8)),
			base);
		return;
	}
	const auto left = rect.x() + S(kIdeaPadX);
	const auto top = rect.y() + (rect.height() - S(6) - S(7) - S(8)) / 2.;
	PaintShimmerBar(p, QRectF(left, top, S(34), S(6)), base);
	PaintShimmerBar(
		p,
		QRectF(left, top + S(6) + S(7), rect.width() - 2 * S(kIdeaPadX), S(8)),
		base);
}

int Ideas::hit(QPoint point) const {
	if (!showing() || _picked >= 0) {
		return -1;
	} else if (!_loading && _moreRect.contains(point)) {
		return kMoreIndex;
	} else if (_thought && _thought->hits(point)) {
		return kThoughtIndex;
	}
	for (auto i = 0; i != int(_items.size()); ++i) {
		if (_items[i].rect.contains(point)) {
			return i;
		}
	}
	return -1;
}

void Ideas::setOver(int over) {
	if (_over == over) {
		return;
	}
	_over = over;
	if (_thought) {
		_thought->setOver(over == kThoughtIndex);
	}
	setCursor((over == -1) ? style::cur_default : style::cur_pointer);
	update();
}

void Ideas::pick(int index) {
	_picked = index;
	setOver(-1);
	update();
	if (anim::Disabled()) {
		_pickTimer.callOnce(0);
	} else {
		_pickTimer.callOnce(kPickMs);
	}
}

void Ideas::mouseMoveEvent(QMouseEvent *e) {
	setOver(hit(e->pos()));
}

void Ideas::mousePressEvent(QMouseEvent *e) {
	_pressed = (e->button() == Qt::LeftButton) ? hit(e->pos()) : -1;
}

void Ideas::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed == -1 || pressed != hit(e->pos())) {
		return;
	} else if (pressed == kMoreIndex) {
		if (_more) {
			_more();
		}
	} else if (pressed == kThoughtIndex) {
		if (_thought) {
			setOver(-1);
			_thought->open();
		}
	} else {
		pick(pressed);
	}
}

void Ideas::leaveEventHook(QEvent *e) {
	setOver(-1);
}

} // namespace Safe::Calm
