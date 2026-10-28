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

#include <optional>

namespace Safe::Circles {
namespace {

constexpr auto kTicksPerSlot = 4;
constexpr auto kGrow = 1.24; // the focused face, Apple Watch's wheel size jump
constexpr auto kShade = 0.16;
constexpr auto kNameReach = 1.5; // pills on the chat either side
constexpr auto kHubLeafOpacity = 0.12;
constexpr auto kHubPadding = 5; // the hub's words keep this far from its edge
constexpr auto kHubPaddingLeast = 3; // when 5 leaves the message out
constexpr auto kFocusClear = 9; // a name keeps this far from the focus ring
constexpr auto kTickClear = 3; // and the bezel's ticks this far from a name
// The focus ring and the faces either side, a badge and the bezel's ticks:
// this far apart at least, plus their antialiased edges (two soft ones
// between the ring and a photo, one beside a crisp badge).
constexpr auto kClear = 2;
constexpr auto kRingEdge = 1.;
constexpr auto kBadgeEdge = 0.5;
constexpr auto kMajorTick = 1.6; // line widths
constexpr auto kTickPulse = 0.6; // the tick at twelve grows this much more
constexpr auto kNotchSwell = 0.4; // the notch at a tick (PaintNotch)

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] double SlotAngle(int count) {
	const auto slots = std::max(SlotCount(count), 1);
	return 2. * M_PI / slots;
}

struct Pill {
	QRectF rect;
	double alpha = 0.;
	const Ui::Text::String *name = nullptr;
};

class Crown final : public Dial {
public:
	[[nodiscard]] Layout layout() const override {
		return Layout::Crown;
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
	// `rings`: the ring line under the faces stops at them (RingDiscs);
	// `apart`: the faces' spread (spread), the ticks keep in step with it.
	void paintBezel(
		Painter &p,
		const DialFrame &frame,
		const std::vector<Pill> &pills,
		const QPainterPath &rings,
		double apart) const;
	void paintHub(Painter &p, Model &model, const DialFrame &frame);
	[[nodiscard]] QPointF at(double angle, double radius) const;
	// Radians the faces either side of the focus are moved out by, so the
	// focus ring keeps kClear from them (NeighbourSpreadShaped) in a circle
	// whose faces have `corners`.
	[[nodiscard]] double spread(const std::vector<double> &corners) const;
	// How far from the centre a badge may reach: inside the ticks by kClear.
	[[nodiscard]] double badgeLimit() const;
	void alignPills(std::vector<Pill> &pills) const;
	[[nodiscard]] double nearest(const QRectF &rect) const;
	// Where a neighbour's name goes; `clear` = how far from the middle of
	// the column it keeps (the focused face's, or past the bezel only the
	// notch's, see notchClear).
	[[nodiscard]] QRectF neighbourPill(
		const PillSize &pill,
		const FacePlace &face,
		double clear) const;
	[[nodiscard]] double focusClear() const;
	[[nodiscard]] double notchClear() const;
	[[nodiscard]] bool outsideBezel(const QRectF &rect) const;
	[[nodiscard]] int pillWidthClear(double clear) const;
	[[nodiscard]] Pill neighbourName(
		Model &model,
		not_null<History*> history,
		const FacePlace &face,
		double alpha);

	QSize _size;
	QPointF _center;
	double _outer = 0.; // the bezel's outer edge
	double _tickLength = 0.;
	double _tickRadius = 0.;
	double _face = 0.;
	double _ring = 0.; // where the faces' centers sit
	double _hub = 0.;
	double _notch = 0.;

	HubPreview _preview;
	LineCache _pills;
	LineCache _widePills;

};

void Crown::resize(QSize size) {
	_size = size;
	const auto margin = S(12);
	const auto notchRoom = S(16);
	const auto side = std::min(
		size.width() - 2 * margin,
		size.height() - 2 * margin - notchRoom);
	_outer = std::max(side, S(120)) / 2.;
	_center = QPointF(
		size.width() / 2.,
		(size.height() + notchRoom) / 2.);
	_tickLength = S(9);
	_tickRadius = _outer - _tickLength;
	_face = std::clamp(_outer * 0.27, double(S(34)), double(S(70)));
	_ring = _tickRadius - S(8) - _face * kGrow / 2.;
	// The neighbours' names sit outside the bezel, so the hub can come close.
	_hub = _ring - _face * kGrow / 2. - S(12);
	_notch = S(15);
}

QPointF Crown::at(double angle, double radius) const {
	return _center + QPointF(std::cos(angle), std::sin(angle)) * radius;
}

double Crown::spread(const std::vector<double> &corners) const {
	// A circle with a forum in it: its rounded square (with its ring) reaches
	// further off its sides than a circle, so the clearance is kept for it
	// wherever it comes round (the spread never changes while a dial
	// turns). A circle of round faces is spaced as it always was.
	auto corner = kRoundFace;
	for (const auto value : corners) {
		corner = std::min(corner, value);
	}
	const auto focused = _face * kGrow;
	const auto reach = FocusRingReach();
	return NeighbourSpreadShaped(
		_ring,
		focused / 2. + reach,
		corner * focused + reach,
		_face / 2.,
		corner * _face,
		S(kClear) + kRingEdge,
		SlotCount(int(corners.size())));
}

double Crown::badgeLimit() const {
	const auto cap = st::lineWidth * kMajorTick * (1. + kTickPulse) / 2.;
	return _tickRadius - cap - S(kClear) - kBadgeEdge;
}

double Crown::nearest(const QRectF &rect) const {
	const auto dx = std::max({
		rect.left() - _center.x(),
		0.,
		_center.x() - rect.right(),
	});
	const auto dy = std::max({
		rect.top() - _center.y(),
		0.,
		_center.y() - rect.bottom(),
	});
	return std::hypot(dx, dy);
}

std::vector<FacePlace> Crown::place(
		double position,
		const std::vector<double> &corners) const {
	auto result = std::vector<FacePlace>();
	const auto count = int(corners.size());
	const auto slots = SlotCount(count);
	const auto apart = spread(corners);
	const auto limit = badgeLimit();
	for (const auto &visible : VisibleSlots(position, count)) {
		// Clockwise from twelve.
		const auto turn = SpreadAngle(visible.offset, slots, apart);
		const auto angle = -M_PI / 2. + turn;
		const auto focus = FocusAmount(visible.offset);
		result.push_back({
			.index = visible.index,
			.center = at(angle, _ring),
			.size = _face * (1. + (kGrow - 1.) * focus),
			.maxSize = _face * kGrow,
			.alpha = visible.alpha,
			.shade = kShade * std::min(1., std::abs(visible.offset)),
			.depth = focus,
			.focus = focus,
			.offset = visible.offset,
			.nameAlpha = NameAlpha(visible.offset, kNameReach),
			// Out toward the bezel, away from the faces either side.
			.badgeAngle = BadgeAngle(turn, focus),
			.dialCenter = _center,
			.badgeLimit = limit,
			.corner = corners[visible.index],
		});
	}
	ranges::sort(result, ranges::less(), &FacePlace::depth);
	return result;
}

void Crown::paintBezel(
		Painter &p,
		const DialFrame &frame,
		const std::vector<Pill> &pills,
		const QPainterPath &rings,
		double apart) const {
	auto hq = PainterHighQualityEnabler(p);
	const auto slots = std::max(SlotCount(frame.count), 1);
	const auto total = slots * kTicksPerSlot;
	const auto glyph = GlyphColor();
	const auto major = Faded(glyph, 0.55);
	const auto minor = Faded(glyph, 0.26);
	const auto base = std::floor(frame.position);
	const auto fraction = frame.position - base;
	const auto clear = double(S(kTickClear));
	// A name sitting on the bezel takes the ticks under it away, like the
	// numerals cut into a watch's bezel.
	const auto hidden = [&](QPointF a, QPointF b) {
		auto result = 0.;
		for (const auto &pill : pills) {
			const auto area = pill.rect.adjusted(-clear, -clear, clear, clear);
			for (const auto t : { 0., 0.5, 1. }) {
				if (area.contains(a + (b - a) * t)) {
					result = std::max(result, pill.alpha);
					break;
				}
			}
		}
		return result;
	};
	for (auto m = 0; m != total; ++m) {
		// The ticks keep in step with the faces (their spread too).
		const auto offset = std::remainder(
			double(m) / kTicksPerSlot - fraction,
			double(slots));
		const auto angle = -M_PI / 2. + SpreadAngle(offset, slots, apart);
		const auto isMajor = (m % kTicksPerSlot) == 0;
		const auto length = isMajor ? _tickLength : _tickLength * 0.5;
		auto color = isMajor ? major : minor;
		auto width = st::lineWidth * (isMajor ? kMajorTick : 1.);
		if (isMajor && std::abs(offset) < 1. / (2. * kTicksPerSlot)) {
			color = Mix(color, JadeColor(), 0.35 + 0.65 * frame.pulse);
			width *= 1. + kTickPulse * frame.pulse;
		}
		const auto from = at(angle, _tickRadius + (_tickLength - length));
		const auto till = at(angle, _tickRadius + _tickLength);
		const auto gone = hidden(from, till);
		if (gone >= 0.99) {
			continue;
		}
		p.setPen(QPen(
			Faded(color, 1. - gone),
			width,
			Qt::SolidLine,
			Qt::RoundCap));
		p.drawLine(from, till);
	}
	p.save();
	ClipOut(p, _size, rings);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(LineColor(), st::lineWidth));
	p.drawEllipse(_center, _ring, _ring);
	p.restore();
}

void Crown::paintHub(Painter &p, Model &model, const DialFrame &frame) {
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(SurfaceColor());
	p.setPen(QPen(SurfaceEdgeColor(), st::lineWidth));
	p.drawEllipse(_center, _hub, _hub);

	p.save();
	auto clip = QPainterPath();
	clip.addEllipse(_center, _hub - st::lineWidth, _hub - st::lineWidth);
	p.setClipPath(clip, Qt::IntersectClip);
	if (const auto history = frame.previewed) {
		// The leaf is a thin line mark behind the words, leaning with the
		// spin.
		PaintLeafOutline(
			p,
			_center,
			_hub * 1.3,
			frame.lean,
			JadeColor(kHubLeafOpacity));
		if (const auto old = frame.previewedOld) {
			// The chat shown before fades out under the new one.
			_preview.paint(
				p,
				model,
				not_null<History*>(old),
				_center,
				_hub - S(kHubPadding),
				_hub - S(kHubPaddingLeast),
				frame.previewOldAlpha);
		}
		_preview.paint(
			p,
			model,
			not_null<History*>(history),
			_center,
			_hub - S(kHubPadding),
			_hub - S(kHubPaddingLeast),
			frame.previewAlpha);
	} else {
		PaintLeafAt(
			p,
			QPointF(_center.x(), _center.y() - _hub * 0.3),
			std::clamp(_hub * 0.45, double(S(18)), double(S(26))),
			frame.lean,
			0.45);
		const auto width = int(_hub * 1.46);
		PaintEmpty(
			p,
			QRect(
				int(_center.x()) - width / 2,
				int(_center.y()),
				width,
				int(_hub * 0.6)),
			frame.loading);
	}
	p.restore();
}

// A neighbour's name outside the bezel: out along its face's direction
// until it clears the ticks, inside the column by PageMargin, clear of the
// focused face and the notch at twelve. Where the page is too small for
// that it rests on the bezel and the ticks under it give way.
QRectF Crown::neighbourPill(
		const PillSize &pill,
		const FacePlace &face,
		double clear) const {
	const auto size = pill.size;
	const auto margin = pill.margin;
	const auto outside = _outer + S(kTickClear) + st::lineWidth;
	const auto d = face.center - _center;
	const auto length = std::hypot(d.x(), d.y());
	const auto direction = (length > 0.) ? (d / length) : QPointF(0., -1.);
	const auto right = (direction.x() >= 0.);
	const auto upper = (direction.y() <= 0.);
	auto rect = QRectF(QPointF(), size);
	rect.moveCenter(face.center
		+ direction * (face.size / 2. + S(4) + size.height() / 2.));
	for (auto i = 0; i != 400 && nearest(rect) < outside; ++i) {
		rect.translate(direction);
	}
	if (upper) {
		if (right) {
			rect.moveLeft(std::max(rect.left(), _center.x() + clear));
		} else {
			rect.moveRight(std::min(rect.right(), _center.x() - clear));
		}
	}
	rect.moveLeft(std::clamp(
		rect.left(),
		margin,
		std::max(_size.width() - margin - rect.width(), margin)));
	const auto top = double(S(4));
	const auto bottom = _size.height() - double(S(4));
	while (nearest(rect) < outside) {
		const auto next = rect.translated(0., upper ? -1. : 1.);
		if (next.top() < top || next.bottom() > bottom) {
			break;
		}
		rect = next;
	}
	return rect;
}

// The names of a pair read on one line: above the hub the lower one rises
// to the other (further from the bezel, never nearer), below it the upper
// one drops.
void Crown::alignPills(std::vector<Pill> &pills) const {
	auto upper = std::optional<double>();
	auto lower = std::optional<double>();
	for (const auto &pill : pills) {
		const auto top = pill.rect.top();
		if (pill.rect.center().y() < _center.y()) {
			upper = upper ? std::min(*upper, top) : top;
		} else {
			lower = lower ? std::max(*lower, top) : top;
		}
	}
	for (auto &pill : pills) {
		const auto above = (pill.rect.center().y() < _center.y());
		pill.rect.moveTop(above ? *upper : *lower);
	}
}

void Crown::paint(
		Painter &p,
		Model &model,
		const std::vector<not_null<History*>> &chats,
		const DialFrame &frame) {
	const auto top = at(-M_PI / 2., _ring);
	PaintFocusLight(p, top, _face * kGrow, 1.);

	const auto corners = model.faceCorners(chats);
	const auto faces = place(frame.position, corners);
	auto pills = std::vector<Pill>();
	for (const auto &face : faces) {
		const auto alpha = face.nameAlpha * face.alpha * frame.names;
		if (face.index >= int(chats.size()) || alpha <= 0.01) {
			continue;
		}
		const auto pill = neighbourName(model, chats[face.index], face, alpha);
		if (pill.name) {
			pills.push_back(pill);
		}
	}
	alignPills(pills);
	const auto hover = HoveredFace(faces, frame.hovered, int(chats.size()));
	paintBezel(p, frame, pills, RingDiscs(faces, hover), spread(corners));

	const auto halo = (hover >= 0) ? HoverHalo(faces[hover]) : QPainterPath();
	for (auto i = 0; i != int(faces.size()); ++i) {
		const auto &face = faces[i];
		if (face.index >= int(chats.size())) {
			continue;
		}
		if (Behind(faces, i, hover)) {
			// It gives way round the hover ring.
			p.save();
			ClipOut(p, _size, halo);
			PaintFace(p, model, chats[face.index], face, face.focus);
			p.restore();
		} else {
			PaintFace(p, model, chats[face.index], face, face.focus);
		}
		if (i == hover) {
			PaintHoverRing(p, model, chats[face.index], faces, i);
		}
	}
	for (const auto &pill : pills) {
		PaintNamePillIn(p, *pill.name, pill.rect, pill.alpha);
	}
	paintHub(p, model, frame);
	PaintNotch(
		p,
		QPointF(_center.x(), _center.y() - _outer - S(3)),
		_notch,
		frame.pulse);
}

bool Crown::grabs(QPointF point) const {
	const auto d = point - _center;
	const auto r = std::sqrt(d.x() * d.x() + d.y() * d.y());
	return (r >= S(16)) && (r <= _outer + S(12));
}

double Crown::turn(QPointF from, QPointF to, int count) const {
	const auto a = from - _center;
	const auto b = to - _center;
	const auto minimum = double(S(10));
	if (std::hypot(a.x(), a.y()) < minimum || std::hypot(b.x(), b.y()) < minimum) {
		return 0.;
	}
	const auto delta = std::remainder(
		std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x()),
		2. * M_PI);
	return -delta / SlotAngle(count);
}

