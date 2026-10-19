/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_icons.h"

#include "ui/style/style_core_scale.h"

#include <QtSvg/QSvgRenderer>

namespace Safe::Look {
namespace {

// Every glyph is drawn on a 28 x 28 grid. A layer either adds its shape or
// cuts it out of what is already drawn (the gap between the two bubbles).
struct Layer {
	const char *svg = nullptr;
	bool cut = false;
};

#define SAFE_LOOK_STROKE "fill=\"none\" stroke=\"#000\" " \
	"stroke-linecap=\"round\" stroke-linejoin=\"round\" "

constexpr auto kContacts = "<path fill-rule=\"evenodd\" d=\""
	"M14 2a12 12 0 1 1 0 24a12 12 0 1 1 0-24Z"
	"M14 6.1a4.35 4.35 0 1 0 0 8.7a4.35 4.35 0 1 0 0-8.7Z"
	"M6.3 21.2C8 18.5 10.8 17.1 14 17.1C17.2 17.1 20 18.5 21.7 21.2"
	"C19.8 23.2 17.1 24.3 14 24.3C10.9 24.3 8.2 23.2 6.3 21.2Z\"/>";

constexpr auto kCalls = "<path d=\""
	"M7.6 3.2h1.6c1.1 0 2 .7 2.3 1.7l1 3.6c.3 1-.1 2-.9 2.6l-1.6 1.2"
	"c1.4 2.9 3.7 5.2 6.6 6.6l1.2-1.6c.6-.8 1.6-1.2 2.6-.9l3.6 1"
	"c1 .3 1.7 1.2 1.7 2.3v1.6c0 2.1-1.8 3.8-3.9 3.7"
	"C11.4 24.4 3.6 16.6 3.9 7.1C4 5 5.6 3.2 7.6 3.2Z\"/>";

constexpr auto kChatsBack = "<path d=\""
	"M17.6 9.6c4.9 0 8.6 3.2 8.6 7.1c0 2-1 3.8-2.5 5.1"
	"c.2 1.3.8 2.4 1.7 3.2c-1.9.1-3.6-.4-4.9-1.5c-.9.3-1.9.4-2.9.4"
	"c-4.9 0-8.6-3.2-8.6-7.1s3.7-7.2 8.6-7.2Z\"/>";

#define SAFE_LOOK_FRONT_BUBBLE \
	"M11.2 3.3c-5 0-9 3.4-9 7.6c0 2.2 1.1 4.2 2.9 5.6" \
	"c-.2 1.4-.9 2.7-1.9 3.6c2.1.1 4-.5 5.4-1.7c.8.2 1.7.3 2.6.3" \
	"c5 0 9-3.4 9-7.7s-4-7.7-9-7.7Z"

constexpr auto kChatsGap = "<path stroke=\"#000\" stroke-width=\"2.4\" "
	"stroke-linejoin=\"round\" d=\"" SAFE_LOOK_FRONT_BUBBLE "\"/>";

constexpr auto kChatsFront = "<path d=\"" SAFE_LOOK_FRONT_BUBBLE "\"/>";

constexpr auto kSettings = "<path fill-rule=\"evenodd\" d=\""
	"M14 3.9a10.1 10.1 0 1 1 0 20.2a10.1 10.1 0 1 1 0-20.2Z"
	"M14 5.9a8.1 8.1 0 1 0 0 16.2a8.1 8.1 0 1 0 0-16.2Z\"/>"
	"<path " SAFE_LOOK_STROKE "stroke-width=\"2.3\" d=\""
	"M14 4L14 2.1M19 5.34L19.95 3.69M22.66 9L24.31 8.05M24 14L25.9 14"
	"M22.66 19L24.31 19.95M19 22.66L19.95 24.31M14 24L14 25.9"
	"M9 22.66L8.05 24.31M5.34 19L3.69 19.95M4 14L2.1 14"
	"M5.34 9L3.69 8.05M9 5.34L8.05 3.69\"/>"
	"<path d=\"M14.37 14.65L20.75 15.22A6.4 6.4 0 0 1 11.68 20.45Z\"/>"
	"<path d=\"M13.25 14L9.57 19.24A6.4 6.4 0 0 1 9.57 8.76Z\"/>"
	"<path d=\"M14.37 13.35L11.68 7.55A6.4 6.4 0 0 1 20.75 12.78Z\"/>";

constexpr auto kCompose = "<path " SAFE_LOOK_STROKE "stroke-width=\"2\" d=\""
	"M14.2 5H8.5A3.5 3.5 0 0 0 5 8.5v11A3.5 3.5 0 0 0 8.5 23h11"
	"a3.5 3.5 0 0 0 3.5-3.5V13.8M12.2 15.8L23.4 4.6\"/>";

constexpr auto kArchive = "<path " SAFE_LOOK_STROKE "stroke-width=\"1.8\" d=\""
	"M5.8 5h16.4a1.8 1.8 0 0 1 1.8 1.8v1.6a1.8 1.8 0 0 1-1.8 1.8H5.8"
	"A1.8 1.8 0 0 1 4 8.4V6.8A1.8 1.8 0 0 1 5.8 5Z"
	"M6 10.2v10.3A2.5 2.5 0 0 0 8.5 23h11a2.5 2.5 0 0 0 2.5-2.5V10.2"
	"M11 14.6h6\"/>";

constexpr auto kSearch = "<path " SAFE_LOOK_STROKE "stroke-width=\"2.2\" d=\""
	"M12 4.5a7.5 7.5 0 1 1 0 15a7.5 7.5 0 1 1 0-15ZM17.5 17.5l6 6\"/>";

#undef SAFE_LOOK_FRONT_BUBBLE
#undef SAFE_LOOK_STROKE

[[nodiscard]] std::vector<Layer> Layers(Glyph glyph) {
	switch (glyph) {
	case Glyph::Contacts: return { { kContacts } };
	case Glyph::Calls: return { { kCalls } };
	case Glyph::Chats: return {
		{ kChatsBack },
		{ kChatsGap, true },
		{ kChatsFront },
	};
	case Glyph::Settings: return { { kSettings } };
	case Glyph::Compose: return { { kCompose } };
	case Glyph::Archive: return { { kArchive } };
	case Glyph::Search: return { { kSearch } };
	}
	return {};
}

[[nodiscard]] QByteArray Document(const char *body) {
	return QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" "
		"width=\"28\" height=\"28\" viewBox=\"0 0 28 28\">")
		+ QByteArray(body)
		+ QByteArray("</svg>");
}

[[nodiscard]] QImage Render(Glyph glyph, int size, QColor color, int ratio) {
	const auto pixels = size * ratio;
	auto result = QImage(
		QSize(pixels, pixels),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		p.setRenderHint(QPainter::Antialiasing);
		for (const auto &layer : Layers(glyph)) {
			auto svg = QSvgRenderer(Document(layer.svg));
			p.setCompositionMode(layer.cut
				? QPainter::CompositionMode_DestinationOut
				: QPainter::CompositionMode_SourceOver);
			svg.render(&p, QRectF(0, 0, pixels, pixels));
		}
		p.setCompositionMode(QPainter::CompositionMode_SourceIn);
		p.fillRect(QRect(0, 0, pixels, pixels), color);
	}
	result.setDevicePixelRatio(ratio);
	return result;
}

struct CacheKey {
	Glyph glyph = Glyph::Contacts;
	int size = 0;
	QRgb color = 0;
	int ratio = 0;

	friend inline auto operator<=>(CacheKey, CacheKey) = default;
};

[[nodiscard]] const QImage &Cached(
		Glyph glyph,
		int size,
		const QColor &color,
		int ratio) {
	static auto cache = base::flat_map<CacheKey, QImage>();
	const auto key = CacheKey{ glyph, size, color.rgba(), ratio };
	const auto i = cache.find(key);
	if (i != end(cache)) {
		return i->second;
	}
	// Theme switches change colors: keep the cache small.
	if (cache.size() > 64) {
		cache.clear();
	}
	return cache.emplace(key, Render(glyph, size, color, ratio)).first->second;
}

} // namespace

void PaintGlyph(
		QPainter &p,
		Glyph glyph,
		QRect rect,
		int size,
		const QColor &color) {
	if (size <= 0) {
		return;
	}
	const auto &image = Cached(
		glyph,
		size,
		color,
		std::max(style::DevicePixelRatio(), 1));
	p.drawImage(
		rect.x() + (rect.width() - size) / 2,
		rect.y() + (rect.height() - size) / 2,
		image);
}

} // namespace Safe::Look
