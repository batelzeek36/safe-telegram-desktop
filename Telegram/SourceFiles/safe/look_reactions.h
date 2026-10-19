/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
struct MessageStyle;
} // namespace Ui

// Reactions inside a message bubble the way Telegram for macOS (the Swift
// app) colors them. In an outgoing bubble your own reaction is a light pill
// (the bubble's text color on a dark bubble, its secondary text color on a
// light one, the way the native app uses the bubble's grey text there) and
// other reactions a faint pill of the same color; in an incoming bubble the
// accent color as before. Only the fill color changes: the pill's shape, the
// emoji, the reactor photos and the reaction animation layer are painted by
// Telegram Desktop as before (InlineList::paint, after paintSingleBg).
namespace Safe::Look {

// The pill fill for a reaction button inside a bubble. `upstream` is what
// Telegram Desktop would use (the bubble's file icon background).
[[nodiscard]] QColor ReactionBg(
	QColor upstream,
	const Ui::MessageStyle &stm,
	bool outbg,
	bool chosen);

// The count / name text on a chosen reaction in an outgoing bubble: the
// bubble's own color on the light pill. Other cases keep `upstream`.
[[nodiscard]] QPen ReactionChosenFg(
	QPen upstream,
	const Ui::MessageStyle &stm,
	bool outbg);

} // namespace Safe::Look
