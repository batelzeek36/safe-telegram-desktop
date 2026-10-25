/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_files.h"

#include "settings.h"

#include <QtCore/QSaveFile>

namespace Safe::Calm {
namespace {

constexpr auto kMaxFileSize = 8 * 1024 * 1024;

[[nodiscard]] QString PathFor(const QString &name) {
	const auto folder = cWorkingDir();
	return folder.isEmpty() ? QString() : (folder + name);
}

} // namespace

QByteArray ReadPrivateFile(const QString &name) {
	auto file = QFile(PathFor(name));
	if (file.fileName().isEmpty()
		|| file.size() > kMaxFileSize
		|| !file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

void WritePrivateFile(const QString &name, const QByteArray &content) {
	const auto path = PathFor(name);
	if (path.isEmpty()) {
		return;
	}
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Safe Calm: could not write %1.").arg(name));
		return;
	}
	file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	file.write(content);
	if (!file.commit()) {
		LOG(("Safe Calm: could not save %1.").arg(name));
	}
}

} // namespace Safe::Calm
