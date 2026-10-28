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
constexpr auto kFocusClear = 9; // a name keeps this far from the focus ring
constexpr auto kTickClear = 3; // and the bezel's ticks this far from a name

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
	void paintBezel(
		Painter &p,
		const DialFrame &frame,
		const std::vector<Pill> &pills) const;
	void paintHub(Painter &p, Model &model, const DialFrame &frame);
	[[nodiscard]] QPointF at(double angle, double radius) const;
	void alignPills(std::vector<Pill> &pills) const;
	[[nodiscard]] double nearest(const QRectF &rect) const;
	[[nodiscard]] QRectF neighbourPill(
		const PillSize &pill,
		const FacePlace &face) const;

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

std::vector<FacePlace> Crown::place(double position, int count) const {
	auto result = std::vector<FacePlace>();
	const auto slot = SlotAngle(count);
	for (const auto &visible : VisibleSlots(position, count)) {
		const auto angle = -M_PI / 2. + visible.offset * slot;
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
			.badgeAngle = BadgeAngle(visible.offset * slot, focus),
		});
	}
	ranges::sort(result, ranges::less(), &FacePlace::depth);
	return result;
}

void Crown::paintBezel(
		Painter &p,
		const DialFrame &frame,
		const std::vector<Pill> &pills) const {
	auto hq = PainterHighQualityEnabler(p);
	const auto slots = std::max(SlotCount(frame.count), 1);
	const auto slot = 2. * M_PI / slots;
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
		const auto offset = double(m) / kTicksPerSlot - fraction;
		auto angle = -M_PI / 2. + offset * slot;
		const auto isMajor = (m % kTicksPerSlot) == 0;
		const auto length = isMajor ? _tickLength : _tickLength * 0.5;
		auto color = isMajor ? major : minor;
		auto width = isMajor ? st::lineWidth * 1.6 : st::lineWidth * 1.;
		auto top = std::remainder(offset * slot, 2. * M_PI);
		if (isMajor && std::abs(top) < slot / (2. * kTicksPerSlot)) {
			color = Mix(color, JadeColor(), 0.35 + 0.65 * frame.pulse);
			width *= 1. + 0.6 * frame.pulse;
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
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(LineColor(), st::lineWidth));
	p.drawEllipse(_center, _ring, _ring);
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
				frame.previewOldAlpha);
		}
		_preview.paint(
			p,
			model,
			not_null<History*>(history),
			_center,
			_hub - S(kHubPadding),
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
		const FacePlace &face) const {
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
		const auto clear = _face * kGrow / 2. + S(kFocusClear);
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

	const auto faces = place(frame.position, frame.count);
	auto pills = std::vector<Pill>();
	const auto maxWidth = pillWidth();
	for (const auto &face : faces) {
		const auto alpha = face.nameAlpha * face.alpha * frame.names;
		if (face.index >= int(chats.size()) || alpha <= 0.01) {
			continue;
		}
		const auto history = chats[face.index];
		const auto &name = _pills.line(
			history,
			{ model.shortName(history).toString() },
			st::chatSimilarName,
			PillTextRoom(maxWidth),
			PillSlack());
		const auto size = NamePillSize(name, maxWidth);
		if (size.size.isEmpty()) {
			continue;
		}
		pills.push_back({
			.rect = neighbourPill(size, face),
			.alpha = alpha,
			.name = &name,
		});
	}
	alignPills(pills);
	paintBezel(p, frame, pills);

	for (const auto &face : faces) {
		if (face.index >= int(chats.size())) {
			continue;
		}
		PaintFace(p, model, chats[face.index], face, face.focus);
		if (face.index == frame.hovered && face.focus < 0.5) {
			PaintHoverRing(p, face);
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
	// From clear of the focused face to the column's margin.
	const auto room = _size.width() / 2.
		- _face * kGrow / 2.
		- S(kFocusClear)
		- PageMargin();
	return int(std::max(room, double(S(56))));
}

} // namespace

std::unique_ptr<Dial> MakeCrown() {
	return std::make_unique<Crown>();
}

} // namespace Safe::Circles
