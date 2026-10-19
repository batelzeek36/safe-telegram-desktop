/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

namespace Safe {

[[nodiscard]] QString AppName();
[[nodiscard]] QString DataFolderName();
[[nodiscard]] QString VersionLabelSuffix();
[[nodiscard]] QString IntroAbout();
[[nodiscard]] QString QtAppName();

} // namespace Safe
