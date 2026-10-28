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
// A name a little too long for its pill takes its padding down to this.
constexpr auto kPillPaddingMin = 4;
constexpr auto kBadgeGap = 2; // background between a badge and what it sits on
constexpr auto kHoverGap = 2; // between a face and its hover ring
constexpr auto kClear = 2; // between a ring and a face it comes near
constexpr auto kRingEdge = 1.; // their two soft edges
constexpr auto kPageMargin = 16;
// A face this share of its largest size or more wears the full badge; a
// smaller one (far, behind the leaf) a small solid dot.
constexpr auto kFullBadge = 0.72;

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

[[nodiscard]] QColor BadgeColor(const Dialogs::BadgesState &state) {
	return (state.unreadMuted && !state.mention)
		? st::dialogsUnreadBgMuted->c
		: st::dialogsUnreadBg->c;
}

// The badge's outline (a pill, or a circle for the dot), centered on `at`.
[[nodiscard]] QPainterPath BadgeShape(QPointF at, QSizeF size, double grow) {
	auto result = QPainterPath();
	const auto rect = QRectF(
		at.x() - size.width() / 2. - grow,
		at.y() - size.height() / 2. - grow,
		size.width() + 2. * grow,
		size.height() + 2. * grow);
	result.addRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
	return result;
}

// Where a face wears its badge and how big it is drawn there.
struct BadgeSpot {
	QPointF at;
	QSizeF size; // empty = no badge
	bool full = false; // the count, else a far face's dot
};

[[nodiscard]] BadgeSpot BadgeSpotOf(
		const Dialogs::BadgesState &state,
		const FacePlace &place) {
	if (!HasBadge(state) || BadgeFade(place.alpha) <= 0.01) {
		return {};
	}
	const auto r = place.size / 2.;
	const auto full = WearsFullBadge(place);
	const auto dot = std::clamp(place.size * 0.26, double(S(6)), double(S(9)));
	const auto size = full ? QSizeF(BadgeSize(state)) : QSizeF(dot, dot);
	// On the face's rim, on its outer side (see BadgeAngle), kept inside the
	// bezel's ticks (see BadgeReach).
	const auto reach = BadgeReach(
		place.center.x() - place.dialCenter.x(),
		place.center.y() - place.dialCenter.y(),
		place.badgeAngle,
		r * 1.018,
		size.width(),
		size.height(),
		place.badgeLimit);
	const auto at = place.center + QPointF(
		std::sin(place.badgeAngle),
		-std::cos(place.badgeAngle)) * reach;
	return { at, size, full };
}

} // namespace

int LineHeightOf(const style::TextStyle &st) {
	return std::max(st.font->height, st.lineHeight);
}

int PageMargin() {
	return S(kPageMargin);
}

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
	const auto was = p.opacity();
	p.setOpacity(was * place.alpha);
	p.drawImage(rect, image);
	if (place.shade > 0.01) {
		p.setPen(Qt::NoPen);
		p.setBrush(Faded(BackgroundColor(), place.shade));
		p.drawEllipse(rect);
	}

	// The badge on the shoulder: the full badge on a face near its full
	// size, a small solid dot on a far one, never a see-through disc.
	const auto state = model.badges(history);
	const auto spot = BadgeSpotOf(state, place);
	const auto at = spot.at;
	const auto badge = spot.size;
	const auto full = spot.full;
	const auto dot = badge.width();
	const auto gap = double(S(kBadgeGap));
	if (ring > 0.01) {
		// The jade ring stops a clear gap short of the badge.
		p.save();
		if (!badge.isEmpty()) {
			auto keep = QPainterPath();
			keep.addRect(rect.adjusted(-r, -r, r, r));
			p.setClipPath(
				keep.subtracted(BadgeShape(at, badge, gap)),
				Qt::IntersectClip);
		}
		const auto outer = r + S(kRingGap) + S(kRingWidth) / 2.;
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(JadeColor(ring), S(kRingWidth)));
		p.drawEllipse(place.center, outer, outer);
		p.restore();
	}
	if (!badge.isEmpty()) {
		// The background between the badge and the photo it sits on.
		p.setOpacity(was * place.alpha * BadgeFade(place.alpha));
		p.save();
		auto face = QPainterPath();
		face.addEllipse(rect);
		p.setClipPath(face, Qt::IntersectClip);
		p.setPen(Qt::NoPen);
		p.setBrush(BackgroundColor());
		p.drawPath(BadgeShape(at, badge, gap));
		p.restore();
		if (full) {
			PaintBadgeAt(p, state, at, 1.);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(Mix(BadgeColor(state), BackgroundColor(), place.shade * 0.3));
			p.drawEllipse(at, dot / 2., dot / 2.);
		}
	}
	p.setOpacity(was);
}

double FocusRingReach() {
	return S(kRingGap) + S(kRingWidth);
}

double HoverRingReach() {
	return S(kHoverGap) + st::lineWidth * 1.5;
}

double RingClear() {
	return S(kClear) + kRingEdge;
}

QPainterPath RingDiscs(const std::vector<FacePlace> &faces, int hovered) {
	auto result = QPainterPath();
	for (auto i = 0; i != int(faces.size()); ++i) {
		const auto &face = faces[i];
		if (face.alpha <= 0.01) {
			continue;
		}
		const auto r = face.size / 2.;
		const auto reach = std::max(
			(face.focus > 0.01) ? (FocusRingReach() * face.focus) : 0.,
			(i == hovered) ? HoverRingReach() : 0.);
		if (reach > 0.) {
			result.addEllipse(face.center, r + reach, r + reach);
		}
	}
	return result;
}

