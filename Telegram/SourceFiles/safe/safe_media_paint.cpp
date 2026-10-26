/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_media_paint.h"

#include "ui/effects/animation_value.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "ui/text/text_custom_emoji.h"

#include <deque>
#include "styles/style_basic.h"
#include "styles/style_chat.h"

namespace Safe {
namespace {

// Plain words, no em dashes (the phone's SafeStrings, M3a / M3b).
const auto kChecking = u"Checking"_q;
const auto kWaiting = u"Waiting for the picture check"_q;
const auto kFlagged = u"Hidden by the picture check"_q;

constexpr auto kWashSide = 6; // A Judge wash keeps colors, never shapes.
constexpr auto kWashCacheLimit = 24;
constexpr auto kFogUserpicSide = 160;

struct FogColors {
	QColor base;
	QColor sheen;
};

[[nodiscard]] FogColors ComputeColors() {
	const auto bg = st::windowBg->c;
	const auto dark = (bg.lightness() < 128);
	// The phone's panel colors, tinted toward the current theme.
	const auto phone = dark ? QColor(0x2B, 0x30, 0x37) : QColor(0xD7, 0xDB, 0xE1);
	const auto base = anim::color(phone, st::windowBgOver->c, 0.3);
	const auto white = QColor(255, 255, 255);
	const auto sheen = anim::color(base, white, dark ? 0.08 : 0.5);
	return { base, sheen };
}

[[nodiscard]] const FogColors &Colors() {
	static auto version = -1;
	static auto cached = FogColors();
	const auto now = style::PaletteVersion();
	if (version != now) {
		version = now;
		cached = ComputeColors();
	}
	return cached;
}

[[nodiscard]] int CornerRadius(Ui::BubbleCornerRounding corner) {
	using Corner = Ui::BubbleCornerRounding;
	switch (corner) {
	case Corner::Small: return st::bubbleRadiusSmall;
	case Corner::Large: return st::bubbleRadiusLarge;
	case Corner::None:
	case Corner::Tail: return 0;
	}
	return 0;
}

void AddCornered(
		QPainterPath &path,
		const QRectF &r,
		qreal tl,
		qreal tr,
		qreal bl,
		qreal br) {
	const auto limit = std::min(r.width(), r.height()) / 2.;
	tl = std::min(tl, limit);
	tr = std::min(tr, limit);
	bl = std::min(bl, limit);
	br = std::min(br, limit);
	path.moveTo(r.left() + tl, r.top());
	path.lineTo(r.right() - tr, r.top());
	if (tr > 0.) {
		path.arcTo(r.right() - 2 * tr, r.top(), 2 * tr, 2 * tr, 90., -90.);
	}
	path.lineTo(r.right(), r.bottom() - br);
	if (br > 0.) {
		path.arcTo(
			r.right() - 2 * br,
			r.bottom() - 2 * br,
			2 * br,
			2 * br,
			0.,
			-90.);
	}
	path.lineTo(r.left() + bl, r.bottom());
	if (bl > 0.) {
		path.arcTo(r.left(), r.bottom() - 2 * bl, 2 * bl, 2 * bl, 270., -90.);
	}
	path.lineTo(r.left(), r.top() + tl);
	if (tl > 0.) {
		path.arcTo(r.left(), r.top(), 2 * tl, 2 * tl, 180., -90.);
	}
	path.closeSubpath();
}

[[nodiscard]] QPainterPath ShapePath(QRect rect, const HoldShape &shape) {
	using Kind = HoldShape::Kind;
	auto result = QPainterPath();
	const auto r = QRectF(rect);
	switch (shape.kind) {
	case Kind::Rect:
		result.addRect(r);
		break;
	case Kind::Ellipse:
		result.addEllipse(r);
		break;
	case Kind::Rounded: {
		const auto radius = qreal(shape.radius);
		AddCornered(result, r, radius, radius, radius, radius);
	} break;
	case Kind::Bubble:
		if (const auto rounding = shape.rounding) {
			AddCornered(
				result,
				r,
				CornerRadius((*rounding)[0]),
				CornerRadius((*rounding)[1]),
				CornerRadius((*rounding)[2]),
				CornerRadius((*rounding)[3]));
		} else {
			const auto radius = qreal(st::roundRadiusSmall);
			AddCornered(result, r, radius, radius, radius, radius);
		}
		break;
	}
	return result;
}

[[nodiscard]] QBrush FogBrush(QRect rect) {
	const auto &colors = Colors();
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	gradient.setStops({
		{ 0., colors.base },
		{ 0.3, colors.base },
		{ 0.5, colors.sheen },
		{ 0.7, colors.base },
		{ 1., colors.base },
	});
	return QBrush(gradient);
}

struct WashEntry {
	uint64 key = 0;
	QSize size;
	int paletteVersion = 0;
	QImage image;
};

[[nodiscard]] std::deque<WashEntry> &WashCache() {
	static auto result = std::deque<WashEntry>();
	return result;
}

[[nodiscard]] QImage Wash(
		uint64 cacheKey,
		QSize size,
		not_null<Image*> thumbnail) {
	auto &cache = WashCache();
	const auto version = style::PaletteVersion();
	for (const auto &entry : cache) {
		if (entry.key == cacheKey
			&& entry.size == size
			&& entry.paletteVersion == version) {
			return entry.image;
		}
	}
	const auto original = thumbnail->original();
	if (original.isNull()) {
		return QImage();
	}
	const auto ratio = style::DevicePixelRatio();
	auto tiny = original.scaled(
		QSize(kWashSide, kWashSide),
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	auto result = tiny.scaled(
		size * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation
	).convertToFormat(QImage::Format_ARGB32_Premultiplied);
	{
		// A frosted layer on top, so a Judge picture reads as the same
		// family as foggy glass.
		auto p = QPainter(&result);
		p.setOpacity(0.45);
		p.fillRect(result.rect(), Colors().base);
	}
	result.setDevicePixelRatio(ratio);
	cache.push_back({ cacheKey, size, version, result });
	while (cache.size() > kWashCacheLimit) {
		cache.pop_front();
	}
	return result;
}

void PaintLabel(QPainter &p, QRect rect, const QString &text) {
	const auto minWidth = style::ConvertScale(72);
	const auto minHeight = style::ConvertScale(40);
	if (rect.width() < minWidth || rect.height() < minHeight) {
		return;
	}
	const auto &font = st::msgDateFont;
	const auto padding = style::ConvertScale(8);
	const auto vertical = style::ConvertScale(3);
	const auto available = rect.width() - 4 * padding;
	if (available <= 0) {
		return;
	}
	const auto elided = font->elided(text, available);
	const auto textWidth = font->width(elided);
	const auto pill = QRect(
		rect.x() + (rect.width() - textWidth - 2 * padding) / 2,
		rect.y() + (rect.height() - font->height - 2 * vertical) / 2,
		textWidth + 2 * padding,
		font->height + 2 * vertical);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::msgDateImgBg);
	const auto radius = pill.height() / 2.;
	p.drawRoundedRect(pill, radius, radius);
	p.setFont(font);
	p.setPen(st::msgDateImgFg);
	p.drawText(
		pill.x() + padding,
		pill.y() + vertical + font->ascent,
		elided);
}

[[nodiscard]] QString LabelText(HeldLook look) {
	switch (look) {
	case HeldLook::Checking: return kChecking;
	case HeldLook::Waiting: return kWaiting;
	case HeldLook::Flagged: return kFlagged;
	case HeldLook::Fog: return QString();
	}
	return QString();
}

class GatedEmoji final : public Ui::Text::CustomEmoji {
public:
	GatedEmoji(
		std::unique_ptr<Ui::Text::CustomEmoji> wrapped,
		Fn<bool()> hidden)
	: _wrapped(std::move(wrapped))
	, _hidden(std::move(hidden)) {
	}

