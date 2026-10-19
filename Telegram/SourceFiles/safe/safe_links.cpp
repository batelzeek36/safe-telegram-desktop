/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_links.h"

#include "core/click_handler_types.h"

namespace Safe {

bool OpenWebOutside(const QString &uri, const QVariant &context) {
	if (WebPagesInside() || uri.isEmpty()) {
		return false;
	}
	auto my = context.value<ClickHandlerContext>();
	my.ignoreIv = true;
	const auto updated = QVariant::fromValue(my);
	if (my.forceExternalUrlConfirmation) {
		HiddenUrlClickHandler::Open(uri, updated);
	} else {
		UrlClickHandler::Open(uri, updated);
	}
	return true;
}

bool OpenPageOutside(const QString &url, const QString &hash) {
	if (WebPagesInside()) {
		return false;
	} else if (url.isEmpty()) {
		return true;
	}
	const auto full = hash.isEmpty() ? url : (url + '#' + hash);
	return OpenWebOutside(full, QVariant());
}

} // namespace Safe
