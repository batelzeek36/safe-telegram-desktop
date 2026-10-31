/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "safe/safe_calm_suggest_prompt.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"

#include <memory>

// Reply ideas above the composer (docs/previews/calm/css/next-suggest.css),
// drawn as faint copies of Zach's own outgoing bubbles: things he could
// say, not app buttons. Two looks (Zach 10-25):
// - Chips (S2): three one-line chips stacked where his reply would go, no
//   container, More beside the shortest. The leaf with an empty field, and
//   Ready ideas when a message lands.
// - Panel (S): "Reply ideas" with More over three bubbles, for one message
//   he picked; it sits above Telegram's reply bar.
// Loading is a shimmer of three ghosts; the ideas then unfurl from the
// leaf's corner, one after another. While Telegram is still transcribing
// voice notes for them, More's place says "listening…" with the leaf.
// Ideas he asked for carry the note line under them (safe_calm_thought).
namespace Safe::Calm {

class Thought;

class Ideas final : public Ui::RpWidget {
public:
	enum class Look : uchar {
		Chips,
		Panel,
	};
	struct Args {
		Fn<void(QString)> pick;
		Fn<void()> more;
		Fn<void()> changed; // the room it takes above the composer changed
		Fn<void()> ask; // the note's Ask, or Enter in it, with words in it
		Fn<void()> thoughtCancelled; // Esc in the note's field
		// The stack has gone; its room is released right after this returns.
		Fn<void()> faded;
	};
	Ideas(QWidget *parent, Args &&args);
	~Ideas();

	void showLoading(Look look);
	// Voice notes Telegram is still transcribing for these ideas (0: none).
	void setListening(int notes);
	void showList(const std::vector<Suggestion> &list);
	void hideAnimated();
	void hideFast();

	[[nodiscard]] bool showing() const;
	[[nodiscard]] Look look() const;
	// The room it takes above the composer, kept while it fades out.
	[[nodiscard]] int reservedHeight() const;

	// Where the composer is, in the parent's coordinates.
	void setAnchor(QRect compose);

	// A new stack: the quiet line when he asked for it, nothing on Ready
	// ideas. The note of the stack before is gone.
	void setThoughtOffered(bool offered);
	// What the next request carries; empty when he wrote nothing.
	[[nodiscard]] QString thought() const;
	[[nodiscard]] bool thoughtOpen() const;
	[[nodiscard]] bool thoughtFocused() const;
	void openThought();
	void foldThought();
	bool focusThought();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Item {
		Ui::Text::String text = Ui::Text::String(1);
		QString raw;
		QString angle;
		QRect rect;
		int textTop = 0;
	};

	void relayout();
	void layoutPanel();
	void layoutChips();
	void paintBox(QPainter &p);
	void paintHead(QPainter &p);
	void paintMore(QPainter &p);
	[[nodiscard]] bool listening() const;
	[[nodiscard]] QString slotText() const;
	void paintIdea(QPainter &p, int index);
	void paintGhost(QPainter &p, QRect rect);
	[[nodiscard]] float64 landing(int index) const;
	[[nodiscard]] int hit(QPoint point) const;
	void setOver(int over);
	void pick(int index);
	void finishHiding();
	void notifyChanged();
	void thoughtResized();

	const Fn<void(QString)> _pick;
	const Fn<void()> _more;
	const Fn<void()> _changed;
	const Fn<void()> _ask;
	const Fn<void()> _thoughtCancelled;
	const Fn<void()> _faded;

	Look _look = Look::Chips;
	bool _loading = false;
	int _listening = 0; // voice notes still being transcribed
	bool _hiding = false;
	bool _painting = false;
	std::vector<Item> _items;
	std::vector<QRect> _ghosts;
	QRect _anchor;
	QRect _box; // the panel, in this widget's coordinates
	QRect _moreRect;
	int _headTop = 0;
	int _reserved = 0;
	int _over = -1;
	int _pressed = -1;
	int _picked = -1;

	Ui::Animations::Simple _shown;
	Ui::Animations::Basic _ticker; // the shimmer and the ideas landing
	crl::time _landedAt = 0;
	base::Timer _pickTimer;
	std::unique_ptr<Thought> _thought;

};

} // namespace Safe::Calm
