/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Safe {

[[nodiscard]] rpl::producer<TextWithEntities> AboutIntro();
[[nodiscard]] TextWithEntities UnsupportedMessageText();

void ShowNotice(const QVariant &context, const QString &text);

// Each returns true when it handled (refused) the request.
bool RefuseOfficialUpdate();
bool RefuseTonSite(const QVariant &context);

} // namespace Safe
