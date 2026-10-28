/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_composer.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "safe/safe_calm.h"
#include "safe/safe_calm_ideas.h"
#include "safe/safe_calm_leaf.h"
#include "safe/safe_calm_panels.h"
#include "safe/safe_calm_queue.h"
#include "safe/safe_calm_run_cards.h"
#include "safe/safe_calm_suggest.h"
#include "safe/safe_calm_voice.h"
#include "ui/abstract_button.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/fields/input_field.h"

#include <QtGui/QTextDocument>
#include <QtWidgets/QTextEdit>

namespace Safe::Calm {
namespace {

constexpr auto kTypingPause = crl::time(700);
constexpr auto kLeafSize = 21;
constexpr auto kPulsePeriod = 1400.;
constexpr auto kHeatMs = crl::time(250);
constexpr auto kReadyPause = crl::time(600); // a burst of messages asks once

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

} // namespace

class LeafButton final : public Ui::AbstractButton {
public:
	LeafButton(not_null<QWidget*> parent, QSize size);

	void setHeat(float64 heat);
	// Lit while its ideas are open (next-suggest.css .leaf-btn.lit).
	void setLit(bool lit);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QString accessibilityName() override;

private:
	float64 _heat = 0.;
	bool _lit = false;
	Ui::Animations::Simple _heatAnimation;
	Ui::Animations::Basic _pulse;

};

class HeatLine final : public Ui::RpWidget {
public:
	explicit HeatLine(not_null<QWidget*> parent);

	void setHeat(float64 heat);
	void restore();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	float64 _heat = 0.;
	Ui::Animations::Simple _animation;

};

LeafButton::LeafButton(not_null<QWidget*> parent, QSize size)
: AbstractButton(parent)
, _pulse([=](crl::time) {
	update();
	return true;
}) {
	resize(size);
	setPointerCursor(true);
}

void LeafButton::setHeat(float64 heat) {
	if (_heat == heat) {
		return;
	}
	const auto from = _heatAnimation.value(_heat);
	_heat = heat;
	_heatAnimation.start([=] { update(); }, from, heat, kHeatMs);
	if (heat > 0.55) {
		if (!_pulse.animating()) {
			_pulse.start();
		}
	} else {
		_pulse.stop();
	}
	update();
}

void LeafButton::setLit(bool lit) {
	if (_lit != lit) {
		_lit = lit;
		update();
	}
}

void LeafButton::onStateChanged(State was, StateChangeSource source) {
	update();
}

QString LeafButton::accessibilityName() {
	return u"Calm mode: reply ideas, or how this draft reads"_q;
}

void LeafButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto heat = _heatAnimation.value(_heat);
	const auto size = S(kLeafSize);
	const auto center = QRectF(rect()).center();
	if (_pulse.animating()) {
		const auto t = (crl::now() % int64(kPulsePeriod)) / kPulsePeriod;
		const auto wave = (1. - std::cos(t * 2. * M_PI)) / 2.;
		p.translate(center);
		p.rotate(-9. * wave);
		p.scale(1. + 0.07 * wave, 1. + 0.07 * wave);
		p.translate(-center);
		const auto opacity = p.opacity();
		p.setOpacity(opacity * (0.55 + 0.45 * wave));
		PaintLeafGlow(p, center, size, HeatColor(heat));
		p.setOpacity(opacity);
	} else if (_lit) {
		const auto opacity = p.opacity();
		p.setOpacity(opacity * 0.7);
		PaintLeafGlow(p, center, size, JadeColor());
		p.setOpacity(opacity);
	}
	const auto alpha = (isOver() || isDown()) ? 1. : 0.88;
	PaintLeaf(
		p,
		QRectF(center.x() - size / 2., center.y() - size / 2., size, size),
		size,
		HeatColor(heat, alpha));
}

HeatLine::HeatLine(not_null<QWidget*> parent)
: RpWidget(parent) {
	setAttribute(Qt::WA_TransparentForMouseEvents);
	hide();
}

void HeatLine::setHeat(float64 heat) {
	if (_heat == heat) {
		return;
	}
	const auto from = _animation.value(_heat);
	_heat = heat;
	_animation.start([=] {
		update();
		if (!_animation.animating() && _heat <= 0.) {
			hide();
		}
	}, from, heat, kHeatMs);
	if (heat > 0.) {
		show();
		raise();
	}
}

