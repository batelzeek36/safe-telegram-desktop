/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"
#include "safe/safe_calm_prompt.h"
#include "safe/safe_calm_settings.h"

class History;
class HistoryItem;

// Calm mode, the message softener (docs/CALM.md): every message Zach is
// about to send can be checked by Sonnet 5.5 (the runner on MAINGEAR, then
// the API) and softened, and messages sent to him can arrive softened.
// The composer pieces live in safe_calm_composer, the panels in
// safe_calm_panels, the settings box in safe_calm_box.
namespace Safe::Calm {

// A draft worth asking about: it has letters, is not a /command, and fits.
[[nodiscard]] bool Checkable(const QString &text);

// Zach's own message. Not just out(): Telegram never flags messages in
// Saved Messages as outgoing (NewMessageFlags), so there his own words count
// as his unless they are a forward of someone else's.
[[nodiscard]] bool IsOwn(not_null<const HistoryItem*> item);

// The request for `text` in `history`: names and the last kMaxContext
// messages loaded before `before` (all of them when it is zero), each with
// its age, for tone.
[[nodiscard]] Request MakeRequest(
	not_null<History*> history,
	Direction direction,
	const QString &text,
	MsgId before = 0);

// Drafts: an answer for exactly this text in this chat, if one came back.
[[nodiscard]] const Verdict *CachedDraft(PeerId peer, const QString &text);

// Asks about `text` as Zach's next message in `history`, or joins the
// question already on its way; `done` runs once on the main thread (at once,
// queued, when the answer is cached) unless CancelDraft() comes first.
uint64 CheckDraft(
	not_null<History*> history,
	const QString &text,
	Fn<void(const Result&)> done);
void CancelDraft(uint64 waiter);
// A calm version Zach took ("Use this") reads as fine without asking again.
void RememberFine(PeerId peer, const QString &text);

// Softened messages Zach sent: marked when the calm version goes out.
void MarkSentSoftened(
	not_null<History*> history,
	const QString &calm,
	const QString &original);
[[nodiscard]] std::optional<QString> WhatYouTyped(
	not_null<const HistoryItem*> item);

// The small leaf by the time: his softened messages and softened ones to him.
[[nodiscard]] bool ShowsLeaf(not_null<const HistoryItem*> item);

// Their side (D), from HistoryItem::setTextValue: what a message sent to
// Zach shows. A message to check shows a placeholder until its verdict, then
// the calm version or the original; the original's words never show.
[[nodiscard]] TextWithEntities FilterIncoming(
	not_null<HistoryItem*> item,
	TextWithEntities text);
// HistoryItem::notificationReady: no notification until the verdict.
[[nodiscard]] bool HoldsNotification(not_null<const HistoryItem*> item);
[[nodiscard]] bool IsHeld(not_null<const HistoryItem*> item);
// Message::paintText: a bubble still being softened paints a soft shimmer
// bar in place of its placeholder text. True when it painted (skip the text).
bool PaintHeldText(
	QPainter &p,
	not_null<const HistoryItem*> item,
	QRect textRect,
	int textWidth,
	int lineHeight,
	const QColor &color);
// A softened message to Zach: never forwarded, translated or copied raw.
[[nodiscard]] bool IsSoftenedIncoming(not_null<const HistoryItem*> item);
// What a softened message to Zach said before it was softened, for the
// private peek in the quiet chats only (safe_peek.h, d32: Zach 2026-10-26
// "id like to be able to tap something to see what jade or my mom said",
// family only). Empty when the message is not softened.
[[nodiscard]] std::optional<TextWithEntities> SoftenedOriginal(
	not_null<const HistoryItem*> item);

} // namespace Safe::Calm
