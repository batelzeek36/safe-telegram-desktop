/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/message_bubble.h"

class Image;

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Safe {

// The outline a stand-in takes, so it sits where the picture would.
struct HoldShape {
	enum class Kind : uchar {
		Rect,
		Rounded,
		Bubble,
		Ellipse,
	};
	Kind kind = Kind::Rect;
	int radius = 0; // Kind::Rounded.
	std::optional<Ui::BubbleRounding> rounding; // Kind::Bubble.

	[[nodiscard]] static HoldShape Rect();
	[[nodiscard]] static HoldShape Rounded(int radius);
	[[nodiscard]] static HoldShape Small(); // st::roundRadiusSmall.
	// No rounding = media inside a link preview (small corners).
	[[nodiscard]] static HoldShape Bubble(
		std::optional<Ui::BubbleRounding> rounding);
	[[nodiscard]] static HoldShape Ellipse();
};

// What a held picture is drawn as.
enum class HeldLook : uchar {
	Fog, // Hide: foggy glass, no label.
	Checking, // Judge, the check has it.
	Waiting, // Judge, no key or nothing to send yet.
	Flagged, // Judge, the check said no.
};

// Foggy glass: a soft theme-colored frosted panel with one diagonal sheen.
// Not one pixel of the picture goes into it.
void PaintFog(QPainter &p, QRect rect, const HoldShape &shape);

// A quiet chat's held picture (safe_quiet_media.h) where it is painted in
// place, such as a shared media tile or a file's thumbnail: a flat panel,
// with the kind word ("photo", "video") when it fits. Never the picture.
void PaintQuiet(
	QPainter &p,
	QRect rect,
	const HoldShape &shape,
	const QString &word);

// A Judge picture before it is cleared: the tiny inline thumbnail washed out
// to a few colors (fog when there is none, and always fog once flagged),
// plus a small label when the space allows.
void PaintJudged(
	QPainter &p,
	QRect rect,
	const HoldShape &shape,
	HeldLook look,
	uint64 cacheKey,
	Image *inlineThumbnail);

// A square of foggy glass the userpic code rounds and scales itself.
[[nodiscard]] QImage *FogUserpicImage();

// A small foggy-glass square for chat list and reply previews.
[[nodiscard]] QImage FogPreviewImage(int size, bool ellipse);

// Custom emoji that decides on each paint whether it shows.
[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> MakeGatedEmoji(
	std::unique_ptr<Ui::Text::CustomEmoji> wrapped,
	Fn<bool()> hidden);

} // namespace Safe
