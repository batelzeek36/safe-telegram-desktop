/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_thought.h"

#include "safe/safe_calm_leaf.h"
#include "safe/safe_calm_marks.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "ui/widgets/fields/input_field.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_layers.h"

#include <QtWidgets/QTextEdit>

namespace Safe::Calm {
namespace {

// Logical pixels, after docs/previews/calm/css/thought.css (.th-note).
constexpr auto kPillHeight = 30;
constexpr auto kPadLeft = 9;
constexpr auto kPadRight = 11;
constexpr auto kIconGap = 6;
constexpr auto kCloud = 15;
constexpr auto kPencil = 14;
constexpr auto kOpenPadLeft = 11;
constexpr auto kOpenPad = 5;
constexpr auto kOpenMin = 42;
constexpr auto kOpenRadius = 21;
constexpr auto kFieldMin = 32;
constexpr auto kFieldMargin = 6;
constexpr auto kFieldMax = 132;
constexpr auto kAskHeight = 32;
constexpr auto kAskLeaf = 16;
constexpr auto kAskPadLeft = 8;
constexpr auto kAskPadRight = 12;
constexpr auto kAskGap = 4;

constexpr auto kWidthMs = crl::time(360);
constexpr auto kSurfaceMs = crl::time(240);
constexpr auto kAskInMs = crl::time(340);

constexpr auto kOpenBorder = 0.42;
constexpr auto kFoldedBorder = 0.24;
constexpr auto kLineBorder = 0.24;
constexpr auto kEmberSoft = 0.82;
constexpr auto kOverLift = 0.12;

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QString HintText() {
	return u"only Claude sees this"_q;
}

[[nodiscard]] QString LineText() {
	return u"what are you thinking?"_q;
}

[[nodiscard]] QString AskText() {
	return u"Ask"_q;
}

[[nodiscard]] const style::font &LineFont() {
	return st::semiboldFont;
}

[[nodiscard]] const style::font &FoldedFont() {
	return st::normalFont;
}

// The composer's field in type and behaviour, on the note's own surface; the
// hint and nothing else in his raw words' ember.
[[nodiscard]] const style::InputField &FieldStyle() {
	static const auto ember = new style::owned_color(EmberColor(kEmberSoft));
	static const auto result = [] {
		auto st = st::historyComposeField;
		st.textBg = st::transparent;
		st.textBgActive = st::transparent;
		st.textFg = st::windowFg;
		st.placeholderFg = ember->color();
		st.placeholderFgActive = ember->color();
		st.placeholderFgError = ember->color();
		st.textMargins = style::margins(0, S(kFieldMargin), 0, S(kFieldMargin));
		st.placeholderMargins = style::margins(S(2), 0, S(2), 0);
		st.heightMin = S(kFieldMin);
		st.heightMax = S(kFieldMax);
		return st;
	}();
	return result;
}

[[nodiscard]] int AskWidth() {
	return S(kAskPadLeft)
		+ S(kAskLeaf)
		+ S(kAskGap)
		+ LineFont()->width(AskText())
		+ S(kAskPadRight);
}

} // namespace

// The leaf that goes to Claude, never to the chat: the approved primary jade
// (Send calm version, Use this). The word is there because the composer's own
// leaf sits right under it and only this one asks.
class Thought::AskButton final : public Ui::AbstractButton {
public:
	explicit AskButton(not_null<QWidget*> parent);

