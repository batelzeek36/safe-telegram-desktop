/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Full-auto's mark (docs/previews/auto/, A.mark; Zach 10-26: the leaf disc,
// the circle violet): a round disc with his leaf cut out of it, so it reads
// as "someone acted", never as Calm's bare leaf. The open ring is full-auto
// present but not acting (quiet).
namespace Safe::Auto {

inline constexpr auto kViolet = 0xAC87F7;

[[nodiscard]] QColor VioletColor(float64 alpha = 1.);

// The disc `size` across, centered in `rect`.
void PaintDisc(QPainter &p, QRectF rect, float64 size, const QColor &color);
void PaintRing(QPainter &p, QRectF rect, float64 size, const QColor &color);

// The two bars of a pause, in a `size` square centered in `rect`.
void PaintPause(QPainter &p, QRectF rect, float64 size, const QColor &color);

// Telegram's reply arrow (the mock's head icon), `size` square.
void PaintReplyArrow(
	QPainter &p,
	QRectF rect,
	float64 size,
	const QColor &color);

} // namespace Safe::Auto
