/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

// What Calm remembers across restarts, in safe_calm_marks.json (0600):
// the softened messages Zach sent (by chat and the calm text, with what he
// typed, for the leaf by the time and "What you typed") and the verdicts on
// messages sent to him (by chat and id, with the hash of the original), so a
// restart shows the calm version at once and never re-asks.
namespace Safe::Calm {

struct IncomingRecord {
	uint64 hash = 0; // TextHash() of the original
	bool soften = false;
	QString calm;
	TimeId at = 0;
};

[[nodiscard]] uint64 TextHash(const QString &text);

void MarkSent(PeerId peer, const QString &calm, const QString &original);
[[nodiscard]] std::optional<QString> SentOriginal(
	PeerId peer,
	const QString &shown);

[[nodiscard]] const IncomingRecord *Incoming(FullMsgId id);
void SetIncoming(FullMsgId id, IncomingRecord record);

} // namespace Safe::Calm