void HeatLine::restore() {
	if (_heat > 0. && isHidden()) {
		show();
		raise();
	}
}

void HeatLine::paintEvent(QPaintEvent *e) {
	const auto heat = _animation.value(_heat);
	if (heat <= 0.) {
		return;
	}
	auto p = QPainter(this);
	const auto w = int(base::SafeRound(width() * heat));
	auto gradient = QLinearGradient(0, 0, std::max(w, 1), 0);
	gradient.setColorAt(0., JadeColor());
	gradient.setColorAt(1., HeatColor(heat));
	p.setOpacity(0.4 + 0.6 * heat);
	p.fillRect(0, 0, w, height(), gradient);
}

Composer::Composer(Args &&args)
: _parent(args.parent)
, _field(args.field)
, _text(std::move(args.text))
, _setText(std::move(args.setText))
, _relayout(std::move(args.relayout))
, _reserve(std::move(args.reserve))
, _leaf(base::make_unique_q<LeafButton>(args.buttonParent, args.buttonSize))
, _heatLine(base::make_unique_q<HeatLine>(args.parent))
, _panel(base::make_unique_q<Panel>(args.parent))
, _ideas(base::make_unique_q<Ideas>(args.parent, Ideas::Args{
	.pick = [=](QString text) { pickIdea(text); },
	.more = [=] {
		const auto item = (_history && _ideasFor)
			? _history->owner().message(_ideasFor)
			: nullptr;
		if (_ideasFor && !item) {
			closeIdeas();
		} else {
			showIdeas(item, true, false);
		}
	},
	.changed = [=] { reserveChanged(); },
}))
, _readyTimer([=] { readyCheck(); })
, _typingTimer([=] { checkTyping(); }) {
	if (args.mainChat) {
		_runs = std::make_unique<RunCards>(RunCards::Args{
			.panel = _panel.get(),
			.text = _text,
			.setText = [=](TextWithTags text) {
				_setText(std::move(text));
				_field->rawTextEdit()->moveCursor(QTextCursor::End);
			},
			.focus = [=] { _field->setFocus(); },
			.takePanel = [=] { stepBackForCard(); },
			.panelFree = [=] { queueCardSync(); },
		});
	}
	_panel->setChangedCallback([=] { panelChanged(); });
	_leaf->hide();
	_leaf->setClickedCallback([=] { leafClicked(); });
	_field->changes() | rpl::on_next([=] {
		textChanged();
	}, _lifetime);
	Changes() | rpl::on_next([=] {
		refreshModes();
	}, _lifetime);
}

Composer::~Composer() {
	cancelChecks();
	CancelSuggest(base::take(_suggestCall));
	_runs = nullptr;
}

void Composer::panelChanged() {
	reserveChanged();
	if (_runs) {
		_runs->panelChanged();
	}
	if (!_panel->showing()) {
		queueCardSync();
	}
}

void Composer::watchQueue() {
	_queueLifetime.destroy();
	if (!_history) {
		return;
	}
	QueueEvents(
		_history->peer->id
	) | rpl::on_next([=](const QueueEvent &event) {
		// A held one says nothing: its clock and the hold tell it already,
		// and a pill would cover the "Send these N as one" pill.
		if (event.fine && !event.held && !_panel->showing()) {
			_panel->showPill(u"Fine as is, sent straight"_q);
		}
		queueCardSync();
	}, _queueLifetime);
}

void Composer::queueCardSync() {
	if (_cardSyncQueued) {
		return;
	}
	_cardSyncQueued = true;
	crl::on_main(this, [=] {
		_cardSyncQueued = false;
		syncQueueCard();
	});
}

// Second look on the queue: one card at a time, for the oldest message
// waiting for his pick (QueueCardFor), never over his own reading (the
// sheet, reply ideas, the one message card).
void Composer::syncQueueCard() {
	if (!_history) {
		return;
	}
	const auto card = QueueCardFor(_history->peer->id);
	const auto showing = _cardFor && _panel->isCard();
	if (!card) {
		if (showing) {
			closeQueueCard();
		}
		_cardFor = FullMsgId();
		return;
	} else if (showing && _cardFor == card->id) {
		return;
	} else if (_ideas->showing()
		|| _panel->isSheet()
		|| _panel->isSummary()
		|| (_panel->isCard() && !_cardFor)) {
		return; // his own reading first; the card waits for it to close
	}
	showQueueCard(*card);
}

