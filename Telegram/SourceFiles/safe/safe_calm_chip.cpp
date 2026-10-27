/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_chip.h"

#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "safe/safe_calm_leaf.h"
#include "safe/safe_calm_marks.h"
#include "safe/safe_calm_queue.h"
#include "safe/safe_calm_run_mark.h"
#include "ui/chat/chat_style.h"
#include "ui/painter.h"
#include "styles/style_chat.h"

namespace Safe::Calm {
namespace {

// Logical pixels, after next-queue.css (.qrow, .qfail, .ring-leaf).
constexpr auto kAbove = 1;
constexpr auto kBelow = 6;
constexpr auto kInset = 8; // in from the bubble's edge
constexpr auto kLine = 30;
constexpr auto kWaitLine = 26;
constexpr auto kTwoLinesRadius = 14;
constexpr auto kRing = 20;
constexpr auto kBang = 15;
constexpr auto kClock = 13;
constexpr auto kGap = 7;
constexpr auto kButtonPad = 7;
constexpr auto kPadRight = 4;
constexpr auto kPadSoftened = 6;
constexpr auto kPadFailed = 9;

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

enum class Action : uchar {
	Undo,
	SendAsIs,
	Delete,
	LetGo,
	Open,
};

struct Links {
	ClickHandlerPtr undo;
	ClickHandlerPtr sendAsIs;
	ClickHandlerPtr remove;
	ClickHandlerPtr letGo;
	ClickHandlerPtr open;
};

struct Button {
	Action action = Action::Undo;
	QString text;
	QRect rect;
	bool quiet = false; // Delete: pill text, not jade
};

struct Layout {
	QueueEvent state;
	QRect pill;
	QRect line; // the first line
	QRect wait; // the second line, when others wait behind it
	QString title;
	QString waiting;
	std::vector<Button> buttons;
};

[[nodiscard]] const style::font &TextFont() {
	return st::msgDateFont;
}

[[nodiscard]] const style::font &ButtonFont() {
	return st::semiboldFont;
}

[[nodiscard]] int ButtonWidth(const QString &text) {
	return 2 * S(kButtonPad) + ButtonFont()->width(text);
}

[[nodiscard]] std::optional<QueueEvent> Shown(
		not_null<const HistoryItem*> item) {
	const auto state = QueueStateOf(item->fullId());
	if (!state
		|| (state->state != QueueState::Softened
			&& state->state != QueueState::Review
			&& state->state != QueueState::Failed)) {
		return std::nullopt;
	}
	return state;
}

[[nodiscard]] bool TwoLines(const QueueEvent &state) {
	return (state.state == QueueState::Failed) && state.holding;
}

[[nodiscard]] int PillHeight(const QueueEvent &state) {
	return S(kLine) + (TwoLines(state) ? S(kWaitLine) : 0);
}

[[nodiscard]] QString WaitingText(int behind) {
	return u"%1 waiting behind it"_q.arg(std::max(behind, 1));
}

// Lays the buttons of one line out from its right end, last one first.
void PlaceButtons(
		std::vector<Button> &buttons,
		std::vector<Button> &&line,
		int right,
		int top,
		int height) {
	for (auto i = line.rbegin(); i != line.rend(); ++i) {
		const auto width = ButtonWidth(i->text);
		right -= width;
		i->rect = QRect(right, top, width, height);
		buttons.push_back(std::move(*i));
		right -= S(kGap);
	}
}

[[nodiscard]] std::optional<Layout> Compute(
		not_null<const HistoryView::Element*> view,
		QRect bubble) {
	const auto item = view->data();
	const auto state = Shown(item);
	if (!state) {
		return std::nullopt;
	}
	const auto height = PillHeight(*state);
	auto result = Layout{ .state = *state };
	auto first = std::vector<Button>();
	auto second = std::vector<Button>();
	auto lineWidth = 0;
	auto waitWidth = 0;
	if (state->state == QueueState::Review) {
		result.title = u"Second look"_q;
		if (state->away) {
			first.push_back({ .action = Action::Open, .text = u"Open"_q });
		}
		lineWidth = S(kPadSoftened)
			+ S(kRing)
			+ S(kGap)
			+ TextFont()->width(result.title)
			+ (first.empty()
				? S(kPadSoftened)
				: (S(kGap) + ButtonWidth(first.back().text)))
			+ S(kPadRight);
	} else if (state->state == QueueState::Softened) {
		result.title = u"Softened"_q;
		first.push_back({ .action = Action::Undo, .text = u"Undo"_q });
		lineWidth = S(kPadSoftened)
			+ S(kRing)
			+ S(kGap)
			+ TextFont()->width(result.title)
			+ S(kGap)
			+ ButtonWidth(first.back().text)
			+ S(kPadRight);
	} else {
		result.title = u"Couldn't check"_q;
		first.push_back({
			.action = Action::SendAsIs,
			.text = u"Send as is"_q,
		});
		first.push_back({
			.action = Action::Delete,
			.text = u"Delete"_q,
			.quiet = true,
		});
		lineWidth = S(kPadFailed)
			+ S(kBang)
			+ S(kGap)
			+ ButtonFont()->width(result.title)
			+ S(kGap)
			+ ButtonWidth(first[0].text)
			+ S(kGap)
			+ ButtonWidth(first[1].text)
			+ S(kPadRight);
		if (TwoLines(*state)) {
			result.waiting = WaitingText(state->behind);
			second.push_back({
				.action = Action::LetGo,
				.text = u"Let the rest go"_q,
			});
			waitWidth = S(kPadFailed)
				+ S(kBang)
				+ S(kGap)
				+ TextFont()->width(result.waiting)
				+ S(kGap)
				+ ButtonWidth(second.back().text)
				+ S(kPadRight);
		}
	}
	const auto width = std::max(lineWidth, waitWidth);
	const auto outer = view->width();
	const auto left = view->hasRightLayout()
		? std::max(bubble.x() + bubble.width() - S(kInset) - width, S(kInset))
		: std::min(bubble.x() + S(kInset), std::max(outer - width, 0));
	const auto top = bubble.y() + bubble.height() + S(kAbove);
	result.pill = QRect(left, top, width, height);
	result.line = QRect(left, top, width, S(kLine));
	const auto right = left + width - S(kPadRight);
	PlaceButtons(result.buttons, std::move(first), right, top, S(kLine));
	if (!second.empty()) {
		result.wait = QRect(left, top + S(kLine), width, S(kWaitLine));
		PlaceButtons(
			result.buttons,
			std::move(second),
			right,
			result.wait.y(),
			S(kWaitLine));
	}
	return result;
}

[[nodiscard]] base::flat_map<FullMsgId, Links> &AllLinks() {
	static auto result = base::flat_map<FullMsgId, Links>();
	return result;
}

// Handlers must stay the same objects while the bubble is queued (press
// and release compare them); bubbles that left the queue drop theirs.
[[nodiscard]] const Links &LinksFor(FullMsgId id) {
	auto &all = AllLinks();
	for (auto i = begin(all); i != end(all);) {
		if (i->first != id && !QueueStateOf(i->first)) {
			i = all.erase(i);
		} else {
			++i;
		}
	}
	auto i = all.find(id);
	if (i == end(all)) {
		// Deferred: the bubble may be destroyed or resized by the action.
		const auto make = [&](Fn<void(FullMsgId)> action) {
			return std::make_shared<LambdaClickHandler>([=] {
				crl::on_main([=] { action(id); });
			});
		};
		i = all.emplace(id, Links{
			.undo = make(QueueUndo),
			.sendAsIs = make(QueueSendAsIs),
			.remove = make(QueueDelete),
			.letGo = make(QueueStepAside),
			.open = make([](FullMsgId id) { QueuePutAway(id, false); }),
		}).first;
	}
	return i->second;
}

[[nodiscard]] const ClickHandlerPtr &LinkFor(
		const Links &links,
		Action action) {
	switch (action) {
	case Action::Undo: return links.undo;
	case Action::SendAsIs: return links.sendAsIs;
	case Action::Delete: return links.remove;
	case Action::LetGo: return links.letGo;
	case Action::Open: return links.open;
	}
	Unexpected("Safe::Calm::Action in LinkFor.");
}

void PaintButton(
		QPainter &p,
		const Button &button,
		const ClickHandlerPtr &link,
		const QColor &pillText) {
	if (ClickHandler::showAsActive(link)) {
		auto highlight = pillText;
		highlight.setAlphaF(ClickHandler::showAsPressed(link) ? 0.16 : 0.10);
		const auto rect = button.rect.marginsRemoved(
			{ 0, S(4), 0, S(4) });
		const auto radius = rect.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(highlight);
		p.drawRoundedRect(rect, radius, radius);
	}
	auto color = button.quiet ? pillText : TickColor(pillText);
	if (button.quiet) {
		color.setAlphaF(color.alphaF() * 0.75);
	}
	p.setFont(ButtonFont());
	p.setPen(color);
	p.drawText(
		button.rect.x() + S(kButtonPad),
		button.rect.y()
			+ (button.rect.height() - ButtonFont()->height) / 2
			+ ButtonFont()->ascent,
		button.text);
}

} // namespace

namespace {

[[nodiscard]] int QueueRowHeight(not_null<const HistoryItem*> item) {
	const auto state = Shown(item);
	return state
		? (S(kAbove) + PillHeight(*state) + S(kBelow))
		: 0;
}

[[nodiscard]] int RunChipTop(
		not_null<const HistoryView::Element*> view,
		QRect bubble) {
	return bubble.y() + bubble.height() + QueueRowHeight(view->data());
}

void PaintQueueRow(
	Painter &p,
	not_null<const HistoryView::Element*> view,
	QRect bubble,
	const Ui::ChatPaintContext &context);

} // namespace

int QueueChipHeight(not_null<const HistoryItem*> item) {
	return QueueRowHeight(item) + RunChipHeight(item);
}

void PaintQueueChip(
		Painter &p,
		not_null<const HistoryView::Element*> view,
		QRect bubble,
		const Ui::ChatPaintContext &context) {
	// Under the bubble, which paints right after this.
	PaintRunEdge(p, view, bubble);
	PaintQueueRow(p, view, bubble, context);
	PaintRunChip(p, view, bubble, RunChipTop(view, bubble), context);
}

namespace {

void PaintQueueRow(
		Painter &p,
		not_null<const HistoryView::Element*> view,
		QRect bubble,
		const Ui::ChatPaintContext &context) {
	const auto layout = Compute(view, bubble);
	if (!layout) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto &state = layout->state;
	const auto pillText = context.st->msgServiceFg()->c;
	const auto radius = TwoLines(state)
		? float64(S(kTwoLinesRadius))
		: layout->pill.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(context.st->msgServiceBg());
	p.drawRoundedRect(layout->pill, radius, radius);

	const auto &line = layout->line;
	const auto baseline = [&](const QRect &row, const style::font &font) {
		return row.y() + (row.height() - font->height) / 2 + font->ascent;
	};
	if (state.state == QueueState::Review) {
		auto x = line.x() + S(kPadSoftened);
		const auto leaf = S(kRing) * 0.62;
		PaintLeaf(
			p,
			QRectF(x, line.y() + (line.height() - S(kRing)) / 2., S(kRing), S(kRing)),
			leaf,
			TickColor(pillText));
		x += S(kRing) + S(kGap);
		p.setFont(TextFont());
		p.setPen(pillText);
		p.drawText(x, baseline(line, TextFont()), layout->title);
	} else if (state.state == QueueState::Softened) {
		const auto remaining = (state.until - crl::now())
			/ float64(kHoldMs);
		auto x = line.x() + S(kPadSoftened);
		PaintRingLeaf(
			p,
			QRectF(x, line.y() + (line.height() - S(kRing)) / 2., S(kRing), S(kRing)),
			remaining);
		x += S(kRing) + S(kGap);
		p.setFont(TextFont());
		p.setPen(pillText);
		p.drawText(x, baseline(line, TextFont()), layout->title);
	} else {
		auto x = line.x() + S(kPadFailed);
		PaintBang(
			p,
			QRectF(x, line.y() + (line.height() - S(kBang)) / 2., S(kBang), S(kBang)));
		x += S(kBang) + S(kGap);
		p.setFont(ButtonFont());
		p.setPen(EmberColor());
		p.drawText(x, baseline(line, ButtonFont()), layout->title);
		if (!layout->wait.isEmpty()) {
			const auto &wait = layout->wait;
			auto faded = pillText;
			faded.setAlphaF(faded.alphaF() * 0.8);
			const auto clockLeft = wait.x()
				+ S(kPadFailed)
				+ (S(kBang) - S(kClock)) / 2.;
			PaintClock(
				p,
				QRectF(clockLeft, wait.y() + (wait.height() - S(kClock)) / 2., S(kClock), S(kClock)),
				faded);
			p.setFont(TextFont());
			p.setPen(faded);
			p.drawText(
				wait.x() + S(kPadFailed) + S(kBang) + S(kGap),
				baseline(wait, TextFont()),
				layout->waiting);
		}
	}
	const auto &links = LinksFor(view->data()->fullId());
	for (const auto &button : layout->buttons) {
		PaintButton(p, button, LinkFor(links, button.action), pillText);
	}
}

} // namespace

ClickHandlerPtr QueueChipLink(
		not_null<const HistoryView::Element*> view,
		QPoint point,
		QRect bubble) {
	if (const auto link = RunChipLink(
			view,
			point,
			bubble,
			RunChipTop(view, bubble))) {
		return link;
	}
	const auto layout = Compute(view, bubble);
	if (!layout || !layout->pill.contains(point)) {
		return nullptr;
	}
	const auto &links = LinksFor(view->data()->fullId());
	for (const auto &button : layout->buttons) {
		if (button.rect.contains(point)) {
			return LinkFor(links, button.action);
		}
	}
	return nullptr;
}

bool QueueShowsFailed(not_null<const HistoryItem*> item) {
	const auto state = QueueStateOf(item->fullId());
	return state && (state->state == QueueState::Failed);
}

} // namespace Safe::Calm
