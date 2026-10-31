/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_marks.h"

#include "safe/safe_calm_leaf.h"
#include "styles/style_chat.h"
#include "ui/painter.h"

#include <QtSvg/QSvgRenderer>

namespace Safe::Calm {
namespace {

// next-kit.js N.icon.more, on a 24-unit canvas.
constexpr auto kRefreshSvg = ""
	"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
	"<path fill=\"#000\" d=\"M17.65 6.35A7.96 7.96 0 0 0 12 4a8 8 0 1 0 "
	"7.73 10h-2.08A6 6 0 1 1 12 6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 "
	"2.35z\"/></svg>";

// thought-ui.js T.cloud and PENCIL, on a 24-unit canvas.
constexpr auto kCloudSvg = ""
	"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
	"<path fill=\"#000\" transform=\"translate(2.6 .6) scale(.8)\" "
	"d=\"M19.35 10.04A7.49 7.49 0 0 0 12 4C9.11 4 6.6 5.64 5.35 8.04A5.994 "
	"5.994 0 0 0 0 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 "
	"0-2.64-2.05-4.78-4.65-4.96z\"/>"
	"<circle fill=\"#000\" cx=\"7.4\" cy=\"19.4\" r=\"1.9\"/>"
	"<circle fill=\"#000\" cx=\"3.7\" cy=\"22.2\" r=\"1.1\"/></svg>";
constexpr auto kPencilSvg = ""
	"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">"
	"<path fill=\"#000\" d=\"M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 "
	"17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39"
	"-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z\"/></svg>";

constexpr auto kShimmerPeriod = crl::time(1100);
constexpr auto kMaxCachedArrows = 8;

[[nodiscard]] float64 Lightness(const QColor &color) {
	return color.toHsl().lightnessF();
}

[[nodiscard]] QImage RenderSvg(
		const char *svgText,
		int pixels,
		const QColor &color) {
	auto result = QImage(
		QSize(pixels, pixels),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		p.setRenderHint(QPainter::Antialiasing);
		auto svg = QSvgRenderer(QByteArray(svgText));
		svg.render(&p, QRectF(0, 0, pixels, pixels));
		p.setCompositionMode(QPainter::CompositionMode_SourceIn);
		p.fillRect(QRect(0, 0, pixels, pixels), color);
	}
	return result;
}

} // namespace

QColor TickColor(const QColor &surfaceText) {
	return (Lightness(surfaceText) > 0.55)
		? QColor(0x8F, 0xF5, 0xC6)
		: QColor(0x2F, 0xAE, 0x78);
}

void PaintRingLeaf(QPainter &p, QRectF rect, float64 remaining) {
	auto hq = PainterHighQualityEnabler(p);
	const auto size = std::min(rect.width(), rect.height());
	const auto stroke = size / 10.;
	const auto radius = size * 0.4;
	const auto center = rect.center();
	const auto ring = QRectF(
		center.x() - radius,
		center.y() - radius,
		2 * radius,
		2 * radius);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(QColor(0x8F, 0xF5, 0xC6, 41), stroke));
	p.drawEllipse(ring);
	const auto left = std::clamp(remaining, 0., 1.);
	if (left > 0.) {
		p.setPen(QPen(JadeColor(), stroke, Qt::SolidLine, Qt::RoundCap));
		p.drawArc(ring, 90 * 16, -int(base::SafeRound(360 * 16 * left)));
	}
	const auto leaf = size / 2.;
	PaintLeaf(
		p,
		QRectF(center.x() - leaf / 2., center.y() - leaf / 2., leaf, leaf),
		leaf,
		JadeColor());
}

