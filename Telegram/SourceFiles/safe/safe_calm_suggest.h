/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_calm_suggest_prompt.h"

class History;
class HistoryItem;

// Reply suggestions for a chat (safe_calm_suggest_prompt for what is asked).
namespace Safe::Calm {

// Three replies Zach could send next in `history`, or to `replyTo` when he
// picked one message. An answer for the same chat state comes from memory
// unless `fresh` ("More"); one made while a voice note was still being
// transcribed is never remembered, nor one asked with `thought` (his note for
// the model, which goes with this request and is kept nowhere else).
// `listening` gets how many voice notes
// Telegram is still transcribing first (0 when that part is over). `done`
// runs once on the main thread unless CancelSuggest() comes first.
uint64 Suggest(
	not_null<History*> history,
	HistoryItem *replyTo,
	bool fresh,
	QString thought,
	Fn<void(int)> listening,
	Fn<void(const SuggestResult&)> done);
void CancelSuggest(uint64 id);

} // namespace Safe::Calm