QPainterPath HoverHalo(const FacePlace &place) {
	const auto r = place.size / 2. + HoverRingReach() + RingClear();
	auto result = QPainterPath();
	result.addEllipse(place.center, r, r);
	return result;
}

void ClipOut(Painter &p, QSizeF size, const QPainterPath &cut) {
	if (cut.isEmpty()) {
		return;
	}
	const auto margin = std::max(size.width(), size.height());
	auto keep = QPainterPath();
	keep.addRect(QRectF(QPointF(), size).adjusted(-margin, -margin, margin, margin));
	p.setClipPath(keep.subtracted(cut), Qt::IntersectClip);
}

bool WearsFullBadge(const FacePlace &place) {
	const auto largest = std::max(place.size, place.maxSize);
	return (place.size / std::max(largest, 1.)) >= kFullBadge;
}

void PaintHoverRing(
		Painter &p,
		Model &model,
		not_null<History*> history,
		const std::vector<FacePlace> &faces,
		int self) {
	if (self < 0 || self >= int(faces.size())) {
		return;
	}
	const auto &place = faces[self];
	if (place.alpha <= 0.01) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto outer = place.size / 2. + S(kHoverGap) + st::lineWidth * 0.75;
	const auto edge = place.size / 2. + HoverRingReach();
	p.save();
	auto cut = QPainterPath();
	const auto spot = BadgeSpotOf(model.badges(history), place);
	if (!spot.size.isEmpty()) {
		cut.addPath(BadgeShape(spot.at, spot.size, S(kBadgeGap)));
	}
	for (auto i = 0; i != int(faces.size()); ++i) {
		const auto &front = faces[i];
		if (front.alpha <= 0.01 || front.depth <= place.depth) {
			continue;
		}
		const auto r = front.size / 2.
			+ ((front.focus > 0.01) ? FocusRingReach() : 0.)
			+ RingClear();
		const auto d = front.center - place.center;
		if (std::hypot(d.x(), d.y()) < r + edge) {
			cut.addEllipse(front.center, r, r);
		}
	}
	if (!cut.isEmpty()) {
		auto keep = QPainterPath();
		keep.addEllipse(place.center, outer * 2., outer * 2.);
		p.setClipPath(keep.subtracted(cut), Qt::IntersectClip);
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(
		Faded(TextColor(), 0.55 * place.alpha),
		st::lineWidth * 1.5));
	p.drawEllipse(place.center, outer, outer);
	p.restore();
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

int PillTextRoom(int maxWidth) {
	return maxWidth - 2 * S(kPillPadding);
}

int PillSlack() {
	return st::chatSimilarName.font->width(QString(QChar(0x2026))) + S(2);
}

PillSize NamePillSize(const Ui::Text::String &name, int maxWidth) {
	const auto padding = S(kPillPadding);
	const auto least = S(kPillPaddingMin);
	const auto room = PillTextRoom(maxWidth);
	if (name.isEmpty() || room <= 0) {
		return {};
	}
	const auto height = LineHeightOf(st::chatSimilarName) + S(5);
	const auto over = name.maxWidth() - room;
	if (over <= 0 || over > PillSlack()) {
		// Fits, or was not let run over (it elides in its pill).
		const auto width = std::min(name.maxWidth(), room) + 2 * padding;
		return { QSizeF(width, height), double(PageMargin()) };
	}
	// A name a little too long keeps whole: the pill's padding gives the
	// room first, then the page's margin.
	const auto fromPadding = std::min(over, 2 * (padding - least));
	const auto fromMargin = over - fromPadding;
	return {
		QSizeF(maxWidth + fromMargin, height),
		double(PageMargin() - fromMargin),
	};
}

void PaintNamePillIn(
		Painter &p,
		const Ui::Text::String &name,
		QRectF rect,
		double alpha) {
	if (alpha <= 0.01 || rect.isEmpty()) {
		return;
	}
	const auto &st = st::chatSimilarName;
	// Centered in the pill; it elides only past the least padding.
	const auto padding = S(kPillPaddingMin);
	const auto height = rect.height();
	auto hq = PainterHighQualityEnabler(p);
	p.setOpacity(alpha);
	p.setPen(QPen(SurfaceEdgeColor(), st::lineWidth));
	p.setBrush(SurfaceColor());
	p.drawRoundedRect(rect, height / 2., height / 2.);
	p.setPen(TextColor());
	name.drawElided(
		p,
		int(std::round(rect.x())) + padding,
		int(std::round(rect.y() + (height - LineHeightOf(st)) / 2.)),
		int(std::round(rect.width())) - 2 * padding,
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
	if (DotOnly(state)) {
		p.setPen(Qt::NoPen);
		p.setBrush(BadgeColor(state));
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

void PaintLeafOutline(
		Painter &p,
		QPointF center,
		double size,
		double lean,
		const QColor &color) {
	const auto &path = Calm::LeafPath();
	const auto box = path.boundingRect();
	const auto span = std::max(box.width(), box.height());
	if (span <= 0.) {
		return;
	}
	const auto scale = size / span;
	auto transform = QTransform();
	transform.translate(center.x(), center.y());
	transform.rotate(lean);
	transform.scale(scale, scale);
	transform.translate(-box.center().x(), -box.center().y());
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(color, st::lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.drawPath(transform.map(path));
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

void PaintEmpty(Painter &p, QRect rect, bool loading) {
	const auto text = loading
		? u"Loading chats"_q
		: u"No chats in this folder yet"_q;
	p.setFont(st::normalFont);
	p.setPen(SubTextColor());
	p.drawText(rect, Qt::AlignCenter, text);
}

} // namespace Safe::Circles
