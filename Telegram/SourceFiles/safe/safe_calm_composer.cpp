/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_composer.h"

#include "base/call_delayed.h"
#include "data/data_peer.h"
#include "history/history.h"
#include "safe/safe_calm.h"
#include "safe/safe_calm_leaf.h"
#include "safe/safe_calm_panels.h"
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

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

} // namespace

class LeafButton final : public Ui::AbstractButton {
public:
	LeafButton(not_null<QWidget*> parent, QSize size);

	void setHeat(float64 heat);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QString accessibilityName() override;

private:
	float64 _heat = 0.;
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

void LeafButton::onStateChanged(State was, StateChangeSource source) {
	update();
}

QString LeafButton::accessibilityName() {
	return u"Calm mode: how this draft reads"_q;
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
, _leaf(base::make_unique_q<LeafButton>(args.buttonParent, args.buttonSize))
, _heatLine(base::make_unique_q<HeatLine>(args.parent))
, _panel(base::make_unique_q<Panel>(args.parent))
, _typingTimer([=] { checkTyping(); }) {
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
}

void Composer::setHistory(History *history) {
	if (_history == history) {
		return;
	}
	cancelChecks();
	_panel->hideFast();
	_history = history;
	_verdict.reset();
	_verdictText = QString();
	_approved.reset();
	setUnderline({});
	setHeat(0);
	refreshModes();
}

void Composer::refreshModes() {
	const auto modes = _history ? ModesFor(_history->peer) : Modes();
	_typing = modes.typing;
	if (_leafShown != _typing) {
		_leafShown = _typing;
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
	_panel->setAnchor(rect);
	_heatLine->setGeometry(rect.x(), rect.y(), rect.width(), S(2));
	_heatLine->restore();
}

QString Composer::currentText() const {
	return _text().text;
}

void Composer::textChanged() {
	const auto text = currentText();
	if (_verdict && _verdictText != text) {
		setUnderline({});
	}
	if (_panel->isSheet()) {
		_panel->hideAnimated();
	}
	if (_panel->isCard() && text != _sendText) {
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
	const auto text = currentText();
	if (!_history || !Checkable(text)) {
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

bool Composer::interceptSend(Fn<void()> resend) {
	if (!_history) {
		return false;
	}
	const auto draft = _text();
	const auto &text = draft.text;
	if (_approved) {
		const auto approved = (*base::take(_approved) == text);
		if (approved) {
			return false;
		}
	}
	const auto modes = ModesFor(_history->peer);
	if (modes.send == SendMode::Off || !Checkable(text)) {
		return false;
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
	} else if (ModesFor(_history->peer).send == SendMode::Autopilot) {
		autopilot(draft, verdict, std::move(resend));
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

void Composer::autopilot(
		const TextWithTags &draft,
		const Verdict &verdict,
		Fn<void()> resend) {
	closePanel();
	MarkSentSoftened(_history, verdict.text, draft.text);
	RememberFine(_history->peer->id, verdict.text);
	_setText({ verdict.text });
	const auto calm = currentText();
	_approved = calm;
	const auto panel = _panel.get();
	const auto restore = _setText;
	const auto field = _field;
	ArmHold(_history, calm, [=](FullMsgId id) {
		panel->showToast(kHoldMs, [=] {
			UndoHold(id);
			panel->hideAnimated();
			restore(draft);
			field->setFocus();
		});
		base::call_delayed(kHoldMs, panel, [=] {
			if (panel->showing() && !panel->isCard() && !panel->isSheet()) {
				panel->hideAnimated();
			}
		});
	});
	resend();
}

bool Composer::handleEscape() {
	if (!_panel->showing()) {
		return false;
	}
	CancelDraft(base::take(_sendWaiter));
	closePanel();
	return true;
}

void Composer::closePanel() {
	_sendText = QString();
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

} // namespace Safe::Calm
