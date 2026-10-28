/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_dial.h"

#include "safe/safe_circles_model.h"
#include "ui/painter.h"

#include "styles/style_basic.h"

namespace Safe::Circles {
namespace {

constexpr auto kFront = 1.32; // the front face, Cover Flow's centre cover
constexpr auto kBack = 0.44;
constexpr auto kShade = 0.62;
constexpr auto kNameReach = 1.5; // pills on the chat either side
constexpr auto kRailDots = 2; // dots on the orbit per slot

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] double SlotAngle(int count) {
	const auto slots = std::max(SlotCount(count), 1);
	return 2. * M_PI / slots;
}

class Orbit final : public Dial {
public:
	[[nodiscard]] Layout layout() const override {
		return Layout::Orbit;
	}
	void resize(QSize size) override;
	[[nodiscard]] std::vector<FacePlace> place(
		double position,
		int count) const override;
	void paint(
		Painter &p,
		Model &model,
		const std::vector<not_null<History*>> &chats,
		const DialFrame &frame) override;
	[[nodiscard]] bool grabs(QPointF point) const override;
	[[nodiscard]] double turn(
		QPointF from,
		QPointF to,
		int count) const override;
	[[nodiscard]] bool opens(QPointF point) const override;
	[[nodiscard]] double pixelsPerChat() const override;
	[[nodiscard]] int pillWidth() const override;

private:
	struct Spot {
		QPointF center;
		double depth = 0.; // 1 in front, -1 behind the leaf
		double scale = 1.;
		double near = 1.; // 0 .. 1 from the back to the front
	};
	[[nodiscard]] Spot spot(double angle) const;
	void paintRail(Painter &p, const DialFrame &frame) const;
	void paintCard(Painter &p, Model &model, const DialFrame &frame) const;
	[[nodiscard]] QPointF frontCenter() const;

	QSize _size;
	QPointF _center;
	double _rx = 0.;
	double _ry = 0.;
	double _face = 0.;
	double _leaf = 0.;
	QRect _card;

};

void Orbit::resize(QSize size) {
	_size = size;
	const auto margin = S(16);
	// A side face is about 0.78 of the base size; it must fit the width.
	const auto sideShare = 0.39;
	const auto faceShare = 0.34;
	_rx = (size.width() / 2. - margin) / (1. + sideShare * faceShare);
	_face = std::clamp(_rx * faceShare, double(S(34)), double(S(66)));
	_rx = size.width() / 2. - margin - sideShare * _face;

	const auto frontRadius = _face * kFront / 2.;
	const auto backRadius = _face * kBack / 2.;
	const auto cardHeight = S(64);
	const auto cardGap = S(18);
	const auto available = size.height() - 2 * margin - cardGap - cardHeight;
	_ry = std::clamp(
		(available - frontRadius - backRadius) / 2.,
		_rx * 0.28,
		_rx * 0.52);

	const auto block = backRadius + 2. * _ry + frontRadius + cardGap + cardHeight;
	const auto spare = std::max(size.height() - block, 0.);
	// Even room above the orbit and under the card, a little more between.
	const auto top = spare * 0.38;
	_center = QPointF(size.width() / 2., top + backRadius + _ry);
	_leaf = std::clamp(
		std::min(_ry * 1.05, _rx * 0.5),
		double(S(40)),
		double(S(116)));
	const auto cardWidth = std::min(size.width() - 2 * margin, S(440));
	_card = QRect(
		(size.width() - cardWidth) / 2,
		int(_center.y() + _ry + frontRadius + cardGap + spare * 0.24),
		cardWidth,
		cardHeight);
}

Orbit::Spot Orbit::spot(double angle) const {
	const auto depth = std::sin(angle);
	const auto near = (depth + 1.) / 2.;
	return {
		.center = QPointF(
			_center.x() + _rx * std::cos(angle),
			_center.y() + _ry * depth),
		.depth = depth,
		.scale = kBack + (kFront - kBack) * std::pow(near, 1.35),
		.near = near,
	};
}

QPointF Orbit::frontCenter() const {
	return spot(M_PI / 2.).center;
}

std::vector<FacePlace> Orbit::place(double position, int count) const {
	auto result = std::vector<FacePlace>();
	const auto slot = SlotAngle(count);
	for (const auto &visible : VisibleSlots(position, count)) {
		// The next chats go to the right of the front face.
		const auto angle = M_PI / 2. - visible.offset * slot;
		const auto where = spot(angle);
		const auto focus = FocusAmount(visible.offset);
		result.push_back({
			.index = visible.index,
			.center = where.center,
			.size = _face * where.scale,
			.maxSize = _face * kFront,
			.alpha = visible.alpha,
			.shade = kShade * (1. - std::pow(where.near, 0.8)),
			.depth = where.depth,
			.focus = focus,
			.offset = visible.offset,
			.nameAlpha = NameAlpha(visible.offset, kNameReach),
		});
	}
	ranges::sort(result, ranges::less(), &FacePlace::depth);
	return result;
}

void Orbit::paintRail(Painter &p, const DialFrame &frame) const {
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(LineColor(), st::lineWidth));
	p.drawEllipse(_center, _rx, _ry);
	const auto front = QRectF(
		_center.x() - _rx,
		_center.y() - _ry,
		_rx * 2.,
		_ry * 2.);
	p.setPen(QPen(Mix(LineColor(), GlyphColor(), 0.45), st::lineWidth * 1.4));
	p.drawArc(front, -10 * 16, -160 * 16);

