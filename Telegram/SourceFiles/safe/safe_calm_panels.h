/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "safe/safe_calm_prompt.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

// The floating pieces Calm shows above a composer (docs/previews/calm/):
// the Second look card (A), the "Reads hot" sheet (B), the Autopilot toast
// (C) and the small pill. One widget paints all of them, buttons included,
// so a panel fades and slides in as one piece.
namespace Safe::Calm {

struct PanelButton {
	enum class Look : uchar {
		Primary, // jade, full width
		Ghost,
		Ember, // a ghost button in ember ("Send mine")
		Undo,
	};
	QString text;
	Look look = Look::Ghost;
	Fn<void()> callback;
};

class Panel final : public Ui::RpWidget {
public:
	explicit Panel(QWidget *parent);

	// A, while the check is on its way.
	void showChecking(const QString &original, Fn<void()> edit);
	// A: the calm version with Send calm version / Send mine / Edit.
	void showCard(
		const Verdict &verdict,
		const QString &original,
		Fn<void()> sendCalm,
		Fn<void()> sendMine,
		Fn<void()> edit);
	// A and C when neither the runner nor the API answered.
	void showFailed(
		const QString &title,
		Fn<void()> sendAsIs,
		Fn<void()> edit);
	// B: what reads hot and the calm version.
	void showSheet(
		const Verdict &verdict,
		Fn<void()> use,
		Fn<void()> keepEditing);
	// C: "Softened", Undo and a ring that runs out in `duration`.
	void showToast(crl::time duration, Fn<void()> undo);
	// A one-line note that hides itself.
	void showPill(const QString &text);

	void hideAnimated();
	void hideFast();
	[[nodiscard]] bool showing() const;
	[[nodiscard]] bool isCard() const;
	[[nodiscard]] bool isSheet() const;
	// Activates the primary button, if any (Enter while a card is up).
	bool activatePrimary();

	// Where the composer is, in the parent's coordinates.
	void setAnchor(QRect compose);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Kind : uchar {
		None,
		Checking,
		Card,
		Failed,
		Sheet,
		Toast,
		Pill,
	};
	struct Laid {
		PanelButton button;
		QRect rect;
	};

	void start(Kind kind, std::vector<PanelButton> buttons);
	void relayout();
	[[nodiscard]] int contentWidth() const;
	[[nodiscard]] int layoutBody(int top, int width);
	void layoutButtons(int top, int width);
	void paintBody(QPainter &p, int width);
	void paintButton(QPainter &p, const Laid &laid, bool over);
	[[nodiscard]] int buttonAt(QPoint point) const;
	void finishHiding();

	Kind _kind = Kind::None;
	QRect _anchor;
	QString _head;
	QString _hint;
	int _heat = 0;
	// A String measures unwrapped below its minimum resize width.
	Ui::Text::String _main = Ui::Text::String(1);
	Ui::Text::String _yours = Ui::Text::String(1);
	std::vector<Ui::Text::String> _notes;
	QString _calmLabel;
	std::vector<Laid> _buttons;
	int _over = -1;
	int _pressed = -1;

	int _headTop = 0;
	int _mainTop = 0;
	int _mainHeight = 0;
	int _yoursTop = 0;
	std::vector<int> _noteTops;
	int _boxTop = 0;
	int _boxHeight = 0;
	int _laidWidth = 0;
	int _relayoutFor = 0;

	Ui::Animations::Simple _shown;
	Ui::Animations::Basic _ticker;
	crl::time _started = 0;
	crl::time _duration = 0;
	bool _hiding = false;
	base::Timer _autoHide;

};

} // namespace Safe::Calm