	void setReady(bool ready);

protected:
	void paintEvent(QPaintEvent *e) override;
	void onStateChanged(State was, StateChangeSource source) override;
	QString accessibilityName() override;

private:
	bool _ready = false;
	Ui::Animations::Simple _in;

};

Thought::AskButton::AskButton(not_null<QWidget*> parent)
: AbstractButton(parent) {
	resize(AskWidth(), S(kAskHeight));
	_in.start([=] { update(); }, 0., 1., kAskInMs, anim::easeOutCirc);
}

void Thought::AskButton::setReady(bool ready) {
	if (_ready != ready) {
		_ready = ready;
		setPointerCursor(ready);
		update();
	}
}

void Thought::AskButton::onStateChanged(State was, StateChangeSource source) {
	update();
}

QString Thought::AskButton::accessibilityName() {
	return u"Ask for three new ideas"_q;
}

void Thought::AskButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto in = _in.value(1.);
	if (in < 1.) {
		const auto center = QRectF(rect()).center();
		p.translate(center);
		p.scale(0.5 + 0.5 * in, 0.5 + 0.5 * in);
		p.translate(-center);
	}
	// Nothing to ask with yet: it waits, dimmed, and does nothing.
	p.setOpacity(in * (_ready ? 1. : 0.5));
	const auto r = QRectF(rect());
	const auto radius = r.height() / 2.;
	auto gradient = QLinearGradient(r.topLeft(), r.bottomRight());
	gradient.setColorAt(0., QColor(0x3A, 0xD5, 0x96));
	gradient.setColorAt(1., QColor(0x22, 0xA8, 0x73));
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(r, radius, radius);
	if (_ready && isOver()) {
		p.setBrush(QColor(255, 255, 255, 28));
		p.drawRoundedRect(r, radius, radius);
	}
	const auto fg = QColor(0x04, 0x26, 0x1A);
	const auto leaf = S(kAskLeaf);
	PaintLeaf(
		p,
		QRectF(S(kAskPadLeft), (height() - leaf) / 2., leaf, leaf),
		leaf,
		fg);
	p.setFont(LineFont());
	p.setPen(fg);
	p.drawText(
		S(kAskPadLeft) + leaf + S(kAskGap),
		(height() - LineFont()->height) / 2 + LineFont()->ascent,
		AskText());
}

Thought::Thought(Args &&args)
: _parent(args.parent)
, _ask(std::move(args.ask))
, _resized(std::move(args.resized))
, _cancelled(std::move(args.cancelled)) {
}

Thought::~Thought() {
	destroyField();
}

int Thought::height() const {
	if (_state != State::Open) {
		return S(kPillHeight);
	} else if (_frozenHeight) {
		return _frozenHeight;
	}
	return std::max(
		S(kOpenMin),
		(_field ? _field->height() : S(kFieldMin)) + 2 * S(kOpenPad));
}

void Thought::moveTo(QRect row) {
	const auto resized = (_row.width() != row.width());
	_row = row;
	placeChildren();
	if (resized) {
		updateShown();
	}
}

void Thought::updateShown() {
	if (_state != State::Folded) {
		_shown = QString();
		return;
	}
	const auto inner = _row.width()
		- S(kPadLeft)
		- S(kCloud)
		- S(kIconGap)
		- S(kIconGap)
		- S(kPencil)
		- S(kPadRight);
	_shown = FoldedFont()->elided(
		QString(_kept).replace(QChar('\n'), QChar(' ')).trimmed(),
		std::max(inner, 1));
}

void Thought::setRowLimit(int limit) {
	const auto clamped = std::clamp(
		limit - 2 * S(kOpenPad),
		S(kFieldMin),
		S(kFieldMax));
	if (_fieldLimit == clamped) {
		return;
	}
	_fieldLimit = clamped;
	if (_field) {
		_field->setMaxHeight(_fieldLimit);
	}
}

bool Thought::opened() const {
	return (_state == State::Open);
}

void Thought::open() {
	if (_state == State::Open) {
		focus();
		return;
	}
	const auto from = pillRect().width();
	_state = State::Open;
	createField();
	animateWidth(from);
	_surface.start([=] { repaint(); }, 0., 1., kSurfaceMs, anim::easeOutCirc);
	fieldChanged();
	focus();
	_resized();
}