void PaintBang(QPainter &p, QRectF rect) {
	auto hq = PainterHighQualityEnabler(p);
	const auto unit = std::min(rect.width(), rect.height()) / 16.;
	const auto origin = rect.center() - QPointF(8 * unit, 8 * unit);
	const auto at = [&](float64 x, float64 y) {
		return origin + QPointF(x * unit, y * unit);
	};
	const auto cut = QColor(0x2A, 0x16, 0x0A);
	p.setPen(Qt::NoPen);
	p.setBrush(EmberColor());
	p.drawEllipse(at(8, 8), 7 * unit, 7 * unit);
	p.setPen(QPen(cut, 2 * unit, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(at(8, 4.2), at(8, 8.8));
	p.setPen(Qt::NoPen);
	p.setBrush(cut);
	p.drawEllipse(at(8, 11.4), unit, unit);
}

void PaintFailedMark(QPainter &p, QPoint rightLineBottom) {
	// Centered where the 11 px sending clock sits (historySendingIcon).
	const auto clock = style::ConvertScale(11);
	const auto center = QPointF(
		rightLineBottom.x() - style::ConvertScale(12) + clock / 2.,
		rightLineBottom.y() - style::ConvertScale(14) + clock / 2.);
	const auto size = style::ConvertScale(14);
	PaintBang(p, QRectF(
		center.x() - size / 2.,
		center.y() - size / 2.,
		size,
		size));
}

void PaintClock(QPainter &p, QRectF rect, const QColor &color) {
	auto hq = PainterHighQualityEnabler(p);
	const auto unit = std::min(rect.width(), rect.height()) / 14.;
	const auto origin = rect.center() - QPointF(7 * unit, 7 * unit);
	const auto at = [&](float64 x, float64 y) {
		return origin + QPointF(x * unit, y * unit);
	};
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(
		color,
		1.4 * unit,
		Qt::SolidLine,
		Qt::RoundCap,
		Qt::RoundJoin));
	p.drawEllipse(at(7, 7), 5.2 * unit, 5.2 * unit);
	const QPointF hand[] = { at(7, 4.3), at(7, 7), at(8.8, 8.2) };
	p.drawPolyline(hand, 3);
}

namespace {

void PaintCachedSvg(
		base::flat_map<std::pair<int, QRgb>, QImage> &cache,
		const char *svgText,
		QPainter &p,
		QRectF rect,
		const QColor &color) {
	const auto ratio = style::DevicePixelRatio();
	const auto pixels = int(std::ceil(
		std::min(rect.width(), rect.height()) * ratio));
	if (pixels <= 0) {
		return;
	}
	const auto key = std::make_pair(pixels, color.rgba());
	auto i = cache.find(key);
	if (i == end(cache)) {
		if (int(cache.size()) >= kMaxCachedArrows) {
			cache.clear();
		}
		auto image = RenderSvg(svgText, pixels, color);
		image.setDevicePixelRatio(ratio);
		i = cache.emplace(key, std::move(image)).first;
	}
	const auto size = pixels / float64(ratio);
	p.drawImage(
		QRectF(
			rect.center().x() - size / 2.,
			rect.center().y() - size / 2.,
			size,
			size),
		i->second);
}

} // namespace

void PaintRefresh(QPainter &p, QRectF rect, const QColor &color) {
	static auto cache = base::flat_map<std::pair<int, QRgb>, QImage>();
	PaintCachedSvg(cache, kRefreshSvg, p, rect, color);
}

void PaintCloud(QPainter &p, QRectF rect, const QColor &color) {
	static auto cache = base::flat_map<std::pair<int, QRgb>, QImage>();
	PaintCachedSvg(cache, kCloudSvg, p, rect, color);
}

void PaintPencil(QPainter &p, QRectF rect, const QColor &color) {
	static auto cache = base::flat_map<std::pair<int, QRgb>, QImage>();
	PaintCachedSvg(cache, kPencilSvg, p, rect, color);
}

void PaintShimmerBar(QPainter &p, QRectF bar, const QColor &base) {
	if (bar.width() <= 0. || bar.height() <= 0.) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto at = [&](float64 alpha) {
		auto result = base;
		result.setAlphaF(alpha);
		return result;
	};
	const auto period = 2. * bar.width();
	const auto phase = (crl::now() % kShimmerPeriod)
		/ float64(kShimmerPeriod);
	const auto start = bar.left() + phase * period;
	auto gradient = QLinearGradient(start, 0., start + period, 0.);
	gradient.setSpread(QGradient::RepeatSpread);
	gradient.setColorAt(0., at(0.10));
	gradient.setColorAt(0.5, at(0.30));
	gradient.setColorAt(1., at(0.10));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	const auto radius = bar.height() / 2.;
	p.drawRoundedRect(bar, radius, radius);
}

} // namespace Safe::Calm
