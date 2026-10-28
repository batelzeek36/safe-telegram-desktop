/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_run_cards.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
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

// "GIFs", "GIF and photos": what stays in its place (no "your").
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
	return joined;
}

[[nodiscard]] int MediaCount(const std::vector<RunMedia> &media) {
	return int(ranges::count_if(media, [](RunMedia kind) {
		return kind != RunMedia::None;
	}));
}

// "A", "A and B", "A, B and C".
[[nodiscard]] QString JoinClauses(QStringList parts) {
	if (parts.size() < 2) {
		return parts.isEmpty() ? QString() : parts.front();
	}
	const auto last = parts.takeLast();
	return parts.join(u", "_q) + u" and "_q + last;
}

// The media of the run, split by whether they already went out.
struct RunMediaSplit {
	std::vector<RunMedia> sent;
	std::vector<RunMedia> held;
};
[[nodiscard]] RunMediaSplit SplitMedia(const Run &run) {
	auto result = RunMediaSplit();
	for (auto i = 0; i != int(run.items.size()); ++i) {
		const auto kind = (i < int(run.media.size()))
			? run.media[i]
			: RunMedia::None;
		if (kind == RunMedia::None) {
			continue;
		}
		(ranges::contains(run.sentItems, run.items[i])
			? result.sent
			: result.held).push_back(kind);
	}
	return result;
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
		QueueSetRun(peer, QueueRun());
		if (!SentMerging(peer)) {
			// Never lose a held message: leaving releases them (a merge in
			// its Undo window keeps its own and lets them go when it ends).
			QueueHoldForCard(peer, false);
			QueueReleaseHeld(peer);
		}
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
		QueueSetRun(peer, QueueRun());
		if (!SentMerging(peer)) {
			QueueHoldForCard(peer, false);
			QueueReleaseHeld(peer); // leaving the chat sends what was held
		}
		SetRunMark(_history, std::nullopt);
	}
	_card = Card::None;
	_cardRun.reset();
	_armed.reset();
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
	// Merge automatically needs the run even with the offers off: a run with
	// sent texts never merges by itself.
	_watcher.setEnabled(merge, hold && autoMerge);
	if (!hold || !autoMerge) {
		cancelAuto(hold); // switched off: the hold goes on (or ends below)
	}
	if (wasHold && !hold && _history && !SentMerging(peer())) {
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
	const auto offer = _merge ? _watcher.offer() : nullptr;
	if (_card != Card::None && _cardRun && !SentMerging(peer)) {
		const auto now = _watcher.run();
		if (!now || now->allTexts() != _cardRun->allTexts()) {
			// She read some, or he sent more (or one went), while it was open.
			const auto read = readSome(*_cardRun);
			closeCard();
			pill(read.isEmpty()
				? u"Your messages changed, so the card closed"_q
				: (read + u", so the card closed"_q));
		}
	}
	if (_auto) {
		const auto now = CurrentRun(_history);
		if (now && (!now->allHeld() || now->heldTexts != _auto->texts)) {
			// He sent more (or one went, or her message came): the hold goes
			// on with all of them.
			cancelAuto(true);
		}
	}
	const auto keepRing = [&](RunMark &mark) {
		if (const auto now = RunMarkFor(peer)) {
			if (now->kind == mark.kind && now->chip == mark.chip) {
				mark.holdUntil = now->holdUntil; // the Undo ring keeps going
			}
		}
	};
	if (_auto) {
		auto mark = RunMark{
			.kind = RunMark::Kind::Held,
			.texts = _auto->texts,
			.chip = _auto->items.back(),
			.count = int(_auto->texts.size()),
		};
		keepRing(mark);
		SetRunMark(_history, mark, [=] { openRun(); }, [=] { undoAuto(); });
		syncQueueRun();
		return;
	}
	if (!offer) {
		SetRunMark(_history, std::nullopt);
		syncQueueRun();
		return;
	}
	auto mark = RunMark{
		.kind = offer->allHeld() ? RunMark::Kind::Held : RunMark::Kind::Sent,
		.texts = offer->allTexts(),
		.chip = offer->items.back(),
		.count = offer->texts(),
	};
	keepRing(mark);
	SetRunMark(_history, mark, [=] { openRun(); }, [=] {
		UndoSentMerge(peer);
	});
	syncQueueRun();
}

void RunCards::syncQueueRun() {
	if (!_history) {
		return;
	}
	const auto run = _watcher.run();
	const auto offer = _merge ? _watcher.offer() : nullptr;
	QueueSetRun(peer(), QueueRun{
		.pill = (offer && !offer->heldTexts.empty()) || _auto.has_value(),
		.sentTexts = run ? int(run->sentTexts.size()) : 0,
	});
}

