/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// The small drawn marks of Calm's send queue and reply ideas, after the
// mock's own SVGs (docs/previews/calm/js/next-kit.js, N.icon and
// N.ringLeaf): the countdown ring round the leaf, the failed-send mark, the
// clock, More's arrow and the skeleton shimmer. Drawn, never glyphs.
namespace Safe::Calm {

// The light jade of ticks and text buttons (the mock's --tick): #8ff5c6 on
// a dark surface, a deeper jade on a light one. `surfaceText` is the text
// color of the surface it sits on, which says which kind it is.
[[nodiscard]] QColor TickColor(const QColor &surfaceText);

// The countdown ring wrapped round the leaf, `remaining` running from 1
// (full ring) to 0, clockwise from twelve like the mock's ring keyframes.
void PaintRingLeaf(QPainter &p, QRectF rect, float64 remaining);

// The failed-send mark: an ember disc with a cut-out exclamation.
void PaintBang(QPainter &p, QRectF rect);

// BottomInfo: the mark where the sending clock would be, given the clock's
// anchor (the right edge of the info and the bottom of its first line).
void PaintFailedMark(QPainter &p, QPoint rightLineBottom);

// The held-bubble clock, stroked in `color`.
void PaintClock(QPainter &p, QRectF rect, const QColor &color);

// More's arrow (Material's refresh), filled in `color`. Rendered images are
// cached per size and color.
void PaintRefresh(QPainter &p, QRectF rect, const QColor &color);

// A skeleton bar with the moving shimmer (calm.css .sk): `base` at 10 %
// with a 30 % band sweeping left to right every 1.1 s.
void PaintShimmerBar(QPainter &p, QRectF bar, const QColor &base);

} // namespace Safe::Calm
