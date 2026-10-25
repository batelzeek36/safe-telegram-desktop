/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Safe {

// The word guard for searches (d22-gif-search), the phone's SafeEmojiGuard
// and SafeEmojiWords (patch 25-emoji-search) with the same list and rules.
//
// Normal form: lowercase, Cyrillic transliterated (translitRusEng), accents
// dropped, leetspeak folded (0 o, 1 i, 3 e, 4 a, 5 s, 7 t, 8 b, @ a, $ s,
// ! i, | i); anything not a-z or 0-9 breaks words. Guarded when the raw query
// holds an explicit-coded emoji, or any word, or all the words glued
// together ("s e x"), contains an anywhere stem, starts with a prefix stem
// or equals a whole-word entry; checked with digits folded and kept.
[[nodiscard]] bool IsGuardedQuery(const QString &query);

} // namespace Safe
