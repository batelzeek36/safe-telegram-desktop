/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "safe/safe_calm_summary.h"

class History;
class HistoryItem;

// A run of his messages that already went out and that nobody has read
// (Zach 10-27: "its like 12 messages and she feels obligated to react to
// every single one"): 5 or more in a row at the bottom of the chat, nothing
// from anyone else between, the oldest under 30 minutes old, his texts and
// his GIFs / stickers / photos / videos (Core::ScanRun). After a 20 s pause
// the runner writes the one message; only then does the run get its jade
// edge and leaf pill (safe_calm_run_mark). Nothing changes without his tap.
namespace Safe::Calm {

struct SentRun {
	PeerId peer = 0;
	std::vector<FullMsgId> items; // oldest first
	std::vector<FullMsgId> texts;
	std::vector<RunMedia> media; // per item; None for a text
	MsgId first = 0;

	[[nodiscard]] bool empty() const {
		return items.empty();
	}
	friend inline bool operator==(const SentRun &a, const SentRun &b) {
		return (a.peer == b.peer) && (a.items == b.items);
	}
};

// What a message is for a run: a text, one of his media, or it ends the run.
[[nodiscard]] std::optional<RunMedia> RunKindOf(
	not_null<const HistoryItem*> item);
// Unread by the people in the chat (Saved Messages has no reader: always).
[[nodiscard]] bool RunUnread(not_null<const HistoryItem*> item);

[[nodiscard]] SentRun CurrentRun(not_null<History*> history);

// The runner's request for these messages (oldest first) with a little of
// the chat before them.
[[nodiscard]] SummaryRequest MakeSummaryRequest(
	not_null<History*> history,
	const std::vector<FullMsgId> &items);

// Watches the open chat for a run worth offering and asks the runner for
// its one message; offer() is set once the runner answered for the run as
// it is now. The composer shows the mark and the card.
class RunWatcher final : public base::has_weak_ptr {
public:
	struct Offer {
		SentRun run;
		QString text; // the one message
	};

	explicit RunWatcher(Fn<void()> changed);
	~RunWatcher();

	void setHistory(History *history);
	void setEnabled(bool enabled);
	[[nodiscard]] const Offer *offer() const;

	// Keep them: no new offer until the run breaks or grows by 5.
	void keep();
	// The merge's Undo window and its work: the offer stays as it is.
	void setMerging(bool merging);
	void refresh();

private:
	void schedule();
	void recompute();
	void ask(const SentRun &run);
	void setOffer(std::optional<Offer> offer);

	const Fn<void()> _changed;
	History *_history = nullptr;
	bool _enabled = false;
	bool _merging = false;
	bool _scheduled = false;
	std::optional<Offer> _offer;
	uint64 _call = 0;
	SentRun _asking;
	base::Timer _timer;
	rpl::lifetime _historyLifetime;

};

} // namespace Safe::Calm