void Thought::fold() {
	if (_state != State::Open) {
		return;
	}
	_kept = _field ? _field->getLastText() : QString();
	const auto from = pillRect().width();
	destroyField();
	_state = _kept.trimmed().isEmpty() ? State::Line : State::Folded;
	if (_state == State::Line) {
		_kept = QString();
	}
	updateShown();
	animateWidth(from);
	_surface.start([=] { repaint(); }, 1., 0., kSurfaceMs, anim::easeOutCirc);
	_resized();
}

void Thought::freeze() {
	if (_field) {
		_frozenHeight = height();
		_frozenFieldAt = _field->pos();
		_frozenField = Ui::GrabWidget(_field.get());
		_frozenButtonAt = _button->pos();
		_frozenButton = Ui::GrabWidget(_button.get());
		destroyField();
	}
	_kept = QString();
	_over = false;
}

QString Thought::words() const {
	const auto &raw = _field ? _field->getLastText() : _kept;
	return raw.trimmed().left(kThoughtMaxLength);
}

bool Thought::focused() const {
	return _field && Ui::InFocusChain(_field.get());
}

bool Thought::focus() {
	if (!_field) {
		return false;
	}
	_field->setFocus();
	return true;
}

bool Thought::hits(QPoint point) const {
	return (_state != State::Open)
		&& _frozenField.isNull()
		&& pillRect().contains(point);
}

void Thought::setOver(bool over) {
	if (_over != over) {
		_over = over;
		repaint();
	}
}

void Thought::createField() {
	_field = base::make_unique_q<Ui::InputField>(
		_parent.get(),
		FieldStyle(),
		Ui::InputField::Mode::MultiLine,
		rpl::single(HintText()));
	_field->setMaxLength(kThoughtMaxLength);
	if (_fieldLimit > 0) {
		_field->setMaxHeight(_fieldLimit);
	}
	_field->setSubmitSettings(Ui::InputField::SubmitSettings::Enter);
	_field->setText(_kept);
	_field->rawTextEdit()->moveCursor(QTextCursor::End);
	_field->show();
	_field->changes() | rpl::on_next([=] {
		fieldChanged();
	}, _field->lifetime());
	_field->heightChanges() | rpl::on_next([=] {
		_resized();
	}, _field->lifetime());
	_field->submits() | rpl::on_next([=] {
		if (!words().isEmpty()) {
			_ask();
		}
	}, _field->lifetime());
	_field->cancelled() | rpl::on_next([=] {
		_cancelled();
	}, _field->lifetime());
	_button = base::make_unique_q<AskButton>(_parent.get());
	_button->setClickedCallback([=] {
		if (!words().isEmpty()) {
			_ask();
		}
	});
	_button->show();
	placeChildren();
}

// Later, not now: Enter and Esc end up here from inside the field's own key
// handling, and Ask from inside its button's click.
void Thought::destroyField() {
	if (_button) {
		_button->hide();
		_button.release()->deleteLater();
	}
	if (_field) {
		_field->hide();
		_field.release()->deleteLater();
	}
}

void Thought::fieldChanged() {
	if (_button) {
		_button->setReady(!words().isEmpty());
	}
}

void Thought::placeChildren() {
	if (!_field) {
		return;
	}
	const auto left = _row.x()
		+ S(kOpenPadLeft)
		+ S(kCloud)
		+ S(kIconGap);
	const auto width = _row.x() + _row.width()
		- S(kOpenPad)
		- _button->width()
		- S(kIconGap)
		- left;
	_field->resizeToWidth(std::max(width, 1));
	_field->moveToLeft(left, _row.y() + S(kOpenPad));
	const auto pill = pillRect();
	_button->move(
		pill.x() + pill.width() - S(kOpenPad) - _button->width(),
		_row.y() + height() - S(kOpenPad) - _button->height());
	_button->raise();
}

