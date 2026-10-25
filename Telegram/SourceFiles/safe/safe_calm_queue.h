/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_histories.h"
#include "data/data_msg_id.h"

class History;

// Autopilot (C) as a send queue (Zach 10-25: "i can keep firing messages and
// they really just go into a send queue, but from my end they just look like
// normal sends until they change"). A sent message shows at once as a normal
// bubble with the clock while its check runs; only its network send waits.
// Fine goes out as typed. Hot flips to the calm version in place, waits
// kHoldMs for Undo, then goes out. A chat's sends leave strictly in order:
// anything sent behind a waiting message (stickers and media too) waits.
// A message whose check failed holds the line until Zach sends it as is,
// deletes it, or steps it aside to let the rest go (Zach 10-25: "hold the
// line by default, with a button to step aside").
namespace Safe::Calm {

using Builder = Fn<Data::Histories::PreparedMessage(
	not_null<History*>,
	FullReplyTo)>;

inline constexpr auto kHoldMs = crl::time(4000);

enum class QueueState : uchar {
	Checking, // the bubble is out, its check is not back yet
	Softened, // shows the calm version, goes out at `until` unless undone
	Waiting, // ready, waits for a message ahead of it
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
};

// The composer, on Send in Autopilot: the next message sent in `history`
// with exactly `text` goes through the queue. Its check starts now, before
// the bubble exists, so the context never holds the message itself.
void QueueNext(not_null<History*> history, const QString &text);

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

[[nodiscard]] rpl::producer<QueueEvent> QueueEvents(PeerId peer);
// The current state of a queued bubble, for views built after the event.
[[nodiscard]] std::optional<QueueEvent> QueueStateOf(FullMsgId id);

} // namespace Safe::Calm
