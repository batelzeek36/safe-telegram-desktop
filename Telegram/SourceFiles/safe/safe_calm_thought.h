/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "ui/effects/animations.h"

namespace Ui {
class InputField;
class RpWidget;
} // namespace Ui

// The note under the reply ideas (Zach 10-30: "an option somewhere to tell
// the model what im thinking so it can give better suggestions than the
// crude way id like to say it"; Take A of docs/previews/calm/thought.html).
// A quiet line in More's family; clicked, its own field, "only Claude sees
// this", with an Ask button; asked, it folds to one line of his words with a
// pencil, in the ember of his raw words.
//
// It is not the composer: a field of its own, so nothing typed here can reach
// the chat, a draft or the typing status, and Calm's check never reads it.
// The words live in this object alone, in memory: Ideas destroys it when the
// ideas close, on a pick and on leaving the chat.
namespace Safe::Calm {

// suggest.MAX_THOUGHT in calm-runner/suggest.py.
inline constexpr auto kThoughtMaxLength = 1000;

class Thought final {
public:
	struct Args {
		not_null<Ui::RpWidget*> parent; // paints through paint()
		Fn<void()> ask; // Ask or Enter with words in the field
		Fn<void()> resized; // height() changed
		Fn<void()> cancelled; // Esc in the field
	};
	explicit Thought(Args &&args);
	~Thought();

	// One row of the ideas stack, in the parent's coordinates.
	[[nodiscard]] int height() const;
	void moveTo(QRect row);
	// How tall the row may grow; past that the field scrolls inside itself.
	void setRowLimit(int limit);

	[[nodiscard]] bool opened() const;
	void open();
	// His words stay, on one line; no words: back to the quiet line.
	void fold();
	// The stack is fading out: a picture of the field for its last frames;
	// the words are gone from here on.
	void freeze();

	// What a request carries: trimmed and capped, empty when he wrote nothing.
	[[nodiscard]] QString words() const;
	[[nodiscard]] bool focused() const;
	bool focus();

	[[nodiscard]] bool hits(QPoint point) const;
	void setOver(bool over);
	void paint(QPainter &p);

private:
	enum class State : uchar {
		Line,
		Open,
		Folded,
	};
	class AskButton;

	void createField();
	void destroyField();
	void fieldChanged();
	void placeChildren();
	void animateWidth(int from);
	void updateShown();
	[[nodiscard]] int restingWidth() const;
	[[nodiscard]] int lineWidth() const;
	[[nodiscard]] QRect pillRect() const;
	void repaint() const;

	const not_null<Ui::RpWidget*> _parent;
	const Fn<void()> _ask;
	const Fn<void()> _resized;
	const Fn<void()> _cancelled;

	State _state = State::Line;
	QString _kept; // as he typed it, while the field is folded away
	QString _shown; // one line of _kept, elided to the row
	base::unique_qptr<Ui::InputField> _field;
	base::unique_qptr<AskButton> _button;
	QRect _row;
	int _fieldLimit = 0;
	bool _over = false;
	QPixmap _frozenField;
	QPixmap _frozenButton;
	QPoint _frozenFieldAt;
	QPoint _frozenButtonAt;
	int _frozenHeight = 0;

	Ui::Animations::Simple _width;
	Ui::Animations::Simple _surface;

};

} // namespace Safe::Calm
