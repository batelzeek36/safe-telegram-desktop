/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_paint.h"

#include "history/history.h"
#include "safe/look_style.h"
#include "safe/safe_calm_leaf.h"
#include "safe/safe_circles_model.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/unread_badge_paint.h"
#include "ui/unread_counter_format.h"

#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"

namespace Safe::Circles {
namespace {

constexpr auto kRingGap = 3.;
constexpr auto kRingWidth = 2.;
constexpr auto kPillPadding = 7;
constexpr auto kBadgeRing = 2;

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] Ui::UnreadBadgeStyle BadgeStyle(
		const Dialogs::BadgesState &state) {
	auto result = Ui::UnreadBadgeStyle();
	result.align = style::al_left;
	result.muted = state.unreadMuted;
	return result;
}

[[nodiscard]] QString BadgeText(const Dialogs::BadgesState &state) {
	return FormatUnreadCounter(
		state.unreadCounter,
		state.mention || state.reaction || state.poll);
}

[[nodiscard]] bool HasBadge(const Dialogs::BadgesState &state) {
	return state.unread || state.mention;
}

// A lone unread mark (marked as unread, no count): a dot.
[[nodiscard]] bool DotOnly(const Dialogs::BadgesState &state) {
	return state.unread && !state.unreadCounter && !state.mention;
}

[[nodiscard]] int LineHeight(const style::TextStyle &st) {
	return st.font->height;
}

} // namespace

