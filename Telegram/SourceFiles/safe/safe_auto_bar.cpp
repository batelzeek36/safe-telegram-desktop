/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_auto_bar.h"

#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "safe/safe_auto.h"
#include "safe/safe_auto_mark.h"
#include "safe/safe_calm_leaf.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/ui_utility.h"

#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "styles/style_widgets.h"

#include <QtCore/QDateTime>
#include <QtCore/QLocale>

namespace Safe::Auto {
namespace {

// Measures in the Mac's own units (the mocks' phone sizes, one notch
// denser, like the Calm panels): the card lines up with the reply row.
constexpr auto kSide = 6;
constexpr auto kGapAbove = 4;
constexpr auto kGapBelow = 4;
constexpr auto kRadius = 14;
constexpr auto kLineHeight = 52;
constexpr auto kIcon = 30;
constexpr auto kPause = 16;
constexpr auto kPadLeft = 12;
constexpr auto kPadRight = 6;
constexpr auto kTextGap = 12;
constexpr auto kSubGap = 2;
constexpr auto kActionHeight = 34;
constexpr auto kActionPad = 12;
constexpr auto kActionRadius = 10;
constexpr auto kHandPadX = 14;
constexpr auto kHandPadY = 11;
constexpr auto kHeadIcon = 15;
constexpr auto kHeadGap = 6;
constexpr auto kReasonGap = 4;
constexpr auto kReasonLines = 3;
constexpr auto kButtonsGap = 11;
constexpr auto kButtonHeight = 34;
constexpr auto kButtonRadius = 12;
constexpr auto kPrimaryPad = 15;
constexpr auto kGhostPad = 14;
constexpr auto kButtonSpacing = 8;

constexpr auto kHeightMs = crl::time(400);
constexpr auto kFadeMs = crl::time(360);
constexpr auto kLeaveMs = crl::time(160);
constexpr auto kClockMs = crl::time(15000);

// The mock's colors (docs/previews/auto/css/auto-bar.css), on the theme's
// own composer color.
constexpr auto kTick = 0x8FF5C6; // the bar's action ("Pause 30 min")
constexpr auto kBarSub = 0x7FE0B4; // the ghost button's words
constexpr auto kPrimaryFrom = 0x3AD596;
constexpr auto kPrimaryTo = 0x22A873;
constexpr auto kPrimaryText = 0x04261A;

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Rgb(int rgb, float64 alpha = 1.) {
	auto result = QColor::fromRgb(QRgb(rgb));
	result.setAlphaF(std::clamp(alpha, 0., 1.));
	return result;
}

[[nodiscard]] QColor Faded(QColor color, float64 opacity) {
	color.setAlphaF(color.alphaF() * std::clamp(opacity, 0., 1.));
	return color;
}

[[nodiscard]] const style::font &TitleFont() {
	return st::semiboldFont;
}

[[nodiscard]] const style::font &SubFont() {
	static const auto result = style::font(
		S(12),
		st::normalFont->flags(),
		st::normalFont->family());
	return result;
}

[[nodiscard]] const style::font &HeadFont() {
	static const auto result = style::font(
		S(12),
		st::semiboldFont->flags(),
		st::semiboldFont->family());
	return result;
}

[[nodiscard]] const style::font &ButtonFont(bool primary) {
	return primary ? st::semiboldFont : st::normalFont;
}

[[nodiscard]] QString TimeText(int64 unix) {
	return QLocale().toString(
		QDateTime::fromSecsSinceEpoch(unix).time(),
		QLocale::ShortFormat);
}

[[nodiscard]] QString PausedTitle(int64 until) {
	return u"Paused · back at %1"_q.arg(TimeText(until));
}

[[nodiscard]] float64 EaseOut(float64 t) {
	const auto left = 1. - std::clamp(t, 0., 1.);
	return 1. - left * left * left;
}

} // namespace

Bar::Bar(Args &&args)
: RpWidget(args.parent)
, _reserve(std::move(args.reserve))
, _replyYourself(std::move(args.replyYourself))
, _clock([=] { refresh(); }) {
	setMouseTracking(true);
	hide();
	Manager::Instance().updates(
	) | rpl::filter([=](int64 chat) {
		return _chat && (chat == _chat);
	}) | rpl::on_next([=] {
		refresh();
	}, lifetime());
}

Bar::~Bar() {
	Manager::Instance().hidden(this);
}

void Bar::setHistory(History *history) {
	if (_history == history) {
		// The host's hideChildWidgets() hides every child on a chat switch.
		if (_face && isHidden()) {
			show();
		}
		return;
	}
	_history = history;
	_historyLifetime.destroy();
	auto &manager = Manager::Instance();
	manager.hidden(this);
	_chat = (history && manager.available())
		? RunnerChatId(history->peer)
		: 0;
	if (_chat) {
		manager.watch(&history->session());
		manager.showing(history->peer, this);
		history->session().changes().messageUpdates(
			Data::MessageUpdate::Flag::NewAdded
		) | rpl::filter([=](const Data::MessageUpdate &update) {
			return (update.item->history() == _history)
				&& update.item->out();
		}) | rpl::on_next([=] {
			refresh();
		}, _historyLifetime);
	}

	// A new chat opens with its bar already in place, no morph.
	_face = nullptr;
	_signature = QString();
	_instant = true;
	refresh();
	_instant = false;
	setVisible(_face != nullptr);
	queueReserve();
}

void Bar::refresh() {
	const auto entry = _chat ? Manager::Instance().entry(_chat) : nullptr;
	auto view = ViewOutput();
	if (entry && entry->state && _history) {
		const auto &state = *entry->state;
		view = Decide({
			.known = true,
			.enabled = state.enabled,
			.now = int64(base::unixtime::now()),
			.pausedUntil = state.pausedUntil,
			.handback = state.handback.has_value(),
			.handbackHeld = HandbackHeld(*entry),
			.quiet = state.quiet,
			.handlingSince = entry->handlingSince,
			.lastOutgoing = (entry->handlingSince
				? LastOutgoing(_history)
				: 0),
			.lastAnswer = entry->lastAnswer,
			.lastFailed = entry->lastFailed,
		});
	}
	auto face = (view.kind == BarKind::Hidden)
		? nullptr
		: makeFace(*entry, view);
	const auto signature = face ? this->signature(*face) : QString();
	if (face) {
		_clock.callEach(kClockMs);
	} else {
		_clock.cancel();
	}
	if (signature == _signature) {
		return;
	}
	_signature = signature;
	morphTo(std::move(face));
}

std::unique_ptr<Bar::Face> Bar::makeFace(
		const Entry &entry,
		const ViewOutput &view) const {
	auto result = std::make_unique<Face>();
	result->view = view;
	const auto &state = *entry.state;
	const auto unreachable = view.unreachable
		? u"Not reachable since %1"_q.arg(TimeText(entry.lastAnswer))
		: QString();
	// Zach 10-27: the quick button pauses; Off lives in the chat menu.
	const auto pause = Button{
		.text = u"Pause %1 min"_q.arg(kPauseMinutes),
		.look = ButtonLook::Text,
		.action = Action::Pause,
	};
	switch (view.kind) {
	case BarKind::On:
		result->title = u"Full-auto on"_q;
		result->sub = !unreachable.isEmpty()
			? unreachable
			: view.night
			? (PaceLine(state.pace) + u", quiet for the night"_q)
			: PaceLine(state.pace);
		result->buttons.push_back(pause);
		break;
	case BarKind::Handling:
		result->title = u"Full-auto on"_q;
		result->sub = !unreachable.isEmpty()
			? unreachable
			: entry.handlingName.isEmpty()
			? u"Answering lightly"_q
			: u"Answering %1 lightly"_q.arg(entry.handlingName);
		result->buttons.push_back(pause);
		break;
	case BarKind::Paused:
		// On again by itself at that time; what comes back is the pace.
		result->title = PausedTitle(state.pausedUntil);
		result->sub = !unreachable.isEmpty()
			? unreachable
			: PaceLine(state.pace);
		result->buttons.push_back({
			.text = u"Resume now"_q,
			.look = ButtonLook::Text,
			.action = Action::Resume,
		});
		break;
	case BarKind::Quiet:
		result->title = u"Quiet, nothing sends"_q;
		result->sub = u"The Max login is out. Gists still come in."_q;
		result->buttons.push_back(pause);
		break;
	case BarKind::Handback:
		result->head = u"Handed back to you"_q;
		result->reason.setText(
			st::defaultTextStyle,
			state.handback->reason.isEmpty()
				? u"Full-auto left this one for you."_q
				: state.handback->reason);
		result->buttons.push_back({
			.text = u"Reply yourself"_q,
			.look = ButtonLook::Primary,
			.action = Action::ReplyYourself,
		});
		result->buttons.push_back({
			.text = u"Let full-auto handle it"_q,
			.look = ButtonLook::Ghost,
			.action = Action::LetItHandle,
		});
		break;
	case BarKind::Hidden:
		break;
	}
	layoutFace(*result, cardWidth());
	return result;
}

QString Bar::signature(const Face &face) const {
	return QString::number(int(face.view.kind))
		+ '|' + face.title
		+ '|' + face.sub
		+ '|' + face.head
		+ '|' + face.reason.toString();
}

int Bar::cardWidth() const {
	return std::max(_width - 2 * S(kSide), S(120));
}

void Bar::layoutFace(Face &face, int width) const {
	if (face.view.kind == BarKind::Handback) {
		const auto inner = width - 2 * S(kHandPadX);
		const auto lineHeight = st::defaultTextStyle.font->height;
		face.reasonTop = S(kHandPadY) + HeadFont()->height + S(kReasonGap);
		face.reasonHeight = std::min(
			face.reason.countHeight(inner),
			kReasonLines * lineHeight);
		const auto buttonsTop = face.reasonTop
			+ face.reasonHeight
			+ S(kButtonsGap);
		auto left = S(kHandPadX);
		for (auto &button : face.buttons) {
			const auto primary = (button.look == ButtonLook::Primary);
			const auto pad = S(primary ? kPrimaryPad : kGhostPad);
			const auto textWidth = ButtonFont(primary)->width(button.text);
			const auto w = std::min(textWidth + 2 * pad, width - left - S(kHandPadX));
			button.rect = QRect(left, buttonsTop, std::max(w, 0), S(kButtonHeight));
			left += w + S(kButtonSpacing);
		}
		face.height = buttonsTop + S(kButtonHeight) + S(kHandPadY);
		return;
	}
	face.height = S(kLineHeight);
	auto right = width - S(kPadRight);
	for (auto &button : face.buttons) {
		const auto w = TitleFont()->width(button.text) + 2 * S(kActionPad);
		button.rect = QRect(
			right - w,
			(face.height - S(kActionHeight)) / 2,
			w,
			S(kActionHeight));
		right -= w;
	}
}

void Bar::morphTo(std::unique_ptr<Face> face) {
	const auto from = reservedHeight();
	const auto opening = !_face && face;
	_was = std::move(_face);
	_face = std::move(face);
	_over = _pressed = -1;
	_busy = false;
	setCursor(style::cur_default);
	if (_face && isHidden() && _history) {
		show();
	}
	_heightFrom = from;
	_height.stop();
	_fade.stop();
	if (_instant) {
		_was = nullptr;
	} else {
		_height.start(
			[=] { heightChanged(); },
			0.,
			1.,
			kHeightMs,
			anim::easeOutQuint);
		_fade.start([=] { update(); }, 0., 1., kFadeMs, anim::linear);
		if (opening) {
			_was = nullptr;
		}
	}
	if (!_face && !_height.animating()) {
		hide();
	}
	queueReserve();
}

void Bar::heightChanged() {
	if (!_height.animating() && !_face) {
		_was = nullptr;
		hide();
	}
	if (_reserve) {
		_reserve();
	}
	update();
}

void Bar::queueReserve() {
	if (_reserveQueued) {
		return;
	}
	_reserveQueued = true;
	Ui::PostponeCall(this, [=] {
		_reserveQueued = false;
		if (_reserve) {
			_reserve();
		}
	});
}

int Bar::targetHeight() const {
	return _face ? (S(kGapAbove) + _face->height + S(kGapBelow)) : 0;
}

int Bar::reservedHeight() const {
	if (!_history || (!_face && !_height.animating())) {
		return 0;
	}
	const auto to = targetHeight();
	return _height.animating()
		? anim::interpolate(_heightFrom, to, _height.value(1.))
		: to;
}

void Bar::moveAbove(int composeTop, int width) {
	if (_width != width) {
		_width = width;
		if (_face) {
			layoutFace(*_face, cardWidth());
		}
		if (_was) {
			layoutFace(*_was, cardWidth());
		}
	}
	_composeTop = composeTop;
	const auto height = reservedHeight();
	setGeometry(0, composeTop - height, width, height);
}

QRect Bar::cardRect() const {
	return QRect(
		S(kSide),
		S(kGapAbove),
		width() - 2 * S(kSide),
		height() - S(kGapAbove) - S(kGapBelow));
}

void Bar::paintEvent(QPaintEvent *e) {
	const auto card = cardRect();
	if (card.height() <= 0 || card.width() <= 0) {
		return;
	}
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	auto clip = QPainterPath();
	clip.addRoundedRect(QRectF(card), S(kRadius), S(kRadius));
	p.setClipPath(clip);
	p.fillRect(card, st::historyComposeAreaBg);

	const auto t = _fade.value(1.);
	const auto in = EaseOut(t);
	const auto out = 1. - EaseOut(t * float64(kFadeMs) / kLeaveMs);
	if (_was) {
		paintTint(p, _was->view.kind, card, 1. - in);
	}
	if (_face) {
		paintTint(p, _face->view.kind, card, in);
	}
	if (_was && out > 0.) {
		paintFace(p, *_was, card, out, false);
	}
	if (_face) {
		paintFace(p, *_face, card, in, true);
	}
}

void Bar::paintTint(QPainter &p, BarKind kind, QRect card, float64 opacity) {
	const auto top = (kind == BarKind::Handback)
		? 0.10
		: (kind == BarKind::Quiet)
		? 0.07
		: 0.;
	if (top <= 0. || opacity <= 0.) {
		return;
	}
	const auto bottom = (kind == BarKind::Handback) ? 0.02 : 0.;
	auto gradient = QLinearGradient(card.topLeft(), card.bottomLeft());
	gradient.setColorAt(0., Calm::EmberColor(top * opacity));
	gradient.setColorAt(1., Calm::EmberColor(bottom * opacity));
	p.fillRect(card, gradient);
}

void Bar::paintFace(
		QPainter &p,
		const Face &face,
		QRect card,
		float64 opacity,
		bool interactive) {
	if (opacity <= 0.) {
		return;
	}
	const auto box = QRect(
		card.x(),
		card.y() + card.height() - face.height,
		card.width(),
		face.height);
	p.save();
	p.setOpacity(opacity);
	const auto kind = face.view.kind;
	if (kind == BarKind::Handback) {
		const auto left = box.x() + S(kHandPadX);
		const auto headTop = box.y() + S(kHandPadY);
		const auto headHeight = HeadFont()->height;
		PaintReplyArrow(
			p,
			QRectF(left, headTop + (headHeight - S(kHeadIcon)) / 2., S(kHeadIcon), S(kHeadIcon)),
			S(kHeadIcon),
			Calm::EmberColor());
		p.setFont(HeadFont());
		p.setPen(Calm::EmberColor());
		p.drawText(
			left + S(kHeadIcon) + S(kHeadGap),
			headTop + HeadFont()->ascent,
			face.head);
		p.setPen(st::windowFg);
		face.reason.draw(p, {
			.position = { left, box.y() + face.reasonTop },
			.outerWidth = width(),
			.availableWidth = box.width() - 2 * S(kHandPadX),
			.elisionLines = kReasonLines,
		});
	} else {
		const auto iconRect = QRectF(
			box.x() + S(kPadLeft),
			box.y() + (box.height() - S(kIcon)) / 2.,
			S(kIcon),
			S(kIcon));
		switch (kind) {
		case BarKind::On:
		case BarKind::Handling:
			PaintDisc(p, iconRect, S(kIcon), VioletColor());
			break;
		case BarKind::Quiet:
			PaintRing(p, iconRect, S(kIcon), Calm::EmberColor());
			break;
		case BarKind::Paused: {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, 20));
			p.drawEllipse(iconRect);
			PaintPause(p, iconRect, S(kPause), st::windowFg->c);
		} break;
		case BarKind::Handback:
		case BarKind::Hidden:
			break;
		}
		const auto textLeft = int(iconRect.right()) + S(kTextGap);
		const auto actions = face.buttons.empty()
			? 0
			: (box.width() - face.buttons.back().rect.x());
		const auto textWidth = box.width() - (textLeft - box.x()) - actions;
		const auto blockHeight = TitleFont()->height
			+ S(kSubGap)
			+ SubFont()->height;
		const auto titleTop = box.y() + (box.height() - blockHeight) / 2;
		p.setFont(TitleFont());
		p.setPen((kind == BarKind::Quiet)
			? Calm::EmberColor()
			: st::windowFg->c);
		p.drawText(
			textLeft,
			titleTop + TitleFont()->ascent,
			TitleFont()->elided(face.title, textWidth));
		p.setFont(SubFont());
		p.setPen(st::windowSubTextFg);
		p.drawText(
			textLeft,
			titleTop + TitleFont()->height + S(kSubGap) + SubFont()->ascent,
			SubFont()->elided(face.sub, textWidth));
	}
	for (auto i = 0; i != int(face.buttons.size()); ++i) {
		const auto &button = face.buttons[i];
		paintButton(
			p,
			button,
			button.rect.translated(box.topLeft()),
			interactive && (i == _over) && !_busy);
	}
	p.restore();
}