void Composer::showQueueCard(const QueueCard &card) {
	const auto id = card.id;
	_cardFor = id;
	_sendText = QString();
	if (card.failed) {
		_panel->showFailed(u"Couldn't check this one"_q, [=] {
			closeQueueCard();
			QueueSendAsIs(id);
		}, [=] {
			editQueued(id);
		});
		return;
	}
	_panel->showCard(Verdict{
		.soften = true,
		.text = card.calm,
	}, card.original, [=] {
		closeQueueCard();
		QueuePickCalm(id);
	}, [=] {
		closeQueueCard();
		QueuePickMine(id);
	}, [=] {
		editQueued(id);
	});
}

void Composer::editQueued(FullMsgId id) {
	closeQueueCard();
	const auto words = QueueEdit(id);
	if (!words) {
		return;
	}
	// His words back in the field, before anything he typed since.
	const auto current = currentText();
	_setText({ current.trimmed().isEmpty()
		? *words
		: (*words + QChar('\n') + current) });
	_field->setFocus();
	_field->rawTextEdit()->moveCursor(QTextCursor::End);
}

void Composer::closeQueueCard() {
	_cardFor = FullMsgId();
	if (_panel->isCard()) {
		_panel->hideAnimated();
	}
}

// The one message card is about to show: the Second look card steps back
// (it comes again after), the sheet and the ideas close.
void Composer::stepBackForCard() {
	if (_ideas->showing()) {
		closeIdeas();
	}
	CancelDraft(base::take(_sendWaiter));
	_sendText = QString();
	_cardFor = FullMsgId();
}

void Composer::setHistory(History *history) {
	if (_history == history) {
		return;
	}
	// The host calls this from its own layout: never relayout it from here.
	_layingOut = true;
	cancelChecks();
	_panel->hideFast();
	CancelSuggest(base::take(_suggestCall));
	_ideasFor = FullMsgId();
	_ideasAuto = false;
	_leaf->setLit(false);
	_ideas->hideFast();
	_cardFor = FullMsgId();
	if (_runs) {
		_runs->setHistory(history);
	}
	_history = history;
	watchQueue();
	queueCardSync();
	_verdict.reset();
	_verdictText = QString();
	_approved.reset();
	setUnderline({});
	setHeat(0);
	watchLandings();
	refreshModes();
	_layingOut = false;
	queueReserveCheck();
}

void Composer::refreshModes() {
	const auto modes = _history ? ModesFor(_history->peer) : Modes();
	_typing = modes.typing;
	_suggest = modes.suggest;
	_ready = modes.ready;
	if (_runs) {
		_runs->setModes(modes.merge, modes.hold, modes.autoMerge);
	}
	if (!_suggest && _ideas->showing()) {
		closeIdeas();
	}
	if (!_ready) {
		_readyTimer.cancel();
		_readyWaitFor = FullMsgId();
	}
	const auto shown = _typing || _suggest;
	if (_leafShown != shown) {
		_leafShown = shown;
		_leaf->setVisible(_leafShown);
		if (_relayout) {
			_relayout();
		}
	}
	if (!_typing) {
		_typingTimer.cancel();
		CancelDraft(base::take(_typingWaiter));
		setUnderline({});
		setHeat(0);
	} else {
		textChanged();
	}
}

int Composer::leafWidth() const {
	return _leafShown ? _leaf->width() : 0;
}

void Composer::moveLeaf(int left, int top) {
	_leaf->move(left, top);
	// The host hides all its children when it switches chats and shows
	// back only the controls it knows about.
	if (_leafShown && _leaf->isHidden()) {
		_leaf->show();
	}
}

void Composer::setComposeRect(QRect rect) {
	_composeRect = rect;
	_layingOut = true;
	_panel->setAnchor(rect);
	_ideas->setAnchor(rect);
	_layingOut = false;
	_heatLine->setGeometry(rect.x(), rect.y(), rect.width(), S(2));
	_heatLine->restore();
	queueReserveCheck();
}

// The host reads reservedHeight() later in its layout pass; this catches
// a pass that does not.
void Composer::queueReserveCheck() {
	if (_reserve
		&& !_reserveQueued
		&& computeReserve() != _appliedReserve) {
		_reserveQueued = true;
		crl::on_main(this, [=] {
			_reserveQueued = false;
			reserveChanged();
		});
	}
}

