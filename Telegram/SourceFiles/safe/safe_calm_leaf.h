/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Zach's leaf (his emoji status, the JADELEAF launcher icon, patch
// 22-leaf-logo on the phone): the same path the mocks draw, the midrib cut
// out even-odd. Calm draws it in the composer, its panels and by the time.
namespace Safe::Calm {

inline constexpr auto kJade = 0x34C98E;
inline constexpr auto kEmber = 0xF0A35E;

[[nodiscard]] QColor JadeColor(float64 alpha = 1.);
[[nodiscard]] QColor EmberColor(float64 alpha = 1.);

// The leaf's heat: jade at 0, then gold, orange and red at heats 1, 2, 3
// (0 .. 1 in thirds).
[[nodiscard]] QColor HeatColor(float64 heat, float64 alpha = 1.);

// The leaf filling a `size` x `size` square centered in `rect`.
[[nodiscard]] const QPainterPath &LeafPath();
void PaintLeaf(QPainter &p, QRectF rect, float64 size, const QColor &color);

// A soft glow in the leaf's own shape (the mocks' drop-shadow), `size`
// across like the leaf, centered on `center`. Rendered images are cached.
void PaintLeafGlow(
	QPainter &p,
	QPointF center,
	float64 size,
	const QColor &color);

// The small leaf by a message's time (HistoryView::BottomInfo): the room it
// takes left of the date, and the leaf in it, on the date's line.
[[nodiscard]] int LeafMarkWidth();
void PaintLeafMark(QPainter &p, int left, int top, int lineHeight);

} // namespace Safe::Calm
