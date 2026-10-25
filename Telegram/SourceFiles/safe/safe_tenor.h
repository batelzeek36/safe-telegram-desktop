/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_media.h"

class ApiWrap;

namespace Api {
struct SendAction;
} // namespace Api

namespace InlineBots {
class Result;
} // namespace InlineBots

namespace Main {
class Session;
} // namespace Main

namespace Safe::Tenor {

// The GIF tab searches Tenor (d23-tenor-gifs, Zach 2026-10-25: "that is NOT
// tenor gif that JADELEAF is using"). Google closed Tenor's API to apps on
// 2026-06-30, so the search reads tenor.com's own search page, which carries
// its 50 results as JSON (store-cache: universal.search.<q>-low-all.results,
// Tenor's "low" content filter; the page has no paging). Each GIF becomes a
// result like the GIF bot's (botInlineResult, type gif, the MP4 as web
// content, the PNG preview as thumbnail), so the panel's own code shows it;
// d22's hold and picture check apply, keyed by Tenor's post id. Sending goes
// as messages.sendMedia with the MP4 link: Telegram fetches it and sends a
// GIF (tested 10-25: documentAttributeAnimated).

// Asks tenor.com for `query`; `done` always runs (an empty answer on any
// failure).
void Search(
	not_null<Main::Session*> session,
	const QString &query,
	Fn<void(const MTPmessages_BotResults&)> done);

// A result Search made: its MP4 link, else empty.
[[nodiscard]] QString ClipUrl(not_null<const InlineBots::Result*> result);

// A result Search made: its picture-check key (Tenor's post id), else empty.
[[nodiscard]] MediaKey KeyOf(not_null<const InlineBots::Result*> result);

// ApiWrap::sendInlineResult for a result Search made, after the local
// message is in the history: sends the clip by its link instead of
// messages.sendInlineBotResult. False (nothing sent) for any other result.
bool Send(
	not_null<ApiWrap*> api,
	not_null<InlineBots::Result*> result,
	const Api::SendAction &action,
	uint64 randomId,
	FullMsgId localId,
	int64 starsPaid,
	Fn<void(bool)> done);

} // namespace Safe::Tenor