int Composer::computeReserve() const {
	return _reserve
		? std::max(_panel->reservedHeight(), _ideas->reservedHeight())
		: 0;
}

int Composer::reservedHeight() const {
	_appliedReserve = computeReserve();
	return _appliedReserve;
}

void Composer::reserveChanged() {
	if (_reserve && !_layingOut && computeReserve() != _appliedReserve) {
		_reserve();
	}
}

QString Composer::currentText() const {
	return _text().text;
}

void Composer::textChanged() {
	const auto text = currentText();
	if (_runs) {
		_runs->fieldChanged(!text.isEmpty());
	}
	if (!text.isEmpty() && _ideas->showing()) {
		closeIdeas(); // the ideas give way to his own words
	}
	if (_verdict && _verdictText != text) {
		setUnderline({});
	}
	if (_panel->isSheet()) {
		_panel->hideAnimated();
	}
	if (_panel->isCard() && !_cardFor && text != _sendText) {
		// The draft the scheduled card holds changed; queued cards stay.
		CancelDraft(base::take(_sendWaiter));
		closePanel();
	}
	if (!_typing || !_history) {
		return;
	}
	if (!Checkable(text)) {
		_typingTimer.cancel();
		CancelDraft(base::take(_typingWaiter));
		_verdict.reset();
		setHeat(0);
		return;
	}
	_typingTimer.callOnce(kTypingPause);
}

void Composer::checkTyping() {
	const auto text = currentText();
	if (!_history || !_typing || !Checkable(text)) {
		return;
	}
	CancelDraft(base::take(_typingWaiter));
	_typingWaiter = CheckDraft(_history, text, [=](const Result &result) {
		_typingWaiter = 0;
		typingAnswered(text, result);
	});
}

void Composer::typingAnswered(const QString &text, const Result &result) {
	if (text != currentText()) {
		return;
	} else if (!result.verdict) {
		if (_sheetWanted) {
			_sheetWanted = false;
			_panel->showPill(u"Calm could not check it right now"_q);
		}
		return;
	}
	_verdict = *result.verdict;
	_verdictText = text;
	setHeat(_verdict->soften ? _verdict->heat : 0);
	setUnderline(_verdict->soften ? _verdict->phrases : QStringList());
	if (base::take(_sheetWanted)) {
		leafClicked();
	}
}

void Composer::leafClicked() {
	if (_ideas->showing()) {
		closeIdeas();
		_field->setFocus();
		return;
	}
	const auto text = currentText();
	if (!_history) {
		return;
	} else if (text.trimmed().isEmpty() && _suggest) {
		showIdeas(nullptr, false, false);
		return;
	} else if (!_typing) {
		_panel->showPill(u"Clear the field for reply ideas"_q);
		return;
	} else if (!Checkable(text)) {
		_panel->showPill(u"Type something and the leaf reads it"_q);
		return;
	}
	if (_verdict && _verdictText == text) {
		if (_verdict->soften) {
			showSheet(*_verdict);
		} else {
			_panel->showPill(u"Reads fine, send it as typed"_q);
		}
		return;
	}
	_sheetWanted = true;
	_typingTimer.cancel();
	checkTyping();
	_panel->showPill(u"Reading it…"_q);
}

void Composer::showSheet(const Verdict &verdict) {
	const auto peer = _history->peer->id;
	_panel->showSheet(verdict, [=] {
		RememberFine(peer, verdict.text);
		closePanel();
		_setText({ verdict.text });
		_field->setFocus();
	}, [=] {
		closePanel();
		_field->setFocus();
	});
}

