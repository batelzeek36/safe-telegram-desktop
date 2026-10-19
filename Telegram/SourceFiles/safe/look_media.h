/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/message_bubble.h"
#include "ui/rect_part.h"

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace Safe::Look {

// The native macOS look for a picture or video in a message: the media sits
// inset inside the bubble with its own rounded corners, so the bubble color
// frames it on every side. A message without a bubble of its own (a picture
// with no caption) gets the frame painted here, under the media.
struct MediaFrame {
	QRect outer; // The whole media area: the bubble shape and its shadow.
	QRect inner; // Where the picture, the video and their overlays go.
	std::optional<Ui::BubbleRounding> outerRounding;
	std::optional<Ui::BubbleRounding> innerRounding;
	bool framed = false;
};

struct MediaFrameArgs {
	// The message paints its own bubble behind the media (a caption, a name,
	// a reply), so the frame is already there.
	bool bubble = false;
	// No frame: link previews, round videos, stickers, service photos.
	bool skip = false;
};

// Paints the frame when the message has no bubble, then returns where the
// media goes. With args.skip (or no rounding) it changes nothing: inner ==
// outer and innerRounding == rounding.
[[nodiscard]] MediaFrame FrameMedia(
	QPainter &p,
	const Ui::ChatPaintContext &context,
	QRect full,
	std::optional<Ui::BubbleRounding> rounding,
	MediaFrameArgs args);

// The corners of media inset in a bubble: every corner is rounded (a square
// corner, where the media meets a caption or a name, becomes a small one).
[[nodiscard]] Ui::BubbleRounding InnerRounding(Ui::BubbleRounding outer);

// One part of an album: only the sides that touch the bubble edge move in.
[[nodiscard]] QRect InsetGroupedPart(QRect geometry, RectParts sides);

// An album (photos and videos in a grid): true when its parts sit inset
// (then each part goes through InsetGroupedPart). Paints the frame behind
// the whole album when the message has no bubble of its own.
[[nodiscard]] bool FrameAlbum(
	QPainter &p,
	const Ui::ChatPaintContext &context,
	QRect full,
	Ui::BubbleRounding rounding,
	MediaFrameArgs args);

} // namespace Safe::Look
