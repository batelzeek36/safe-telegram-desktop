/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_bots.h"

#include "base/flat_map.h"
#include "core/click_handler_types.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "history/history_item_reply_markup.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "payments/payments_form.h"
#include "safe/safe_ask.h"
#include "safe/safe_trust.h"
#include "ui/layers/show.h"
#include "window/window_session_controller.h"

namespace Safe {
namespace {

// Inline mode asks on every keystroke: one box per bot in this window.
constexpr auto kInlineRepeatWindow = crl::time(30000);

// The `reason` words of safe_trust.h this file uses.
[[nodiscard]] QString BotReason() {
	return u"bot"_q;
}

[[nodiscard]] QString MiniAppReason() {
	return u"miniapp"_q;
}

[[nodiscard]] QString GameReason() {
	return u"game"_q;
}

[[nodiscard]] QString PaymentReason() {
	return u"payment"_q;
}

[[nodiscard]] QString InlineReason() {
	return u"inline"_q;
}

// Inline bots that showed the box, and when (pruned on every check, so it
// holds at most the bots refused in the last 30 seconds).
base::flat_map<not_null<PeerData*>, crl::time> InlineAsked;

[[nodiscard]] bool InlineAskedJustNow(not_null<PeerData*> bot) {
	const auto now = crl::now();
	for (auto i = begin(InlineAsked); i != end(InlineAsked);) {
		if ((now - i->second) >= kInlineRepeatWindow) {
			i = InlineAsked.erase(i);
		} else {
			++i;
		}
	}
	if (InlineAsked.contains(bot)) {
		return true;
	}
	InlineAsked.emplace(bot, now);
	return false;
}

[[nodiscard]] QString ButtonReason(HistoryMessageMarkupButton::Type type) {
	using Type = HistoryMessageMarkupButton::Type;
	switch (type) {
	case Type::Buy: return PaymentReason();
	case Type::Game: return GameReason();
	case Type::WebView:
	case Type::SimpleWebView: return MiniAppReason();
	case Type::SwitchInline:
	case Type::SwitchInlineSame: return InlineReason();
	default: return BotReason();
	}
}

// Buttons that reach no bot (a plain URL goes through the link gate, a
// profile through the chat gate, Suggest* answer a post suggestion).
[[nodiscard]] bool ButtonPasses(HistoryMessageMarkupButton::Type type) {
	using Type = HistoryMessageMarkupButton::Type;
	switch (type) {
	case Type::Url:
	case Type::CopyText:
	case Type::Disabled:
	case Type::UserProfile:
	case Type::SuggestDecline:
	case Type::SuggestAccept:
	case Type::SuggestChange: return true;
	default: return false;
	}
}

} // namespace

bool IsOpenBot(UserData *bot) {
	return bot && IsOpen(bot);
}

bool RefuseBot(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> bot,
		const QString &reason) {
	if (IsOpen(bot)) {
		return false;
	}
	ShowAskBox(std::move(show), bot, reason);
	return true;
}

bool RefuseMiniApp(const InlineBots::WebViewDescriptor &descriptor) {
	const auto bot = descriptor.bot;
	if (IsOpen(bot)) {
		return false;
	}
	auto show = descriptor.parentShow;
	if (!show) {
		if (const auto controller = descriptor.context.controller.get()) {
			show = controller->uiShow();
		}
	}
	const auto game = v::is<InlineBots::WebViewSourceGame>(
		descriptor.source);
	ShowAskBox(std::move(show), bot, game ? GameReason() : MiniAppReason());
	return true;
}

bool RefuseBotButton(
		std::shared_ptr<Ui::Show> show,
		not_null<const HistoryItem*> item,
		const HistoryMessageMarkupButton &button) {
	if (ButtonPasses(button.type)) {
		return false;
	}
	const auto bot = item->getMessageBot();
	if (!bot || IsOpen(bot)) {
		return false;
	}
	ShowAskBox(std::move(show), bot, ButtonReason(button.type));
	return true;
}

bool RefuseInvoice(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	const auto invoice = media ? media->invoice() : nullptr;
	if (!invoice || invoice->receiptMsgId) {
		return false;
	}
	const auto bot = item->getMessageBot();
	if (!bot || IsOpen(bot)) {
		return false;
	}
	ShowAskBox(bot, PaymentReason());
	return true;
}

bool RefusePaymentForm(
		const Payments::InvoiceId &id,
		const MTPpayments_PaymentForm &form) {
	const auto slug = v::is<Payments::InvoiceSlug>(id.value);
	if (!slug && !v::is<Payments::InvoiceMessage>(id.value)) {
		return false;
	}
	const auto session = Payments::SessionFromId(id);
	const auto botId = form.match([&](const MTPDpayments_paymentForm &data) {
		session->data().processUsers(data.vusers());
		return uint64(data.vbot_id().v);
	}, [&](const MTPDpayments_paymentFormStars &data) {
		session->data().processUsers(data.vusers());
		return uint64(data.vbot_id().v);
	}, [](const MTPDpayments_paymentFormStarGift &) {
		return uint64(0);
	});
	if (!botId) {
		// No bot named: a slug link stays closed, anything else passes.
		return slug;
	}
	const auto bot = session->data().user(UserId(botId));
	if (!bot->isBot() && !slug) {
		// A channel's or a person's form (paid media): not a bot invoice.
		return false;
	} else if (IsOpen(bot)) {
		return false;
	}
	ShowAskBox(bot, PaymentReason());
	return true;
}

QString RefusedError() {
	return u"SAFE_TELEGRAM_OFF"_q;
}

bool RefuseUrlAuth(
		not_null<Main::Session*> session,
		const MTPDurlAuthResultRequest &request,
		const QString &url,
		QVariant context) {
	const auto bot = session->data().processUser(request.vbot());
	if (IsOpen(bot)) {
		return false;
	}
	if (!url.isEmpty()) {
		UrlClickHandler::Open(url, std::move(context));
	}
	return true;
}

UserData *OpenInlineBot(std::shared_ptr<Ui::Show> show, UserData *bot) {
	if (!bot || IsOpen(bot)) {
		return bot;
	} else if (!InlineAskedJustNow(bot)) {
		ShowAskBox(std::move(show), bot, InlineReason());
	}
	return nullptr;
}

bool RefuseAddedBots(
		std::shared_ptr<Ui::Show> show,
		const std::vector<not_null<UserData*>> &users) {
	for (const auto &user : users) {
		if (user->isBot() && !IsOpen(user)) {
			ShowAskBox(std::move(show), user, BotReason());
			return true;
		}
	}
	return false;
}

bool RefuseAddedPeers(
		std::shared_ptr<Ui::Show> show,
		const std::vector<not_null<PeerData*>> &peers) {
	for (const auto &peer : peers) {
		const auto user = peer->asUser();
		if (user && user->isBot() && !IsOpen(user)) {
			ShowAskBox(std::move(show), user, BotReason());
			return true;
		}
	}
	return false;
}

bool RefuseBusinessBot(UserData *bot) {
	if (!bot || IsOpen(bot)) {
		return false;
	}
	ShowAskBox(bot, BotReason());
	return true;
}

} // namespace Safe