bool Composer::interceptSend(Fn<void()> resend, bool scheduled) {
	if (!_history) {
		return false;
	}
	const auto draft = _text();
	const auto &text = draft.text;
	if (_runs && !scheduled && _runs->interceptSend(draft)) {
		return true; // the replace he armed with Edit
	}
	if (_approved) {
		const auto approved = (*base::take(_approved) == text);
		if (approved) {
			return false;
		}
	}
	const auto modes = ModesFor(_history->peer);
	if (_cardFor && _panel->isCard() && text.trimmed().isEmpty()) {
		_panel->activatePrimary(); // Enter on an empty field: the card's pick
		return true;
	}
	const auto checkable = (modes.send != SendMode::Off) && Checkable(text);
	if (!scheduled) {
		const auto hold = _runs && modes.hold;
		if (!checkable && !hold) {
			return false;
		}
		CancelDraft(base::take(_sendWaiter));
		if (_panel->isSheet() || (_panel->isCard() && !_cardFor)) {
			closePanel();
		}
		QueueNext(_history, text, QueueSend{
			.check = checkable,
			.review = (modes.send == SendMode::Review),
			.hold = hold,
		});
		return false;
	}
	// A scheduled send cannot wait in the queue: what was held goes now, and
	// Second look holds this draft in the field as before.
	QueueReleaseHeld(_history->peer->id);
	if (!checkable) {
		return false;
	}
	if (_cardFor) {
		_cardFor = FullMsgId();
		_panel->hideFast();
	}
	if (_panel->isCard()) {
		if (_sendText == text) {
			_panel->activatePrimary();
			return true;
		}
		closePanel();
	}
	CancelDraft(base::take(_sendWaiter));
	if (const auto cached = CachedDraft(_history->peer->id, text)) {
		if (!cached->soften) {
			_panel->showPill(u"Fine as is, sent straight"_q);
			return false;
		}
		sendAnswered(draft, { .verdict = *cached }, std::move(resend));
		return true;
	}
	_sendText = text;
	_panel->showChecking(text, [=] {
		CancelDraft(base::take(_sendWaiter));
		closePanel();
		_field->setFocus();
	});
	_sendWaiter = CheckDraft(_history, text, [=](const Result &result) {
		_sendWaiter = 0;
		sendAnswered(draft, result, resend);
	});
	return true;
}

void Composer::sendAnswered(
		const TextWithTags &draft,
		const Result &result,
		Fn<void()> resend) {
	const auto text = draft.text;
	if (!_history || currentText() != text) {
		closePanel();
		return;
	}
	const auto edit = [=] {
		closePanel();
		_field->setFocus();
	};
	if (!result.verdict) {
		LOG(("Safe Calm: no verdict for a draft (%1).").arg(result.failure));
		_sendText = text;
		_panel->showFailed(u"Couldn't check this one"_q, [=] {
			closePanel();
			_approved = text;
			resend();
		}, edit);
		return;
	}
	const auto verdict = *result.verdict;
	if (!verdict.soften) {
		_panel->showPill(u"Fine as is, sent straight"_q);
		_approved = text;
		resend();
		return;
	}
	_sendText = text;
	_panel->showCard(verdict, text, [=] {
		sendCalm(draft, verdict, resend);
	}, [=] {
		closePanel();
		_approved = text;
		resend();
	}, edit);
}

void Composer::sendCalm(
		const TextWithTags &draft,
		const Verdict &verdict,
		Fn<void()> resend) {
	closePanel();
	MarkSentSoftened(_history, verdict.text, draft.text);
	RememberFine(_history->peer->id, verdict.text);
	_setText({ verdict.text });
	_approved = currentText();
	resend();
}

bool Composer::handleEscape() {
	if (_ideas->showing()) {
		closeIdeas();
		return true;
	} else if (_runs && _runs->handleEscape()) {
		return true;
	} else if (_cardFor && _panel->isCard()) {
		// Put away: the row under the bubble offers Open; the next card
		// (if any) comes up.
		const auto id = base::take(_cardFor);
		_panel->hideAnimated();
		QueuePutAway(id, true);
		return true;
	} else if (!_panel->showing()) {
		return false;
	}
	CancelDraft(base::take(_sendWaiter));
	closePanel();
	return true;
}

void Composer::closePanel() {
	_sendText = QString();
	_cardFor = FullMsgId();
	_panel->hideAnimated();
}

void Composer::cancelChecks() {
	_typingTimer.cancel();
	_sheetWanted = false;
	CancelDraft(base::take(_typingWaiter));
	CancelDraft(base::take(_sendWaiter));
}

void Composer::setHeat(int heat) {
	const auto value = std::clamp(heat, 0, 3) / 3.;
	_leaf->setHeat(value);
	_heatLine->setHeat(value);
}

void Composer::setUnderline(const QStringList &phrases) {
	const auto edit = _field->rawTextEdit();
	auto list = QList<QTextEdit::ExtraSelection>();
	if (!phrases.isEmpty()) {
		const auto document = edit->document();
		auto format = QTextCharFormat();
		format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
		format.setUnderlineColor(EmberColor(0.95));
		for (const auto &phrase : phrases) {
			auto cursor = document->find(
				phrase,
				0,
				QTextDocument::FindCaseSensitively);
			while (!cursor.isNull()) {
				list.push_back({ cursor, format });
				cursor = document->find(
					phrase,
					cursor,
					QTextDocument::FindCaseSensitively);
			}
		}
	}
	if (!list.isEmpty() || !edit->extraSelections().isEmpty()) {
		edit->setExtraSelections(list);
	}
}