	int width() override {
		return _wrapped->width();
	}
	QString entityData() override {
		return _wrapped->entityData();
	}
	std::optional<Ui::Text::CustomEmojiVerticalMetrics> vertical(
			const style::TextStyle &st) override {
		return _wrapped->vertical(st);
	}
	QString replacementText() override {
		return _wrapped->replacementText();
	}
	EntitiesInText replacementEntities() override {
		return _wrapped->replacementEntities();
	}
	Ui::Text::CustomEmojiSemantics semantics() override {
		return _wrapped->semantics();
	}
	void paint(QPainter &p, const Context &context) override {
		if (_hidden()) {
			const auto side = context.scaled
				? std::min(context.size.width(), context.size.height())
				: Ui::Text::AdjustCustomEmojiSize(st::emojiSize);
			PaintFog(
				p,
				QRect(context.position, QSize(side, side)),
				HoldShape::Rounded(side / 4));
			return;
		}
		_wrapped->paint(p, context);
	}
	void unload() override {
		_wrapped->unload();
	}
	bool ready() override {
		return _hidden() || _wrapped->ready();
	}
	bool readyInDefaultState() override {
		return _hidden() || _wrapped->readyInDefaultState();
	}

private:
	const std::unique_ptr<Ui::Text::CustomEmoji> _wrapped;
	const Fn<bool()> _hidden;

};

} // namespace

HoldShape HoldShape::Rect() {
	return {};
}

HoldShape HoldShape::Rounded(int radius) {
	return { .kind = Kind::Rounded, .radius = radius };
}

HoldShape HoldShape::Small() {
	return Rounded(st::roundRadiusSmall);
}

HoldShape HoldShape::Bubble(std::optional<Ui::BubbleRounding> rounding) {
	return { .kind = Kind::Bubble, .rounding = rounding };
}

HoldShape HoldShape::Ellipse() {
	return { .kind = Kind::Ellipse };
}

void PaintFog(QPainter &p, QRect rect, const HoldShape &shape) {
	if (rect.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(FogBrush(rect));
	p.drawPath(ShapePath(rect, shape));
}

void PaintQuiet(
		QPainter &p,
		QRect rect,
		const HoldShape &shape,
		const QString &word) {
	if (rect.isEmpty()) {
		return;
	}
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Colors().base);
		p.drawPath(ShapePath(rect, shape));
	}
	if (word.isEmpty()) {
		return;
	}
	const auto &font = st::normalFont;
	const auto padding = style::ConvertScale(6);
	if (rect.width() < font->width(word) + 2 * padding
		|| rect.height() < font->height + 2 * padding) {
		return;
	}
	p.setFont(font);
	p.setPen(st::windowSubTextFg);
	p.drawText(rect, Qt::AlignCenter, word);
}

