/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_identity.h"

namespace Safe {

QString AppName() {
	return u"Safe Telegram"_q;
}

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

} // namespace Safe
