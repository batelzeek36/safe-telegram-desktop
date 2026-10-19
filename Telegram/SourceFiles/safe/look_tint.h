/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/style/style_core_types.h"

namespace Ui {
class IconButton;
class RpWidget;
} // namespace Ui

// The icons on the floating chat pills the way Telegram for macOS (the Swift
// app) draws them: white on a dark theme (measured #ffffff on Zach's night
// theme: back, call, search, more, attach, emoji, microphone), where Telegram
// Desktop uses its grey menu icon color. On a light theme nothing changes.
// Everything follows theme switches: the colors are recomputed on every
// palette change.
namespace Safe::Look {

// White while the chat bars float on a dark theme; nothing otherwise (or when
// `enabled` is false: a button showing an accent "active" icon keeps it).
[[nodiscard]] std::optional<QColor> PillIconColor(bool enabled = true);

// Paints these buttons' icons in PillIconColor(), now and after every palette
// change for as long as `owner` lives. `paletteChanged`, when given, runs
// after each palette change too (for a button with its own active state).
void WhitenPillIcons(
	not_null<Ui::RpWidget*> owner,
	std::vector<Ui::IconButton*> buttons,
	Fn<void()> paletteChanged = nullptr);

// `original` as the pill palette has it: the theme's color, except that the
// icon colors (menu and compose icons) are white on a dark theme. Use it for
// colors that widgets read at paint time; it follows theme switches.
[[nodiscard]] style::color PillColor(const style::color &original);

// A copy of `original` drawn with the pill palette (see PillColor), for
// widgets that take a style::icon. Created once per icon, kept in step with
// the palette.
[[nodiscard]] const style::icon &PillIcon(const style::icon &original);

} // namespace Safe::Look