void PaintJudged(
		QPainter &p,
		QRect rect,
		const HoldShape &shape,
		HeldLook look,
		uint64 cacheKey,
		Image *inlineThumbnail) {
	if (rect.isEmpty()) {
		return;
	}
	const auto wash = (look == HeldLook::Fog
		|| look == HeldLook::Flagged
		|| !inlineThumbnail)
		? QImage()
		: Wash(cacheKey, rect.size(), inlineThumbnail);
	if (wash.isNull()) {
		PaintFog(p, rect, shape);
	} else {
		auto hq = PainterHighQualityEnabler(p);
		p.save();
		p.setClipPath(ShapePath(rect, shape), Qt::IntersectClip);
		p.drawImage(rect, wash);
		p.restore();
	}
	if (const auto text = LabelText(look); !text.isEmpty()) {
		PaintLabel(p, rect, text);
	}
}

QImage *FogUserpicImage() {
	static auto version = -1;
	static auto result = QImage();
	const auto now = style::PaletteVersion();
	if (version != now || result.isNull()) {
		version = now;
		result = QImage(
			QSize(kFogUserpicSide, kFogUserpicSide),
			QImage::Format_ARGB32_Premultiplied);
		result.fill(Qt::transparent);
		auto p = QPainter(&result);
		PaintFog(p, result.rect(), HoldShape::Rect());
	}
	return &result;
}

QImage FogPreviewImage(int size, bool ellipse) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(size, size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = QPainter(&result);
	PaintFog(
		p,
		QRect(0, 0, size, size),
		ellipse ? HoldShape::Ellipse() : HoldShape::Small());
	p.end();
	return result;
}

std::unique_ptr<Ui::Text::CustomEmoji> MakeGatedEmoji(
		std::unique_ptr<Ui::Text::CustomEmoji> wrapped,
		Fn<bool()> hidden) {
	return std::make_unique<GatedEmoji>(
		std::move(wrapped),
		std::move(hidden));
}

} // namespace Safe
