/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_policy.h"

class ClickHandler;

namespace Main {
class Session;
} // namespace Main

namespace Safe {

// Loads the content settings now and after every app config refresh, so a
// setting turned on elsewhere is turned back off within the hour.
void WatchSensitiveContent(not_null<Main::Session*> session);

[[nodiscard]] std::shared_ptr<ClickHandler> MakeSensitiveRefusedLink();

} // namespace Safe