void RunCards::openRun() {
	if (!_history) {
		return;
	} else if (_auto && !_auto->text.isEmpty()) {
		return; // its Undo window runs: Undo is the pill's button
	}
	const auto peer = this->peer();
	if (SentMerging(peer)) {
		return;
	}
	// A tap while the automatic one is written: the card takes over.
	cancelAuto(false);
	const auto found = _watcher.run();
	if (!found || found->texts() < Core::kRunTexts) {
		return;
	}
	const auto run = *found;
	_cardRun = run;
	if (!run.heldTexts.empty()) {
		QueueHoldForCard(peer, true);
	}
	_takePanel();
	_card = Card::Writing;
	_panel->showWriting(Title(run.texts()), {
		{ u"Close"_q, PanelButton::Look::Ghost, [=] { closeCard(); } },
		{
			run.allHeld() ? u"Send as typed"_q : u"Keep them"_q,
			PanelButton::Look::Ghost,
			[=] { declineRun(); },
		},
	});
	SharedClient().cancel(base::take(_summaryCall));
	_summaryCall = SharedClient().summarize(
		MakeSummaryRequest(_history, run.items),
		crl::guard(this, [=](SummaryResult result) {
			_summaryCall = 0;
			if (_card != Card::Writing || this->peer() != peer) {
				return;
			} else if (result.text.isEmpty()) {
				LOG(("Safe Calm: no one message for a run (%1)."
					).arg(result.failure));
				closeCard();
				pill(u"Calm could not write one right now"_q);
				return;
			}
			showRun(result.text);
		}));
}

QString RunCards::readerLine(const Run &run) const {
	const auto it = (run.sentItems.size() == 1) ? u"it"_q : u"them"_q;
	const auto peer = _history ? _history->peer.get() : nullptr;
	const auto user = peer ? peer->asUser() : nullptr;
	if (user && !user->isSelf()) {
		return u"%1 hasn't read %2 yet."_q.arg(user->shortName(), it);
	}
	return u"Nobody has read %1 yet."_q.arg(it);
}

QString RunCards::readSome(const Run &run) const {
	if (!_history) {
		return QString();
	}
	for (const auto &id : run.sentItems) {
		const auto item = _history->owner().message(id);
		if (item && !RunUnread(item)) {
			const auto user = _history->peer->asUser();
			return (user && !user->isSelf())
				? u"%1 read some of them"_q.arg(user->shortName())
				: u"Someone read some of them"_q;
		}
	}
	return QString();
}

// Exactly what Send this instead will do (Zach 10-27 16:08: "Edits your
// first message, removes the other sent one and sends nothing else. She
// hasn't read them yet.").
QString RunCards::cardLine(const Run &run) const {
	const auto media = SplitMedia(run);
	if (run.allHeld()) {
		auto line = u"Goes out in place of your %1."_q.arg(
			Texts(int(run.heldTexts.size())));
		if (const auto words = MediaWords(media.held); !words.isEmpty()) {
			line += (MediaCount(media.held) == 1)
				? u" Your %1 goes out as it is, in its place."_q.arg(words)
				: u" Your %1 go out as they are, in their place."_q.arg(words);
		}
		if (const auto words = MediaWords(media.sent); !words.isEmpty()) {
			line += (MediaCount(media.sent) == 1)
				? u" Your %1 stays."_q.arg(words)
				: u" Your %1 stay."_q.arg(words);
			return line + u" None of these texts has gone out yet."_q;
		}
		return line + u" Nothing has gone out yet."_q;
	}
	const auto sent = int(run.sentTexts.size());
	const auto held = int(run.heldTexts.size());
	const auto heldWords = MediaWords(media.held);
	const auto first = _history
		? _history->owner().message(run.sentTexts.front())
		: nullptr;
	const auto inPlace = first && CanEditInPlace(first);
	auto line = QString();
	if (inPlace) {
		auto parts = QStringList{ u"Edits your first message"_q };
		if (sent > 1) {
			parts.push_back(!held
				? u"removes the other %1"_q.arg(Texts(sent - 1))
				: (sent == 2)
				? u"removes the other sent one"_q
				: u"removes the other %1 sent ones"_q.arg(sent - 1));
		}
		if (held) {
			parts.push_back(heldWords.isEmpty()
				? u"sends nothing else"_q
				: u"sends only your %1"_q.arg(heldWords));
		}
		line = JoinClauses(parts) + QChar('.');
	} else {
		// Telegram will not edit it (the 48 h limit): a new message instead.
		const auto them = (sent == 1) ? u"it"_q : u"them"_q;
		const auto texts = held
			? ((sent == 1) ? u"sent text"_q : u"%1 sent texts"_q.arg(sent))
			: Texts(sent);
		line = !held
			? u"Removes your %1 and sends this after %2."_q.arg(texts, them)
			: heldWords.isEmpty()
			? u"Removes your %1 and sends only this, after %2."_q.arg(
				texts,
				them)
			: u"Removes your %1 and sends your %2, then this."_q.arg(
				texts,
				heldWords);
	}
	if (const auto words = MediaWords(media.sent); !words.isEmpty()) {
		line += (MediaCount(media.sent) == 1)
			? u" Your %1 stays."_q.arg(words)
			: u" Your %1 stay."_q.arg(words);
	}
	return line + QChar(' ') + readerLine(run);
}

