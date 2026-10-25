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

// Jade at 0, ember at 1.
[[nodiscard]] QColor HeatColor(float64 heat, float64 alpha = 1.);

// The leaf filling a `size` x `size` square centered in `rect`.
[[nodiscard]] const QPainterPath &LeafPath();
void PaintLeaf(QPainter &p, QRectF rect, float64 size, const QColor &color);

} // namespace Safe::Calm
