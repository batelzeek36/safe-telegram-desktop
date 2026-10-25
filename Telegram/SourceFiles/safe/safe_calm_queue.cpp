/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_queue.h"

#include "base/timer.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "safe/safe_calm.h"

namespace Safe::Calm {
namespace {

// A QueueNext() that no send picked up within this time is dropped.
constexpr auto kArmedFor = crl::time(3000);
constexpr auto kRebuildsKept = 64;

struct Ticket {
	uint64 id = 0;
	PeerId peer = 0;
	QString text;
	crl::time at = 0;
	uint64 waiter = 0;
	std::optional<Result> result; // came back before its bubble existed
};

enum class Step : uchar {
	Checking,
	Ready,
	Softened,
	Failed,
};

struct Entry {
	uint64 ticket = 0; // 0: not checked (sent behind a waiting message)
	FullMsgId id; // the local bubble; empty when the send had none
	uint64 randomId = 0;
	QString original;
	QString calm;
	Builder message;
	Fn<Builder(const QString&)> rebuild;
	Fn<void(Builder)> send;
	Step step = Step::Checking;
	crl::time until = 0;
	bool aside = false; // Failed and stepped aside: does not block
};

struct PeerQueue {
	base::weak_ptr<Main::Session> session;
	std::deque<Entry> entries;
	base::Timer timer; // the next Softened entry's Undo window ends
	base::Timer ring; // repaints the countdown rings while any is Softened
};

struct Queues {
	std::vector<Ticket> armed;
	base::flat_map<uint64, Fn<Builder(const QString&)>> rebuilds;
	std::deque<uint64> rebuildOrder;
	base::flat_map<PeerId, std::unique_ptr<PeerQueue>> peers;
	rpl::event_stream<std::pair<PeerId, QueueEvent>> events;
	uint64 nextTicket = 0;
	bool releasing = false;
};

[[nodiscard]] Queues &State() {
	static auto result = Queues();
	return result;
}

void Pump(PeerId peer);

[[nodiscard]] QueueState Public(const Entry &entry, bool first) {
	switch (entry.step) {
	case Step::Checking: return QueueState::Checking;
	case Step::Softened: return QueueState::Softened;
	case Step::Failed: return QueueState::Failed;
	case Step::Ready: break;
	}
	return first ? QueueState::Sent : QueueState::Waiting;
}

[[nodiscard]] PeerQueue *QueueFor(PeerId peer) {
	auto &peers = State().peers;
	const auto i = peers.find(peer);
	return (i != end(peers)) ? i->second.get() : nullptr;
}

[[nodiscard]] HistoryItem *Bubble(const PeerQueue &queue, FullMsgId id) {
	const auto session = queue.session.get();
	return (session && id) ? session->data().message(id) : nullptr;
}

[[nodiscard]] HistoryItem *Bubble(FullMsgId id) {
	const auto queue = QueueFor(id.peer);
	return queue ? Bubble(*queue, id) : nullptr;
}

// The row under the bubble (safe_calm_chip) comes and goes with the state,
// and the failed mark takes the clock's place by the time (BottomInfo).
void Refresh(HistoryItem *item) {
	if (item) {
		auto &owner = item->history()->owner();
		owner.notifyItemDataChange(item);
		owner.requestItemResize(item);
	}
}

void Emit(PeerId peer, FullMsgId id, QueueState state, crl::time until = 0) {
	if (!id) {
		return;
	}
	auto event = QueueEvent{ id, state, until };
	if (const auto now = QueueStateOf(id)) {
		event.holding = now->holding;
		event.behind = now->behind;
		event.aside = now->aside;
	}
	State().events.fire({ peer, event });
}

void TickRings(PeerQueue &queue) {
	auto any = false;
	for (const auto &entry : queue.entries) {
		if (entry.step == Step::Softened) {
			any = true;
			if (const auto item = Bubble(queue, entry.id)) {
				item->history()->owner().requestItemRepaint(item);
			}
		}
	}
	if (!any) {
		queue.ring.cancel();
	} else if (!queue.ring.isActive()) {
		queue.ring.callEach(40);
	}
}

[[nodiscard]] Entry *FindEntry(FullMsgId id, PeerQueue **queue = nullptr) {
	if (!id) {
		return nullptr;
	}
	const auto found = QueueFor(id.peer);
	if (!found) {
		return nullptr;
	}
	for (auto &entry : found->entries) {
		if (entry.id == id) {
			if (queue) {
				*queue = found;
			}
			return &entry;
		}
	}
	return nullptr;
}

// The bubble and the text Telegram matches the server's answer against.
void ShowText(
		not_null<HistoryItem*> item,
		uint64 randomId,
		const QString &text) {
	auto &owner = item->history()->owner();
	item->setText({ text });
	owner.requestItemTextRefresh(item);
	if (randomId) {
		owner.unregisterMessageSentData(randomId);
		owner.registerMessageSentData(
			randomId,
			item->history()->peer->id,
			text);
	}
}

void ExpireArmed() {
	auto &armed = State().armed;
	const auto now = crl::now();
	for (auto i = begin(armed); i != end(armed);) {
		if (now - i->at > kArmedFor) {
			CancelDraft(i->waiter);
			i = armed.erase(i);
		} else {
			++i;
		}
	}
}

// The send that follows QueueNext() in the same chat is its message. The text
// can differ (sending turns **bold** marks into entities), so a mismatch is
// only logged: a hot draft must never slip out unchecked.
[[nodiscard]] std::optional<Ticket> TakeArmed(
		PeerId peer,
		const QString &text) {
	auto &armed = State().armed;
	for (auto i = begin(armed); i != end(armed); ++i) {
		if (i->peer == peer) {
			if (i->text != text) {
				LOG(("Safe Calm: queued text changed on the way to sending."));
			}
			auto result = std::move(*i);
			armed.erase(i);
			return result;
		}
	}
	return std::nullopt;
}

void Apply(PeerQueue &queue, Entry &entry, const Result &result) {
	const auto peer = entry.id.peer;
	if (!result.verdict) {
		LOG(("Safe Calm: no verdict for a queued message (%1)."
			).arg(result.failure));
		entry.step = Step::Failed;
		Refresh(Bubble(queue, entry.id));
		Emit(peer, entry.id, QueueState::Failed);
		return;
	}
	const auto &verdict = *result.verdict;
	if (!verdict.soften) {
		entry.step = Step::Ready;
		return;
	}
	const auto item = Bubble(queue, entry.id);
	if (!entry.rebuild || !item) {
		// Never let a hot draft go out as typed without him choosing it.
		LOG(("Safe Calm: a softened message could not be rebuilt."));
		entry.step = Step::Failed;
		Refresh(item);
		Emit(peer, entry.id, QueueState::Failed);
		return;
	}
	entry.calm = verdict.text;
	entry.step = Step::Softened;
	entry.until = crl::now() + kHoldMs;
	MarkSentSoftened(item->history(), entry.calm, entry.original);
	RememberFine(peer, entry.calm);
	ShowText(item, entry.randomId, entry.calm);
	Refresh(item);
	TickRings(queue);
	Emit(peer, entry.id, QueueState::Softened, entry.until);
}

void Answered(uint64 ticket, const Result &result) {
	auto &state = State();
	for (auto &[peer, queue] : state.peers) {
		for (auto &entry : queue->entries) {
			if (entry.ticket == ticket && entry.step == Step::Checking) {
				Apply(*queue, entry, result);
				Pump(peer);
				return;
			}
		}
	}
	for (auto &armed : state.armed) {
		if (armed.id == ticket) {
			armed.result = result;
			return;
		}
	}
}

void Pump(PeerId peer) {
	auto &state = State();
	const auto queue = QueueFor(peer);
	if (!queue) {
		return;
	} else if (!queue->session) {
		state.peers.remove(peer);
		return;
	}
	auto &entries = queue->entries;
	auto blocker = (Entry*)nullptr;
	for (auto i = 0; i < int(entries.size());) {
		auto &entry = entries[i];
		if (entry.id && !Bubble(*queue, entry.id)) {
			// Deleted from the chat while it waited.
			entries.erase(begin(entries) + i);
			continue;
		} else if (entry.step == Step::Failed && entry.aside) {
			++i;
			continue;
		} else if (entry.step == Step::Checking
			|| entry.step == Step::Failed) {
			blocker = &entry;
			break;
		} else if (entry.step == Step::Softened) {
			const auto left = entry.until - crl::now();
			if (left > 0) {
				queue->timer.callOnce(left);
				blocker = &entry;
				break;
			}
		}
		const auto builder = (entry.step == Step::Softened)
			? entry.rebuild(entry.calm)
			: entry.message;
		const auto send = std::move(entry.send);
		const auto id = entry.id;
		entries.erase(begin(entries) + i);
		state.releasing = true;
		send(builder);
		state.releasing = false;
		Refresh(Bubble(*queue, id));
		Emit(peer, id, QueueState::Sent);
	}
	TickRings(*queue);
	if (blocker && blocker->step == Step::Failed) {
		// Its row offers "Let the rest go" only while others wait on it.
		Refresh(Bubble(*queue, blocker->id));
	}
	for (const auto &entry : entries) {
		if (entry.step == Step::Ready && &entry != blocker) {
			Emit(peer, entry.id, QueueState::Waiting);
		}
	}
	if (entries.empty()) {
		state.peers.remove(peer);
	}
}

[[nodiscard]] PeerQueue &EnsureQueue(not_null<History*> history) {
	auto &peers = State().peers;
	const auto peer = history->peer->id;
	auto i = peers.find(peer);
	if (i == end(peers)) {
		auto queue = std::make_unique<PeerQueue>();
		queue->session = base::make_weak(&history->session());
		queue->timer.setCallback([=] { Pump(peer); });
		queue->ring.setCallback([=] {
			if (const auto queue = QueueFor(peer)) {
				TickRings(*queue);
			}
		});
		i = peers.emplace(peer, std::move(queue)).first;
	}
	return *i->second;
}

} // namespace

void QueueNext(not_null<History*> history, const QString &text) {
	auto &state = State();
	ExpireArmed();
	const auto id = ++state.nextTicket;
	auto ticket = Ticket{
		.id = id,
		.peer = history->peer->id,
		.text = text.trimmed(),
		.at = crl::now(),
	};
	state.armed.push_back(std::move(ticket));
	// Joins the typing check for this text, or answers from its cache.
	const auto waiter = CheckDraft(history, text, [=](const Result &result) {
		Answered(id, result);
	});
	for (auto &armed : state.armed) {
		if (armed.id == id) {
			armed.waiter = waiter;
		}
	}
}

void OfferRebuild(
		uint64 randomId,
		Fn<Builder(const QString&)> rebuild) {
	auto &state = State();
	if (!state.armed.empty() && randomId && rebuild) {
		state.rebuilds[randomId] = std::move(rebuild);
		state.rebuildOrder.push_back(randomId);
		while (int(state.rebuildOrder.size()) > kRebuildsKept) {
			state.rebuilds.remove(state.rebuildOrder.front());
			state.rebuildOrder.pop_front();
		}
	}
}

bool DeferSend(
		not_null<History*> history,
		uint64 randomId,
		Builder message,
		Fn<void(Builder)> send) {
	auto &state = State();
	if (state.releasing) {
		return false;
	}
	ExpireArmed();
	const auto peer = history->peer->id;
	const auto local = randomId
		? history->owner().message(
			history->owner().messageIdByRandomId(randomId))
		: nullptr;
	const auto text = local
		? local->originalText().text.trimmed()
		: QString();
	auto ticket = text.isEmpty() ? std::nullopt : TakeArmed(peer, text);
	const auto waiting = (QueueFor(peer) != nullptr);
	if (!ticket && !waiting) {
		return false;
	}
	auto &queue = EnsureQueue(history);
	auto &entry = queue.entries.emplace_back(Entry{
		.ticket = ticket ? ticket->id : 0,
		.id = local ? local->fullId() : FullMsgId(),
		.randomId = randomId,
		.original = ticket ? ticket->text : QString(),
		.message = std::move(message),
		.send = std::move(send),
		.step = ticket ? Step::Checking : Step::Ready,
	});
	if (const auto i = state.rebuilds.find(randomId); i != end(state.rebuilds)) {
		entry.rebuild = std::move(i->second);
		state.rebuilds.erase(i);
	}
	if (ticket) {
		Emit(peer, entry.id, QueueState::Checking);
		if (ticket->result) {
			Apply(queue, entry, *ticket->result);
		}
	}
	Pump(peer);
	return true;
}

void QueueUndo(FullMsgId id) {
	auto queue = (PeerQueue*)nullptr;
	const auto entry = FindEntry(id, &queue);
	if (!entry || entry->step != Step::Softened) {
		return;
	}
	const auto item = Bubble(*queue, id);
	if (item) {
		ShowText(item, entry->randomId, entry->original);
	}
	entry->step = Step::Ready;
	Refresh(item);
	Pump(id.peer);
}

void QueueSendAsIs(FullMsgId id) {
	const auto entry = FindEntry(id);
	if (!entry || entry->step != Step::Failed) {
		return;
	}
	entry->step = Step::Ready;
	entry->aside = false;
	Refresh(Bubble(id));
	Pump(id.peer);
}

void QueueStepAside(FullMsgId id) {
	const auto entry = FindEntry(id);
	if (!entry || entry->step != Step::Failed || entry->aside) {
		return;
	}
	entry->aside = true;
	Refresh(Bubble(id));
	Pump(id.peer);
}

void QueueDelete(FullMsgId id) {
	auto queue = (PeerQueue*)nullptr;
	if (!FindEntry(id, &queue)) {
		return;
	}
	auto &entries = queue->entries;
	for (auto i = begin(entries); i != end(entries); ++i) {
		if (i->id == id) {
			const auto randomId = i->randomId;
			entries.erase(i);
			if (const auto item = Bubble(*queue, id)) {
				if (randomId) {
					item->history()->owner().unregisterMessageRandomId(
						randomId);
				}
				item->destroy();
			}
			break;
		}
	}
	Emit(id.peer, id, QueueState::Dropped);
	Pump(id.peer);
}

rpl::producer<QueueEvent> QueueEvents(PeerId peer) {
	return State().events.events(
	) | rpl::filter([=](const std::pair<PeerId, QueueEvent> &event) {
		return event.first == peer;
	}) | rpl::map([](const std::pair<PeerId, QueueEvent> &event) {
		return event.second;
	});
}

std::optional<QueueEvent> QueueStateOf(FullMsgId id) {
	const auto queue = QueueFor(id.peer);
	if (!queue) {
		return std::nullopt;
	}
	const auto &entries = queue->entries;
	for (auto i = 0; i < int(entries.size()); ++i) {
		const auto &entry = entries[i];
		if (entry.id != id) {
			continue;
		}
		auto result = QueueEvent{ id, Public(entry, !i), entry.until };
		result.aside = entry.aside;
		if (entry.step == Step::Failed && !entry.aside) {
			result.behind = int(std::count_if(
				begin(entries) + i + 1,
				end(entries),
				[](const Entry &other) {
					return other.step != Step::Failed || !other.aside;
				}));
			result.holding = (result.behind > 0);
		}
		return result;
	}
	return std::nullopt;
}

} // namespace Safe::Calm
