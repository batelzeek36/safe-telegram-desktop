/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/unique_qptr.h"
#include "safe/safe_calm_prompt.h"

class History;

namespace Ui {
class InputField;
class RpWidget;
} // namespace Ui

namespace Safe::Calm {

class LeafButton;
class HeatLine;
class Panel;

// Calm on one message composer (HistoryWidget's own and ComposeControls'):
// the leaf in the bar with its heat line and the wavy underline (B), and
// the send intercept for Second look (A) and Autopilot (C).
class Composer final {
public:
	struct Args {
		not_null<Ui::RpWidget*> parent; // the panels and the heat line
		not_null<QWidget*> buttonParent; // the leaf button
		not_null<Ui::InputField*> field;
		QSize buttonSize;
		Fn<TextWithTags()> text; // what Send would send
		Fn<void(TextWithTags)> setText;
		Fn<void()> relayout; // the leaf was shown or hidden
	};
	explicit Composer(Args &&args);
	~Composer();

	void setHistory(History *history);

	// The width the leaf takes in the bar (zero while hidden).
	[[nodiscard]] int leafWidth() const;
	void moveLeaf(int left, int top);
	// The compose bar, in the parent's coordinates.
	void setComposeRect(QRect rect);

	// True when Calm took this send; `resend` runs again (and passes) once
	// Zach chose, or the calm version was armed.
	[[nodiscard]] bool interceptSend(Fn<void()> resend);
	bool handleEscape();

private:
	void refreshModes();
	void textChanged();
	void checkTyping();
	void typingAnswered(const QString &text, const Result &result);
	void leafClicked();
	void showSheet(const Verdict &verdict);
	void sendAnswered(
		const TextWithTags &draft,
		const Result &result,
		Fn<void()> resend);
	void sendCalm(
		const TextWithTags &draft,
		const Verdict &verdict,
		Fn<void()> resend);
	void autopilot(
		const TextWithTags &draft,
		const Verdict &verdict,
		Fn<void()> resend);
	void setHeat(int heat);
	void setUnderline(const QStringList &phrases);
	void cancelChecks();
	void closePanel();
	[[nodiscard]] QString currentText() const;

	const not_null<Ui::RpWidget*> _parent;
	const not_null<Ui::InputField*> _field;
	const Fn<TextWithTags()> _text;
	const Fn<void(TextWithTags)> _setText;
	const Fn<void()> _relayout;
	History *_history = nullptr;

	base::unique_qptr<LeafButton> _leaf;
	base::unique_qptr<HeatLine> _heatLine;
	base::unique_qptr<Panel> _panel;
	QRect _composeRect;
	bool _leafShown = false;
	bool _typing = false;

	base::Timer _typingTimer;
	uint64 _typingWaiter = 0;
	uint64 _sendWaiter = 0;
	QString _sendText;
	std::optional<Verdict> _verdict; // for the current text, from typing
	QString _verdictText;
	bool _sheetWanted = false;
	std::optional<QString> _approved;
	rpl::lifetime _lifetime;

};

} // namespace Safe::Calm
