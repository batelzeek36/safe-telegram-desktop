/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm.h"

#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "safe/safe_calm_client.h"
#include "safe/safe_calm_shared.h"
#include "safe/safe_calm_store.h"

namespace Safe::Calm {
namespace {

constexpr auto kMaxCached = 64;

using DraftKey = std::pair<PeerId, QString>;

struct Pending {
	uint64 call = 0;
	std::vector<std::pair<uint64, Fn<void(const Result&)>>> waiters;
};

struct Drafts {
	base::flat_map<DraftKey, Verdict> cache;
	std::deque<DraftKey> order;
	base::flat_map<DraftKey, Pending> pending;
	base::flat_map<uint64, DraftKey> waiterKeys;
	uint64 nextWaiter = 0;
};

[[nodiscard]] Drafts &DraftState() {
	static auto result = Drafts();
	return result;
}

void Remember(const DraftKey &key, const Verdict &verdict) {
	auto &state = DraftState();
	if (!state.cache.contains(key)) {
		state.order.push_back(key);
	}
	state.cache[key] = verdict;
	while (int(state.order.size()) > kMaxCached) {
		state.cache.remove(state.order.front());
		state.order.pop_front();
	}
}

void Answer(const DraftKey &key, const Result &result) {
	auto &state = DraftState();
	const auto i = state.pending.find(key);
	if (i == end(state.pending)) {
		return;
	}
	auto waiters = std::move(i->second.waiters);
	state.pending.erase(i);
	if (result.verdict) {
		Remember(key, *result.verdict);
	}
	for (auto &[waiter, done] : waiters) {
		state.waiterKeys.remove(waiter);
		done(result);
	}
}

[[nodiscard]] QString DisplayName(not_null<PeerData*> peer) {
	const auto &name = peer->isUser() ? peer->shortName() : peer->name();
	return name.isEmpty() ? peer->name() : name;
}

} // namespace

Client &SharedClient() {
	static auto result = Client();
	return result;
}

QString OwnerName() {
	return u"Zach"_q;
}

bool Checkable(const QString &text) {
	const auto trimmed = text.trimmed();
	if (trimmed.isEmpty()
		|| trimmed.size() > kMaxTextLength
		|| trimmed.startsWith(QChar('/'))) {
		return false;
	}
	return ranges::any_of(trimmed, [](QChar ch) { return ch.isLetter(); });
}

Request MakeRequest(
		not_null<History*> history,
		Direction direction,
		const QString &text,
		MsgId before) {
	const auto peer = history->peer;
	auto result = Request{
		.direction = direction,
		.text = text,
		.sender = (direction == Direction::Out)
			? OwnerName()
			: QString(),
		.recipient = (direction == Direction::Out)
			? DisplayName(peer)
			: OwnerName(),
	};
	auto lines = std::vector<ContextLine>();
	for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			const auto item = (*m)->data();
			if (before && (!item->isRegular() || item->id >= before)) {
				continue;
			} else if (item->isService() || IsHeld(item)) {
				continue;
			}
			const auto line = item->originalText().text.trimmed();
			if (line.isEmpty()) {
				continue;
			}
			lines.push_back({
				.from = item->out() ? OwnerName() : DisplayName(item->from()),
				.text = line,
			});
			if (int(lines.size()) >= kMaxContext) {
				break;
			}
		}
		if (int(lines.size()) >= kMaxContext) {
			break;
		}
	}
	result.context = { lines.rbegin(), lines.rend() };
	return result;
}

const Verdict *CachedDraft(PeerId peer, const QString &text) {
	const auto &cache = DraftState().cache;
	const auto i = cache.find(DraftKey(peer, text));
	return (i != end(cache)) ? &i->second : nullptr;
}

uint64 CheckDraft(
		not_null<History*> history,
		const QString &text,
		Fn<void(const Result&)> done) {
	auto &state = DraftState();
	const auto key = DraftKey(history->peer->id, text);
	const auto waiter = ++state.nextWaiter;
	state.waiterKeys.emplace(waiter, key);
	if (const auto cached = CachedDraft(key.first, text)) {
		auto result = Result{ .verdict = *cached };
		crl::on_main([=, done = std::move(done)] {
			if (DraftState().waiterKeys.remove(waiter)) {
				done(result);
			}
		});
		return waiter;
	}
	auto &pending = state.pending[key];
	pending.waiters.emplace_back(waiter, std::move(done));
	if (!pending.call) {
		pending.call = SharedClient().check(
			MakeRequest(history, Direction::Out, text),
			[=](Result result) { Answer(key, result); });
	}
	return waiter;
}

void CancelDraft(uint64 waiter) {
	auto &state = DraftState();
	const auto k = state.waiterKeys.find(waiter);
	if (k == end(state.waiterKeys)) {
		return;
	}
	const auto key = k->second;
	state.waiterKeys.erase(k);
	const auto i = state.pending.find(key);
	if (i == end(state.pending)) {
		return;
	}
	auto &waiters = i->second.waiters;
	waiters.erase(
		ranges::remove(waiters, waiter, &std::pair<uint64, Fn<void(const Result&)>>::first),
		end(waiters));
	if (waiters.empty()) {
		SharedClient().cancel(i->second.call);
		state.pending.erase(i);
	}
}

void MarkSentSoftened(
		not_null<History*> history,
		const QString &calm,
		const QString &original) {
	MarkSent(history->peer->id, calm, original);
}

std::optional<QString> WhatYouTyped(not_null<const HistoryItem*> item) {
	if (!item->out()) {
		return std::nullopt;
	}
	return SentOriginal(item->history()->peer->id, item->originalText().text);
}

bool ShowsLeaf(not_null<const HistoryItem*> item) {
	return item->out()
		? WhatYouTyped(item).has_value()
		: IsSoftenedIncoming(item);
}

} // namespace Safe::Calm