void Composer::suggestFor(not_null<HistoryItem*> item) {
	if (_history != item->history() || !_suggest) {
		return;
	}
	showIdeas(item, false, false);
	_field->setFocus();
}

void Composer::showIdeas(HistoryItem *replyTo, bool fresh, bool automatic) {
	if (!_history) {
		return;
	}
	CancelSuggest(base::take(_suggestCall));
	if (_panel->isCard() || _panel->isSheet() || _panel->isSummary()) {
		CancelDraft(base::take(_sendWaiter));
		_sendText = QString();
		_cardFor = FullMsgId(); // a queued card comes back after the ideas
		_panel->hideFast();
	}
	_ideasFor = replyTo ? replyTo->fullId() : FullMsgId();
	_ideasAuto = automatic;
	_leaf->setLit(true);
	_ideas->showLoading(replyTo ? Ideas::Look::Panel : Ideas::Look::Chips);
	_suggestCall = Suggest(
		_history,
		replyTo,
		fresh,
		crl::guard(this, [=](int waiting) {
			_ideas->setListening(waiting);
		}),
		crl::guard(this, [=](const SuggestResult &result) {
			_suggestCall = 0;
			ideasAnswered(result);
		}));
}

void Composer::ideasAnswered(const SuggestResult &result) {
	if (!_ideas->showing()) {
		return;
	} else if (result.list.empty()) {
		LOG(("Safe Calm: no reply ideas (%1).").arg(result.failure));
		const auto automatic = _ideasAuto;
		closeIdeas();
		if (!automatic) {
			_panel->showPill(u"Calm could not get ideas right now"_q);
		}
		return;
	}
	_ideas->showList(result.list);
}

void Composer::closeIdeas() {
	CancelSuggest(base::take(_suggestCall));
	_ideasFor = FullMsgId();
	_ideasAuto = false;
	_leaf->setLit(false);
	_ideas->hideAnimated();
	queueCardSync();
}

void Composer::pickIdea(const QString &text) {
	if (!_history) {
		return;
	}
	// A pick is his to send: the typing check reads it as fine.
	RememberFine(_history->peer->id, text);
	closeIdeas();
	_setText({ text });
	_field->setFocus();
	_field->rawTextEdit()->moveCursor(QTextCursor::End);
}

void Composer::watchLandings() {
	_historyLifetime.destroy();
	_readyTimer.cancel();
	_readyWaitFor = FullMsgId();
	if (!_history) {
		return;
	}
	const auto history = _history;
	auto &owner = history->owner();
	owner.newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return (item->history() == history);
	}) | rpl::on_next([=](not_null<HistoryItem*> item) {
		messageLanded(item);
	}, _historyLifetime);
	owner.itemViewRefreshRequest(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return _readyWaitFor && (item->fullId() == _readyWaitFor);
	}) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		if (!IsHeld(item)) {
			_readyWaitFor = FullMsgId();
			_readyTimer.callOnce(kReadyPause);
		}
	}, _historyLifetime);
}

void Composer::messageLanded(not_null<HistoryItem*> item) {
	if (!_ready
		|| IsOwn(item)
		|| !item->isRegular()
		|| item->isService()
		|| (item->originalText().text.trimmed().isEmpty()
			&& !IsVoiceNote(item))) { // a voice note is heard first
		return;
	} else if (IsHeld(item)) {
		// Their side is still softening it: ideas come once it shows.
		_readyWaitFor = item->fullId();
		return;
	}
	_readyWaitFor = FullMsgId();
	_readyTimer.callOnce(kReadyPause);
}

void Composer::readyCheck() {
	if (!_history
		|| !_ready
		|| !currentText().isEmpty()
		|| _panel->isCard()
		|| _panel->isSheet()
		|| _panel->isSummary()
		|| (_ideas->showing() && !_ideasAuto)
		|| _field->isHidden()
		|| !_field->window()->isActiveWindow()) {
		return;
	}
	showIdeas(nullptr, false, true);
}

} // namespace Safe::Calm
