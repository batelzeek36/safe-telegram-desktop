/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_notices.h"

#include "core/click_handler_types.h"
#include "safe/safe_identity.h"
#include "safe/safe_policy.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "window/window_session_controller.h"

namespace Safe {

rpl::producer<TextWithEntities> AboutIntro() {
	return rpl::single(TextWithEntities::Simple(
		u"An unofficial build of Telegram Desktop.\n"
		"Public discovery, sensitive media and in-app web pages "
		"are off.\nLinks open in your browser."_q));
}

TextWithEntities UnsupportedMessageText() {
	auto result = TextWithEntities::Simple(
		u"This message needs a newer Telegram Desktop. It shows once "_q
		+ AppName()
		+ u" moves to that release."_q);
	result.entities.push_front(
		EntityInText(EntityType::Italic, 0, result.text.size()));
	return result;
}

void ShowNotice(const QVariant &context, const QString &text) {
	const auto my = context.value<ClickHandlerContext>();
	if (const auto window = my.sessionWindow.get()) {
		window->showToast(text);
	} else if (my.show) {
		my.show->showToast(text);
	} else {
		Ui::Toast::Show(text);
	}
}

bool RefuseOfficialUpdate() {
	if (OfficialUpdatesAllowed()) {
		return false;
	}
	ShowNotice(
		QVariant(),
		AppName()
		+ u" updates come with its own builds. "
		"It does not install the official app."_q);
	return true;
}

bool RefuseTonSite(const QVariant &context) {
	if (WebPagesInside()) {
		return false;
	}
	ShowNotice(context, u"TON sites do not open in "_q + AppName() + u"."_q);
	return true;
}

} // namespace Safe
