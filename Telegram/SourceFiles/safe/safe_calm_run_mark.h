/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_msg_id.h"
#include "ui/click_handler.h"

class History;
class HistoryItem;
class Painter;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

// How a run of his messages that could go as one looks in the chat (Zach
// 10-27: "a UI element should appear and highlight all the messages and i
// can tap it if i want to - its not automatic"): the texts that would merge
// get a quiet jade edge (the leaf's own jade, painted under the bubble),
// and a leaf pill sits under the run's last message, in the C1 row's look
// (a service pill like "Softened · Undo"). Tapping it opens the card.
// - Sent: a run that already went out, "Make these 13 one message"; while
//   the merge's Undo window runs, the countdown ring, "Making it one" and
//   Undo.
// - Held: texts still held while he types, "Send these 4 as one".
// HistoryView::Message paints it through safe_calm_chip (no extra hooks).
namespace Safe::Calm {

struct RunMark {
	enum class Kind : uchar {
		Sent,
		Held,
	};
	Kind kind = Kind::Sent;
	std::vector<FullMsgId> texts; // edged in jade
	FullMsgId chip; // the pill goes under this one
	int count = 0; // how many texts
	crl::time holdUntil = 0; // Sent: the merge's Undo window ends
};

// One mark per chat; nullopt clears it. `open` runs on a tap of the pill,
// `undo` on its Undo while the merge's window runs.
void SetRunMark(
	not_null<History*> history,
	std::optional<RunMark> mark,
	Fn<void()> open = nullptr,
	Fn<void()> undo = nullptr);
void SetRunMarkHold(PeerId peer, crl::time until);
[[nodiscard]] const RunMark *RunMarkFor(PeerId peer);

// For safe_calm_chip: the room the pill takes under `item`, its painting
// below `top` (and the jade edge round the bubble), and its links.
[[nodiscard]] int RunChipHeight(not_null<const HistoryItem*> item);
void PaintRunEdge(
	QPainter &p,
	not_null<const HistoryView::Element*> view,
	QRect bubble);
void PaintRunChip(
	Painter &p,
	not_null<const HistoryView::Element*> view,
	QRect bubble,
	int top,
	const Ui::ChatPaintContext &context);
[[nodiscard]] ClickHandlerPtr RunChipLink(
	not_null<const HistoryView::Element*> view,
	QPoint point,
	QRect bubble,
	int top);

} // namespace Safe::Calm
