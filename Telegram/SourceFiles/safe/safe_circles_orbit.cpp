/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_dial.h"

#include "safe/safe_circles_model.h"
#include "safe/safe_circles_preview.h"
#include "ui/painter.h"
#include "ui/text/text.h"

#include "styles/style_basic.h"
#include "styles/style_chat.h"

namespace Safe::Circles {
namespace {

constexpr auto kFront = 1.08; // the front of the orbit
constexpr auto kBack = 0.44;
// The focused face grows on top of its place, Cover Flow's centre cover: the
// faces either side are about three quarters of it and never touch its ring.
constexpr auto kFocusGrow = 0.24;
constexpr auto kFocused = kFront * (1. + kFocusGrow);
constexpr auto kLeafGlow = 0.3;
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
		const std::vector<double> &corners) const override;
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
	// `badges`: whether the card repeats the unread count of the chat it
	// shows, and of the one fading out under it.
	void paintCard(
		Painter &p,
		Model &model,
		const DialFrame &frame,
		std::pair<bool, bool> badges);
	[[nodiscard]] QPointF frontCenter() const;
	// A neighbour's name under the face row, `clear` from the middle.
	[[nodiscard]] QRectF besidePill(
		const PillSize &pill,
		const FacePlace &face,
		double clear) const;
	[[nodiscard]] double faceClear() const;
	// How near the middle a pill `height` tall may come with its round end
	// tucked under the front face's ring, RingClear() from it, the front
	// face having `corner` (FacePlace::corner: a forum's ring is a rounded
	// square, wider low down than a circle).
	[[nodiscard]] double ringClear(double height, double corner) const;
	[[nodiscard]] int pillWidthClear(double clear) const;
	// A neighbour's name and where it goes: a name cut in the room clear
	// of the front face (its shape `frontCorner`) may take the room under
	// its ring's curve.
	void paintName(
		Painter &p,
		Model &model,
		not_null<History*> history,
		const FacePlace &face,
		double alpha,
		double frontCorner);

	QSize _size;
	QPointF _center;
	double _rx = 0.;
	double _ry = 0.;
	double _face = 0.;
	double _leaf = 0.;
	QRect _card;

	CardPreview _preview;
	LineCache _pills;
	LineCache _widePills;

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

