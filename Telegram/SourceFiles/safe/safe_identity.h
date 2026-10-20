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

// An upstream line that names this app as "Telegram" ("Open Telegram",
// "Quit Telegram", "Show Telegram"), with the first "Telegram" changed to
// AppName(), in any language (like the phone build's rename rules). Lines
// that already carry AppName() are returned as they are.
[[nodiscard]] QString NameInText(QString text);

} // namespace Safe
