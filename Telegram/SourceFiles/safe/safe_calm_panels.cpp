/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_panels.h"

#include "safe/safe_calm_leaf.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"

namespace Safe::Calm {
namespace {

// Logical pixels, after the mocks in docs/previews/calm/css/calm.css.
constexpr auto kSide = 12;
constexpr auto kGap = 8;
constexpr auto kMaxWidth = 440;
constexpr auto kShadow = 8;
constexpr auto kPadding = 12;
constexpr auto kRadius = 16;
constexpr auto kHeadHeight = 18;
constexpr auto kLeafHead = 15;
constexpr auto kSkip = 9;
constexpr auto kBubblePadX = 11;
constexpr auto kBubblePadY = 8;
constexpr auto kBubbleRadius = 12;
constexpr auto kSkeleton = 44;
constexpr auto kButtonHeight = 36;
constexpr auto kButtonRadius = 12;
constexpr auto kPillHeight = 28;
constexpr auto kHeatBar = 14;
constexpr auto kPillShowMs = crl::time(1700);
constexpr auto kAnimationMs = crl::time(220);

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, float64 alpha) {
	color.setAlphaF(color.alphaF() * alpha);
	return color;
}

[[nodiscard]] const style::font &BodyFont() {
	return st::normalFont;
}

[[nodiscard]] const style::font &HeadFont() {
	return st::semiboldFont;
}

[[nodiscard]] const style::font &SmallFont() {
	return st::msgDateFont;
}

} // namespace

Panel::Panel(QWidget *parent)
: RpWidget(parent)
, _ticker([=](crl::time now) {
	update();
	return true;
})
, _autoHide([=] { hideAnimated(); }) {
	setMouseTracking(true);
	hide();
}

bool Panel::showing() const {
	return (_kind != Kind::None) && !_hiding && !isHidden();
}

bool Panel::isCard() const {
	return showing()
		&& (_kind == Kind::Card
			|| _kind == Kind::Checking
			|| _kind == Kind::Failed);
}

bool Panel::isSheet() const {
	return showing() && (_kind == Kind::Sheet);
}

bool Panel::isSummary() const {
	return showing() && (_kind == Kind::Summary || _kind == Kind::Writing);
}

bool Panel::isPill() const {
	return showing() && (_kind == Kind::Pill);
}

void Panel::showSummary(
		const QString &title,
		const QString &text,
		const QString &line,
		std::vector<PanelButton> buttons) {
	_head = title;
	_hint = u"only you see this card"_q;
	_main.setText(st::defaultTextStyle, text);
	_line.setText(st::defaultTextStyle, line);
	if (!_label) {
		// minWidth 0 would make the label's text one line that never wraps.
		static const auto style = [] {
			auto result = st::defaultFlatLabel;
			result.minWidth = 1;
			return result;
		}();
		_label = base::make_unique_q<Ui::FlatLabel>(this, style);
		_label->setSelectable(true);
		_label->setTextColorOverride(st::historyTextOutFg->c);
		_label->hide();
	}
	_label->setText(text);
	_label->setContextCopyText(u"Copy text"_q);
	start(Kind::Summary, std::move(buttons));
}

void Panel::showWriting(
		const QString &title,
		std::vector<PanelButton> buttons) {
	_head = title;
	_hint = u"writing it…"_q;
	start(Kind::Writing, std::move(buttons));
	_ticker.start();
}

void Panel::updateLabel() {
	if (!_label) {
		return;
	}
	const auto show = (_kind == Kind::Summary)
		&& !_hiding
		&& !isHidden()
		&& !_shown.animating()
		&& (_laidWidth > 0);
	if (show) {
		const auto inner = _laidWidth - 2 * S(kPadding);
		const auto width = inner - 2 * S(kBubblePadX);
		_label->resizeToWidth(width);
		_label->moveToLeft(
			S(kShadow) + S(kPadding) + S(kBubblePadX),
			_mainTop + S(kBubblePadY));
		_label->show();
		_label->raise();
	} else if (!_label->isHidden()) {
		_label->hide();
	}
	update();
}

