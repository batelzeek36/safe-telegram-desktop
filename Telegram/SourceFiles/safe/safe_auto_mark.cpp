/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_auto_mark.h"

#include "safe/safe_calm_leaf.h"
#include "ui/painter.h"

namespace Safe::Auto {
namespace {

// The mock's 20-unit box: the disc's leaf is 12.6 across, the ring's 10.4
// inside a circle of radius 8.9 drawn 1.7 wide.
constexpr auto kBox = 20.;
constexpr auto kDiscLeaf = 12.6;
constexpr auto kRingLeaf = 10.4;
constexpr auto kRingRadius = 8.9;
constexpr auto kRingStroke = 1.7;
// The leaf path's own canvas (safe_calm_leaf.cpp): 47 units across
// around (54, 54).
constexpr auto kLeafCenter = 54.;
constexpr auto kLeafSpan = 47.;

[[nodiscard]] QPainterPath LeafIn(QPointF center, float64 size) {
	const auto scale = size / kLeafSpan;
	auto transform = QTransform();
	transform.translate(center.x(), center.y());
	transform.scale(scale, scale);
	transform.translate(-kLeafCenter, -kLeafCenter);
	return transform.map(Calm::LeafPath());
}

[[nodiscard]] QPainterPath ReplyArrow() {
	auto result = QPainterPath();
	result.moveTo(10, 9);
	result.lineTo(10, 5);
	result.lineTo(3, 12);
	result.lineTo(10, 19);
	result.lineTo(10, 14.9);
	result.cubicTo(15, 14.9, 18.5, 16.5, 21, 20);
	result.cubicTo(20, 15, 17, 10, 10, 9);
	result.closeSubpath();
	return result;
}

} // namespace

QColor VioletColor(float64 alpha) {
	auto result = QColor::fromRgb(QRgb(kViolet));
	result.setAlphaF(std::clamp(alpha, 0., 1.));
	return result;
}

void PaintDisc(QPainter &p, QRectF rect, float64 size, const QColor &color) {
	const auto center = rect.center();
	const auto unit = size / kBox;
	// Even-odd over the circle and the leaf (its midrib already a hole in
	// it): the leaf is cut out, the midrib stays as part of the disc.
	auto path = QPainterPath();
	path.setFillRule(Qt::OddEvenFill);
	path.addEllipse(center, size / 2., size / 2.);
	path.addPath(LeafIn(center, kDiscLeaf * unit));
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(path);
}

void PaintRing(QPainter &p, QRectF rect, float64 size, const QColor &color) {
	const auto center = rect.center();
	const auto unit = size / kBox;
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(color, kRingStroke * unit));
	p.drawEllipse(center, kRingRadius * unit, kRingRadius * unit);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(LeafIn(center, kRingLeaf * unit));
}

void PaintPause(QPainter &p, QRectF rect, float64 size, const QColor &color) {
	const auto unit = size / 24.;
	const auto left = rect.center().x() - size / 2.;
	const auto top = rect.center().y() - size / 2.;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	const auto bar = [&](float64 x) {
		p.drawRoundedRect(
			QRectF(left + x * unit, top + 5 * unit, 3.6 * unit, 14 * unit),
			0.8 * unit,
			0.8 * unit);
	};
	bar(7.);
	bar(13.4);
}

void PaintReplyArrow(
		QPainter &p,
		QRectF rect,
		float64 size,
		const QColor &color) {
	static const auto path = ReplyArrow();
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	p.translate(rect.center().x() - size / 2., rect.center().y() - size / 2.);
	p.scale(size / 24., size / 24.);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawPath(path);
	p.restore();
}

} // namespace Safe::Auto
