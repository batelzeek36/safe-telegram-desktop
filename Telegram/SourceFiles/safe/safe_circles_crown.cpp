/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_dial.h"

#include "safe/safe_circles_model.h"
#include "ui/painter.h"
#include "ui/text/text.h"

#include "styles/style_basic.h"

namespace Safe::Circles {
namespace {

constexpr auto kTicksPerSlot = 4;
constexpr auto kGrow = 1.24; // the focused face, Apple Watch's wheel size jump
constexpr auto kShade = 0.16;
constexpr auto kNameReach = 1.5; // pills on the chat either side

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] double SlotAngle(int count) {
	const auto slots = std::max(SlotCount(count), 1);
	return 2. * M_PI / slots;
}

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
	void paintBezel(Painter &p, const DialFrame &frame) const;
	void paintHub(Painter &p, Model &model, const DialFrame &frame) const;
	[[nodiscard]] QPointF at(double angle, double radius) const;
	[[nodiscard]] QRectF outsidePill(
		const Ui::Text::String &name,
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
	// The neighbours' names sit outside the ring, so the hub can come close.
	_hub = _ring - _face * kGrow / 2. - S(12);
	_notch = S(15);
}

QPointF Crown::at(double angle, double radius) const {
	return _center + QPointF(std::cos(angle), std::sin(angle)) * radius;
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
		});
	}
	ranges::sort(result, ranges::less(), &FacePlace::depth);
	return result;
}

void Crown::paintBezel(Painter &p, const DialFrame &frame) const {
	auto hq = PainterHighQualityEnabler(p);
	const auto slots = std::max(SlotCount(frame.count), 1);
	const auto slot = 2. * M_PI / slots;
	const auto total = slots * kTicksPerSlot;
	const auto glyph = GlyphColor();
	const auto major = Faded(glyph, 0.55);
	const auto minor = Faded(glyph, 0.26);
	const auto base = std::floor(frame.position);
	const auto fraction = frame.position - base;
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
		p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
		p.drawLine(
			at(angle, _tickRadius + (_tickLength - length)),
			at(angle, _tickRadius + _tickLength));
	}
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen(LineColor(), st::lineWidth));
	p.drawEllipse(_center, _ring, _ring);
}

void Crown::paintHub(
		Painter &p,
		Model &model,
		const DialFrame &frame) const {
	auto hq = PainterHighQualityEnabler(p);
	p.setBrush(SurfaceColor());
	p.setPen(QPen(SurfaceEdgeColor(), st::lineWidth));
	p.drawEllipse(_center, _hub, _hub);

	const auto leaf = double(S(22));
	const auto leafGap = S(8);
	const auto textRect = QRect(
		int(_center.x() - _hub * 0.78),
		int(_center.y() - _hub * 0.82),
		int(_hub * 1.56),
		int(_hub * 1.64));
	if (!frame.focused) {
		PaintLeafAt(
			p,
			QPointF(_center.x(), _center.y() - leaf),
			leaf,
			frame.lean,
			0.45);
		PaintEmpty(
			p,
			textRect.translated(0, int(leaf)),
			frame.loading);
		return;
	}
	const auto history = not_null<History*>(frame.focused);
	auto clip = QPainterPath();
	clip.addEllipse(_center, _hub - st::lineWidth, _hub - st::lineWidth);
	p.save();
	p.setClipPath(clip);
	const auto text = PaintCenteredPreview(p, model, history, textRect, true);
	const auto total = leaf + leafGap + text;
	const auto top = _center.y() - total / 2.;
	PaintLeafAt(
		p,
		QPointF(_center.x(), top + leaf / 2.),
		leaf,
		frame.lean,
		0.45);
	const auto block = QRect(
		textRect.x(),
		int(top + leaf + leafGap),
		textRect.width(),
		text);
	PaintCenteredPreview(p, model, history, block);
	p.restore();
}

// A neighbour's name just outside the ring, over the bezel, clear of the
// focused face at twelve and inside the page.
QRectF Crown::outsidePill(
		const Ui::Text::String &name,
		const FacePlace &face) const {
	const auto d = face.center - _center;
	const auto length = std::hypot(d.x(), d.y());
	const auto direction = (length > 0.) ? (d / length) : QPointF(0., -1.);
	auto rect = NamePillRect(name, QPointF(), pillWidth());
	if (rect.isEmpty()) {
		return rect;
	}
	const auto reach = face.size / 2.
		+ S(3)
		+ std::abs(direction.x()) * rect.width() / 2.
		+ std::abs(direction.y()) * rect.height() / 2.;
	rect.moveCenter(face.center + direction * reach);
	const auto clear = _face * kGrow / 2. + S(4);
	if (direction.x() > 0.) {
		rect.moveLeft(std::max(rect.left(), _center.x() + clear));
	} else {
		rect.moveRight(std::min(rect.right(), _center.x() - clear));
	}
	const auto margin = double(S(6));
	rect.moveLeft(std::clamp(
		rect.left(),
		margin,
		std::max(_size.width() - margin - rect.width(), margin)));
	rect.moveTop(std::max(rect.top(), margin));
	return rect;
}

void Crown::paint(
		Painter &p,
		Model &model,
		const std::vector<not_null<History*>> &chats,
		const DialFrame &frame) {
	const auto top = at(-M_PI / 2., _ring);
	PaintFocusLight(p, top, _face * kGrow, 1.);
	paintBezel(p, frame);

	const auto faces = place(frame.position, frame.count);
	for (const auto &face : faces) {
		if (face.index >= int(chats.size())) {
			continue;
		}
		PaintFace(p, model, chats[face.index], face, face.focus);
	}
	for (const auto &face : faces) {
		if (face.index >= int(chats.size())) {
			continue;
		}
		const auto &name = model.shortName(chats[face.index]);
		const auto hovered = (face.index == frame.hovered) && face.focus < 0.5;
		if (face.nameAlpha * face.alpha > 0.01) {
			PaintNamePillIn(
				p,
				name,
				outsidePill(name, face),
				face.nameAlpha * face.alpha);
		} else if (hovered && face.alpha > 0.01) {
			PaintNamePill(
				p,
				name,
				face.center + QPointF(0., face.size / 2.),
				pillWidth(),
				face.alpha);
		}
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
	// From clear of the focused face to the page's edge.
	const auto room = _size.width() / 2. - _face * kGrow / 2. - S(4) - S(6);
	return int(std::clamp(room, double(S(56)), double(S(104))));
}

} // namespace

std::unique_ptr<Dial> MakeCrown() {
	return std::make_unique<Crown>();
}

} // namespace Safe::Circles
