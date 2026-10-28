/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_histories.h"
#include "data/data_msg_id.h"

class History;
class HistoryItem;

// Calm's send queue (Zach 10-25: "i can keep firing messages and they
// really just go into a send queue, but from my end they just look like
// normal sends until they change"). A sent message shows at once as a
// normal bubble with the clock while its check runs; only its network send
// waits. A chat's sends leave strictly in order: anything sent behind a
// waiting message (stickers and media too) waits.
// - Autopilot: fine goes out as typed; hot flips to the calm version in
//   place, waits kHoldMs for Undo, then goes out.
// - Second look (Zach 10-27, the Android design "CAP HANDOFF 10-27"): fine
//   goes out as typed; hot holds the line behind it with his words in the
//   bubble while the Second look card asks, one card at a time, oldest
//   first (QueueCardFor).
// - A message whose check failed holds the line until Zach sends it as is,
//   deletes it, or steps it aside to let the rest go (Zach 10-25: "hold the
//   line by default, with a button to step aside").
// - The hold (Zach 10-27: "what if theres like a 10 second delay when i
//   send each message ... so the recipient doesnt see a burst and then a
//   massive edit"): with "Hold my messages while I'm typing" on, his texts
//   also wait while he keeps typing in that chat and go out ~12 s after he
//   stops (safe_calm_core.h); 3+ held texts can go as one (QueueMergeHeld).
namespace Safe::Calm {

using Builder = Fn<Data::Histories::PreparedMessage(
	not_null<History*>,
	FullReplyTo)>;

inline constexpr auto kHoldMs = crl::time(4000);

enum class QueueState : uchar {
	Checking, // the bubble is out, its check is not back yet
	Softened, // shows the calm version, goes out at `until` unless undone
	Review, // Second look: hot, waits for his pick on the card
	Waiting, // ready, waits for a message ahead of it or for the hold
	Failed, // no answer: Send as is or Delete
	Sent, // left the queue for the network
	Dropped, // deleted before it went out
};

struct QueueEvent {
	FullMsgId id;
	QueueState state = QueueState::Checking;
	crl::time until = 0; // Softened: when it goes out
	bool holding = false; // Failed: messages behind it are waiting on it
	int behind = 0; // Failed and holding: how many wait behind it
	bool aside = false; // Failed: stepped aside, the rest go on
	bool review = false; // sent in Second look (cards, not the 4 s hold)
	bool away = false; // its card was put away: the row offers Open
	bool held = false; // waits for the hold (he is still typing)
	bool fine = false; // a Second look check just came back fine
};

struct QueueSend {
	bool check = false; // ask Calm about it (Second look or Autopilot)
	bool review = false; // Second look, not Autopilot
	bool hold = false; // wait while he keeps typing
};

// The composer, on Send: the next message sent in `history` with exactly
// `text` goes through the queue. Its check starts now, before the bubble
// exists, so the context never holds the message itself.
void QueueNext(
	not_null<History*> history,
	const QString &text,
	QueueSend how);

// ApiWrap::sendMessage: how to build the same text request with other words
// (the calm version, plain, no entities).
void OfferRebuild(
	uint64 randomId,
	Fn<Builder(const QString&)> rebuild);

// Data::Histories::sendPreparedMessage: true when `send` was kept for later.
// `send` is called again with the request to make (the original `message`,
// or a rebuild with the calm version).
[[nodiscard]] bool DeferSend(
	not_null<History*> history,
	uint64 randomId,
	Builder message,
	Fn<void(Builder)> send);

// A softened message: the bubble flips back to his words and those go out.
void QueueUndo(FullMsgId id);
// A failed check: his words go out as typed (once it reaches the front), the
// rest go ahead without it, or the bubble goes.
void QueueSendAsIs(FullMsgId id);
void QueueStepAside(FullMsgId id);
void QueueDelete(FullMsgId id);

// Second look on the queue: the card that shows now in this chat, if any.
struct QueueCard {
	FullMsgId id;
	QString original;
	QString calm; // empty when the check failed
	bool failed = false;
};
[[nodiscard]] std::optional<QueueCard> QueueCardFor(PeerId peer);
// Send calm version: the bubble turns calm and goes out with those words.
void QueuePickCalm(FullMsgId id);
// Send mine: his words go out as typed.
void QueuePickMine(FullMsgId id);
// Edit: the bubble goes before anything left the Mac; his words come back.
[[nodiscard]] std::optional<QString> QueueEdit(FullMsgId id);
// Esc on the card puts it away (the row under the bubble offers Open).
void QueuePutAway(FullMsgId id, bool away);

// The hold. The composer reports his typing in the chat; a reply, a
// scheduled send, an edit, "Send now", leaving the chat or quitting
// releases the held ones at once.
void QueueTyping(PeerId peer, bool fieldBusy);
void QueueReleaseHeld(PeerId peer);
// The merge card is open: the held ones wait for his pick.
void QueueHoldForCard(PeerId peer, bool open);
// Merge automatically (Core::DecideHoldEnd): while `due` is set for a chat,
// a hold that ends with 3+ texts keeps them waiting (as for the card) and
// calls `due`, which ends with QueueMergeHeld, QueueReleaseHeld (Undo, no
// one message) or QueueHoldForCard(peer, false) (his messages changed).
void QueueSetAutoMerge(PeerId peer, Fn<void()> due);
[[nodiscard]] bool QueueHeld(not_null<const HistoryItem*> item);

// The held texts of this chat in order, with the media held among them.
struct HeldGroup {
	std::vector<FullMsgId> items; // all held, oldest first
	std::vector<FullMsgId> texts; // the texts among them
	QStringList words; // the texts' words (what he typed)
};
[[nodiscard]] HeldGroup QueueHeldGroup(PeerId peer);
// The automatic merge's Undo window: the first held text shows `text`;
// empty puts his own words back (even when the group changed). False when
// the group changed under it.
bool QueuePreviewHeld(
	PeerId peer,
	const std::vector<FullMsgId> &texts,
	const QString &text);
// Send this instead: the first held text becomes `text`, the other held
// texts go before anything left the Mac, the media stay in their place,
// and everything goes out now. False when the group changed under it.
bool QueueMergeHeld(
	PeerId peer,
	const std::vector<FullMsgId> &texts,
	const QString &text);

[[nodiscard]] rpl::producer<QueueEvent> QueueEvents(PeerId peer);
// The current state of a queued bubble, for views built after the event.
[[nodiscard]] std::optional<QueueEvent> QueueStateOf(FullMsgId id);
[[nodiscard]] bool QueueBusy(PeerId peer);

// Core::Application::preventsQuit: the held ones go now, and the app waits
// (up to a few seconds) until what was released reached the server.
[[nodiscard]] bool QueuePreventsQuit();

} // namespace Safe::Calm
