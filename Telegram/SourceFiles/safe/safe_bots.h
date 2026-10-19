/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class PeerData;
class UserData;
struct HistoryMessageMarkupButton;

namespace InlineBots {
struct WebViewDescriptor;
} // namespace InlineBots

namespace Payments {
struct InvoiceId;
} // namespace Payments

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace Safe {

// The bot gates (round 2, d13). One rule everywhere: a bot whose tier is
// Trusted or Allowed keeps every feature; any other bot is refused, and the
// refusal shows the Ask to allow box (Safe::ShowAskBox) unless it says
// "silently". Every Refuse* returns true when the caller must stop.

// True for a bot (or any user) that is Trusted or Allowed. False for nullptr.
[[nodiscard]] bool IsOpenBot(UserData *bot);

// The plain gate: /start, a bot added to a group, a business bot. `reason`
// is one of the words safe_trust.h lists ("bot", "miniapp", "game",
// "payment", "inline").
bool RefuseBot(
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> bot,
	const QString &reason);

// Every Mini App, web app button, menu button, attach-menu app and game goes
// through AttachWebView::open: refused unless the descriptor's bot is open
// (reason "game" for a game, "miniapp" for the rest).
bool RefuseMiniApp(const InlineBots::WebViewDescriptor &descriptor);

// An inline or reply keyboard button: refused when the bot behind the message
// (HistoryItem::getMessageBot) is not open. Url, CopyText, Disabled,
// UserProfile and the Suggest* buttons always pass (they reach no bot, or go
// through the link and chat gates). A message with no bot behind it (a
// person's or a channel's) passes. A null `show` puts the box in a window of
// the message's account.
bool RefuseBotButton(
	std::shared_ptr<Ui::Show> show,
	not_null<const HistoryItem*> item,
	const HistoryMessageMarkupButton &button);

// A message invoice whose bot is not open (receipts and paid media with no
// bot behind them pass; the form gate below has the last word).
bool RefuseInvoice(not_null<const HistoryItem*> item);

// The payment form, when it arrives (message and slug invoices only; Stars
// top-ups, Premium gift codes and Star gifts pass), like the phone build's
// SafeGate.refusePaymentForm: the form's bot must be open. A form whose
// bot_id is a person or a channel passes for a message invoice (paid media),
// a slug link with no known bot or a closed one is refused. The caller ends
// the checkout with Error{ Form, RefusedError() }.
bool RefusePaymentForm(
	const Payments::InvoiceId &id,
	const MTPpayments_PaymentForm &form);

// The error id a refused payment form carries, so CheckoutProcess closes
// without its "Error:" panel.
[[nodiscard]] QString RefusedError();

// "Log in with Telegram" on a website (a login button or a login URL): the
// bot that would receive the login must be open. Otherwise the plain `url`
// opens in the browser without logging in (what declining does), and true.
bool RefuseUrlAuth(
	not_null<Main::Session*> session,
	const MTPDurlAuthResultRequest &request,
	const QString &url,
	QVariant context);

// Inline mode in the message field ("@bot query"): returns `bot` when it is
// open, else nullptr (no query is sent, no results show). Runs on every
// keystroke, so the box shows once per bot per 30 seconds.
[[nodiscard]] UserData *OpenInlineBot(
	std::shared_ptr<Ui::Show> show,
	UserData *bot);

// Adding members or admins to a group or channel: refused when any of the
// users is a bot that is not open (the box names the first such bot). People
// pass.
bool RefuseAddedBots(
	std::shared_ptr<Ui::Show> show,
	const std::vector<not_null<UserData*>> &users);

// The same for the people picked for a new basic group.
bool RefuseAddedPeers(
	std::shared_ptr<Ui::Show> show,
	const std::vector<not_null<PeerData*>> &peers);

// Telegram Business, connected bot: saving settings that keep a bot that is
// not open connected is refused (removing a bot always passes).
bool RefuseBusinessBot(UserData *bot);

} // namespace Safe
