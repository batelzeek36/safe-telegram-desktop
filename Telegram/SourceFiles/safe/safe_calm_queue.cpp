/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_queue.h"

#include "base/timer.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "safe/safe_calm.h"
#include "safe/safe_calm_core.h"

namespace Safe::Calm {
namespace {

// A QueueNext() that no send picked up within this time is dropped.
constexpr auto kArmedFor = crl::time(3000);
constexpr auto kRebuildsKept = 64;
// Quitting waits this long at most for released messages to reach the
// server (and for checks still on their way).
constexpr auto kQuitWait = crl::time(8000);
constexpr auto kQuitPoll = crl::time(250);

struct Ticket {
	uint64 id = 0;
	PeerId peer = 0;
	QString text;
	QueueSend how;
	crl::time at = 0;
	uint64 waiter = 0;
	std::optional<Result> result; // came back before its bubble existed
};

enum class Step : uchar {
	Checking,
	Ready,
	Softened,
	Review,
	Failed,
};

struct Entry {
	uint64 ticket = 0; // 0: not armed (sent behind a waiting message)
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
	bool review = false; // Second look: a card asks, no 4 s hold
	bool away = false; // its card was put away
	bool useCalm = false; // goes out rebuilt with `calm` (picked or merged)
	bool hold = false; // waits for the chat's hold
	bool text = false; // one of his texts (armed), not media sent behind
};

struct PeerQueue {
	base::weak_ptr<Main::Session> session;
	std::deque<Entry> entries;
	Core::HoldGate gate;
	base::Timer timer; // the next Softened entry's Undo window ends
	base::Timer ring; // repaints the countdown rings while any is Softened
	base::Timer holdTimer; // the hold ends
};

struct Queues {
	std::vector<Ticket> armed;
	base::flat_map<uint64, Fn<Builder(const QString&)>> rebuilds;
	std::deque<uint64> rebuildOrder;
	base::flat_map<PeerId, std::unique_ptr<PeerQueue>> peers;
	base::flat_map<PeerId, Fn<void()>> autoMerge; // QueueSetAutoMerge
	base::flat_map<PeerId, QueueRun> runs; // QueueSetRun
	rpl::event_stream<std::pair<PeerId, QueueEvent>> events;
	uint64 nextTicket = 0;
	bool releasing = false;