void Panel::showChecking(const QString &original, Fn<void()> edit) {
	_head = u"Calmer version"_q;
	_hint = u"checking…"_q;
	_yours.setText(st::defaultTextStyle, original);
	start(Kind::Checking, {
		{ u"Edit"_q, PanelButton::Look::Ghost, std::move(edit) },
	});
	_ticker.start();
}

void Panel::showCard(
		const Verdict &verdict,
		const QString &original,
		Fn<void()> sendCalm,
		Fn<void()> sendMine,
		Fn<void()> edit) {
	_head = u"Calmer version"_q;
	_hint = u"only you see this card"_q;
	_main.setText(st::defaultTextStyle, verdict.text);
	_yours.setText(st::defaultTextStyle, original);
	start(Kind::Card, {
		{ u"Send calm version"_q, PanelButton::Look::Primary, std::move(sendCalm) },
		{ u"Send mine"_q, PanelButton::Look::Ember, std::move(sendMine) },
		{ u"Edit"_q, PanelButton::Look::Ghost, std::move(edit) },
	});
}

void Panel::showFailed(
		const QString &title,
		Fn<void()> sendAsIs,
		Fn<void()> edit) {
	_head = title;
	_hint = QString();
	_main.setText(
		st::defaultTextStyle,
		u"Calm could not reach the runner or the API, so nothing was "
		"sent. Send it as typed, or edit it first."_q);
	start(Kind::Failed, {
		{ u"Send as is"_q, PanelButton::Look::Ember, std::move(sendAsIs) },
		{ u"Edit"_q, PanelButton::Look::Ghost, std::move(edit) },
	});
}

void Panel::showSheet(
		const Verdict &verdict,
		Fn<void()> use,
		Fn<void()> keepEditing) {
	_head = u"Reads hot"_q;
	_hint = QString();
	_heat = std::clamp(verdict.heat, 1, 3);
	_notes.clear();
	_notes.reserve(verdict.notes.size());
	for (const auto &note : verdict.notes) {
		_notes.emplace_back(1);
		_notes.back().setText(st::defaultTextStyle, note);
	}
	_calmLabel = u"CALMER VERSION"_q;
	_main.setText(st::defaultTextStyle, verdict.text);
	start(Kind::Sheet, {
		{ u"Use this"_q, PanelButton::Look::Primary, std::move(use) },
		{ u"Keep editing"_q, PanelButton::Look::Ghost, std::move(keepEditing) },
	});
}

void Panel::setPillLift(int lift) {
	if (_pillLift != lift) {
		_pillLift = lift;
		if (isPill()) {
			relayout();
		}
	}
}

void Panel::showPill(const QString &text) {
	_head = text;
	_hint = QString();
	start(Kind::Pill, {});
	_autoHide.callOnce(kPillShowMs);
}

void Panel::start(Kind kind, std::vector<PanelButton> buttons) {
	const auto wasShown = showing();
	_kind = kind;
	_hiding = false;
	_ticker.stop();
	_autoHide.cancel();
	_over = _pressed = -1;
	_buttons.clear();
	for (auto &button : buttons) {
		_buttons.push_back({ .button = std::move(button) });
	}
	relayout();
	show();
	raise();
	if (!wasShown) {
		_shown.start([=] {
			update();
			if (!_shown.animating()) {
				updateLabel();
			}
		}, 0., 1., kAnimationMs, anim::easeOutCirc);
	}
	updateLabel();
	notifyChanged();
}

void Panel::hideAnimated() {
	if (!showing()) {
		return;
	}
	_hiding = true;
	_autoHide.cancel();
	updateLabel();
	_shown.start([=] {
		update();
		if (!_shown.animating()) {
			finishHiding();
		}
	}, 1., 0., kAnimationMs, anim::easeOutCirc);
}

void Panel::hideFast() {
	_shown.stop();
	finishHiding();
}

void Panel::finishHiding() {
	_kind = Kind::None;
	_hiding = false;
	updateLabel();
	_ticker.stop();
	_autoHide.cancel();
	_buttons.clear();
	hide();
	notifyChanged();
}

void Panel::setChangedCallback(Fn<void()> changed) {
	_changed = std::move(changed);
}

