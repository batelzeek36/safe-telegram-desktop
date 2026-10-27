/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"

class History;
class HistoryItem;

// One message instead of a run that already went out (Zach 10-27: "OR it
// could edit the first message in place, thats probably better huh"):
// after an Undo window like Autopilot's (kHoldMs, the ring and Undo on the
// run's pill), and only if nobody has read any of the run, his first text
// of the run is edited into the one message and his other texts of the run
// are deleted for everyone. His GIFs, stickers and photos stay where they
// are. If the first text cannot be edited, the texts are deleted and the one
// message is sent after the run instead. Anything read, or anything Telegram
// will not delete for everyone, and nothing changes (a toast says why).
namespace Safe::Calm {

struct SentMerge {
	std::vector<FullMsgId> items; // the whole run: nobody may have read any
	std::vector<FullMsgId> texts; // oldest first: the first is edited
	TextWithTags text;
	bool fromField = false; // his own edit: Undo or an abort gives it back
};

struct MergeHooks {
	Fn<void(QString)> toast; // why nothing changed
	Fn<void(TextWithTags)> restore; // his edited words back in the field
	Fn<void()> finished; // done, undone or aborted
};

void StartSentMerge(
	not_null<History*> history,
	SentMerge merge,
	MergeHooks hooks);
void UndoSentMerge(PeerId peer);
[[nodiscard]] bool SentMerging(PeerId peer);

// What the card can promise.
[[nodiscard]] bool CanEditInPlace(not_null<HistoryItem*> item);
[[nodiscard]] bool CanRemoveForAll(not_null<HistoryItem*> item);

} // namespace Safe::Calm
