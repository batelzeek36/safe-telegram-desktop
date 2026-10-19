/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Safe {

// The picture check's API key, written here in CI by
// .github/safe/inject_secrets.py from the SAFE_JUDGE_KEY repository secret
// (Anthropic workspace "Safe Telegram", the same key the phone build uses).
//
// THIS REPOSITORY IS PUBLIC: the committed copy stays empty. An empty key
// means the picture check is unavailable, so every picture it would have
// judged stays blurred and says it is waiting for the check.
inline constexpr auto kJudgeKey = "";

} // namespace Safe