void Bar::paintButton(
		QPainter &p,
		const Button &button,
		QRect rect,
		bool over) {
	const auto dim = _busy ? 0.5 : 1.;
	auto fg = QColor();
	switch (button.look) {
	case ButtonLook::Primary: {
		auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
		gradient.setColorAt(0., Rgb(kPrimaryFrom, dim));
		gradient.setColorAt(1., Rgb(kPrimaryTo, dim));
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawRoundedRect(rect, S(kButtonRadius), S(kButtonRadius));
		if (over) {
			p.setBrush(QColor(255, 255, 255, 28));
			p.drawRoundedRect(rect, S(kButtonRadius), S(kButtonRadius));
		}
		fg = Rgb(kPrimaryText);
	} break;
	case ButtonLook::Ghost: {
		p.setPen(QPen(Calm::JadeColor(0.16), st::lineWidth));
		p.setBrush(over ? st::windowBgOver->c : QColor(Qt::transparent));
		const auto half = st::lineWidth / 2.;
		p.drawRoundedRect(
			QRectF(rect).adjusted(half, half, -half, -half),
			S(kButtonRadius),
			S(kButtonRadius));
		fg = Rgb(kBarSub, dim);
	} break;
	case ButtonLook::Text:
		if (over) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(rect, S(kActionRadius), S(kActionRadius));
		}
		fg = Rgb(kTick, dim);
		break;
	}
	const auto &font = (button.look == ButtonLook::Text)
		? TitleFont()
		: ButtonFont(button.look == ButtonLook::Primary);
	p.setFont(font);
	p.setPen(fg);
	const auto text = font->elided(button.text, rect.width() - S(8));
	p.drawText(
		rect.x() + (rect.width() - font->width(text)) / 2,
		rect.y() + (rect.height() - font->height) / 2 + font->ascent,
		text);
}