QColor Mix(const QColor &a, const QColor &b, double amount) {
	const auto t = std::clamp(amount, 0., 1.);
	return QColor::fromRgbF(
		a.redF() + (b.redF() - a.redF()) * t,
		a.greenF() + (b.greenF() - a.greenF()) * t,
		a.blueF() + (b.blueF() - a.blueF()) * t,
		a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QColor Faded(QColor color, double alpha) {
	color.setAlphaF(color.alphaF() * std::clamp(alpha, 0., 1.));
	return color;
}

QColor BackgroundColor() {
	return st::dialogsBg->c;
}

QColor LineColor() {
	return Look::SeparatorColor();
}

QColor GlyphColor() {
	return Look::GlyphColor();
}

QColor TextColor() {
	return st::dialogsNameFg->c;
}

QColor SubTextColor() {
	return st::dialogsTextFg->c;
}

QColor SurfaceColor() {
	return Mix(st::dialogsBg->c, st::dialogsBgOver->c, 0.75);
}

QColor SurfaceEdgeColor() {
	return Mix(LineColor(), GlyphColor(), 0.3);
}

QColor JadeColor(double alpha) {
	return Calm::JadeColor(alpha);
}

void PaintFace(
		Painter &p,
		Model &model,
		not_null<History*> history,
		const FacePlace &place,
		double ring) {
	if (place.alpha <= 0.01 || place.size < 2.) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto ratio = style::DevicePixelRatio();
	const auto largest = std::max(place.size, place.maxSize);
	const auto pixels = int(std::ceil(largest * ratio / 8.) * 8);
	const auto &image = model.face(history, pixels);
	const auto r = place.size / 2.;
	const auto rect = QRectF(
		place.center.x() - r,
		place.center.y() - r,
		place.size,
		place.size);
	p.setOpacity(place.alpha);
	p.drawImage(rect, image);
	if (place.shade > 0.01) {
		p.setPen(Qt::NoPen);
		p.setBrush(Faded(BackgroundColor(), place.shade));
		p.drawEllipse(rect);
	}
	if (ring > 0.01) {
		const auto outer = r + S(kRingGap) + S(kRingWidth) / 2.;
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(JadeColor(ring), S(kRingWidth)));
		p.drawEllipse(place.center, outer, outer);
	}
	const auto state = model.badges(history);
	if (HasBadge(state)) {
		const auto at = place.center + QPointF(r * 0.72, -r * 0.72);
		PaintBadgeAt(p, state, at, 1. - place.shade * 0.6);
	}
	p.setOpacity(1.);
}

void PaintFocusLight(Painter &p, QPointF center, double radius, double alpha) {
	if (alpha <= 0.01 || radius <= 0.) {
		return;
	}
	auto gradient = QRadialGradient(center, radius);
	gradient.setColorAt(0., JadeColor(0.22 * alpha));
	gradient.setColorAt(0.55, JadeColor(0.08 * alpha));
	gradient.setColorAt(1., JadeColor(0.));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawEllipse(center, radius, radius);
}

void PaintNamePill(
		Painter &p,
		const Ui::Text::String &name,
		QPointF faceBottom,
		int maxWidth,
		double alpha) {
	if (alpha <= 0.01 || name.isEmpty()) {
		return;
	}
	const auto &st = st::chatSimilarName;
	const auto padding = S(kPillPadding);
	const auto textWidth = std::min(name.maxWidth(), maxWidth - 2 * padding);
	if (textWidth <= 0) {
		return;
	}
	const auto height = LineHeight(st) + S(5);
	const auto width = textWidth + 2 * padding;
	const auto rect = QRectF(
		faceBottom.x() - width / 2.,
		faceBottom.y() - height * 0.35,
		width,
		height);
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(alpha);
	p.setPen(QPen(SurfaceEdgeColor(), st::lineWidth));
	p.setBrush(SurfaceColor());
	p.drawRoundedRect(rect, height / 2., height / 2.);
	p.setPen(TextColor());
	name.drawElided(
		p,
		int(std::round(rect.x())) + padding,
		int(std::round(rect.y() + (height - LineHeight(st)) / 2.)),
		textWidth,
		1,
		style::al_center);
	p.setOpacity(1.);
}

QSize BadgeSize(const Dialogs::BadgesState &state) {
	if (!HasBadge(state)) {
		return QSize();
	} else if (DotOnly(state)) {
		const auto d = st::dialogsUnreadHeight * 0.6;
		return QSize(int(d), int(d));
	}
	return Ui::CountUnreadBadgeSize(BadgeText(state), BadgeStyle(state));
}

QSize PaintBadgeAt(
		Painter &p,
		const Dialogs::BadgesState &state,
		QPointF center,
		double alpha) {
	const auto size = BadgeSize(state);
	if (size.isEmpty() || alpha <= 0.01) {
		return size;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto was = p.opacity();
	p.setOpacity(was * alpha);
	const auto ring = S(kBadgeRing);
	const auto outer = QRectF(
		center.x() - size.width() / 2. - ring,
		center.y() - size.height() / 2. - ring,
		size.width() + 2 * ring,
		size.height() + 2 * ring);
	p.setPen(Qt::NoPen);
	p.setBrush(BackgroundColor());
	p.drawRoundedRect(outer, outer.height() / 2., outer.height() / 2.);
	if (DotOnly(state)) {
		p.setBrush(state.unreadMuted
			? st::dialogsUnreadBgMuted
			: st::dialogsUnreadBg);
		p.drawEllipse(center, size.width() / 2., size.height() / 2.);
	} else {
		Ui::PaintUnreadBadge(
			p,
			BadgeText(state),
			int(std::round(center.x() - size.width() / 2.)),
			int(std::round(center.y() - size.height() / 2.)),
			BadgeStyle(state));
	}
	p.setOpacity(was);
	return size;
}

void PaintLeafAt(
		Painter &p,
		QPointF center,
		double size,
		double lean,
		double glow) {
	p.save();
	p.translate(center);
	p.rotate(lean);
	if (glow > 0.01) {
		Calm::PaintLeafGlow(p, QPointF(), size, JadeColor(glow));
	}
	Calm::PaintLeaf(
		p,
		QRectF(-size / 2., -size / 2., size, size),
		size,
		JadeColor());
	p.restore();
}

void PaintNotch(
		Painter &p,
		QPointF tip,
		double width,
		double pulse,
		bool pointsDown) {
	const auto grow = 1. + 0.4 * std::clamp(pulse, 0., 1.);
	const auto w = width * grow;
	const auto h = w * 0.62;
	const auto dir = pointsDown ? -1. : 1.;
	auto path = QPainterPath();
	path.moveTo(tip);
	path.lineTo(tip.x() - w / 2., tip.y() + dir * h);
	path.quadTo(
		QPointF(tip.x(), tip.y() + dir * h * 1.18),
		QPointF(tip.x() + w / 2., tip.y() + dir * h));
	path.closeSubpath();
	auto hq = PainterHighQualityEnabler(p);
	if (pulse > 0.01) {
		auto light = QRadialGradient(tip, w * 1.6);
		light.setColorAt(0., JadeColor(0.45 * pulse));
		light.setColorAt(1., JadeColor(0.));
		p.setPen(Qt::NoPen);
		p.setBrush(light);
		p.drawEllipse(tip, w * 1.6, w * 1.6);
	}
	p.setPen(QPen(JadeColor(), st::lineWidth * 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(JadeColor());
	p.drawPath(path);
}

int PaintCenteredPreview(
		Painter &p,
		Model &model,
		not_null<History*> history,
		QRect rect,
		bool measureOnly) {
	const auto &name = model.name(history);
	const auto &line = model.preview(history);
	const auto width = rect.width();
	const auto nameLine = LineHeight(st::chatUniqueTitle);
	const auto nameHeight = std::min(name.countHeight(width), nameLine * 2);
	const auto textLine = LineHeight(st::defaultTextStyle);
	const auto lineHeight = line.isEmpty()
		? 0
		: std::min(line.countHeight(width), textLine * 2);
	const auto time = model.time(history);
	const auto state = model.badges(history);
	const auto badge = BadgeSize(state);
	const auto timeFont = st::dialogsDateFont;
	const auto metaHeight = std::max(
		time.isEmpty() ? 0 : timeFont->height,
		badge.height());
	const auto gapName = S(4);
	const auto gapMeta = S(8);
	const auto total = nameHeight
		+ (lineHeight ? (gapName + lineHeight) : 0)
		+ (metaHeight ? (gapMeta + metaHeight) : 0);
	if (measureOnly) {
		return total;
	}
	auto y = rect.y() + (rect.height() - total) / 2;
	p.setPen(TextColor());
	name.drawElided(p, rect.x(), y, width, 2, style::al_center);
	y += nameHeight;
	if (lineHeight) {
		y += gapName;
		p.setPen(SubTextColor());
		line.drawElided(p, rect.x(), y, width, 2, style::al_center);
		y += lineHeight;
	}
	if (metaHeight) {
		y += gapMeta;
		const auto timeWidth = time.isEmpty() ? 0 : timeFont->width(time);
		const auto between = (timeWidth && !badge.isEmpty()) ? S(8) : 0;
		const auto full = timeWidth + between + badge.width();
		auto x = rect.x() + (width - full) / 2;
		if (timeWidth) {
			p.setFont(timeFont);
			p.setPen(st::dialogsDateFg);
			p.drawText(
				x,
				y + (metaHeight - timeFont->height) / 2 + timeFont->ascent,
				time);
			x += timeWidth + between;
		}
		if (!badge.isEmpty()) {
			PaintBadgeAt(
				p,
				state,
				QPointF(x + badge.width() / 2., y + metaHeight / 2.),
				1.);
		}
	}
	return total;
}

void PaintRowPreview(
		Painter &p,
		Model &model,
		not_null<History*> history,
		QRect rect) {
	const auto padding = S(14);
	const auto inner = rect.marginsRemoved({ padding, 0, padding, 0 });
	const auto &name = model.name(history);
	const auto &line = model.preview(history);
	const auto time = model.time(history);
	const auto state = model.badges(history);
	const auto badge = BadgeSize(state);
	const auto timeFont = st::dialogsDateFont;
	const auto nameLine = LineHeight(st::chatUniqueTitle);
	const auto textLine = LineHeight(st::defaultTextStyle);
	const auto gap = S(3);
	const auto top = rect.y() + (rect.height() - nameLine - gap - textLine) / 2;

	const auto timeWidth = time.isEmpty() ? 0 : timeFont->width(time);
	if (timeWidth) {
		p.setFont(timeFont);
		p.setPen(st::dialogsDateFg);
		p.drawText(
			inner.x() + inner.width() - timeWidth,
			top + (nameLine - timeFont->height) / 2 + timeFont->ascent,
			time);
	}
	const auto nameWidth = inner.width() - timeWidth - (timeWidth ? S(10) : 0);
	p.setPen(TextColor());
	name.drawElided(p, inner.x(), top, nameWidth, 1);

	const auto lineTop = top + nameLine + gap;
	const auto badgeSkip = badge.isEmpty() ? 0 : (badge.width() + S(10));
	if (!line.isEmpty()) {
		p.setPen(SubTextColor());
		line.drawElided(p, inner.x(), lineTop, inner.width() - badgeSkip, 1);
	}
	if (!badge.isEmpty()) {
		PaintBadgeAt(
			p,
			state,
			QPointF(
				inner.x() + inner.width() - badge.width() / 2.,
				lineTop + textLine / 2.),
			1.);
	}
}

void PaintEmpty(Painter &p, QRect rect, bool loading) {
	const auto text = loading
		? u"Loading chats"_q
		: u"No chats in this folder yet"_q;
	p.setFont(st::normalFont);
	p.setPen(SubTextColor());
	p.drawText(rect, Qt::AlignCenter, text);
}

} // namespace Safe::Circles