void Panel::notifyChanged() {
	if (!_changed) {
		return;
	} else if (_painting) {
		crl::on_main(this, [=] { _changed(); });
	} else {
		_changed();
	}
}

int Panel::reservedHeight() const {
	return (isHidden() || _kind == Kind::None || _kind == Kind::Pill)
		? 0
		: _reserved;
}

bool Panel::activatePrimary() {
	if (!showing()) {
		return false;
	}
	for (const auto &laid : _buttons) {
		if (laid.button.look == PanelButton::Look::Primary
			&& laid.button.callback) {
			const auto callback = laid.button.callback;
			callback();
			return true;
		}
	}
	return false;
}

void Panel::setAnchor(QRect compose) {
	if (_anchor != compose) {
		_anchor = compose;
		if (_kind != Kind::None) {
			const auto was = _reserved;
			relayout();
			if (_reserved != was) {
				notifyChanged();
			}
		}
	}
}

int Panel::contentWidth() const {
	const auto available = _anchor.width() - 2 * S(kSide);
	switch (_kind) {
	case Kind::Pill:
		return std::min(
			available,
			S(kPadding) * 2 + S(13) + S(6) + SmallFont()->width(_head));
	default:
		return std::min(S(kMaxWidth), available);
	}
}

int Panel::layoutBody(int top, int width) {
	const auto inner = width - 2 * S(kPadding);
	_headTop = top;
	top += S(kHeadHeight) + S(kSkip);
	_noteTops.clear();
	switch (_kind) {
	case Kind::Checking:
		_mainTop = top;
		_mainHeight = S(kSkeleton);
		top += _mainHeight + S(kSkip) - S(2);
		_yoursTop = top;
		top += SmallFont()->height + S(4);
		break;
	case Kind::Card:
		_mainTop = top;
		_mainHeight = _main.countHeight(inner - 2 * S(kBubblePadX))
			+ 2 * S(kBubblePadY);
		top += _mainHeight + S(kSkip) - S(2);
		_yoursTop = top;
		top += SmallFont()->height + S(4);
		break;
	case Kind::Failed:
		_mainTop = top;
		_mainHeight = _main.countHeight(inner);
		top += _mainHeight + S(kSkip);
		break;
	case Kind::Writing:
		_mainTop = top;
		_mainHeight = S(kSkeleton);
		top += _mainHeight + S(kSkip);
		break;
	case Kind::Summary:
		_mainTop = top;
		_mainHeight = _main.countHeight(inner - 2 * S(kBubblePadX))
			+ 2 * S(kBubblePadY);
		top += _mainHeight + S(kSkip) - S(2);
		_lineTop = top;
		top += _line.countHeight(inner - S(4)) + S(kSkip);
		break;
	case Kind::Sheet: {
		for (const auto &note : _notes) {
			_noteTops.push_back(top);
			top += note.countHeight(inner - S(13) - S(2)) + S(6);
		}
		_boxTop = top + S(4);
		const auto label = SmallFont()->height + S(3);
		_mainTop = _boxTop + S(kBubblePadY) + label;
		_mainHeight = _main.countHeight(inner - 2 * S(kBubblePadX));
		_boxHeight = label + _mainHeight + 2 * S(kBubblePadY);
		top = _boxTop + _boxHeight + S(kSkip) + S(1);
	} break;
	default:
		break;
	}
	return top;
}

void Panel::layoutButtons(int top, int width) {
	const auto inner = width - 2 * S(kPadding);
	const auto left = S(kShadow) + S(kPadding);
	const auto height = S(kButtonHeight);
	auto primary = std::vector<int>();
	auto rest = std::vector<int>();
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		((_buttons[i].button.look == PanelButton::Look::Primary
			&& _kind != Kind::Sheet) ? primary : rest).push_back(i);
	}
	for (const auto i : primary) {
		_buttons[i].rect = QRect(left, top, inner, height);
		top += height + S(2);
	}
	if (!rest.empty()) {
		const auto gap = S(8);
		const auto each = (inner - gap * (int(rest.size()) - 1))
			/ int(rest.size());
		auto x = left;
		for (const auto i : rest) {
			_buttons[i].rect = QRect(x, top, each, height);
			x += each + gap;
		}
	}
}

