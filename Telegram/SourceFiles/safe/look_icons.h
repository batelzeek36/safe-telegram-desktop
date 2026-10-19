/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Filled glyphs in the style of Telegram for macOS (the Swift app), drawn
// from our own vector shapes (not Apple's symbols) and tinted with a theme
// color at paint time, so they follow night, day and custom themes.
namespace Safe::Look {

enum class Glyph {
	Contacts, // a person in a filled circle
	Calls, // a phone handset
	Chats, // two speech bubbles, the front one cut out of the back one
	Settings, // a gear with a three-part center
	Compose, // a square with a pencil (the "new message" button)
	Archive, // an archive box (the Archived Chats pill)
	Search, // a magnifier (the search field)
};

// Paints the glyph centered in `rect` (square or not, the glyph is square,
// `size` logical pixels on a side) in `color`. Rendered images are cached per
// glyph, size, color and pixel ratio.
void PaintGlyph(
	QPainter &p,
	Glyph glyph,
	QRect rect,
	int size,
	const QColor &color);

} // namespace Safe::Look