	// Small dots that ride the orbit with the faces, so a spin reads even
	// on a short circle.
	const auto slots = std::max(SlotCount(frame.count), 1);
	const auto slot = 2. * M_PI / slots;
	const auto fraction = frame.position - std::floor(frame.position);
	const auto total = slots * kRailDots;
	const auto glyph = GlyphColor();
	p.setPen(Qt::NoPen);
	for (auto m = 0; m != total; ++m) {
		const auto offset = double(m) / kRailDots - fraction + 0.5 / kRailDots;
		const auto where = spot(M_PI / 2. - offset * slot);
		const auto r = st::lineWidth * (0.9 + 1.1 * where.near);
		p.setBrush(Faded(glyph, 0.18 + 0.4 * where.near));
		p.drawEllipse(where.center, r, r);
	}
}

void Orbit::paintCard(
		Painter &p,
		Model &model,
		const DialFrame &frame) const {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = S(14);
	p.setBrush(SurfaceColor());
	p.setPen(QPen(SurfaceEdgeColor(), st::lineWidth));
	p.drawRoundedRect(QRectF(_card), radius, radius);
	if (frame.focused) {
		PaintRowPreview(p, model, not_null<History*>(frame.focused), _card);
	} else {
		PaintEmpty(p, _card, frame.loading);
	}
}

void Orbit::paint(
		Painter &p,
		Model &model,
		const std::vector<not_null<History*>> &chats,
		const DialFrame &frame) {
	const auto front = frontCenter();
	const auto frontRadius = _face * kFront / 2.;

	// The pool of light the front face stands in.
	{
		auto hq = PainterHighQualityEnabler(p);
		const auto pool = QPointF(front.x(), front.y() + frontRadius * 0.8);
		const auto rx = frontRadius * 1.9;
		const auto ry = frontRadius * 0.55;
		auto gradient = QRadialGradient(QPointF(0., 0.), 1.);
		gradient.setColorAt(0., JadeColor(0.2 + 0.12 * frame.pulse));
		gradient.setColorAt(1., JadeColor(0.));
		p.save();
		p.translate(pool);
		p.scale(rx, ry);
		p.setPen(Qt::NoPen);
		p.setBrush(gradient);
		p.drawEllipse(QPointF(), 1., 1.);
		p.restore();
	}
	paintRail(p, frame);

	const auto faces = place(frame.position, frame.count);
	const auto leafCenter = QPointF(_center.x(), _center.y() - _leaf * 0.12);
	auto leafDrawn = false;
	const auto drawLeaf = [&] {
		PaintLeafAt(p, leafCenter, _leaf, frame.lean, 0.6);
		leafDrawn = true;
	};
	for (const auto &face : faces) {
		if (!leafDrawn && face.depth >= 0.) {
			drawLeaf();
		}
		if (face.index >= int(chats.size())) {
			continue;
		}
		PaintFace(p, model, chats[face.index], face, face.focus);
	}
	if (!leafDrawn) {
		drawLeaf();
	}
	for (const auto &face : faces) {
		if (face.index >= int(chats.size())) {
			continue;
		}
		const auto hovered = (face.index == frame.hovered) && face.focus < 0.5;
		const auto alpha = std::max(face.nameAlpha, hovered ? 1. : 0.)
			* face.alpha;
		if (alpha > 0.01) {
			PaintNamePill(
				p,
				model.shortName(chats[face.index]),
				face.center + QPointF(0., face.size / 2.),
				pillWidth(),
				alpha);
		}
	}
	PaintNotch(
		p,
		QPointF(front.x(), front.y() - frontRadius - S(7)),
		S(13),
		frame.pulse);
	paintCard(p, model, frame);
}

bool Orbit::grabs(QPointF point) const {
	const auto nx = (point.x() - _center.x()) / _rx;
	const auto ny = (point.y() - _center.y()) / _ry;
	const auto rho = std::sqrt(nx * nx + ny * ny);
	const auto frontReach = 1. + (_face * kFront / 2. + S(8)) / _ry;
	return (rho >= 0.3) && (rho <= ((ny > 0.) ? frontReach : 1.5));
}

double Orbit::turn(QPointF from, QPointF to, int count) const {
	const auto angleOf = [&](QPointF point) {
		return std::atan2(
			(point.y() - _center.y()) / _ry,
			(point.x() - _center.x()) / _rx);
	};
	const auto delta = std::remainder(angleOf(to) - angleOf(from), 2. * M_PI);
	return delta / SlotAngle(count);
}

bool Orbit::opens(QPointF point) const {
	return _card.contains(point.toPoint());
}

double Orbit::pixelsPerChat() const {
	return std::max(_rx * SlotAngle(kMaxSlots) * 0.6, double(S(36)));
}

int Orbit::pillWidth() const {
	return int(std::clamp(_rx * 0.5, double(S(56)), double(S(110))));
}

} // namespace

std::unique_ptr<Dial> MakeOrbit() {
	return std::make_unique<Orbit>();
}

} // namespace Safe::Circles