bool Crown::opens(QPointF point) const {
	const auto d = point - _center;
	return (d.x() * d.x() + d.y() * d.y()) <= _hub * _hub;
}

double Crown::pixelsPerChat() const {
	return std::max(_ring * SlotAngle(kMaxSlots) * 0.8, double(S(36)));
}

int Crown::pillWidth() const {
	return pillWidthClear(focusClear());
}

int Crown::pillWidthClear(double clear) const {
	// From `clear` of the middle to the column's margin.
	const auto room = _size.width() / 2. - clear - PageMargin();
	return int(std::max(room, double(S(56))));
}

double Crown::focusClear() const {
	return _face * kGrow / 2. + S(kFocusClear);
}

double Crown::notchClear() const {
	return _notch * (1. + kNotchSwell) / 2. + S(kFocusClear);
}

bool Crown::outsideBezel(const QRectF &rect) const {
	return nearest(rect) >= _outer + S(kTickClear) + st::lineWidth - 0.5;
}

// A neighbour's name pill. The pills of the focus's pair sit past the bezel,
// where the only thing between them is the notch: a name cut in the room
// clear of the focused face may take the room up to clear of the notch (the
// width the gap over the focus does not use), when it still sits past the
// bezel there ("🌺 Amber's…", not "🌺 Amb…").
Pill Crown::neighbourName(
		Model &model,
		not_null<History*> history,
		const FacePlace &face,
		double alpha) {
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
		return {};
	}
	if (name.toString() != text.text) {
		const auto wide = pillWidthClear(notchClear());
		const auto &roomy = _widePills.line(
			history,
			text,
			st::chatSimilarName,
			PillTextRoom(wide),
			PillSlack());
		const auto more = NamePillSize(roomy, wide);
		if (!more.size.isEmpty() && more.size.width() > size.size.width()) {
			const auto rect = neighbourPill(more, face, notchClear());
			if (outsideBezel(rect)) {
				return { .rect = rect, .alpha = alpha, .name = &roomy };
			}
		}
	}
	return {
		.rect = neighbourPill(size, face, focusClear()),
		.alpha = alpha,
		.name = &name,
	};
}

} // namespace

std::unique_ptr<Dial> MakeCrown() {
	return std::make_unique<Crown>();
}

} // namespace Safe::Circles
