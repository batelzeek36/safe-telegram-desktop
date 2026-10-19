/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// The native macOS look (Telegram for macOS, the Swift app) carried over to
// this Telegram Desktop fork: shared measures and theme-derived colors. Every
// color comes from the active theme, so night, day and custom themes all work.
namespace Safe::Look {

// A hairline between chat list rows (the native app's row separator):
// the theme's hover background pushed half again as far from the list
// background, so it reads on dark and light themes alike.
[[nodiscard]] QColor SeparatorColor();

// How much bubble shows around a picture or video in a message bubble.
[[nodiscard]] int MediaInset();

} // namespace Safe::Look