int Bar::buttonAt(QPoint point) const {
	if (!_face || _busy) {
		return -1;
	}
	const auto card = cardRect();
	const auto origin = QPoint(
		card.x(),
		card.y() + card.height() - _face->height);
	for (auto i = 0; i != int(_face->buttons.size()); ++i) {
		if (_face->buttons[i].rect.translated(origin).contains(point)) {
			return i;
		}
	}
	return -1;
}

void Bar::mouseMoveEvent(QMouseEvent *e) {
	const auto over = buttonAt(e->pos());
	if (_over != over) {
		_over = over;
		setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void Bar::mousePressEvent(QMouseEvent *e) {
	_pressed = (e->button() == Qt::LeftButton) ? buttonAt(e->pos()) : -1;
}

void Bar::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0 || pressed != buttonAt(e->pos()) || !_face) {
		return;
	}
	const auto action = _face->buttons[pressed].action;
	// The action can rebuild the face (and this button) right away.
	crl::on_main(this, [=] { activate(action); });
}

void Bar::leaveEventHook(QEvent *e) {
	if (_over >= 0) {
		_over = -1;
		setCursor(style::cur_default);
		update();
	}
}

void Bar::activate(Action action) {
	if (!_history || !_chat || _busy) {
		return;
	}
	const auto peer = _history->peer;
	auto &manager = Manager::Instance();
	if (action == Action::ReplyYourself) {
		const auto entry = manager.entry(_chat);
		const auto msg = (entry && entry->state && entry->state->handback)
			? entry->state->handback->msg
			: int64(0);
		manager.hold(_chat);
		if (_replyYourself) {
			_replyYourself(msg
				? FullMsgId(peer->id, MsgId(msg))
				: FullMsgId());
		}
		return;
	}
	auto change = Change();
	switch (action) {
	case Action::Pause: change.pauseMinutes = kPauseMinutes; break;
	case Action::Resume: change.unpause = true; break;
	case Action::LetItHandle: change.resume = true; break;
	case Action::ReplyYourself:
	case Action::None: return;
	}
	_busy = true;
	_over = -1;
	setCursor(style::cur_default);
	update();
	manager.change(peer, change, crl::guard(this, [=](bool ok) {
		_busy = false;
		update();
		if (!ok) {
			Ui::Toast::Show(
				window(),
				u"Couldn't reach full-auto. Try again in a moment."_q);
		}
	}));
}

} // namespace Safe::Auto
