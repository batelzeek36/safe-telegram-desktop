/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/weak_ptr.h"
#include "data/data_msg_id.h"
#include "safe/safe_calm_prompt.h"
#include "safe/safe_calm_suggest_prompt.h"

class History;
class HistoryItem;

namespace Ui {
class InputField;
class RpWidget;
} // namespace Ui

namespace Safe::Calm {

class LeafButton;
class HeatLine;
class Panel;
class Ideas;
class RunCards;
struct QueueCard;

// Calm on one message composer (HistoryWidget's own and ComposeControls'):
// the leaf in the bar with its heat line and the wavy underline (B), the
// send intercept for Second look (A) and Autopilot (C), both on the send
// queue (Zach 10-27: the bubble goes out at once, Second look cards come one
// at a time, oldest first), reply ideas: the leaf with an empty field, one
// message from its menu, and Ready ideas when a message lands; and on the
// main chat's composer the hold and one message instead of many
// (safe_calm_run_cards, docs/desktop/STATE-SUMMARIZE.md).
class Composer final : public base::has_weak_ptr {
public:
	struct Args {
		not_null<Ui::RpWidget*> parent; // the panels and the heat line
		not_null<QWidget*> buttonParent; // the leaf button
		not_null<Ui::InputField*> field;
		QSize buttonSize;
		Fn<TextWithTags()> text; // what Send would send
		Fn<void(TextWithTags)> setText;
		Fn<void()> relayout; // the leaf was shown or hidden
		// The host moves the chat up by reservedHeight(); without it the
		// pieces float over the chat.
		Fn<void()> reserve;
		// HistoryWidget's composer: the hold and one message instead of
		// many (topics, replies and scheduled have neither).
		bool mainChat = false;
	};
	explicit Composer(Args &&args);
	~Composer();

	void setHistory(History *history);

	// The width the leaf takes in the bar (zero while hidden).
	[[nodiscard]] int leafWidth() const;
	void moveLeaf(int left, int top);
	// The compose bar, in the parent's coordinates.
	void setComposeRect(QRect rect);
	// The room the card, the sheet or the ideas take above the bar, so
	// nothing Calm shows covers the newest messages.
	[[nodiscard]] int reservedHeight() const;

	// Reply ideas for one message; the host put it in the reply slot.
	void suggestFor(not_null<HistoryItem*> item);

	// True when Calm took this send; `resend` runs again (and passes) once
	// Zach chose. Second look and Autopilot never take it: the send goes out
	// as a bubble at once and its network send waits in the queue
	// (safe_calm_queue). A scheduled send cannot wait there, so its Second
	// look card still holds the draft in the field.
	[[nodiscard]] bool interceptSend(
		Fn<void()> resend,
		bool scheduled = false);
	bool handleEscape();

private:
	void refreshModes();
	void textChanged();
	void checkTyping();
	void typingAnswered(const QString &text, const Result &result);
	void leafClicked();
	void showSheet(const Verdict &verdict);
	void sendAnswered(
		const TextWithTags &draft,
		const Result &result,
		Fn<void()> resend);
	void sendCalm(
		const TextWithTags &draft,
		const Verdict &verdict,
		Fn<void()> resend);
	void setHeat(int heat);
	void setUnderline(const QStringList &phrases);
	void cancelChecks();
	void closePanel();
	[[nodiscard]] QString currentText() const;

	void showIdeas(HistoryItem *replyTo, bool fresh, bool automatic);
	void ideasAnswered(const SuggestResult &result);
	void closeIdeas();
	void pickIdea(const QString &text);
	void watchLandings();
	void messageLanded(not_null<HistoryItem*> item);
	void readyCheck();
	[[nodiscard]] int computeReserve() const;
	void reserveChanged();
	void queueReserveCheck();
	void panelChanged();

	void watchQueue();
	void queueCardSync();
	void syncQueueCard();
	void showQueueCard(const QueueCard &card);
	void editQueued(FullMsgId id);
	void closeQueueCard();
	void stepBackForCard();

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Ui::InputField*> _field;
	const Fn<TextWithTags()> _text;
	const Fn<void(TextWithTags)> _setText;
	const Fn<void()> _relayout;
	const Fn<void()> _reserve;
	History *_history = nullptr;

	base::unique_qptr<LeafButton> _leaf;
	base::unique_qptr<HeatLine> _heatLine;
	base::unique_qptr<Panel> _panel;
	base::unique_qptr<Ideas> _ideas;
	std::unique_ptr<RunCards> _runs;
	QRect _composeRect;
	bool _leafShown = false;
	bool _typing = false;
	bool _suggest = false;
	bool _ready = false;

	uint64 _suggestCall = 0;
	FullMsgId _ideasFor; // the message the panel answers; empty for chips
	bool _ideasAuto = false; // Ready ideas, not asked for
	base::Timer _readyTimer;
	FullMsgId _readyWaitFor; // landed, still held by their side
	rpl::lifetime _historyLifetime;
	mutable int _appliedReserve = 0;
	bool _layingOut = false;
	bool _reserveQueued = false;

	base::Timer _typingTimer;
	uint64 _typingWaiter = 0;
	uint64 _sendWaiter = 0;
	QString _sendText;
	std::optional<Verdict> _verdict; // for the current text, from typing
	QString _verdictText;
	bool _sheetWanted = false;
	std::optional<QString> _approved;
	FullMsgId _cardFor; // the queued message whose Second look card shows
	bool _cardSyncQueued = false;
	rpl::lifetime _queueLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Safe::Calm
