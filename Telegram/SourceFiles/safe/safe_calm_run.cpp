/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_run.h"

#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "safe/safe_calm.h"
#include "safe/safe_calm_client.h"
#include "safe/safe_calm_core.h"
#include "safe/safe_calm_shared.h"

namespace Safe::Calm {
namespace {

// Looks again this often while a chat is open (loads, the 30 min window).
constexpr auto kRecheck = crl::time(10000);

struct Answered {
	std::vector<FullMsgId> items;
	QString text; // empty: the runner could not answer for this run
};

struct PeerRuns {
	std::optional<Core::Kept> kept;
	std::optional<Answered> answered;
};

[[nodiscard]] base::flat_map<PeerId, PeerRuns> &Runs() {
	static auto result = base::flat_map<PeerId, PeerRuns>();
	return result;
}

[[nodiscard]] Core::RunKind CoreKind(std::optional<RunMedia> kind) {
	return !kind
		? Core::RunKind::Breaks
		: (*kind == RunMedia::None)
		? Core::RunKind::Text
		: Core::RunKind::Media;
}

} // namespace

std::optional<RunMedia> RunKindOf(not_null<const HistoryItem*> item) {
	if (item->isService()
		|| item->Has<HistoryMessageForwarded>()
		|| item->Has<HistoryMessageVia>()) {
		return std::nullopt;
	}
	if (const auto reply = item->Get<HistoryMessageReply>()) {
		// A reply to someone else's message is not part of a run.
		const auto to = reply->resolvedMessage.get();
		if (reply->externalPeerId() || !to || !IsOwn(to)) {
			return std::nullopt;
		}
	}
	const auto media = item->media();
	if (!media || media->webpage()) {
		return item->originalText().text.trimmed().isEmpty()
			? std::nullopt
			: std::make_optional(RunMedia::None);
	} else if (const auto document = media->document()) {
		if (document->sticker()) {
			return RunMedia::Sticker;
		} else if (document->isAnimation() || document->isGifv()) {
			return RunMedia::Gif;
		} else if (document->isVideoFile() && !document->isVideoMessage()) {
			return RunMedia::Video;
		}
		return std::nullopt;
	} else if (media->photo()) {
		return RunMedia::Photo;
	}
	return std::nullopt;
}

bool RunUnread(not_null<const HistoryItem*> item) {
	const auto history = item->history();
	return history->peer->isSelf() || item->unread(history);
}

SentRun CurrentRun(not_null<History*> history) {
	auto result = SentRun{ .peer = history->peer->id };
	if (!history->loadedAtBottom()) {
		return result;
	}
	auto entries = std::vector<Core::RunEntry>();
	auto items = std::vector<not_null<HistoryItem*>>();
	auto kinds = std::vector<RunMedia>();
	const auto enough = Core::kMaxRun + 1;
	for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			const auto item = (*m)->data();
			const auto kind = RunKindOf(item);
			entries.push_back({
				.kind = CoreKind(kind),
				.own = IsOwn(item),
				.sent = item->isRegular(),
				.unread = RunUnread(item),
				.date = item->date(),
				.id = item->id.bare,
			});
			items.push_back(item);
			kinds.push_back(kind.value_or(RunMedia::None));
			if (entries.back().kind == Core::RunKind::Breaks
				|| !entries.back().own
				|| int(entries.size()) >= enough) {
				break;
			}
		}
		if (!entries.empty()
			&& (entries.back().kind == Core::RunKind::Breaks
				|| !entries.back().own
				|| int(entries.size()) >= enough)) {
			break;
		}
	}
	const auto scan = Core::ScanRun(entries);
	for (auto i = scan.count; i != 0;) {
		--i;
		const auto id = items[i]->fullId();
		result.items.push_back(id);
		result.media.push_back(kinds[i]);
		if (kinds[i] == RunMedia::None) {
			result.texts.push_back(id);
		}
	}
	if (scan.count) {
		result.first = items[scan.count - 1]->id;
	}
	return result;
}

SummaryRequest MakeSummaryRequest(
		not_null<History*> history,
		const std::vector<FullMsgId> &items) {
	auto &owner = history->owner();
	auto first = MsgId();
	for (const auto &id : items) {
		const auto item = owner.message(id);
		if (item && item->isRegular()) {
			first = item->id;
			break;
		}
	}
	// Held bubbles are not on the server yet: everything regular is before
	// them, so the context is the chat as it is.
	const auto base = MakeRequest(
		history,
		Direction::Out,
		u"-"_q,
		first ? first : ServerMaxMsgId);
	auto result = SummaryRequest{
		.recipient = base.recipient,
		.chat = base.chat,
	};
	const auto from = std::max(int(base.context.size()) - kSummaryContext, 0);
	result.context.assign(base.context.begin() + from, base.context.end());
	const auto now = base::unixtime::now();
	for (const auto &id : items) {
		const auto item = owner.message(id);
		if (!item) {
			continue;
		}
		const auto kind = RunKindOf(item).value_or(RunMedia::None);
		auto line = RunLine{
			.media = kind,
			.ago = AgoText(now - item->date()),
		};
		const auto text = item->originalText().text.trimmed();
		if (kind == RunMedia::None) {
			if (text.isEmpty()) {
				continue;
			}
			line.text = text;
		} else {
			line.caption = text;
		}
		result.run.push_back(std::move(line));
	}
	return result;
}