	// Quitting: released bubbles not yet on the server.
	std::vector<std::pair<base::weak_ptr<Main::Session>, FullMsgId>> inflight;
	std::unique_ptr<base::Timer> quitTimer;
	crl::time quitStarted = 0;
	bool quitDone = false;
};

[[nodiscard]] Queues &State() {
	static auto result = Queues();
	return result;
}

void Pump(PeerId peer);

// Released bubbles leave this list once the server took them (their local id
// is gone) or they were deleted; it only matters while quitting.
void PruneInflight() {
	auto &inflight = State().inflight;
	for (auto i = begin(inflight); i != end(inflight);) {
		const auto session = i->first.get();
		const auto item = session
			? session->data().message(i->second)
			: nullptr;
		if (!item || item->isRegular()) {
			i = inflight.erase(i);
		} else {
			++i;
		}
	}
}

[[nodiscard]] QueueState Public(const Entry &entry, bool first) {
	switch (entry.step) {
	case Step::Checking: return QueueState::Checking;
	case Step::Softened: return QueueState::Softened;
	case Step::Review: return QueueState::Review;
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

void Emit(
		PeerId peer,
		FullMsgId id,
		QueueState state,
		crl::time until = 0,
		bool fine = false) {
	if (!id) {
		return;
	}
	auto event = QueueEvent{ id, state, until };
	if (const auto now = QueueStateOf(id)) {
		event.holding = now->holding;
		event.behind = now->behind;
		event.aside = now->aside;
		event.review = now->review;
		event.away = now->away;
		event.held = now->held;
	}
	event.fine = fine;
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

// The bubble goes before anything left the Mac.
void DestroyBubble(PeerQueue &queue, const Entry &entry) {
	if (const auto item = Bubble(queue, entry.id)) {
		if (entry.randomId) {
			item->history()->owner().unregisterMessageRandomId(
				entry.randomId);
		}
		item->destroy();
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
		if (entry.review) {
			Emit(peer, entry.id, QueueState::Waiting, 0, true);
		}
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
	if (entry.review) {
		// Second look: his words stay in the bubble until he picks.
		entry.step = Step::Review;
		Refresh(item);
		Emit(peer, entry.id, QueueState::Review);
		return;
	}
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

[[nodiscard]] int HeldTexts(const PeerQueue &queue) {
	return int(ranges::count_if(queue.entries, [](const Entry &entry) {
		return entry.hold && entry.text && entry.id;
	}));
}

// Everything held goes on (the hold ended, or something released it).
void OpenGate(PeerId peer, PeerQueue &queue) {
	queue.gate = Core::HoldGate();
	queue.holdTimer.cancel();
	auto changed = std::vector<FullMsgId>();
	for (auto &entry : queue.entries) {
		if (entry.hold) {
			entry.hold = false;
			changed.push_back(entry.id);
		}
	}
	for (const auto &id : changed) {
		Emit(peer, id, QueueState::Waiting);
	}
}

// Starts or reschedules the hold's end; opens the gate when it is due.
void CheckHold(PeerId peer, PeerQueue &queue) {
	auto &gate = queue.gate;
	if (!gate.active) {
		return;
	}
	const auto texts = HeldTexts(queue);
	const auto anyHeld = ranges::any_of(queue.entries, &Entry::hold);
	if (!anyHeld) {
		OpenGate(peer, queue);
		return;
	}
	const auto &runs = State().runs;
	const auto run = runs.contains(peer) ? runs.find(peer)->second : QueueRun();
	gate.pill = run.pill;
	const auto now = crl::now();
	const auto autoMerge = State().autoMerge.contains(peer);
	switch (Core::DecideHoldEnd(gate, now, texts, run.sentTexts, autoMerge)) {
	case Core::HoldEnd::Wait: break;
	case Core::HoldEnd::Release:
		OpenGate(peer, queue);
		return;
	case Core::HoldEnd::Merge:
		// They wait while the one message is written and its Undo runs.
		gate.card = true;
		queue.holdTimer.cancel();
		crl::on_main([=] {
			const auto &hooks = State().autoMerge;
			const auto i = hooks.find(peer);
			if (i != end(hooks) && i->second) {
				const auto due = i->second;
				due();
			} else {
				QueueReleaseHeld(peer); // the chat closed meanwhile
			}
		});
		return;
	}
	const auto at = Core::HoldReleaseAt(gate);
	if (at == Core::kWaitsForCard) {
		queue.holdTimer.cancel();
	} else {
		queue.holdTimer.callOnce(std::max(at - now, crl::time(1)));
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
	CheckHold(peer, *queue);
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
			|| entry.step == Step::Review
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
		if (entry.hold && queue->gate.active) {
			blocker = &entry;
			break;
		}
		const auto calm = (entry.step == Step::Softened) || entry.useCalm;
		const auto builder = calm ? entry.rebuild(entry.calm) : entry.message;
		const auto send = std::move(entry.send);
		const auto id = entry.id;
		entries.erase(begin(entries) + i);
		state.releasing = true;
		send(builder);
		state.releasing = false;
		if (id && queue->session) {
			PruneInflight();
			state.inflight.emplace_back(queue->session, id);
		}
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
		queue->holdTimer.setCallback([=] { Pump(peer); });
		queue->ring.setCallback([=] {
			if (const auto queue = QueueFor(peer)) {
				TickRings(*queue);
			}
		});
		i = peers.emplace(peer, std::move(queue)).first;
	}
	return *i->second;
}

[[nodiscard]] bool IsReply(const HistoryItem *item) {
	return item && item->Has<HistoryMessageReply>();
}

// Quitting: what was released and is still only on the Mac.
[[nodiscard]] bool QuitPending() {
	auto &state = State();
	PruneInflight();
	const auto &inflight = state.inflight;
	const auto checking = ranges::any_of(state.peers, [](const auto &pair) {
		return ranges::any_of(pair.second->entries, [](const Entry &entry) {
			return entry.step == Step::Checking;
		});
	});
	return !inflight.empty() || checking;
}

// Quitting: nothing waits for him any more except a card he never answered.
void ReleaseAllForQuit() {
	auto peers = std::vector<PeerId>();
	for (auto &[peer, queue] : State().peers) {
		OpenGate(peer, *queue);
		for (auto &entry : queue->entries) {
			if (entry.step == Step::Softened) {
				entry.until = 0; // its Undo window ends with the app
			}
		}
		peers.push_back(peer);
	}
	for (const auto peer : peers) {
		Pump(peer);
	}
}

} // namespace

void QueueNext(
		not_null<History*> history,
		const QString &text,
		QueueSend how) {
	auto &state = State();
	ExpireArmed();
	const auto id = ++state.nextTicket;
	auto ticket = Ticket{
		.id = id,
		.peer = history->peer->id,
		.text = text.trimmed(),
		.how = how,
		.at = crl::now(),
	};
	if (!how.check) {
		// Held only: nothing to ask, it is ready once the hold ends.
		ticket.result = Result{ .verdict = Verdict{ .text = ticket.text } };
	}
	state.armed.push_back(std::move(ticket));
	if (!how.check) {
		return;
	}
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
	const auto reply = IsReply(local);
	if (reply) {
		// A reply goes at once, and what was held before it too.
		if (const auto queue = QueueFor(peer)) {
			OpenGate(peer, *queue);
		}
	}
	const auto waiting = (QueueFor(peer) != nullptr);
	if (!ticket && !waiting) {
		return false;
	}
	const auto how = ticket ? ticket->how : QueueSend();
	auto &queue = EnsureQueue(history);
	const auto hold = ticket
		? (how.hold && !reply)
		: queue.gate.active; // media sent behind held texts wait with them
	auto &entry = queue.entries.emplace_back(Entry{
		.ticket = ticket ? ticket->id : 0,
		.id = local ? local->fullId() : FullMsgId(),
		.randomId = randomId,
		.original = ticket ? ticket->text : QString(),
		.message = std::move(message),
		.send = std::move(send),
		.step = (ticket && how.check) ? Step::Checking : Step::Ready,
		.review = how.review,
		.hold = hold,
		.text = ticket.has_value(),
	});
	if (const auto i = state.rebuilds.find(randomId); i != end(state.rebuilds)) {
		entry.rebuild = std::move(i->second);
		state.rebuilds.erase(i);
	}
	if (ticket && how.hold && !reply) {
		Core::HoldSent(queue.gate, crl::now(), false);
	}
	if (ticket && how.check) {
		Emit(peer, entry.id, QueueState::Checking);
		if (ticket->result) {
			Apply(queue, entry, *ticket->result);
		}
	} else if (entry.hold) {
		Emit(peer, entry.id, QueueState::Waiting);
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
	entry->away = false;
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
			const auto entry = std::move(*i);
			entries.erase(i);
			DestroyBubble(*queue, entry);
			break;
		}
	}
	Emit(id.peer, id, QueueState::Dropped);
	Pump(id.peer);
}

std::optional<QueueCard> QueueCardFor(PeerId peer) {
	const auto queue = QueueFor(peer);
	if (!queue) {
		return std::nullopt;
	}
	auto list = std::vector<Core::CardEntry>();
	list.reserve(queue->entries.size());
	for (const auto &entry : queue->entries) {
		using S = Core::CardStep;
		const auto step = (entry.step == Step::Checking)
			? S::Checking
			: (entry.step == Step::Review)
			? S::Review
			: (entry.step == Step::Failed && entry.review && !entry.aside)
			? S::FailedReview
			: S::Other;
		list.push_back({ .step = step, .away = entry.away });
	}
	const auto index = Core::CardIndex(list);
	if (index < 0) {
		return std::nullopt;
	}
	const auto &entry = queue->entries[index];
	const auto failed = (entry.step == Step::Failed);
	return QueueCard{
		.id = entry.id,
		.original = entry.original,
		.calm = failed ? QString() : entry.calm,
		.failed = failed,
	};
}

void QueuePickCalm(FullMsgId id) {
	auto queue = (PeerQueue*)nullptr;
	const auto entry = FindEntry(id, &queue);
	if (!entry || entry->step != Step::Review) {
		return;
	}
	const auto item = Bubble(*queue, id);
	if (!item || !entry->rebuild) {
		return;
	}
	MarkSentSoftened(item->history(), entry->calm, entry->original);
	RememberFine(id.peer, entry->calm);
	ShowText(item, entry->randomId, entry->calm);
	entry->useCalm = true;
	entry->step = Step::Ready;
	entry->away = false;
	Refresh(item);
	Pump(id.peer);
}

void QueuePickMine(FullMsgId id) {
	const auto entry = FindEntry(id);
	if (!entry || entry->step != Step::Review) {
		return;
	}
	entry->step = Step::Ready;
	entry->away = false;
	Refresh(Bubble(id));
	Pump(id.peer);
}

std::optional<QString> QueueEdit(FullMsgId id) {
	auto queue = (PeerQueue*)nullptr;
	const auto found = FindEntry(id, &queue);
	if (!found
		|| (found->step != Step::Review && found->step != Step::Failed)) {
		return std::nullopt;
	}
	auto &entries = queue->entries;
	auto words = QString();
	for (auto i = begin(entries); i != end(entries); ++i) {
		if (i->id == id) {
			const auto entry = std::move(*i);
			entries.erase(i);
			const auto item = Bubble(*queue, id);
			words = entry.original.isEmpty() && item
				? item->originalText().text
				: entry.original;
			DestroyBubble(*queue, entry);
			break;
		}
	}
	Emit(id.peer, id, QueueState::Dropped);
	Pump(id.peer);
	return words;
}

void QueuePutAway(FullMsgId id, bool away) {
	const auto entry = FindEntry(id);
	if (!entry
		|| entry->away == away
		|| (entry->step != Step::Review
			&& !(entry->step == Step::Failed && entry->review))) {
		return;
	}
	entry->away = away;
	Refresh(Bubble(id));
	Emit(id.peer, id, Public(*entry, false));
}

void QueueTyping(PeerId peer, bool fieldBusy) {
	const auto queue = QueueFor(peer);
	if (!queue || !queue->gate.active) {
		return;
	}
	Core::HoldTyped(queue->gate, crl::now(), fieldBusy);
	Pump(peer); // reschedules the hold's end (or releases, when due)
}

void QueueReleaseHeld(PeerId peer) {
	const auto queue = QueueFor(peer);
	if (!queue || !queue->gate.active) {
		return;
	}
	OpenGate(peer, *queue);
	Pump(peer);
}

void QueueHoldForCard(PeerId peer, bool open) {
	const auto queue = QueueFor(peer);
	if (!queue || !queue->gate.active || queue->gate.card == open) {
		return;
	}
	queue->gate.card = open;
	if (!open) {
		// He looked and closed it: the usual wait after he stops.
		queue->gate.lastActivity = crl::now();
	}
	Pump(peer);
}

void QueueSetAutoMerge(PeerId peer, Fn<void()> due) {
	auto &hooks = State().autoMerge;
	if (due) {
		hooks[peer] = std::move(due);
	} else {
		hooks.remove(peer);
	}
}

void QueueSetRun(PeerId peer, QueueRun run) {
	auto &runs = State().runs;
	const auto i = runs.find(peer);
	const auto was = (i != end(runs)) ? i->second : QueueRun();
	if (was == run) {
		return;
	} else if (run == QueueRun()) {
		runs.remove(peer);
	} else {
		runs[peer] = run;
	}
	const auto queue = QueueFor(peer);
	if (queue && queue->gate.active) {
		Pump(peer); // the pill's extra wait, or a merge that may not happen
	}
}

bool QueueHeld(not_null<const HistoryItem*> item) {
	const auto queue = QueueFor(item->history()->peer->id);
	if (!queue || !queue->gate.active) {
		return false;
	}
	const auto id = item->fullId();
	return ranges::any_of(queue->entries, [&](const Entry &entry) {
		return entry.hold && (entry.id == id);
	});
}

HeldGroup QueueHeldGroup(PeerId peer) {
	auto result = HeldGroup();
	const auto queue = QueueFor(peer);
	if (!queue || !queue->gate.active) {
		return result;
	}
	for (const auto &entry : queue->entries) {
		if (!entry.hold || !entry.id || !Bubble(*queue, entry.id)) {
			continue;
		}
		result.items.push_back(entry.id);
		if (entry.text) {
			result.texts.push_back(entry.id);
			result.words.push_back(entry.original);
		}
	}
	return result;
}

bool QueuePreviewHeld(
		PeerId peer,
		const std::vector<FullMsgId> &texts,
		const QString &text) {
	const auto queue = QueueFor(peer);
	if (!queue || texts.empty()) {
		return false;
	} else if (!text.isEmpty() && !QueueStillHeld(peer, texts)) {
		return false; // his words come back whatever changed
	}
	const auto first = FindEntry(texts.front());
	const auto item = first ? Bubble(*queue, first->id) : nullptr;
	if (!first || !item) {
		return false;
	}
	ShowText(item, first->randomId, text.isEmpty() ? first->original : text);
	Refresh(item);
	return true;
}

bool QueueMergeHeld(
		PeerId peer,
		const std::vector<FullMsgId> &texts,
		const QString &text) {
	const auto queue = QueueFor(peer);
	if (!queue
		|| texts.empty()
		|| text.isEmpty()
		|| !QueueStillHeld(peer, texts)) {
		return false;
	}
	const auto first = FindEntry(texts.front());
	const auto item = first ? Bubble(*queue, first->id) : nullptr;
	if (!first || !item || !first->rebuild) {
		LOG(("Safe Calm: held messages could not be merged."));
		return false;
	}
	auto words = QStringList();
	for (const auto &id : texts) {
		if (const auto entry = FindEntry(id)) {
			words.push_back(entry->original);
		}
	}
	MarkSentSoftened(item->history(), text, words.join(u"\n"_q));
	RememberFine(peer, text);
	ShowText(item, first->randomId, text);
	first->calm = text;
	first->useCalm = true;
	first->step = Step::Ready; // a check still on its way is not needed
	first->away = false;
	Refresh(item);
	auto &entries = queue->entries;
	for (auto i = 1; i < int(texts.size()); ++i) {
		const auto id = texts[i];
		for (auto j = begin(entries); j != end(entries); ++j) {
			if (j->id == id) {
				const auto entry = std::move(*j);
				entries.erase(j);
				DestroyBubble(*queue, entry);
				break;
			}
		}
		Emit(peer, id, QueueState::Dropped);
	}
	OpenGate(peer, *queue);
	Pump(peer);
	return true;
}

bool QueueStillHeld(PeerId peer, const std::vector<FullMsgId> &texts) {
	const auto held = QueueHeldGroup(peer).texts;
	return ranges::all_of(texts, [&](FullMsgId id) {
		return ranges::contains(held, id);
	});
}

bool QueueDropHeld(PeerId peer, const std::vector<FullMsgId> &texts) {
	const auto queue = QueueFor(peer);
	if (!queue || texts.empty() || !QueueStillHeld(peer, texts)) {
		return false;
	}
	auto &entries = queue->entries;
	for (const auto &id : texts) {
		for (auto j = begin(entries); j != end(entries); ++j) {
			if (j->id == id) {
				const auto entry = std::move(*j);
				entries.erase(j);
				DestroyBubble(*queue, entry);
				break;
			}
		}
		Emit(peer, id, QueueState::Dropped);
	}
	if (!HeldTexts(*queue)) {
		OpenGate(peer, *queue); // the media held among them go now
	} else {
		// He sent more meanwhile: those wait for the hold as usual.
		queue->gate.card = false;
		queue->gate.lastActivity = crl::now();
	}
	Pump(peer);
	return true;
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
		result.review = entry.review;
		result.away = entry.away;
		result.held = entry.hold && queue->gate.active;
		if (result.held && result.state == QueueState::Sent) {
			result.state = QueueState::Waiting;
		}
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

bool QueueBusy(PeerId peer) {
	const auto queue = QueueFor(peer);
	return queue && !queue->entries.empty();
}

bool QueuePreventsQuit() {
	auto &state = State();
	if (state.quitDone) {
		return false;
	}
	ReleaseAllForQuit();
	if (!QuitPending()) {
		state.quitDone = true;
		return false;
	}
	if (!state.quitTimer) {
		LOG(("Safe Calm: quitting waits for messages on their way."));
		state.quitStarted = crl::now();
		state.quitTimer = std::make_unique<base::Timer>([] {
			auto &state = State();
			ReleaseAllForQuit();
			if (!QuitPending()
				|| crl::now() - state.quitStarted >= kQuitWait) {
				state.quitTimer->cancel();
				state.quitDone = true;
				::Core::Quit();
			}
		});
		state.quitTimer->callEach(kQuitPoll);
	}
	return true;
}

} // namespace Safe::Calm
