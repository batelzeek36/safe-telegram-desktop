/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_ask.h"

#include "core/application.h"
#include "data/data_peer.h"
#include "main/main_session.h"
#include "safe/safe_trust.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"
#include "window/window_separate_id.h"
#include "window/window_session_controller.h"

namespace Safe {
namespace {

// One click can hit two gates (a link, then the screen it opens): the same
// chat within this window shows one box.
constexpr auto kRepeatWindow = crl::time(3000);

// User-visible text (C++ constants: cloud language packs cannot change them).
[[nodiscard]] QString NotListedText() {
	return u"Not on your Safe Telegram lists."_q;
}

[[nodiscard]] QString ChatOffText() {
	return u"This chat is off in Safe Telegram."_q;
}

[[nodiscard]] QString BotOffText() {
	return u"This bot is off in Safe Telegram."_q;
}

[[nodiscard]] QString AskedText() {
	return u"Asked. It opens once it is approved."_q;
}

[[nodiscard]] QString AlreadyAskedText() {
	return u"Already asked. It opens once it is approved."_q;
}

[[nodiscard]] QString AskButtonText() {
	return u"Ask to allow"_q;
}

[[nodiscard]] QString CancelButtonText() {
	return u"Cancel"_q;
}

[[nodiscard]] QString CloseButtonText() {
	return u"Close"_q;
}

enum class Kind : uchar {
	Person,
	Bot,
	Group,
	Channel,
};

struct Described {
	Kind kind = Kind::Group;
	QString title;
	QString username;
};

struct Last {
	QString key;
	crl::time at = 0;
};

Last LastShown;

[[nodiscard]] bool ShownJustNow(const QString &key) {
	const auto now = crl::now();
	if (LastShown.key == key && (now - LastShown.at) < kRepeatWindow) {
		return true;
	}
	LastShown = { key, now };
	return false;
}

[[nodiscard]] QString DefaultTitle(Kind kind) {
	switch (kind) {
	case Kind::Person: return u"A person"_q;
	case Kind::Bot: return u"A bot"_q;
	case Kind::Channel: return u"A channel"_q;
	case Kind::Group: return u"A group"_q;
	}
	return u"A chat"_q;
}

// Text-only facts from the local cache: no photo is ever read.
[[nodiscard]] Described Describe(not_null<PeerData*> peer) {
	const auto kind = peer->isUser()
		? (peer->isBot() ? Kind::Bot : Kind::Person)
		: peer->isBroadcast()
		? Kind::Channel
		: Kind::Group;
	const auto &name = peer->name();
	return {
		.kind = kind,
		.title = name.isEmpty() ? DefaultTitle(kind) : name,
		.username = peer->username(),
	};
}

[[nodiscard]] QString BodyText(const Described &described, QString line) {
	return described.username.isEmpty()
		? line
		: (u"@"_q + described.username + u"\n\n"_q + line);
}

[[nodiscard]] bool Usable(const std::shared_ptr<Ui::Show> &show) {
	return show && show->valid();
}

void Toast(const std::shared_ptr<Ui::Show> &show, const QString &text) {
	if (Usable(show)) {
		show->showToast(text);
	} else {
		Ui::Toast::Show(text);
	}
}

// The account's window (the peer's own window first), like ApiWrap's
// ShowForPeer: never another account's window, never a switch.
[[nodiscard]] std::shared_ptr<Ui::Show> ShowForPeer(
		not_null<PeerData*> peer) {
	const auto session = &peer->session();
	const auto pick = [&](Window::Controller *window)
	-> std::shared_ptr<Ui::Show> {
		const auto controller = window
			? window->sessionController()
			: nullptr;
		return (controller && (&controller->session() == session))
			? std::shared_ptr<Ui::Show>(controller->uiShow())
			: nullptr;
	};
	if (auto result = pick(Core::App().windowFor(Window::SeparateId(peer)))) {
		return result;
	}
	return pick(Core::App().activeWindow());
}

// `ask` queues the request; true when it is new, false when it was queued.
void ShowAskable(
		std::shared_ptr<Ui::Show> show,
		const Described &described,
		const QString &line,
		Fn<bool()> ask) {
	auto box = Ui::MakeConfirmBox({
		.text = BodyText(described, line),
		.confirmed = [=](Fn<void()> close) {
			close();
			Toast(show, ask() ? AskedText() : AlreadyAskedText());
		},
		.confirmText = AskButtonText(),
		.cancelText = CancelButtonText(),
		.title = described.title,
	});
	show->showBox(std::move(box));
}

void ShowAsked(
		std::shared_ptr<Ui::Show> show,
		const Described &described) {
	show->showBox(Ui::MakeInformBox({
		.text = BodyText(described, AlreadyAskedText()),
		.confirmText = CloseButtonText(),
		.title = described.title,
	}));
}

} // namespace

void ShowAskBox(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		const QString &reason) {
	const auto status = StatusOf(peer);
	if (status == Status::Trusted || status == Status::Allowed) {
		return;
	} else if (!Usable(show)) {
		show = ShowForPeer(peer);
	}
	if (ShownJustNow(u"peer:"_q + QString::number(peer->id.value))) {
		return;
	}
	const auto described = Describe(peer);
	const auto offLine = (described.kind == Kind::Bot)
		? BotOffText()
		: ChatOffText();
	if (!Usable(show)) {
		Toast(nullptr, (status == Status::OffRequested)
			? AlreadyAskedText()
			: (status == Status::OffKnown)
			? offLine
			: NotListedText());
		return;
	}
	const auto ask = [=] {
		return Ask(peer, reason);
	};
	switch (status) {
	case Status::OffRequested:
		ShowAsked(show, described);
		return;
	case Status::OffKnown:
		ShowAskable(show, described, offLine, ask);
		return;
	case Status::New:
	case Status::OffNew:
		ShowAskable(show, described, NotListedText(), ask);
		return;
	case Status::Trusted:
	case Status::Allowed:
		return;
	}
}

void ShowAskBox(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const QString &reason) {
	ShowAskBox(
		std::shared_ptr<Ui::Show>(controller->uiShow()),
		peer,
		reason);
}

void ShowAskBox(not_null<PeerData*> peer, const QString &reason) {
	ShowAskBox(std::shared_ptr<Ui::Show>(), peer, reason);
}

void ShowAskInviteBox(
		std::shared_ptr<Ui::Show> show,
		const QString &hash,
		const QString &title,
		bool channel,
		const QString &reason) {
	if (hash.isEmpty() || ShownJustNow(u"invite:"_q + hash)) {
		return;
	} else if (!Usable(show)) {
		Toast(nullptr, NotListedText());
		return;
	}
	const auto kind = channel ? Kind::Channel : Kind::Group;
	const auto described = Described{
		.kind = kind,
		.title = title.isEmpty() ? DefaultTitle(kind) : title,
	};
	ShowAskable(show, described, NotListedText(), [=] {
		return AskInvite(hash, described.title, reason);
	});
}

void ShowRefusalToast(std::shared_ptr<Ui::Show> show, const QString &text) {
	Toast(show, text);
}

} // namespace Safe