void Panel::relayout() {
	if (_kind == Kind::None || _anchor.isEmpty()) {
		return;
	}
	const auto width = contentWidth();
	_laidWidth = width;
	auto height = 0;
	if (_kind == Kind::Pill) {
		height = S(kPillHeight);
	} else {
		const auto top = layoutBody(S(kShadow) + S(kPadding), width);
		const auto primaryRows = ranges::any_of(_buttons, [&](const Laid &laid) {
			return (laid.button.look == PanelButton::Look::Primary)
				&& (_kind != Kind::Sheet);
		}) ? 1 : 0;
		const auto rows = primaryRows + (_buttons.size() > primaryRows ? 1 : 0);
		layoutButtons(top, width);
		height = (top - S(kShadow))
			+ rows * S(kButtonHeight)
			+ (rows > 1 ? S(2) : 0)
			+ S(kPadding) - S(4);
	}
	const auto full = QSize(width + 2 * S(kShadow), height + 2 * S(kShadow));
	const auto centered = (_kind == Kind::Pill);
	const auto x = centered
		? (_anchor.x() + (_anchor.width() - width) / 2)
		: (_anchor.x() + _anchor.width() - S(kSide) - width);
	const auto y = _anchor.y()
		- S(kGap)
		- height
		- (centered ? _pillLift : 0);
	setGeometry(QRect(QPoint(x - S(kShadow), y - S(kShadow)), full));
	_reserved = S(kGap) + height;
	updateLabel();
}

void Panel::paintEvent(QPaintEvent *e) {
	if (_kind == Kind::None) {
		return;
	}
	const auto box = rect().marginsRemoved(
		{ S(kShadow), S(kShadow), S(kShadow), S(kShadow) });
	if (box.width() != _laidWidth && _relayoutFor != box.width()) {
		_relayoutFor = box.width();
		_painting = true;
		relayout();
		notifyChanged();
		_painting = false;
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto progress = _shown.value(_hiding ? 0. : 1.);
	if (progress < 1.) {
		const auto scale = 0.96 + 0.04 * progress;
		const auto origin = (_kind == Kind::Pill)
			? QPointF(box.center().x(), box.bottom())
			: QPointF(box.right(), box.bottom());
		p.translate(origin);
		p.scale(scale, scale);
		p.translate(-origin);
		p.setOpacity(progress);
	}
	const auto radius = (_kind == Kind::Pill) ? S(14) : S(kRadius);
	for (auto i = 3; i != 0; --i) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0, 0, 0, 14 * (4 - i)));
		const auto spread = S(2) * i;
		p.drawRoundedRect(
			QRectF(box).adjusted(-spread, -spread + S(3), spread, spread + S(3)),
			radius + spread,
			radius + spread);
	}
	const auto ember = (_kind == Kind::Sheet || _kind == Kind::Failed);
	p.setBrush(st::boxBg);
	p.setPen(QPen(
		ember ? EmberColor(0.32) : JadeColor(0.24),
		st::lineWidth));
	p.drawRoundedRect(QRectF(box).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
	p.translate(box.topLeft() - QPoint(S(kShadow), S(kShadow)));
	paintBody(p, box.width());
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		paintButton(p, _buttons[i], (i == _over));
	}
}

