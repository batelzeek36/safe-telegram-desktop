/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Safe {

// The trust list, written here in CI by .github/safe/inject_secrets.py from
// the SAFE_TRUST_JSON repository secret (the "android" block of the control
// repo's private/trust.json: three arrays of dialog ids, no names).
//
// THIS REPOSITORY IS PUBLIC: the committed copy stays empty, and a build
// without the secret shows nothing but Saved Messages and the service
// account. Never commit a filled copy (the pre-commit hook refuses it).
inline constexpr auto kTrustListJson = "";

} // namespace Safe
