/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Safe {

// Calm mode's runner (Sonnet 5.5 on Zach's Max login, always on at MAINGEAR,
// reached through the Cloudflare tunnel) and its key, written here at build
// time by .github/safe/inject_secrets.py from SAFE_CALM_URL / SAFE_CALM_KEY
// (the Mac keeps them in ~/.config/safe-telegram/calm.env).
//
// THIS REPOSITORY IS PUBLIC: the committed copies stay empty. Without them
// Calm mode skips the runner and goes straight to the API fallback on the
// picture-check key; without that too, a check fails and Calm holds.
inline constexpr auto kCalmUrl = "";
inline constexpr auto kCalmKey = "";

} // namespace Safe
