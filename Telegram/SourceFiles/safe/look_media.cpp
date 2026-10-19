/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_media.h"

#include "safe/look_style.h"
#include "ui/cached_round_corners.h"
#include "ui/chat/chat_style.h"

namespace Safe::Look {
namespace {

using Corner = Ui::BubbleCornerRounding;

[[nodiscard]] Corner InnerCorner(Corner outer) {
	switch (outer) {
	case Corner::Large: return Corner::Large;
	case Corner::Small: return Corner::Small;
	case Corner::None:
	case Corner::Tail: return Corner::Small;
	}
	return Corner::Small;
}

void FillFrame(
		QPainter &p,
		const Ui::ChatPaintContext &context,
		QRect rect,
		Ui::BubbleRounding rounding) {
	const auto stm = context.messageStyle();
	auto corners = Ui::CornersPixmaps();
	for (auto i = 0; i != 4; ++i) {
		switch (rounding[i]) {
		case Corner::Large: corners.p[i] = stm->msgBgCornersLarge.p[i]; break;
		case Corner::Small: corners.p[i] = stm->msgBgCornersSmall.p[i]; break;
		default: break;
		}
	}
	Ui::FillRoundRect(p, rect, stm->msgBg, corners);
}

} // namespace

Ui::BubbleRounding InnerRounding(Ui::BubbleRounding outer) {
	auto result = outer;
	result.topLeft = InnerCorner(outer.topLeft);
	result.topRight = InnerCorner(outer.topRight);
	result.bottomLeft = InnerCorner(outer.bottomLeft);
	result.bottomRight = InnerCorner(outer.bottomRight);
	return result;
}

MediaFrame FrameMedia(
		QPainter &p,
		const Ui::ChatPaintContext &context,
		QRect full,
		std::optional<Ui::BubbleRounding> rounding,
		MediaFrameArgs args) {
	auto result = MediaFrame{
		.outer = full,
		.inner = full,
		.outerRounding = rounding,
		.innerRounding = rounding,
	};
	const auto inset = MediaInset();
	if (args.skip
		|| !rounding
		|| full.width() <= 4 * inset
		|| full.height() <= 4 * inset) {
		return result;
	}
	if (!args.bubble) {
		FillFrame(p, context, full, *rounding);
	}
	result.inner = full.marginsRemoved({ inset, inset, inset, inset });
	result.innerRounding = InnerRounding(*rounding);
	result.framed = true;
	return result;
}

QRect InsetGroupedPart(QRect geometry, RectParts sides) {
	const auto inset = MediaInset();
	return geometry.marginsRemoved({
		(sides & RectPart::Left) ? inset : 0,
		(sides & RectPart::Top) ? inset : 0,
		(sides & RectPart::Right) ? inset : 0,
		(sides & RectPart::Bottom) ? inset : 0,
	});
}

bool FrameAlbum(
		QPainter &p,
		const Ui::ChatPaintContext &context,
		QRect full,
		Ui::BubbleRounding rounding,
		MediaFrameArgs args) {
	const auto inset = MediaInset();
	if (args.skip || full.width() <= 4 * inset || full.height() <= 4 * inset) {
		return false;
	}
	if (!args.bubble) {
		FillFrame(p, context, full, rounding);
	}
	return true;
}

} // namespace Safe::Look
