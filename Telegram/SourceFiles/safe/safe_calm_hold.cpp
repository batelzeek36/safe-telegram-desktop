/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm.h"

#include "base/timer.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace Safe::Calm {
namespace {

// An armed hold that no send picked up within this time is dropped.
constexpr auto kArmedFor = crl::time(3000);

struct Armed {
	PeerId peer = 0;
	QString text;
	Fn<void(FullMsgId)> held;
	crl::time at = 0;
};

struct HeldSend {
	base::weak_ptr<Main::Session> session;
	Fn<void()> send;
	base::Timer timer;
};

struct Holds {
	std::optional<Armed> armed;
	base::flat_map<FullMsgId, std::unique_ptr<HeldSend>> held;
};

[[nodiscard]] Holds &HoldState() {
	static auto result = Holds();
	return result;
}

[[nodiscard]] std::unique_ptr<HeldSend> Take(FullMsgId id) {
	auto &held = HoldState().held;
	const auto i = held.find(id);
	if (i == end(held)) {
		return nullptr;
	}
	auto result = std::move(i->second);
	held.erase(i);
	return result;
}

} // namespace

void ArmHold(
		not_null<History*> history,
		const QString &text,
		Fn<void(FullMsgId)> held) {
	HoldState().armed = Armed{
		.peer = history->peer->id,
		.text = text.trimmed(),
		.held = std::move(held),
		.at = crl::now(),
	};
}

bool DeferSend(not_null<HistoryItem*> local, Fn<void()> send) {
	auto &state = HoldState();
	if (!state.armed) {
		return false;
	}
	auto armed = base::take(*state.armed);
	state.armed = std::nullopt;
	if (armed.peer != local->history()->peer->id
		|| armed.text != local->originalText().text.trimmed()
		|| crl::now() - armed.at > kArmedFor) {
		return false;
	}
	const auto id = local->fullId();
	auto entry = std::make_unique<HeldSend>();
	entry->session = base::make_weak(&local->history()->session());
	entry->send = std::move(send);
	entry->timer.setCallback([=] { SendHeldNow(id); });
	entry->timer.callOnce(kHoldMs);
	state.held[id] = std::move(entry);
	if (armed.held) {
		armed.held(id);
	}
	return true;
}

void SendHeldNow(FullMsgId local) {
	const auto entry = Take(local);
	if (!entry) {
		return;
	}
	const auto session = entry->session.get();
	if (session && session->data().message(local)) {
		entry->send();
	}
}

void UndoHold(FullMsgId local) {
	const auto entry = Take(local);
	if (!entry) {
		return;
	}
	if (const auto session = entry->session.get()) {
		if (const auto item = session->data().message(local)) {
			item->destroy();
		}
	}
}

} // namespace Safe::Calm