void Panel::paintBody(QPainter &p, int width) {
	const auto left = S(kShadow) + S(kPadding);
	const auto inner = width - 2 * S(kPadding);
	const auto top = S(kShadow);
	if (_kind == Kind::Pill) {
		const auto h = S(kPillHeight);
		PaintLeaf(p, QRectF(left, top + (h - S(13)) / 2., S(13), S(13)), S(13), JadeColor());
		p.setFont(SmallFont());
		p.setPen(st::windowFg);
		p.drawText(
			left + S(13) + S(6),
			top + (h - SmallFont()->height) / 2 + SmallFont()->ascent,
			_head);
		return;
	}

	// Head row: leaf, title, then the hint or the heat bars on the right.
	const auto hot = (_kind == Kind::Sheet || _kind == Kind::Failed);
	const auto headColor = hot ? EmberColor() : JadeColor();
	{
		const auto leafRect = QRectF(
			left,
			_headTop + (S(kHeadHeight) - S(kLeafHead)) / 2.,
			S(kLeafHead),
			S(kLeafHead));
		const auto opacity = p.opacity();
		p.setOpacity(opacity * 0.55);
		PaintLeafGlow(p, leafRect.center(), S(kLeafHead), headColor);
		p.setOpacity(opacity);
		PaintLeaf(p, leafRect, S(kLeafHead), headColor);
		p.setFont(HeadFont());
		p.setPen(hot ? EmberColor() : st::windowSubTextFg->c);
		const auto baseline = _headTop
			+ (S(kHeadHeight) - HeadFont()->height) / 2
			+ HeadFont()->ascent;
		p.drawText(left + S(kLeafHead) + S(7), baseline, _head);
		if (_kind == Kind::Sheet) {
			auto x = left + inner - (3 * S(kHeatBar) + 2 * S(3));
			for (auto i = 0; i != 3; ++i) {
				p.setPen(Qt::NoPen);
				p.setBrush((i < _heat) ? EmberColor() : EmberColor(0.22));
				p.drawRoundedRect(
					QRectF(x, _headTop + (S(kHeadHeight) - S(4)) / 2., S(kHeatBar), S(4)),
					S(2),
					S(2));
				x += S(kHeatBar) + S(3);
			}
		} else if (!_hint.isEmpty()) {
			p.setFont(SmallFont());
			p.setPen(st::windowSubTextFg);
			const auto w = SmallFont()->width(_hint);
			p.drawText(left + inner - w, baseline, _hint);
		}
	}

	const auto drawText = [&](
			const Ui::Text::String &text,
			int x,
			int y,
			int w,
			const style::color &color,
			int lines = 0) {
		p.setPen(color);
		text.draw(p, {
			.position = { x, y },
			.outerWidth = width,
			.availableWidth = w,
			.elisionLines = lines,
		});
	};
	switch (_kind) {
	case Kind::Writing:
	case Kind::Checking: {
		p.setPen(Qt::NoPen);
		const auto pulse = 0.5 + 0.5 * std::sin(crl::now() / 260.);
		p.setBrush(JadeColor(0.10 + 0.08 * pulse));
		p.drawRoundedRect(QRectF(left, _mainTop, inner, _mainHeight), S(kBubbleRadius), S(kBubbleRadius));
		p.setBrush(st::windowSubTextFg->c);
		p.setOpacity(p.opacity() * (0.18 + 0.1 * pulse));
		p.drawRoundedRect(QRectF(left + S(kBubblePadX), _mainTop + S(12), inner * 0.7, S(7)), S(3), S(3));
		p.drawRoundedRect(QRectF(left + S(kBubblePadX), _mainTop + S(25), inner * 0.42, S(7)), S(3), S(3));
		p.setOpacity(p.opacity() / (0.18 + 0.1 * pulse));
		if (_kind == Kind::Writing) {
			break;
		}
	} [[fallthrough]];
	case Kind::Card: {
		if (_kind == Kind::Card) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgOutBg);
			p.drawRoundedRect(QRectF(left, _mainTop, inner, _mainHeight), S(kBubbleRadius), S(kBubbleRadius));
			drawText(
				_main,
				left + S(kBubblePadX),
				_mainTop + S(kBubblePadY),
				inner - 2 * S(kBubblePadX),
				st::historyTextOutFg);
		}
		const auto label = u"You wrote"_q;
		p.setFont(SmallFont());
		p.setPen(EmberColor(0.85));
		p.drawText(left + S(2), _yoursTop + SmallFont()->ascent, label);
		const auto labelWidth = SmallFont()->width(label) + S(5);
		drawText(
			_yours,
			left + S(2) + labelWidth,
			_yoursTop + (SmallFont()->height - st::normalFont->height) / 2,
			inner - S(4) - labelWidth,
			st::windowSubTextFg,
			1);
	} break;
	case Kind::Failed:
		drawText(_main, left, _mainTop, inner, st::windowFg);
		break;
	case Kind::Summary: {
		p.setPen(Qt::NoPen);
		p.setBrush(st::msgOutBg);
		p.drawRoundedRect(QRectF(left, _mainTop, inner, _mainHeight), S(kBubbleRadius), S(kBubbleRadius));
		if (!_label || _label->isHidden()) {
			drawText(
				_main,
				left + S(kBubblePadX),
				_mainTop + S(kBubblePadY),
				inner - 2 * S(kBubblePadX),
				st::historyTextOutFg);
		}
		drawText(_line, left + S(2), _lineTop, inner - S(4), st::windowSubTextFg);
	} break;
	case Kind::Sheet: {
		for (auto i = 0; i != int(_notes.size()); ++i) {
			p.setPen(Qt::NoPen);
			p.setBrush(EmberColor(0.82));
			p.drawEllipse(QRectF(left + S(1), _noteTops[i] + S(6), S(5), S(5)));
			drawText(_notes[i], left + S(13), _noteTops[i], inner - S(13) - S(2), st::windowFg);
		}
		p.setBrush(JadeColor(0.10));
		p.setPen(QPen(JadeColor(0.18), st::lineWidth));
		p.drawRoundedRect(QRectF(left, _boxTop, inner, _boxHeight), S(kBubbleRadius), S(kBubbleRadius));
		p.setFont(SmallFont());
		p.setPen(JadeColor());
		p.drawText(left + S(kBubblePadX), _boxTop + S(kBubblePadY) + SmallFont()->ascent, _calmLabel);
		drawText(
			_main,
			left + S(kBubblePadX),
			_mainTop,
			inner - 2 * S(kBubblePadX),
			st::windowFg);
	} break;
	default:
		break;
	}
}