void Thought::animateWidth(int from) {
	_width.stop();
	const auto to = restingWidth();
	if (from == to || anim::Disabled()) {
		repaint();
		return;
	}
	_width.start([=] {
		if (_field) {
			placeChildren();
		}
		repaint();
	}, float64(from), float64(to), kWidthMs, anim::easeOutCirc);
}

int Thought::lineWidth() const {
	return S(kPadLeft)
		+ S(kCloud)
		+ S(kIconGap)
		+ LineFont()->width(LineText())
		+ S(kPadRight);
}

int Thought::restingWidth() const {
	return (_state == State::Line)
		? std::min(lineWidth(), _row.width() ? _row.width() : lineWidth())
		: _row.width();
}

QRect Thought::pillRect() const {
	const auto width = _width.animating()
		? int(base::SafeRound(_width.value(restingWidth())))
		: restingWidth();
	return QRect(_row.x(), _row.y(), std::max(width, 1), height());
}

void Thought::repaint() const {
	_parent->update();
}

void Thought::paint(QPainter &p) {
	if (_row.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto open = _surface.value((_state == State::Open) ? 1. : 0.);
	const auto rect = QRectF(pillRect());
	const auto radius = std::min(rect.height() / 2., float64(S(kOpenRadius)));
	const auto folded = (_state == State::Folded)
		|| (_state == State::Open && !_kept.trimmed().isEmpty());

	// More's family at rest; the approved card surface while open.
	const auto rest = folded
		? EmberColor(kFoldedBorder)
		: JadeColor(kLineBorder);
	p.setPen(QPen(
		anim::color(rest, EmberColor(kOpenBorder), open),
		st::lineWidth));
	p.setBrush(anim::color(st::msgServiceBg, st::boxBg, open));
	p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

	const auto tick = TickColor(st::msgServiceFg->c);
	if (_over && _state != State::Open) {
		auto lift = tick;
		lift.setAlphaF(kOverLift);
		p.setPen(Qt::NoPen);
		p.setBrush(lift);
		p.drawRoundedRect(rect, radius, radius);
	}

	const auto cloudLeft = rect.x()
		+ S(kPadLeft)
		+ (S(kOpenPadLeft) - S(kPadLeft)) * open;
	const auto restTop = rect.y() + (S(kPillHeight) - S(kCloud)) / 2.;
	const auto openTop = rect.y()
		+ S(kOpenPad)
		+ (S(kFieldMin) - S(kCloud)) / 2.;
	const auto cloudColor = (_state == State::Line && open <= 0.)
		? tick
		: EmberColor(kEmberSoft);
	PaintCloud(
		p,
		QRectF(
			cloudLeft,
			restTop + (openTop - restTop) * open,
			S(kCloud),
			S(kCloud)),
		cloudColor);

	const auto textLeft = int(cloudLeft) + S(kCloud) + S(kIconGap);
	if (!_frozenField.isNull()) {
		p.drawPixmap(_frozenFieldAt, _frozenField);
		p.drawPixmap(_frozenButtonAt, _frozenButton);
	} else if (_state == State::Line) {
		p.setFont(LineFont());
		p.setPen(tick);
		p.drawText(
			textLeft,
			int(rect.y()) + (S(kPillHeight) - LineFont()->height) / 2
				+ LineFont()->ascent,
			LineText());
	} else if (_state == State::Folded) {
		p.setFont(FoldedFont());
		p.setPen(EmberColor(kEmberSoft));
		p.drawText(
			textLeft,
			int(rect.y()) + (S(kPillHeight) - FoldedFont()->height) / 2
				+ FoldedFont()->ascent,
			_shown);
		PaintPencil(
			p,
			QRectF(
				rect.x() + rect.width() - S(kPadRight) - S(kPencil),
				rect.y() + (S(kPillHeight) - S(kPencil)) / 2.,
				S(kPencil),
				S(kPencil)),
			EmberColor(0.8));
	}
}

} // namespace Safe::Calm