void RunCards::showRun(const QString &text) {
	if (!_cardRun) {
		closeCard();
		return;
	}
	const auto peer = this->peer();
	const auto run = *_cardRun;
	const auto count = run.texts();
	_card = Card::Shown;
	auto send = Fn<void()>();
	if (run.allHeld()) {
		send = [=] {
			const auto merged = QueueMergeHeld(peer, run.heldTexts, text);
			closeCard();
			if (!merged) {
				pill(u"Your messages changed, so nothing was merged"_q);
			}
		};
	} else {
		send = [=] { sendSent({ text }, run, false); };
	}
	_panel->showSummary(Title(count), text, cardLine(run), {
		{ u"Send this instead"_q, PanelButton::Look::Primary, send },
		{ u"Edit"_q, PanelButton::Look::Ghost, [=] {
			editInField(
				text,
				run,
				run.allHeld()
					? u"Send it to go in place of the %1"_q.arg(Texts(count))
					: u"Send it to replace the %1"_q.arg(Texts(count)));
		} },
		{
			run.allHeld() ? u"Send as typed"_q : u"Keep them"_q,
			PanelButton::Look::Ghost,
			[=] { declineRun(); },
		},
	});
}

// Keep them (some went out: the held ones wait for the hold as usual) or
// Send as typed (all held: they go now). No pill for exactly this run.
void RunCards::declineRun() {
	const auto allHeld = _cardRun && _cardRun->allHeld();
	_watcher.keep();
	closeCard();
	if (allHeld && _history) {
		QueueReleaseHeld(peer());
	}
}

void RunCards::sendSent(
		const TextWithTags &text,
		const Run &run,
		bool fromField) {
	if (!_history || run.sentTexts.empty()) {
		closeCard();
		return;
	}
	const auto weak = base::make_weak(this);
	const auto peer = this->peer();
	if (!run.heldTexts.empty()) {
		// They wait through the Undo window; the merge lets them go (or
		// drops them) when it ends.
		QueueHoldForCard(peer, true);
	}
	StartSentMerge(_history, SentMerge{
		.items = run.sentItems,
		.texts = run.sentTexts,
		.held = run.heldTexts,
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
	const auto merging = SentMerging(peer);
	if (merging) {
		_watcher.setMerging(true);
	}
	closeCard(merging);
}

void RunCards::editInField(
		const QString &text,
		Run run,
		const QString &note) {
	closeCard();
	const auto current = _text().text;
	_setText({ current.trimmed().isEmpty()
		? text
		: (text + QChar('\n') + current) });
	_armed = std::move(run);
	_focus();
	pill(note);
}

bool RunCards::interceptSend(const TextWithTags &draft) {
	if (!_armed || !_history || draft.text.trimmed().isEmpty()) {
		return false;
	}
	const auto run = *base::take(_armed);
	const auto peer = this->peer();
	if (run.allHeld()) {
		if (QueueMergeHeld(peer, run.heldTexts, draft.text)) {
			_setText({});
			return true;
		}
		pill(u"Your messages changed, so this goes as a new one"_q);
		return false;
	}
	if (const auto read = readSome(run); !read.isEmpty()) {
		pill(read + u", so this goes as a new one"_q);
		return false;
	} else if (!run.heldTexts.empty()
		&& !QueueStillHeld(peer, run.heldTexts)) {
		pill(u"Your messages changed, so this goes as a new one"_q);
		return false;
	} else if (SentMerging(peer)) {
		return false;
	}
	sendSent(draft, run, true);
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
		const auto held = _cardRun && !_cardRun->heldTexts.empty();
		_card = Card::None;
		_cardRun.reset();
		SharedClient().cancel(base::take(_summaryCall));
		if (held && _history) {
			QueueHoldForCard(peer(), false);
		}
	}
}

void RunCards::closeCard(bool keepHeld) {
	const auto was = _card;
	const auto held = _cardRun && !_cardRun->heldTexts.empty();
	_card = Card::None;
	_cardRun.reset();
	SharedClient().cancel(base::take(_summaryCall));
	if (held && !keepHeld && _history) {
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
	// Only the run: held texts from before her last message go as typed.
	const auto run = CurrentRun(_history);
	if (!run || !run->allHeld() || run->texts() < Core::kRunTexts) {
		// A run with sent texts waits for his tap: as typed now.
		QueueReleaseHeld(peer);
		return;
	} else if (_card != Card::None) {
		return; // the card is on them already
	}
	cancelAuto(false);
	_auto = AutoMerge{ .items = run->items, .texts = run->heldTexts };
	_autoTimer.callOnce(Core::kAutoMergeWriteMs);
	_autoCall = SharedClient().summarize(
		MakeSummaryRequest(_history, run->items),
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