void Panel::paintButton(QPainter &p, const Laid &laid, bool over) {
	const auto &rect = laid.rect;
	const auto &text = laid.button.text;
	auto fg = QColor();
	switch (laid.button.look) {
	case PanelButton::Look::Primary: {
		auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
		gradient.setColorAt(0., QColor(0x3A, 0xD5, 0x96));
		gradient.setColorAt(1., QColor(0x22, 0xA8, 0x73));
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRoundedRect(rect, S(kButtonRadius), S(kButtonRadius));
		if (over) {
			p.setBrush(QColor(255, 255, 255, 28));
			p.drawRoundedRect(rect, S(kButtonRadius), S(kButtonRadius));
		}
		fg = QColor(0x04, 0x26, 0x1A);
	} break;
	case PanelButton::Look::Ghost:
	case PanelButton::Look::Ember:
		if (over) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(rect, S(kButtonRadius), S(kButtonRadius));
		}
		fg = (laid.button.look == PanelButton::Look::Ember)
			? EmberColor(0.9)
			: st::windowSubTextFg->c;
		break;
	}
	const auto &font = (laid.button.look == PanelButton::Look::Primary)
		? HeadFont()
		: BodyFont();
	p.setFont(font);
	p.setPen(fg);
	auto textLeft = rect.x() + (rect.width() - font->width(text)) / 2;
	if (laid.button.look == PanelButton::Look::Primary) {
		const auto leaf = S(15);
		textLeft += (leaf + S(7)) / 2;
		PaintLeaf(
			p,
			QRectF(textLeft - leaf - S(7), rect.y() + (rect.height() - leaf) / 2., leaf, leaf),
			leaf,
			fg);
	}
	p.drawText(
		textLeft,
		rect.y() + (rect.height() - font->height) / 2 + font->ascent,
		text);
}

int Panel::buttonAt(QPoint point) const {
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		if (_buttons[i].rect.contains(point)) {
			return i;
		}
	}
	return -1;
}

void Panel::mouseMoveEvent(QMouseEvent *e) {
	const auto over = showing() ? buttonAt(e->pos()) : -1;
	if (_over != over) {
		_over = over;
		setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void Panel::mousePressEvent(QMouseEvent *e) {
	_pressed = showing() ? buttonAt(e->pos()) : -1;
}

void Panel::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed >= 0 && pressed == buttonAt(e->pos()) && showing()) {
		if (const auto callback = _buttons[pressed].button.callback) {
			callback();
		}
	}
}

void Panel::leaveEventHook(QEvent *e) {
	if (_over >= 0) {
		_over = -1;
		setCursor(style::cur_default);
		update();
	}
}

} // namespace Safe::Calm