RunWatcher::RunWatcher(Fn<void()> changed)
: _changed(std::move(changed))
, _timer([=] { recompute(); }) {
}

RunWatcher::~RunWatcher() {
	SharedClient().cancel(base::take(_call));
}

void RunWatcher::setHistory(History *history) {
	if (_history == history) {
		return;
	}
	SharedClient().cancel(base::take(_call));
	_asking = SentRun();
	_historyLifetime.destroy();
	_timer.cancel();
	_merging = false;
	_history = history;
	setOffer(std::nullopt);
	if (!_history) {
		return;
	}
	const auto watched = _history;
	auto &owner = watched->owner();
	owner.newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return (item->history() == watched);
	}) | rpl::on_next([=] {
		schedule();
	}, _historyLifetime);
	owner.itemIdChanged(
	) | rpl::filter([=](const Data::Session::IdChange &change) {
		return (change.newId.peer == watched->peer->id);
	}) | rpl::on_next([=] {
		schedule();
	}, _historyLifetime);
	owner.itemRemoved(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (item->history() == watched);
	}) | rpl::on_next([=] {
		schedule();
	}, _historyLifetime);
	watched->session().changes().historyUpdates(
		watched,
		Data::HistoryUpdate::Flag::OutboxRead
	) | rpl::on_next([=] {
		schedule();
	}, _historyLifetime);
	schedule();
}

void RunWatcher::setEnabled(bool enabled) {
	if (_enabled != enabled) {
		_enabled = enabled;
		schedule();
	}
}

const RunWatcher::Offer *RunWatcher::offer() const {
	return _offer ? &*_offer : nullptr;
}

void RunWatcher::keep() {
	if (!_offer || !_history) {
		return;
	}
	auto &runs = Runs()[_offer->run.peer];
	runs.kept = Core::Kept{
		.first = _offer->run.first.bare,
		.count = int(_offer->run.items.size()),
	};
	setOffer(std::nullopt);
}

void RunWatcher::setMerging(bool merging) {
	if (_merging != merging) {
		_merging = merging;
		if (!merging) {
			setOffer(std::nullopt);
			schedule();
		}
	}
}

void RunWatcher::refresh() {
	schedule();
}

void RunWatcher::schedule() {
	if (_scheduled) {
		return;
	}
	_scheduled = true;
	crl::on_main(this, [=] {
		_scheduled = false;
		recompute();
	});
}

void RunWatcher::recompute() {
	if (!_history || !_enabled) {
		SharedClient().cancel(base::take(_call));
		setOffer(std::nullopt);
		return;
	} else if (_merging) {
		return;
	}
	_timer.callOnce(kRecheck);
	const auto run = CurrentRun(_history);
	if (_offer && _offer->run == run) {
		return; // shown: stays until the run breaks, Keep or the merge
	}
	auto &runs = Runs()[run.peer];
	if (runs.kept && (run.empty() || runs.kept->first != run.first.bare)) {
		runs.kept.reset(); // the run he kept broke
	}
	auto scan = Core::RunScan{
		.count = int(run.items.size()),
		.texts = int(run.texts.size()),
		.first = run.first.bare,
	};
	auto &owner = _history->owner();
	if (!run.empty()) {
		const auto oldest = owner.message(run.items.front());
		const auto newest = owner.message(run.items.back());
		scan.oldest = oldest ? oldest->date() : 0;
		scan.newest = newest ? newest->date() : 0;
	}
	const auto kept = runs.kept ? &*runs.kept : nullptr;
	const auto decision = Core::DecideOffer(
		scan,
		base::unixtime::now(),
		kept);
	switch (decision.offer) {
	case Core::Offer::None:
		SharedClient().cancel(base::take(_call));
		setOffer(std::nullopt);
		return;
	case Core::Offer::Later:
		setOffer(std::nullopt);
		_timer.callOnce(std::clamp(
			crl::time(decision.retryIn * 1000),
			crl::time(500),
			kRecheck));
		return;
	case Core::Offer::Ask:
		break;
	}
	if (runs.answered && runs.answered->items == run.items) {
		if (runs.answered->text.isEmpty()) {
			setOffer(std::nullopt); // no answer for this run: no offer
		} else {
			setOffer(Offer{ .run = run, .text = runs.answered->text });
		}
		return;
	}
	setOffer(std::nullopt);
	if (!_call || !(_asking == run)) {
		ask(run);
	}
}

void RunWatcher::ask(const SentRun &run) {
	SharedClient().cancel(base::take(_call));
	_asking = run;
	const auto peer = run.peer;
	const auto items = run.items;
	_call = SharedClient().summarize(
		MakeSummaryRequest(_history, items),
		crl::guard(this, [=](SummaryResult result) {
			_call = 0;
			if (result.text.isEmpty()) {
				LOG(("Safe Calm: no one message for a run (%1)."
					).arg(result.failure));
			}
			Runs()[peer].answered = Answered{
				.items = items,
				.text = result.text,
			};
			schedule();
		}));
}

void RunWatcher::setOffer(std::optional<Offer> offer) {
	if (!_offer && !offer) {
		return;
	} else if (_offer
		&& offer
		&& _offer->run == offer->run
		&& _offer->text == offer->text) {
		return;
	}
	_offer = std::move(offer);
	if (_changed) {
		_changed();
	}
}

} // namespace Safe::Calm
