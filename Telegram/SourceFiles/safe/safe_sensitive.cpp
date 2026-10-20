/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_sensitive.h"

#include "api/api_sensitive_content.h"
#include "apiwrap.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "safe/safe_identity.h"
#include "safe/safe_notices.h"
#include "ui/click_handler.h"

namespace Safe {

void WatchSensitiveContent(not_null<Main::Session*> session) {
	if (!SensitiveLocked()) {
		return;
	}
	const auto reload = [=] {
		session->api().sensitiveContent().reload();
	};
	reload();
	session->appConfig().refreshed(
	) | rpl::on_next(reload, session->lifetime());
}

std::shared_ptr<ClickHandler> MakeSensitiveRefusedLink() {
	return std::make_shared<LambdaClickHandler>([](ClickContext context) {
		ShowNotice(
			context.other,
			u"Sensitive media stays hidden in "_q + AppName() + u"."_q);
	});
}

} // namespace Safe
