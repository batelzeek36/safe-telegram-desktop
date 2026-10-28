/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_run_cards.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "safe/safe_calm.h"
#include "safe/safe_calm_client.h"
#include "safe/safe_calm_core.h"
#include "safe/safe_calm_merge.h"
#include "safe/safe_calm_panels.h"
#include "safe/safe_calm_queue.h"
#include "safe/safe_calm_run_mark.h"
#include "safe/safe_calm_shared.h"
#include "ui/toast/toast.h"

namespace Safe::Calm {
namespace {

// The automatic merge's Undo window draws the same ring as the others.
static_assert(Core::kAutoMergeUndoMs == kHoldMs);

[[nodiscard]] QString Title(int texts) {
	return u"One message instead of %1?"_q.arg(texts);
}

[[nodiscard]] QString Texts(int count) {
	return (count == 1) ? u"text"_q : u"%1 texts"_q.arg(count);
}

// "Your GIFs", "Your GIF and 2 photos": what stays in its place.
[[nodiscard]] QString MediaWords(const std::vector<RunMedia> &media) {
	auto counts = std::array<int, 4>{ 0, 0, 0, 0 };
	auto order = std::vector<int>();
	for (const auto kind : media) {
		const auto index = (kind == RunMedia::Gif)
			? 0
			: (kind == RunMedia::Sticker)
			? 1
			: (kind == RunMedia::Photo)
			? 2
			: (kind == RunMedia::Video)
			? 3
			: -1;
		if (index < 0) {
			continue;
		} else if (!counts[index]++) {
			order.push_back(index);
		}
	}
	if (order.empty()) {
		return QString();
	}
	static const auto singular = std::array<QString, 4>{
		u"GIF"_q, u"sticker"_q, u"photo"_q, u"video"_q,
	};
	static const auto plural = std::array<QString, 4>{
		u"GIFs"_q, u"stickers"_q, u"photos"_q, u"videos"_q,
	};
	auto parts = QStringList();
	for (const auto index : order) {
		parts.push_back((counts[index] == 1)
			? singular[index]
			: plural[index]);
	}
	auto joined = parts.takeLast();
	if (!parts.isEmpty()) {
		joined = parts.join(u", "_q) + u" and "_q + joined;
	}
	return u"Your "_q + joined;
}

[[nodiscard]] int MediaCount(const std::vector<RunMedia> &media) {
	return int(ranges::count_if(media, [](RunMedia kind) {
		return kind != RunMedia::None;
	}));
}

} // namespace

RunCards::RunCards(Args &&args)
: _panel(args.panel)
, _text(std::move(args.text))
, _setText(std::move(args.setText))
, _focus(std::move(args.focus))
, _takePanel(std::move(args.takePanel))
, _panelFree(std::move(args.panelFree))
, _watcher([=] { scheduleMark(); })
, _autoTimer([=] { autoTimer(); }) {
}

RunCards::~RunCards() {
	SharedClient().cancel(base::take(_summaryCall));
	cancelAuto(false);
	if (_autoHook) {
		QueueSetAutoMerge(base::take(_autoHook), nullptr);
	}
	if (_history) {
		const auto peer = this->peer();
		QueueHoldForCard(peer, false);
		// Never lose a held message: leaving releases them.
		QueueReleaseHeld(peer);
		SetRunMark(_history, std::nullopt);
	}
}

PeerId RunCards::peer() const {
	return _history ? _history->peer->id : PeerId();
}

void RunCards::setHistory(History *history) {
	if (_history == history) {
		return;
	}
	SharedClient().cancel(base::take(_summaryCall));
	cancelAuto(false);
	if (_history) {
		const auto peer = this->peer();
		QueueHoldForCard(peer, false);
		QueueReleaseHeld(peer); // leaving the chat sends what was held
		SetRunMark(_history, std::nullopt);
	}
	_card = Card::None;
	_armed.reset();
	_heldItems.clear();
	_heldTexts.clear();
	_historyLifetime.destroy();
	_history = history;
	_watcher.setHistory(history);
	if (_history) {
		QueueEvents(peer()) | rpl::on_next([=] {
			scheduleMark();
		}, _historyLifetime);
	}
	syncAutoHook();
	scheduleMark();
}

void RunCards::setModes(bool merge, bool hold, bool autoMerge) {
	const auto wasHold = _hold;
	_merge = merge;
	_hold = hold;
	_autoMerge = autoMerge;
	_watcher.setEnabled(merge);
	if (!hold || !autoMerge) {
		cancelAuto(hold); // switched off: the hold goes on (or ends below)
	}
	if (wasHold && !hold && _history) {
		QueueReleaseHeld(peer());
	}
	syncAutoHook();
	scheduleMark();
}

bool RunCards::cardShown() const {
	return (_card != Card::None) && _panel->isSummary();
}

void RunCards::scheduleMark() {
	if (_markQueued) {
		return;
	}
	_markQueued = true;
	crl::on_main(this, [=] {
		_markQueued = false;
		updateMark();
	});
}

void RunCards::updateMark() {
	if (!_history) {
		return;
	}
	const auto peer = this->peer();
	const auto group = _hold ? QueueHeldGroup(peer) : HeldGroup();
	if ((_card == Card::Held || _card == Card::HeldWriting)
		&& group.texts != _heldTexts) {
		// He sent more (or one went) while the card was open.
		closeCard();
		pill(u"Your messages changed, so the card closed"_q);
	}
	if (_auto && group.texts != _auto->texts) {
		// He sent more (or one went): the hold goes on with all of them.
		cancelAuto(true);
	}
	if (int(group.texts.size()) >= Core::kHoldPillTexts) {
		auto mark = RunMark{
			.kind = RunMark::Kind::Held,
			.texts = group.texts,
			.chip = group.items.back(),
			.count = int(group.texts.size()),
		};
		if (const auto now = RunMarkFor(peer)) {
			if (now->kind == mark.kind && now->chip == mark.chip) {
				mark.holdUntil = now->holdUntil; // the Undo ring keeps going
			}
		}
		SetRunMark(_history, mark, [=] { openHeld(); }, [=] { undoAuto(); });
		return;
	}
	const auto offer = _merge ? _watcher.offer() : nullptr;
	if (!offer) {
		if (_card == Card::Sent) {
			closeCard();
		}
		SetRunMark(_history, std::nullopt);
		return;
	}
	auto mark = RunMark{
		.kind = RunMark::Kind::Sent,
		.texts = offer->run.texts,
		.chip = offer->run.items.back(),
		.count = int(offer->run.texts.size()),
	};
	if (const auto now = RunMarkFor(peer)) {
		if (now->kind == mark.kind && now->chip == mark.chip) {
			mark.holdUntil = now->holdUntil; // the merge's ring keeps going
		}
	}
	SetRunMark(_history, mark, [=] { openSent(); }, [=] {
		UndoSentMerge(peer);
	});
}

void RunCards::openSent() {
	const auto offer = _watcher.offer();
	if (!_history || !offer || SentMerging(peer())) {
		return;
	}
	const auto run = offer->run;
	const auto text = offer->text;
	const auto first = _history->owner().message(run.texts.front());
	const auto inPlace = first && CanEditInPlace(first);
	const auto count = int(run.texts.size());
	auto line = inPlace
		? u"Edits your first message and removes the other %1."_q.arg(
			Texts(count - 1))
		: u"Removes your %1 and sends this after them."_q.arg(
			Texts(count));
	if (const auto media = MediaWords(run.media); !media.isEmpty()) {
		line += u" %1 %2."_q.arg(
			media,
			(MediaCount(run.media) == 1) ? u"stays"_q : u"stay"_q);
	}
	line += u" Nobody has read them yet."_q;
	_takePanel();
	_card = Card::Sent;
	_panel->showSummary(Title(count), text, line, {
		{ u"Send this instead"_q, PanelButton::Look::Primary, [=] {
			sendSent({ text }, false);
		} },
		{ u"Edit"_q, PanelButton::Look::Ghost, [=] {
			editInField(
				text,
				Armed{ .items = run.items, .texts = run.texts },
				u"Send it to replace the %1"_q.arg(Texts(count)));
		} },
		{ u"Keep them"_q, PanelButton::Look::Ghost, [=] {
			_watcher.keep();
			closeCard();
		} },
	});
}

void RunCards::sendSent(const TextWithTags &text, bool fromField) {
	const auto offer = _watcher.offer();
	if (!_history || (!offer && !_armed)) {
		closeCard();
		return;
	}
	const auto items = fromField ? _armed->items : offer->run.items;
	const auto texts = fromField ? _armed->texts : offer->run.texts;
	const auto weak = base::make_weak(this);
	const auto peer = this->peer();
	StartSentMerge(_history, SentMerge{
		.items = items,
		.texts = texts,
		.text = text,
		.fromField = fromField,
	}, MergeHooks{
		.toast = [=](QString text) {
			if (const auto strong = weak.get(); strong && strong->peer() == peer) {
				strong->pill(text);
			} else {
				Ui::Toast::Show(text);
			}
		},
		.restore = [=](TextWithTags text) {
			if (const auto strong = weak.get(); strong && strong->peer() == peer) {
				if (strong->_text().text.trimmed().isEmpty()) {
					strong->_setText(text);
				}
			}
		},
		.finished = [=] {
			if (const auto strong = weak.get()) {
				strong->_watcher.setMerging(false);
				strong->scheduleMark();
			}
		},
	});
	if (SentMerging(peer)) {
		_watcher.setMerging(true);
	}
	closeCard();
}

void RunCards::openHeld() {
	if (!_history) {
		return;
	} else if (_auto && !_auto->text.isEmpty()) {
		return; // its Undo window runs: Undo is the pill's button
	}
	// A tap while the automatic one is written: the card takes over.
	cancelAuto(false);
	const auto peer = this->peer();
	const auto group = QueueHeldGroup(peer);
	if (int(group.texts.size()) < Core::kHoldPillTexts) {
		return;
	}
	_heldItems = group.items;
	_heldTexts = group.texts;
	QueueHoldForCard(peer, true);
	_takePanel();
	_card = Card::HeldWriting;
	_panel->showWriting(Title(int(group.texts.size())), {
		{ u"Close"_q, PanelButton::Look::Ghost, [=] { closeCard(); } },
		{ u"Send as typed"_q, PanelButton::Look::Ghost, [=] {
			closeCard();
			QueueReleaseHeld(peer);
		} },
	});
	SharedClient().cancel(base::take(_summaryCall));
	_summaryCall = SharedClient().summarize(
		MakeSummaryRequest(_history, group.items),
		crl::guard(this, [=](SummaryResult result) {
			_summaryCall = 0;
			if (_card != Card::HeldWriting || this->peer() != peer) {
				return;
			} else if (result.text.isEmpty()) {
				LOG(("Safe Calm: no one message for held texts (%1)."
					).arg(result.failure));
				closeCard();
				pill(u"Calm could not write one right now"_q);
				return;
			}
			showHeld(result.text);
		}));
}

void RunCards::showHeld(const QString &text) {
	const auto peer = this->peer();
	const auto texts = _heldTexts;
	const auto count = int(texts.size());
	auto media = std::vector<RunMedia>();
	if (_history) {
		for (const auto &id : _heldItems) {
			if (const auto item = _history->owner().message(id)) {
				media.push_back(RunKindOf(item).value_or(RunMedia::None));
			}
		}
	}
	auto line = u"Goes out in place of your %1."_q.arg(Texts(count));
	if (const auto words = MediaWords(media); !words.isEmpty()) {
		line += u" %1 go out as they are, in their place."_q.arg(words);
	}
	line += u" Nothing has gone out yet."_q;
	_card = Card::Held;
	_panel->showSummary(Title(count), text, line, {
		{ u"Send this instead"_q, PanelButton::Look::Primary, [=] {
			const auto merged = QueueMergeHeld(peer, texts, text);
			closeCard();
			if (!merged) {
				pill(u"Your messages changed, so nothing was merged"_q);
			}
		} },
		{ u"Edit"_q, PanelButton::Look::Ghost, [=] {
			editInField(
				text,
				Armed{ .held = true, .items = _heldItems, .texts = texts },
				u"Send it to go in place of the %1"_q.arg(Texts(count)));
		} },
		{ u"Send as typed"_q, PanelButton::Look::Ghost, [=] {
			closeCard();
			QueueReleaseHeld(peer);
		} },
	});
}

void RunCards::editInField(
		const QString &text,
		Armed armed,
		const QString &note) {
	closeCard();
	const auto current = _text().text;
	_setText({ current.trimmed().isEmpty()
		? text
		: (text + QChar('\n') + current) });
	_armed = std::move(armed);
	_focus();
	pill(note);
}

bool RunCards::interceptSend(const TextWithTags &draft) {
	if (!_armed || !_history || draft.text.trimmed().isEmpty()) {
		return false;
	}
	const auto armed = *base::take(_armed);
	const auto peer = this->peer();
	if (armed.held) {
		if (QueueHeldGroup(peer).texts == armed.texts
			&& QueueMergeHeld(peer, armed.texts, draft.text)) {
			_setText({});
			return true;
		}
		pill(u"Your messages changed, so this goes as a new one"_q);
		return false;
	}
	for (const auto &id : armed.items) {
		const auto item = _history->owner().message(id);
		if (item && !RunUnread(item)) {
			pill(u"They read some of them, so this goes as a new one"_q);
			return false;
		}
	}
	if (SentMerging(peer)) {
		return false;
	}
	_armed = armed; // sendSent reads its run from here
	sendSent(draft, true);
	_armed.reset();
	_setText({});
	return true;
}

void RunCards::fieldChanged(bool busy) {
	if (_hold && _history) {
		QueueTyping(peer(), busy);
	}
	if (_armed && !busy) {
		_armed.reset(); // he cleared the field: a plain send again
	}
}

bool RunCards::handleEscape() {
	if (!cardShown()) {
		return false;
	}
	closeCard();
	return true;
}

void RunCards::panelChanged() {
	if (_card != Card::None && !_panel->isSummary()) {
		// Something else took the panel, or it closed.
		const auto held = (_card == Card::Held || _card == Card::HeldWriting);
		_card = Card::None;
		SharedClient().cancel(base::take(_summaryCall));
		if (held && _history) {
			QueueHoldForCard(peer(), false);
		}
	}
}

void RunCards::closeCard() {
	const auto was = _card;
	_card = Card::None;
	SharedClient().cancel(base::take(_summaryCall));
	if ((was == Card::Held || was == Card::HeldWriting) && _history) {
		QueueHoldForCard(peer(), false);
	}
	if (was != Card::None && _panel->isSummary()) {
		_panel->hideAnimated();
	}
	if (was != Card::None && _panelFree) {
		_panelFree();
	}
}

void RunCards::syncAutoHook() {
	const auto peer = (_history && _hold && _autoMerge)
		? this->peer()
		: PeerId();
	if (_autoHook == peer) {
		return;
	} else if (_autoHook) {
		QueueSetAutoMerge(_autoHook, nullptr);
	}
	_autoHook = peer;
	if (peer) {
		const auto weak = base::make_weak(this);
		QueueSetAutoMerge(peer, [=] {
			if (const auto strong = weak.get()) {
				strong->startAuto();
			} else {
				QueueReleaseHeld(peer);
			}
		});
	}
}

// The hold ended with 3+ texts held: the queue keeps them while the one
// message is written (Core::kAutoMergeWriteMs at most).
void RunCards::startAuto() {
	if (!_history || this->peer() != _autoHook) {
		return;
	}
	const auto peer = this->peer();
	const auto group = QueueHeldGroup(peer);
	if (int(group.texts.size()) < Core::kHoldPillTexts) {
		QueueReleaseHeld(peer);
		return;
	} else if (_card == Card::Held || _card == Card::HeldWriting) {
		return; // the card is on them already
	}
	cancelAuto(false);
	_auto = AutoMerge{ .items = group.items, .texts = group.texts };
	_autoTimer.callOnce(Core::kAutoMergeWriteMs);
	_autoCall = SharedClient().summarize(
		MakeSummaryRequest(_history, group.items),
		crl::guard(this, [=](SummaryResult result) {
			_autoCall = 0;
			if (!_auto || !_auto->text.isEmpty() || this->peer() != peer) {
				return;
			} else if (result.text.isEmpty()) {
				autoFailed(result.failure);
				return;
			}
			autoWritten(result.text);
		}));
}

void RunCards::autoWritten(const QString &text) {
	const auto peer = this->peer();
	if (!QueuePreviewHeld(peer, _auto->texts, text)) {
		cancelAuto(true); // his messages changed meanwhile
		return;
	}
	_auto->text = text;
	const auto until = crl::now() + Core::kAutoMergeUndoMs;
	_autoTimer.callOnce(Core::kAutoMergeUndoMs);
	updateMark();
	SetRunMarkHold(peer, until);
}

void RunCards::autoTimer() {
	if (!_auto) {
		return;
	} else if (_auto->text.isEmpty()) {
		autoFailed(u"too slow"_q);
	} else {
		commitAuto();
	}
}

// No Undo: the one message goes out in their place, the media as they are.
void RunCards::commitAuto() {
	const auto peer = this->peer();
	const auto merge = *base::take(_auto);
	_autoTimer.cancel();
	SetRunMarkHold(peer, 0);
	if (!QueueMergeHeld(peer, merge.texts, merge.text)) {
		QueuePreviewHeld(peer, merge.texts, QString());
		QueueHoldForCard(peer, false);
		pill(u"Your messages changed, so nothing was merged"_q);
	}
	scheduleMark();
}

// Undo: his texts go out as typed, now.
void RunCards::undoAuto() {
	if (!_auto) {
		return;
	}
	const auto peer = this->peer();
	cancelAuto(false);
	QueueReleaseHeld(peer);
}

void RunCards::autoFailed(const QString &why) {
	LOG(("Safe Calm: no one message for the held texts (%1), sent as typed."
		).arg(why));
	const auto peer = this->peer();
	cancelAuto(false);
	QueueReleaseHeld(peer);
	pill(u"Calm could not write one, so they went as typed"_q);
}

void RunCards::cancelAuto(bool resumeHold) {
	if (!_auto) {
		return;
	}
	const auto merge = *base::take(_auto);
	_autoTimer.cancel();
	SharedClient().cancel(base::take(_autoCall));
	if (!_history) {
		return;
	}
	const auto peer = this->peer();
	SetRunMarkHold(peer, 0);
	if (!merge.text.isEmpty()) {
		QueuePreviewHeld(peer, merge.texts, QString());
	}
	if (resumeHold) {
		QueueHoldForCard(peer, false);
	}
	scheduleMark();
}

void RunCards::pill(const QString &text) {
	if (_panel->showing() && !_panel->isPill()) {
		Ui::Toast::Show(text); // never over a card he is reading
	} else {
		_panel->showPill(text);
	}
}

} // namespace Safe::Calm
