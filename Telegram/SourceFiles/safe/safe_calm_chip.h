/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/click_handler.h"

class HistoryItem;
class Painter;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

// C1, the row under a queued bubble (Zach 10-25: "I also like C1 inline for
// the softened indicator"; docs/previews/calm/css/next-queue.css .qrow).
// A pill right under the bubble it belongs to, in the message list:
// Softened = the countdown ring round the leaf, "Softened", Undo.
// Failed = "Couldn't check", Send as is, Delete, and while others wait on it
// a second line: "N waiting behind it", Let the rest go.
// Second look (Zach 10-27, on the queue) = the leaf and "Second look" while
// its card asks above the composer; "Open" once he put the card away.
// Below it, a run that could go as one gets its leaf pill and the texts their
// jade edge (safe_calm_run_mark).
// HistoryView::Message adds its height below the bubble, paints it and asks
// it for links (hooks in docs/desktop/d24-calm.py).
namespace Safe::Calm {

// The room the row takes under the bubble, zero when it shows none.
[[nodiscard]] int QueueChipHeight(not_null<const HistoryItem*> item);

void PaintQueueChip(
	Painter &p,
	not_null<const HistoryView::Element*> view,
	QRect bubble,
	const Ui::ChatPaintContext &context);

// The same handler objects between calls, while the bubble is queued.
[[nodiscard]] ClickHandlerPtr QueueChipLink(
	not_null<const HistoryView::Element*> view,
	QPoint point,
	QRect bubble);

// BottomInfo: the failed mark takes the clock's place by the time.
[[nodiscard]] bool QueueShowsFailed(not_null<const HistoryItem*> item);

} // namespace Safe::Calm
