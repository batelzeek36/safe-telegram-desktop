/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_identity.h"

namespace Safe {

// The name the app shows (Zach, 2026-10-19: JADEGRAM, all caps; hook script
// d18-jadegram). Only the shown name: the bundle id, the data folder, the Qt
// app name and every file inside the data folder keep their Safe Telegram
// names, so the login and the local data stay where they are.
QString AppName() {
	return u"JADEGRAM"_q;
}

// ~/Library/Application Support/<this>/ holds the login and all local data.
// It is never renamed with the app: a new folder would log out and orphan it.
QString DataFolderName() {
	return u"Safe Telegram"_q;
}

QString VersionLabelSuffix() {
	return u" (unofficial)"_q;
}

QString IntroAbout() {
	return u"An unofficial build of Telegram Desktop.\n"
		"Public discovery and sensitive media stay off."_q;
}

QString QtAppName() {
	return u"SafeTelegramDesktop"_q;
}

QString NameInText(QString text) {
	const auto name = AppName();
	if (text.contains(name)) {
		return text;
	}
	const auto upstream = u"Telegram"_q;
	const auto at = text.indexOf(upstream);
	if (at < 0) {
		return text;
	}
	return text.replace(at, upstream.size(), name);
}

} // namespace Safe
