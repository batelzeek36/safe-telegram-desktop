/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Safe::Calm {

// Calm's small files in the app's data folder (cWorkingDir()), owner-only
// (0600), written whole through a temporary file so a crash never leaves
// half a file. Main thread only.
[[nodiscard]] QByteArray ReadPrivateFile(const QString &name);
void WritePrivateFile(const QString &name, const QByteArray &content);

} // namespace Safe::Calm
