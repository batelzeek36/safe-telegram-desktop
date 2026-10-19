/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_chat.h"

#include "safe/look_style.h"
#include "safe/look_tint.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_scale.h"

#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"
#include "styles/style_userpic_button.h"
#include "styles/style_widgets.h"

namespace Safe::Look {
namespace {

// Measured on the native app (night theme, Zach's screenshots): the top
// pills and circles are 40pt tall, 8pt apart, the photo inside the peer
// pill is 32pt with 4pt around it; the composer circles are 40pt with 6pt
// to the field pill; text starts 14pt inside the pill's rounded end.
constexpr auto kPillHeight = 40;
constexpr auto kPillGap = 8;
constexpr auto kPhotoSize = 32;
constexpr auto kPhotoLeft = 6;
constexpr auto kPhotoInset = 4;
constexpr auto kInfoWidth = 44;
constexpr auto kRightInset = 8;
constexpr auto kSideInset = 8;
constexpr auto kComposeGap = 6;
constexpr auto kFieldTextLeft = 14;
constexpr auto kCardSide = 6;
constexpr auto kCardVertical = 3;
constexpr auto kCardRadius = 14;
constexpr auto kPinnedBelow = 6;

[[nodiscard]] int Scaled(int value) {
	return style::ConvertScale(value);
}

void FillPill(QPainter &p, const QRectF &rect, const QColor &color) {
	const auto radius = rect.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(rect, radius, radius);
}

void FillCircle(QPainter &p, QPointF center, int diameter, const QColor &c) {
	const auto r = diameter / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(c);
	p.drawEllipse(center, r, r);
}

[[nodiscard]] bool Shown(QWidget *widget) {
	return widget && !widget->isHidden();
}

} // namespace

bool FloatingChatBars() {
	return true;
}

const style::UserpicButton &TopBarInfoStyle(const style::UserpicButton &st) {
	if (!FloatingChatBars() || &st != &st::topBarInfoButton) {
		return st;
	}
	static const auto result = [] {
		auto result = st::topBarInfoButton;
		result.size = QSize(Scaled(kInfoWidth), st::topBarHeight);
		result.photoSize = Scaled(kPhotoSize);
		result.photoPosition = QPoint(Scaled(kPhotoLeft), -1);
		return result;
	}();
	return result;
}

int TopBarRightInset() {
	return FloatingChatBars() ? Scaled(kRightInset) : 0;
}

int TopShadowWidth(int width) {
	return FloatingChatBars() ? 0 : width;
}

QRect BarShadowGeometry(QRect geometry) {
	return FloatingChatBars() ? QRect() : geometry;
}

bool PaintTopBarPills(
		QPainter &p,
		not_null<QWidget*> bar,
		bool floating,
		QWidget *back,
		QWidget *info,
		int rightTaken) {
	if (!FloatingChatBars() || !floating) {
		return false;
	}
	const auto color = st::topBarBg->c;
	const auto height = Scaled(kPillHeight);
	const auto gap = Scaled(kPillGap);
	const auto center = st::topBarHeight / 2;
	const auto top = center - height / 2;
	auto hq = PainterHighQualityEnabler(p);

	// The action buttons: visible children in the normal row, right of
	// _rightTaken (selection buttons slide in from above, out of the row).
	auto actions = QRect();
	const auto rightZone = bar->width() - rightTaken - st::lineWidth;
	for (const auto child : bar->children()) {
		const auto widget = qobject_cast<QWidget*>(child);
		if (!Shown(widget) || widget == back || widget == info) {
			continue;
		}
		const auto geometry = widget->geometry();
		if (geometry.x() >= rightZone
			&& geometry.y() >= 0
			&& geometry.y() < center
			&& geometry.height() >= height / 2) {
			actions = actions.united(geometry);
		}
	}
	auto peerRight = bar->width() - Scaled(kSideInset);
	if (!actions.isEmpty()) {
		const auto pill = QRect(
			actions.x(),
			top,
			actions.width(),
			height);
		FillPill(p, pill, color);
		peerRight = pill.x() - gap;
	}

	auto peerLeft = Scaled(kSideInset);
	if (Shown(back)) {
		const auto circle = back->geometry().center();
		FillCircle(p, QPointF(circle.x() + 1, center), height, color);
		peerLeft = circle.x() + 1 + height / 2 + gap;
	}
	if (Shown(info)) {
		const auto photoLeft = info->x() + Scaled(kPhotoLeft);
		peerLeft = std::max(peerLeft, photoLeft - Scaled(kPhotoInset));
	}
	if (peerRight > peerLeft + height) {
		FillPill(
			p,
			QRect(peerLeft, top, peerRight - peerLeft, height),
			color);
	}
	return true;
}

void PaintPinnedBack(not_null<QWidget*> wrap, QRect clip) {
	auto p = QPainter(wrap);
	if (!FloatingChatBars()) {
		p.fillRect(clip, st::historyPinnedBg);
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto side = Scaled(kSideInset);
	const auto pill = QRect(
		side,
		0,
		wrap->width() - 2 * side,
		wrap->height() - Scaled(kPinnedBelow));
	const auto radius = std::min(pill.height() / 2., Scaled(20) * 1.);
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyPinnedBg);
	p.drawRoundedRect(pill, radius, radius);
}

void PlacePinnedBar(not_null<Ui::RpWidget*> bar, QWidget *right, int width) {
	const auto side = FloatingChatBars() ? Scaled(kSideInset) : 0;
	bar->resizeToWidth(width - 2 * side);
	bar->move(side, 0);
	if (right) {
		right->move(width - side - right->width(), 0);
	}
}

const style::InputField &ComposeFieldStyle() {
	if (!FloatingChatBars()) {
		return st::historyComposeField;
	}
	static const auto result = [] {
		auto result = st::historyComposeField;
		result.textBg = st::transparent;
		// The native placeholder grey (lighter than Telegram Desktop's).
		result.placeholderFg = PlaceholderFg();
		result.placeholderFgActive = PlaceholderFg();
		// The field starts at the attach button's right edge; the pill
		// starts a gap after the attach circle (centered in that button).
		const auto pillLeft = st::historyAttach.width / 2
			+ 1
			+ Scaled(kPillHeight) / 2
			+ Scaled(kComposeGap)
			- st::historyAttach.width;
		result.textMargins = result.textMargins + QMargins(
			pillLeft + Scaled(kFieldTextLeft),
			0,
			0,
			0);
		return result;
	}();
	return result;
}

const style::EmojiButton &EmojiToggleStyle() {
	if (!FloatingChatBars()) {
		return st::historyAttachEmoji;
	}
	static const auto result = [] {
		auto result = st::historyAttachEmoji;
		result.bg = st::transparent;
		// White face and circle on a dark theme, like the native app.
		result.lineFg = PillColor(st::historyAttachEmoji.lineFg);
		result.lineFgOver = PillColor(st::historyAttachEmoji.lineFgOver);
		result.inner.icon = PillIcon(st::historyAttachEmoji.inner.icon);
		result.inner.iconOver = PillIcon(
			st::historyAttachEmoji.inner.iconOver);
		return result;
	}();
	return result;
}

void PaintComposeBack(
		QPainter &p,
		QRect band,
		int replyHeight,
		QWidget *attach,
		QWidget *field,
		QWidget *send) {
	if (!FloatingChatBars() || !field) {
		p.fillRect(band, st::historyReplyBg);
		return;
	}
	const auto color = st::historyComposeAreaBg->c;
	const auto height = Scaled(kPillHeight);
	const auto gap = Scaled(kComposeGap);
	auto hq = PainterHighQualityEnabler(p);

	if (replyHeight > 0) {
		const auto side = Scaled(kCardSide);
		const auto vertical = Scaled(kCardVertical);
		const auto card = QRect(
			band.x() + side,
			band.y() + vertical,
			band.width() - 2 * side,
			replyHeight - 2 * vertical);
		const auto radius = Scaled(kCardRadius) * 1.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::historyReplyBg);
		p.drawRoundedRect(card, radius, radius);
	}

	// Buttons are as tall as the single line band; the circles and the
	// pill share their center line, the pill grows up with the field.
	const auto anchor = Shown(send) ? send : (Shown(attach) ? attach : field);
	const auto middle = anchor->geometry().center().y() + 1;
	const auto bottom = middle + height / 2;
	auto left = band.x() + Scaled(kSideInset);
	auto right = band.x() + band.width() - Scaled(kSideInset);
	if (Shown(attach)) {
		const auto x = attach->geometry().center().x() + 1;
		FillCircle(p, QPointF(x, middle), height, color);
		left = x + height / 2 + gap;
	}
	if (Shown(send)) {
		const auto x = send->geometry().center().x() + 1;
		FillCircle(p, QPointF(x, middle), height, color);
		right = x - height / 2 - gap;
	}
	const auto top = std::min(middle - height / 2, field->y());
	if (right > left + height) {
		const auto pill = QRect(left, top, right - left, bottom - top);
		const auto radius = height / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawRoundedRect(pill, radius, radius);
	}
}

} // namespace Safe::Look
