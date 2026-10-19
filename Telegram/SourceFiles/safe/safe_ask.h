/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Safe {

// The Ask to allow box: a chat or bot that a gate refused, named in text only
// (its name and @username, never a photo or an avatar). The wording comes
// from Safe::StatusOf(peer):
// - never listed (OffNew, or a New person a gate applies to, like a call):
//   "Not on your Safe Telegram lists." with Ask to allow / Cancel;
// - already asked (OffRequested): "Already asked. It opens once it is
//   approved." with Close;
// - listed as off (OffKnown): "This chat is off in Safe Telegram." ("This
//   bot is off ..." for a bot) with Ask to allow / Cancel, like the phone
//   build's SafeAsk.
// Trusted and Allowed show nothing. Ask to allow calls Safe::Ask(peer,
// reason) and toasts "Asked. It opens once it is approved." (or "Already
// asked ..." when the request was queued before). `reason` is one of the
// words safe_trust.h lists ("join", "invite", "link", "open", "bot",
// "miniapp", "game", "payment", "inline", "call"). The same chat refused
// twice within 3 seconds shows one box (one click that hits two gates).
void ShowAskBox(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer,
	const QString &reason);
void ShowAskBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const QString &reason);

// No window at hand (the data layer, the open refusal point): the box goes to
// a window of the peer's account, or a toast when none is open.
void ShowAskBox(not_null<PeerData*> peer, const QString &reason);

// An invite link whose chat Telegram does not name (chatInvite carries a
// title only): the never-listed box, and Ask to allow queues
// Safe::AskInvite(hash, title, reason); the already-asked box while that
// request waits (Safe::InviteRequested, safe_trust_invites.h).
void ShowAskInviteBox(
	std::shared_ptr<Ui::Show> show,
	const QString &hash,
	const QString &title,
	bool channel,
	const QString &reason);

// A refusal that names no single chat (a folder link, a call with people in
// it): a toast on `show`, or a plain toast when `show` is gone.
void ShowRefusalToast(std::shared_ptr<Ui::Show> show, const QString &text);

} // namespace Safe
