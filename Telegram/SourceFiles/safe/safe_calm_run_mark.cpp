/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_run_mark.h"

#include "base/timer.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "safe/safe_calm_leaf.h"
#include "safe/safe_calm_marks.h"
#include "safe/safe_calm_queue.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "styles/style_chat.h"

namespace Safe::Calm {
namespace {

// Logical pixels, the C1 row's (safe_calm_chip.cpp, next-queue.css .qrow).
constexpr auto kAbove = 1;
constexpr auto kBelow = 6;
constexpr auto kInset = 8;
constexpr auto kLine = 30;
constexpr auto kLeaf = 13;
constexpr auto kRing = 20;
constexpr auto kGap = 7;
constexpr auto kPadLeft = 10;
constexpr auto kPadRight = 12;
constexpr auto kButtonPad = 7;
constexpr auto kEdge = 3; // the jade edge round a bubble that would merge
constexpr auto kGlow = 7;

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

struct Marked {
	base::weak_ptr<Main::Session> session;
	RunMark mark;
	Fn<void()> open;
	Fn<void()> undo;
	ClickHandlerPtr openLink;
	ClickHandlerPtr undoLink;
	std::unique_ptr<base::Timer> ring;
};

[[nodiscard]] base::flat_map<PeerId, Marked> &Marks() {
	static auto result = base::flat_map<PeerId, Marked>();
	return result;
}

[[nodiscard]] const Marked *Find(PeerId peer) {
	const auto &marks = Marks();
	const auto i = marks.find(peer);
	return (i != end(marks)) ? &i->second : nullptr;
}

[[nodiscard]] bool Holding(const RunMark &mark) {
	return (mark.kind == RunMark::Kind::Sent)
		&& (mark.holdUntil > crl::now());
}

[[nodiscard]] const style::font &TextFont() {
	return st::msgDateFont;
}

[[nodiscard]] const style::font &ButtonFont() {
	return st::semiboldFont;
}

[[nodiscard]] QString Title(const RunMark &mark) {
	if (Holding(mark)) {
		return u"Making it one"_q;
	} else if (mark.kind == RunMark::Kind::Held) {
		return u"Send these %1 as one"_q.arg(mark.count);
	}
	return u"Make these %1 one message"_q.arg(mark.count);
}

struct Layout {
	QRect pill;
	QRect undo; // empty unless the merge's window runs
	QString title;
	bool holding = false;
};

[[nodiscard]] std::optional<Layout> Compute(
		const Marked &marked,
		not_null<const HistoryView::Element*> view,
		QRect bubble,
		int top) {
	const auto &mark = marked.mark;
	if (mark.chip != view->data()->fullId()) {
		return std::nullopt;
	}
	auto result = Layout{ .title = Title(mark), .holding = Holding(mark) };
	const auto undoText = u"Undo"_q;
	const auto undoWidth = 2 * S(kButtonPad) + ButtonFont()->width(undoText);
	const auto &font = result.holding ? TextFont() : ButtonFont();
	const auto width = S(kPadLeft)
		+ (result.holding ? S(kRing) : S(kLeaf))
		+ S(kGap)
		+ font->width(result.title)
		+ (result.holding ? (S(kGap) + undoWidth + S(4)) : S(kPadRight));
	const auto outer = view->width();
	const auto left = view->hasRightLayout()
		? std::max(bubble.x() + bubble.width() - S(kInset) - width, S(kInset))
		: std::min(bubble.x() + S(kInset), std::max(outer - width, 0));
	result.pill = QRect(left, top + S(kAbove), width, S(kLine));
	if (result.holding) {
		result.undo = QRect(
			left + width - S(4) - undoWidth,
			result.pill.y(),
			undoWidth,
			S(kLine));
	}
	return result;
}

void RefreshItem(Main::Session *session, FullMsgId id, bool resize) {
	if (!session || !id) {
		return;
	}
	if (const auto item = session->data().message(id)) {
		auto &owner = item->history()->owner();
		if (resize) {
			owner.notifyItemDataChange(item);
			owner.requestItemResize(item);
		} else {
			owner.requestItemRepaint(item);
		}
	}
}

void RefreshMark(Main::Session *session, const RunMark &mark) {
	for (const auto &id : mark.texts) {
		RefreshItem(session, id, false);
	}
	RefreshItem(session, mark.chip, true);
}

[[nodiscard]] bool SameLook(const RunMark &a, const RunMark &b) {
	return (a.kind == b.kind)
		&& (a.texts == b.texts)
		&& (a.chip == b.chip)
		&& (a.count == b.count)
		&& (a.holdUntil == b.holdUntil);
}

void TickRing(PeerId peer) {
	auto &marks = Marks();
	const auto i = marks.find(peer);
	if (i == end(marks)) {
		return;
	}
	auto &marked = i->second;
	RefreshItem(marked.session.get(), marked.mark.chip, false);
	if (!Holding(marked.mark) && marked.ring) {
		marked.ring->cancel();
		// The title and the width change back.
		RefreshItem(marked.session.get(), marked.mark.chip, true);
	}
}

} // namespace

void SetRunMark(
		not_null<History*> history,
		std::optional<RunMark> mark,
		Fn<void()> open,
		Fn<void()> undo) {
	auto &marks = Marks();
	const auto peer = history->peer->id;
	const auto session = &history->session();
	const auto i = marks.find(peer);
	if (!mark) {
		if (i != end(marks)) {
			const auto old = std::move(i->second.mark);
			marks.erase(i);
			RefreshMark(session, old);
		}
		return;
	}
	if (i != end(marks)) {
		auto &marked = i->second;
		marked.open = std::move(open);
		marked.undo = std::move(undo);
		if (SameLook(marked.mark, *mark)) {
			return;
		}
		const auto old = std::exchange(marked.mark, *mark);
		RefreshMark(session, old);
		RefreshMark(session, marked.mark);
		return;
	}
	auto &marked = marks[peer];
	marked.session = base::make_weak(session);
	marked.mark = *mark;
	marked.open = std::move(open);
	marked.undo = std::move(undo);
	// Deferred: a tap may clear the mark (and this handler) right away.
	marked.openLink = std::make_shared<LambdaClickHandler>([=] {
		crl::on_main([=] {
			if (const auto found = Find(peer); found && found->open) {
				const auto callback = found->open;
				callback();
			}
		});
	});
	marked.undoLink = std::make_shared<LambdaClickHandler>([=] {
		crl::on_main([=] {
			if (const auto found = Find(peer); found && found->undo) {
				const auto callback = found->undo;
				callback();
			}
		});
	});
	RefreshMark(session, marked.mark);
}

void SetRunMarkHold(PeerId peer, crl::time until) {
	auto &marks = Marks();
	const auto i = marks.find(peer);
	if (i == end(marks) || i->second.mark.holdUntil == until) {
		return;
	}
	auto &marked = i->second;
	marked.mark.holdUntil = until;
	RefreshItem(marked.session.get(), marked.mark.chip, true);
	if (until > crl::now()) {
		if (!marked.ring) {
			marked.ring = std::make_unique<base::Timer>([=] {
				TickRing(peer);
			});
		}
		marked.ring->callEach(40);
	} else if (marked.ring) {
		marked.ring->cancel();
	}
}

const RunMark *RunMarkFor(PeerId peer) {
	const auto found = Find(peer);
	return found ? &found->mark : nullptr;
}

int RunChipHeight(not_null<const HistoryItem*> item) {
	const auto found = Find(item->history()->peer->id);
	return (found && found->mark.chip == item->fullId())
		? (S(kAbove) + S(kLine) + S(kBelow))
		: 0;
}

void PaintRunEdge(
		QPainter &p,
		not_null<const HistoryView::Element*> view,
		QRect bubble) {
	const auto item = view->data();
	const auto found = Find(item->history()->peer->id);
	if (!found || !ranges::contains(found->mark.texts, item->fullId())) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = float64(st::bubbleRadiusLarge);
	p.setPen(Qt::NoPen);
	// A soft halo, then the jade edge; the bubble paints over the middle.
	p.setBrush(JadeColor(0.10));
	const auto glowX = float64(S(kGlow));
	const auto glowY = glowX / 2.;
	const auto glow = QRectF(bubble).marginsAdded(
		QMarginsF(glowX, glowY, glowX, glowY));
	p.drawRoundedRect(glow, radius + S(kGlow), radius + S(kGlow));
	p.setBrush(JadeColor(0.42));
	const auto edgeWidth = float64(S(kEdge));
	const auto edge = QRectF(bubble).marginsAdded(
		QMarginsF(edgeWidth, edgeWidth, edgeWidth, edgeWidth));
	p.drawRoundedRect(edge, radius + S(kEdge), radius + S(kEdge));
}

void PaintRunChip(
		Painter &p,
		not_null<const HistoryView::Element*> view,
		QRect bubble,
		int top,
		const Ui::ChatPaintContext &context) {
	const auto found = Find(view->data()->history()->peer->id);
	if (!found) {
		return;
	}
	const auto layout = Compute(*found, view, bubble, top);
	if (!layout) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto pillText = context.st->msgServiceFg()->c;
	const auto &pill = layout->pill;
	const auto radius = pill.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(context.st->msgServiceBg());
	p.drawRoundedRect(pill, radius, radius);
	if (!layout->holding && ClickHandler::showAsActive(found->openLink)) {
		auto highlight = pillText;
		highlight.setAlphaF(
			ClickHandler::showAsPressed(found->openLink) ? 0.16 : 0.10);
		p.setBrush(highlight);
		p.drawRoundedRect(pill, radius, radius);
	}
	const auto jade = TickColor(pillText);
	auto x = pill.x() + S(kPadLeft);
	if (layout->holding) {
		const auto remaining = (found->mark.holdUntil - crl::now())
			/ float64(kHoldMs);
		PaintRingLeaf(
			p,
			QRectF(x, pill.y() + (pill.height() - S(kRing)) / 2., S(kRing), S(kRing)),
			std::clamp(remaining, 0., 1.));
		x += S(kRing) + S(kGap);
		p.setFont(TextFont());
		p.setPen(pillText);
		p.drawText(
			x,
			pill.y() + (pill.height() - TextFont()->height) / 2 + TextFont()->ascent,
			layout->title);
		const auto &undo = layout->undo;
		if (ClickHandler::showAsActive(found->undoLink)) {
			auto highlight = pillText;
			highlight.setAlphaF(
				ClickHandler::showAsPressed(found->undoLink) ? 0.16 : 0.10);
			const auto rect = undo.marginsRemoved({ 0, S(4), 0, S(4) });
			p.setBrush(highlight);
			p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		}
		p.setFont(ButtonFont());
		p.setPen(jade);
		p.drawText(
			undo.x() + S(kButtonPad),
			undo.y() + (undo.height() - ButtonFont()->height) / 2 + ButtonFont()->ascent,
			u"Undo"_q);
		return;
	}
	PaintLeaf(
		p,
		QRectF(x, pill.y() + (pill.height() - S(kLeaf)) / 2., S(kLeaf), S(kLeaf)),
		S(kLeaf),
		jade);
	x += S(kLeaf) + S(kGap);
	p.setFont(ButtonFont());
	p.setPen(jade);
	p.drawText(
		x,
		pill.y() + (pill.height() - ButtonFont()->height) / 2 + ButtonFont()->ascent,
		layout->title);
}

ClickHandlerPtr RunChipLink(
		not_null<const HistoryView::Element*> view,
		QPoint point,
		QRect bubble,
		int top) {
	const auto found = Find(view->data()->history()->peer->id);
	if (!found) {
		return nullptr;
	}
	const auto layout = Compute(*found, view, bubble, top);
	if (!layout || !layout->pill.contains(point)) {
		return nullptr;
	} else if (layout->holding) {
		return layout->undo.contains(point) ? found->undoLink : nullptr;
	}
	return found->openLink;
}

} // namespace Safe::Calm
