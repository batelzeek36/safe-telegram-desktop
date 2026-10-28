/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "safe/safe_calm_run.h"

class History;

namespace Safe::Calm {

class Panel;

// One message instead of many, on the main chat's composer (Zach 10-27,
// docs/desktop/STATE-SUMMARIZE.md). One run (safe_calm_run.h): his texts in
// a row that nobody has read, the ones the hold keeps (safe_calm_queue) and
// the ones already sent, with no time limit between them. With 3+ texts the
// whole run gets the jade edge and the pill ("Send these N as one" while all
// are held, "Make these N one message" once any went out); a tap writes the
// one message and opens the card:
// - all held: Send this instead (the first held text becomes the one
//   message, the others never go out, at once) / Edit / Send as typed.
// - with sent texts: Send this instead, after the Undo window
//   (safe_calm_merge): his first sent text is edited in place, his other
//   sent texts are deleted for everyone and the held texts never go out;
//   anything read meanwhile and nothing changes / Edit / Keep them.
// The card's quiet line says exactly that. Edit puts the one message in his
// field; his next Send is the replace. Keep them (or Send as typed) hides
// the pill for exactly that run; any other set brings it back. Nothing
// merges without his tap, unless "Merge automatically" is on (Zach 10-27,
// off by default): then a hold that ends with 3+ texts held and none of the
// run sent gets its one message written, shown in the first held bubble
// with "Sent as one", a countdown ring and Undo for Core::kAutoMergeUndoMs,
// and it goes out in their place; Undo, or no one message in time, sends
// them as typed. A run with sent texts always waits for his tap.
class RunCards final : public base::has_weak_ptr {
public:
	struct Args {
		not_null<Panel*> panel;
		Fn<TextWithTags()> text;
		Fn<void(TextWithTags)> setText;
		Fn<void()> focus;
		Fn<void()> takePanel; // the card is about to show: others step back
		Fn<void()> panelFree; // the card closed: others may show again
	};
	explicit RunCards(Args &&args);
	~RunCards();

	void setHistory(History *history);
	void setModes(bool merge, bool hold, bool autoMerge);

	// The composer's Send: true when this send was the replace he armed with
	// Edit (the field is cleared).
	[[nodiscard]] bool interceptSend(const TextWithTags &draft);
	// His field changed (the hold waits while he types).
	void fieldChanged(bool busy);
	bool handleEscape();
	// The panel closed or showed something else.
	void panelChanged();
	[[nodiscard]] bool cardShown() const;

private:
	enum class Card : uchar {
		None,
		Writing,
		Shown,
	};
	struct AutoMerge {
		std::vector<FullMsgId> items;
		std::vector<FullMsgId> texts;
		QString text; // the one message, once written
	};

	void scheduleMark();
	void updateMark();
	void syncQueueRun();
	void openRun();
	void showRun(const QString &text);
	void sendSent(const TextWithTags &text, const Run &run, bool fromField);
	void declineRun();
	void editInField(const QString &text, Run run, const QString &note);
	// `keepHeld`: the held texts stay held for the merge's Undo window.
	void closeCard(bool keepHeld = false);
	void pill(const QString &text);
	[[nodiscard]] PeerId peer() const;
	[[nodiscard]] QString cardLine(const Run &run) const;
	// "Amberelle hasn't read them yet." (or "Nobody ..." in a group).
	[[nodiscard]] QString readerLine(const Run &run) const;
	// "Amberelle read some of them" when she did, else empty.
	[[nodiscard]] QString readSome(const Run &run) const;

	// Merge automatically.
	void syncAutoHook();
	void startAuto();
	void autoWritten(const QString &text);
	void autoTimer();
	void commitAuto();
	void undoAuto();
	void autoFailed(const QString &why);
	// The automatic merge stops: his words come back in the first bubble;
	// `resumeHold` = the hold goes on (his messages changed).
	void cancelAuto(bool resumeHold);

	const not_null<Panel*> _panel;
	const Fn<TextWithTags()> _text;
	const Fn<void(TextWithTags)> _setText;
	const Fn<void()> _focus;
	const Fn<void()> _takePanel;
	const Fn<void()> _panelFree;
	RunWatcher _watcher;
	History *_history = nullptr;
	bool _merge = false;
	bool _hold = false;
	bool _autoMerge = false;
	PeerId _autoHook = 0; // the chat whose hold ends call startAuto()
	std::optional<AutoMerge> _auto;
	uint64 _autoCall = 0;
	base::Timer _autoTimer; // the one message is late, or Undo ran out
	bool _markQueued = false;
	Card _card = Card::None;
	std::optional<Run> _cardRun; // the run the card is about
	std::optional<Run> _armed; // Edit: his next Send replaces this run
	uint64 _summaryCall = 0;
	rpl::lifetime _historyLifetime;

};

} // namespace Safe::Calm
