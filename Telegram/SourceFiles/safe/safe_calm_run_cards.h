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
// docs/desktop/STATE-SUMMARIZE.md):
// - the hold: his texts wait while he types (safe_calm_queue); 3+ held
//   texts get the jade edge and "Send these N as one"; a tap opens the card
//   (the one message comes on the tap): Send this instead / Edit / Send as
//   typed. Ignored, they go out as typed when the hold ends.
// - a run that already went out unread: the jade edge and "Make these N one
//   message" once the runner wrote it; a tap opens the card: Send this
//   instead / Edit / Keep them. Send this instead edits his first text in
//   place and removes the others after the Undo window (safe_calm_merge).
// Edit puts the one message in his field; his next Send is the replace.
// Nothing merges without his tap, unless "Merge automatically" is on (Zach
// 10-27, off by default): then a hold that ends with 3+ texts gets its one
// message written, shown in the first held bubble with "Sent as one", a
// countdown ring and Undo for Core::kAutoMergeUndoMs, and it goes out in
// their place; Undo, or no one message in time, sends them as typed. The
// after-send offer stays tap-only.
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
		Sent,
		HeldWriting,
		Held,
	};
	struct Armed {
		bool held = false;
		std::vector<FullMsgId> items;
		std::vector<FullMsgId> texts;
	};
	struct AutoMerge {
		std::vector<FullMsgId> items;
		std::vector<FullMsgId> texts;
		QString text; // the one message, once written
	};

	void scheduleMark();
	void updateMark();
	void openSent();
	void openHeld();
	void showHeld(const QString &text);
	void sendSent(const TextWithTags &text, bool fromField);
	void editInField(const QString &text, Armed armed, const QString &note);
	void closeCard();
	void pill(const QString &text);
	[[nodiscard]] PeerId peer() const;

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
	std::optional<Armed> _armed;
	std::vector<FullMsgId> _heldItems; // the held card's messages
	std::vector<FullMsgId> _heldTexts;
	uint64 _summaryCall = 0;
	rpl::lifetime _historyLifetime;

};

} // namespace Safe::Calm
