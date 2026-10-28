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
#include "safe/safe_calm_core.h"
#include "safe/safe_calm_queue.h"
#include "safe/safe_calm_shared.h"

namespace Safe::Calm {
namespace {

// Looks again this often while a chat is open (history loads at the bottom).
constexpr auto kRecheck = crl::time(10000);

struct PeerRuns {
	std::optional<Core::Kept> kept;
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

[[nodiscard]] Core::RunScan ScanOf(const Run &run) {
	return {
		.count = int(run.items.size()),
		.texts = run.texts(),
		.heldTexts = int(run.heldTexts.size()),
		.anchor = run.anchor.bare,
	};
}

} // namespace

std::vector<FullMsgId> Run::allTexts() const {
	auto result = sentTexts;
	result.insert(end(result), begin(heldTexts), end(heldTexts));
	return result;
}

std::optional<RunMedia> RunKindOf(not_null<const HistoryItem*> item) {
	if (item->isService()
		|| item->Has<HistoryMessageForwarded>()
		|| item->Has<HistoryMessageVia>()
		|| item->Has<HistoryMessageReply>()) {
		// Replies stay out of runs (Zach 10-27): their quote stays attached.
		return std::nullopt;
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

std::optional<Run> CurrentRun(not_null<History*> history) {
	if (!history->loadedAtBottom()) {
		// The bottom is not in memory (he jumped far up): unknown, so the
		// run stays as it was (and what he declined stays declined).
		return std::nullopt;
	}
	auto result = Run{ .peer = history->peer->id };
	auto entries = std::vector<Core::RunEntry>();
	auto items = std::vector<not_null<HistoryItem*>>();
	auto kinds = std::vector<RunMedia>();
	const auto enough = Core::kMaxRun + 1;
	// Everything the scan may need: up to the first message that ends it.
	const auto ends = [](const Core::RunEntry &entry) {
		return !entry.own
			|| entry.kind == Core::RunKind::Breaks
			|| entry.failed
			|| (entry.sent && !entry.unread);
	};
	auto done = false;
	for (auto b = history->blocks.rbegin(); !done && b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			const auto item = (*m)->data();
			const auto kind = RunKindOf(item);
			const auto sent = item->isRegular();
			const auto held = !sent && QueueHeld(item);
			entries.push_back({
				.kind = CoreKind(kind),
				.own = IsOwn(item),
				.sent = sent,
				.held = held,
				.unread = sent && RunUnread(item),
				.failed = !sent && !held && item->hasFailed(),
				.id = item->id.bare,
			});
			items.push_back(item);
			kinds.push_back(kind.value_or(RunMedia::None));
			if (ends(entries.back()) || int(entries.size()) >= enough) {
				done = true;
				break;
			}
		}
	}
	const auto scan = Core::ScanRun(entries);
	if (scan.settling) {
		return std::nullopt;
	}
	result.anchor = MsgId(scan.anchor);
	for (auto i = scan.count; i != 0;) {
		--i;
		const auto id = items[i]->fullId();
		const auto held = entries[i].held;
		result.items.push_back(id);
		result.media.push_back(kinds[i]);
		if (!held) {
			result.sentItems.push_back(id);
		}
		if (kinds[i] == RunMedia::None) {
			(held ? result.heldTexts : result.sentTexts).push_back(id);
		}
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

RunWatcher::~RunWatcher() = default;

void RunWatcher::setHistory(History *history) {
	if (_history == history) {
		return;
	}
	_historyLifetime.destroy();
	_timer.cancel();
	_merging = false;
	_history = history;
	set(std::nullopt, false);
	if (!_history) {
		return;
	}
	const auto watched = _history;
	const auto peer = watched->peer->id;
	auto &owner = watched->owner();
	owner.newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return (item->history() == watched);
	}) | rpl::on_next([=] {
		schedule();
	}, _historyLifetime);
	owner.itemIdChanged(
	) | rpl::filter([=](const Data::Session::IdChange &change) {
		return (change.newId.peer == peer);
	}) | rpl::on_next([=] {
		schedule();
	}, _historyLifetime);
	owner.itemRemoved(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (item->history() == watched);
	}) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		if (IsOwn(item)) {
			// One of his went: the set he declined is not there any more.
			Runs()[peer].kept.reset();
		}
		schedule();
	}, _historyLifetime);
	watched->session().changes().historyUpdates(
		watched,
		Data::HistoryUpdate::Flag::OutboxRead
	) | rpl::on_next([=] {
		schedule();
	}, _historyLifetime);
	QueueEvents(peer) | rpl::on_next([=] {
		schedule(); // held, released, merged or dropped
	}, _historyLifetime);
	schedule();
}

void RunWatcher::setEnabled(bool offers, bool watch) {
	watch = watch || offers;
	if (_offers != offers || _watch != watch) {
		_offers = offers;
		_watch = watch;
		schedule();
	}
}

const Run *RunWatcher::run() const {
	return _run ? &*_run : nullptr;
}

const Run *RunWatcher::offer() const {
	return (_run && _offered) ? &*_run : nullptr;
}

void RunWatcher::keep() {
	if (!_run) {
		return;
	}
	Runs()[_run->peer].kept = Core::Kept{
		.anchor = _run->anchor.bare,
		.texts = _run->texts(),
	};
	set(_run, false);
}

void RunWatcher::setMerging(bool merging) {
	if (_merging != merging) {
		_merging = merging;
		if (!merging) {
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
	if (!_history || !_watch) {
		set(std::nullopt, false);
		return;
	} else if (_merging) {
		return; // the merge's Undo window: as it is
	}
	_timer.callOnce(kRecheck);
	const auto run = CurrentRun(_history);
	if (!run) {
		return; // one of his on its way, or no bottom: as it is for now
	}
	const auto scan = ScanOf(*run);
	auto &kept = Runs()[run->peer].kept;
	if (kept && (kept->anchor != scan.anchor || kept->texts != scan.texts)) {
		kept.reset(); // the run changed: what he declined is gone
	}
	const auto offered = _offers && Core::OfferRun(scan, kept ? &*kept : nullptr);
	set(run->empty() ? std::nullopt : run, offered);
}

void RunWatcher::set(std::optional<Run> run, bool offered) {
	const auto same = (_run.has_value() == run.has_value())
		&& (!run
			|| ((*_run == *run)
				&& (_run->anchor == run->anchor)
				&& (_run->heldTexts == run->heldTexts)));
	if (same && _offered == offered) {
		return;
	}
	_run = std::move(run);
	_offered = _run && offered;
	if (_changed) {
		_changed();
	}
}

} // namespace Safe::Calm