	const auto frontRadius = _face * kFocused / 2.;
	const auto backRadius = _face * kBack / 2.;
	const auto cardHeight = S(64);
	const auto cardGap = S(24); // the neighbours' names sit in it
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

std::vector<FacePlace> Orbit::place(
		double position,
		const std::vector<double> &corners) const {
	auto result = std::vector<FacePlace>();
	const auto count = int(corners.size());
	const auto slot = SlotAngle(count);
	for (const auto &visible : VisibleSlots(position, count)) {
		// The next chats go to the right of the front face.
		const auto angle = M_PI / 2. - visible.offset * slot;
		const auto where = spot(angle);
		const auto focus = FocusAmount(visible.offset);
		result.push_back({
			.index = visible.index,
			.center = where.center,
			.size = _face * where.scale * (1. + kFocusGrow * focus),
			.maxSize = _face * kFocused,
			.alpha = visible.alpha,
			.shade = kShade * (1. - std::pow(where.near, 0.8)),
			.depth = where.depth,
			.focus = focus,
			.offset = visible.offset,
			.nameAlpha = NameAlpha(visible.offset, kNameReach),
			// Away from the leaf, lifted above a name under the face.
			.badgeAngle = OrbitOutward(
				angle,
				_rx,
				_ry,
				Smooth(1. - std::abs(std::abs(visible.offset) - 1.))),
			.corner = corners[visible.index],
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
		const DialFrame &frame,
		std::pair<bool, bool> badges) {
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = S(14);
	p.setBrush(SurfaceColor());
	p.setPen(QPen(SurfaceEdgeColor(), st::lineWidth));
	p.drawRoundedRect(QRectF(_card), radius, radius);
	if (const auto history = frame.previewed) {
		if (const auto old = frame.previewedOld) {
			// The chat shown before fades out under the new one.
			_preview.paint(
				p,
				model,
				not_null<History*>(old),
				_card,
				frame.previewOldAlpha,
				badges.second);
		}
		_preview.paint(
			p,
			model,
			not_null<History*>(history),
			_card,
			frame.previewAlpha,
			badges.first);
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
	const auto frontRadius = _face * kFocused / 2.;

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
	const auto corners = model.faceCorners(chats);
	const auto faces = place(frame.position, corners);
	const auto hover = HoveredFace(faces, frame.hovered, int(chats.size()));
	// The rail never shows in a ring's gap; the faces behind the hovered
	// face give way round its ring (the leaf's soft glow is left whole).
	p.save();
	ClipOut(p, _size, RingDiscs(faces, hover));
	paintRail(p, frame);
	p.restore();
	const auto halo = (hover >= 0) ? HoverHalo(faces[hover]) : QPainterPath();
	const auto behind = [&](bool under, auto &&paint) {
		if (under) {
			p.save();
			ClipOut(p, _size, halo);
			paint();
			p.restore();
		} else {
			paint();
		}
	};
	const auto leafCenter = QPointF(_center.x(), _center.y() - _leaf * 0.12);
	auto leafDrawn = false;
	const auto drawLeaf = [&] {
		PaintLeafAt(p, leafCenter, _leaf, frame.lean, kLeafGlow);
		leafDrawn = true;
	};
	for (auto i = 0; i != int(faces.size()); ++i) {
		const auto &face = faces[i];
		if (!leafDrawn && face.depth >= 0.) {
			drawLeaf();
		}
		if (face.index >= int(chats.size())) {
			continue;
		}
		behind(Behind(faces, i, hover), [&] {
			PaintFace(p, model, chats[face.index], face, face.focus);
		});
		if (i == hover) {
			PaintHoverRing(p, model, chats[face.index], faces, i);
		}
	}
	if (!leafDrawn) {
		drawLeaf();
	}
	// The names show once the dial rests: the focused chat is in front, and
	// its ring's shape is what a name may tuck under.
	const auto frontCorner = frame.focused
		? model.faceCorner(not_null<History*>(frame.focused))
		: kRoundFace;
	for (const auto &face : faces) {
		const auto alpha = face.nameAlpha * face.alpha * frame.names;
		if (face.index >= int(chats.size()) || alpha <= 0.01) {
			continue;
		}
		paintName(p, model, chats[face.index], face, alpha, frontCorner);
	}
	PaintNotch(
		p,
		QPointF(front.x(), front.y() - frontRadius - S(7)),
		S(13),
		frame.pulse);
	// A face wearing its count says it already: the card repeats only a
	// far face's dot (as the Crown's hub, never the count twice).
	const auto cardBadge = [&](History *history) {
		for (const auto &face : faces) {
			if (face.index < int(chats.size())
				&& chats[face.index].get() == history
				&& face.alpha >= 0.5) {
				return !WearsFullBadge(face);
			}
		}
		return true;
	};
	paintCard(
		p,
		model,
		frame,
		{
			!frame.previewFocus && cardBadge(frame.previewed),
			cardBadge(frame.previewedOld),
		});
}

// A neighbour's name under the face row, level with the front face's
// bottom edge (below the neighbours, never on a face), pushed out to the side
// clear of the front face, and kept inside the page.
void Orbit::paintName(
		Painter &p,
		Model &model,
		not_null<History*> history,
		const FacePlace &face,
		double alpha,
		double frontCorner) {
	const auto text = TextWithEntities{ model.shortName(history).toString() };
	const auto narrow = pillWidth();
	const auto &name = _pills.line(
		history,
		text,
		st::chatSimilarName,
		PillTextRoom(narrow),
		PillSlack());
	const auto size = NamePillSize(name, narrow);
	if (size.size.isEmpty()) {
		return;
	}
	if (name.toString() != text.text) {
		const auto clear = ringClear(size.size.height(), frontCorner);
		const auto wide = pillWidthClear(clear);
		if (wide > narrow) {
			const auto &roomy = _widePills.line(
				history,
				text,
				st::chatSimilarName,
				PillTextRoom(wide),
				PillSlack());
			const auto more = NamePillSize(roomy, wide);
			if (!more.size.isEmpty() && more.size.width() > size.size.width()) {
				PaintNamePillIn(p, roomy, besidePill(more, face, clear), alpha);
				return;
			}
		}
	}
	PaintNamePillIn(p, name, besidePill(size, face, faceClear()), alpha);
}

double Orbit::faceClear() const {
	return _face * kFocused / 2. + S(4);
}

double Orbit::ringClear(double height, double corner) const {
	// The pill's centre sits this far under the front face's centre
	// (besidePill), its round end a circle height / 2 across: its centre
	// keeps out of the ring's outline grown by RingClear() and that end.
	const auto end = height / 2.;
	const auto front = _face * kFocused;
	const auto below = front / 2. + height * 0.15;
	const auto grow = FocusRingReach() + RingClear() + end;
	const auto across = RimAcross(
		front / 2. + grow,
		std::min(corner, kRoundFace) * front + grow,
		below);
	return std::clamp(across - end, double(S(4)), faceClear());
}

int Orbit::pillWidthClear(double clear) const {
	const auto room = _size.width() / 2. - clear - PageMargin();
	return int(std::max(room, double(S(56))));
}

QRectF Orbit::besidePill(
		const PillSize &pill,
		const FacePlace &face,
		double clear) const {
	if (pill.size.isEmpty()) {
		return QRectF();
	}
	const auto frontBottom = frontCenter().y() + _face * kFocused / 2.;
	auto rect = QRectF(QPointF(), pill.size);
	rect.moveCenter(QPointF(face.center.x(), frontBottom));
	rect.translate(0., rect.height() * 0.15);
	if (face.center.x() > _center.x()) {
		rect.moveLeft(std::max(rect.left(), _center.x() + clear));
	} else {
		rect.moveRight(std::min(rect.right(), _center.x() - clear));
	}
	const auto margin = pill.margin;
	rect.moveLeft(std::clamp(
		rect.left(),
		margin,
		std::max(_size.width() - margin - rect.width(), margin)));
	return rect;
}

bool Orbit::grabs(QPointF point) const {
	const auto nx = (point.x() - _center.x()) / _rx;
	const auto ny = (point.y() - _center.y()) / _ry;
	const auto rho = std::sqrt(nx * nx + ny * ny);
	const auto frontReach = 1. + (_face * kFocused / 2. + S(8)) / _ry;
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
	// From clear of the front face to the column's margin.
	return pillWidthClear(faceClear());
}

} // namespace

std::unique_ptr<Dial> MakeOrbit() {
	return std::make_unique<Orbit>();
}

} // namespace Safe::Circles
