/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_policy.h"

namespace Safe {

// Instant View and other in-app web pages: each returns true when it sent the
// page to the default browser instead (Telegram's own links still open inside).
bool OpenWebOutside(const QString &uri, const QVariant &context);
bool OpenPageOutside(const QString &url, const QString &hash);

} // namespace Safe
