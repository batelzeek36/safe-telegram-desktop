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

// One run of his messages that nobody has read yet (Zach 10-27: "its like 12
// messages and she feels obligated to react to every single one"; 16:08:
// "she hasnt read these three yet so it should offer to merge - it doesnt
// matter how far apart i sent them"): his own messages in a row at the
// bottom of the chat since the other side's last message, the ones already
// sent and unread and the ones the hold still keeps on his side, as one
// sequence in order, with no time limit between them (Core::ScanRun). Texts
// merge; his GIFs / stickers / photos / videos count and stay in place. With
// 3 or more texts the run gets its jade edge and leaf pill at once
// (safe_calm_run_mark); a tap writes the one message (safe_calm_run_cards).
// Nothing changes without his tap.
namespace Safe::Calm {

struct Run {
	PeerId peer = 0;
	std::vector<FullMsgId> items; // oldest first: the sent, then the held
	std::vector<RunMedia> media; // per item; None for a text
	std::vector<FullMsgId> sentItems; // nobody may have read any
	std::vector<FullMsgId> sentTexts; // oldest first: the first is edited
	std::vector<FullMsgId> heldTexts; // never go out once it is one
	MsgId anchor = 0; // the message just before the run

	[[nodiscard]] bool empty() const {
		return items.empty();
	}
	[[nodiscard]] int texts() const {
		return int(sentTexts.size() + heldTexts.size());
	}
	[[nodiscard]] bool allHeld() const {
		return sentTexts.empty();
	}
	// The texts the jade edge marks, in order.
	[[nodiscard]] std::vector<FullMsgId> allTexts() const;

	friend inline bool operator==(const Run &a, const Run &b) {
		return (a.peer == b.peer) && (a.items == b.items);
	}
};

// What a message is for a run: a text, one of his media, or it ends the run
// (a reply, a forward, a file, a voice note, a service message).
[[nodiscard]] std::optional<RunMedia> RunKindOf(
	not_null<const HistoryItem*> item);
// Unread by the people in the chat (Saved Messages has no reader: always).
// One-on-one: after her read mark; a group: read by anyone = read.
[[nodiscard]] bool RunUnread(not_null<const HistoryItem*> item);

// The run at the bottom of the chat now; nullopt while one of his is on
// its way (neither on the server nor held, nor failed): look again once it
// lands; nullopt too while the bottom of the chat is not loaded.
[[nodiscard]] std::optional<Run> CurrentRun(not_null<History*> history);

// The runner's request for these messages (oldest first) with a little of
// the chat before them.
[[nodiscard]] SummaryRequest MakeSummaryRequest(
	not_null<History*> history,
	const std::vector<FullMsgId> &items);

// Watches the open chat's run: run() is the run as it is (for the hold and
// Merge automatically), offer() the same run when it gets the pill (3+
// texts, not the set he declined). The composer shows the mark and cards.
class RunWatcher final : public base::has_weak_ptr {
public:
	explicit RunWatcher(Fn<void()> changed);
	~RunWatcher();

	void setHistory(History *history);
	// `offers`: "Too many in a row: offer one message"; `watch`: the run is
	// wanted anyway (the hold or Merge automatically need it).
	void setEnabled(bool offers, bool watch);
	[[nodiscard]] const Run *run() const;
	[[nodiscard]] const Run *offer() const;

	// Keep them / Send as typed: no pill for exactly this run.
	void keep();
	// The merge's Undo window and its work: the run stays as it is.
	void setMerging(bool merging);
	void refresh();

private:
	void schedule();
	void recompute();
	void set(std::optional<Run> run, bool offered);

	const Fn<void()> _changed;
	History *_history = nullptr;
	bool _offers = false;
	bool _watch = false;
	bool _merging = false;
	bool _scheduled = false;
	std::optional<Run> _run;
	bool _offered = false;
	base::Timer _timer;
	rpl::lifetime _historyLifetime;

};

} // namespace Safe::Calm
