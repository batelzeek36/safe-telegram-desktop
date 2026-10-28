/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "data/data_msg_id.h"
#include "safe/safe_auto_view.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"

class History;

namespace Safe::Auto {

struct Entry;

// The bar above the composer while full-auto runs in a chat (mocks:
// docs/previews/auto/, "The bar above the composer"; Zach 10-26: always
// showing in a full-auto chat). On the Mac it is a rounded card in the
// composer's top slot, the shape the floating look gives the reply row
// (safe/look_chat.cpp). It changes in place: one height change with the
// words cross-fading, the old words leaving fast, the card's top edge the
// only thing that travels.
class Bar final : public Ui::RpWidget {
public:
	struct Args {
		not_null<QWidget*> parent;
		// The host moves the chat up by reservedHeight().
		Fn<void()> reserve;
		// Reply yourself: the message goes in the reply slot, the field
		// gets the focus.
		Fn<void(FullMsgId)> replyYourself;
	};
	explicit Bar(Args &&args);
	~Bar();

	// The chat the composer shows (nullptr when there is no field). Called
	// from the host's own layout: never relayouts the host directly.
	void setHistory(History *history);
	// The top of the compose area (reply row included), parent coordinates.
	void moveAbove(int composeTop, int width);
	[[nodiscard]] int reservedHeight() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Action : uchar {
		None,
		Pause, // 30 min (kPauseMinutes), then on again by itself
		Resume, // a pause ends now
		ReplyYourself,
		LetItHandle,
	};
	enum class ButtonLook : uchar {
		Text, // the one-line bar's action ("Pause 30 min")
		Primary,
		Ghost,
	};
	struct Button {
		QString text;
		ButtonLook look = ButtonLook::Text;
		Action action = Action::None;
		QRect rect; // in the face's own box, its bottom on the card's
	};
	struct Face {
		ViewOutput view;
		QString title;
		QString sub;
		QString head;
		Ui::Text::String reason = Ui::Text::String(1);
		std::vector<Button> buttons;
		int height = 0;
		int reasonTop = 0;
		int reasonHeight = 0;
	};

	void refresh();
	[[nodiscard]] std::unique_ptr<Face> makeFace(
		const Entry &entry,
		const ViewOutput &view) const;
	void layoutFace(Face &face, int width) const;
	[[nodiscard]] QString signature(const Face &face) const;
	void morphTo(std::unique_ptr<Face> face);
	void heightChanged();
	void queueReserve();
	[[nodiscard]] int cardWidth() const;
	[[nodiscard]] int targetHeight() const;
	[[nodiscard]] QRect cardRect() const;
	[[nodiscard]] int buttonAt(QPoint point) const;
	void activate(Action action);
	void paintFace(
		QPainter &p,
		const Face &face,
		QRect card,
		float64 opacity,
		bool interactive);
	void paintButton(QPainter &p, const Button &button, QRect rect, bool over);
	void paintTint(QPainter &p, BarKind kind, QRect card, float64 opacity);

	const Fn<void()> _reserve;
	const Fn<void(FullMsgId)> _replyYourself;
	History *_history = nullptr;
	int64 _chat = 0;

	std::unique_ptr<Face> _face;
	std::unique_ptr<Face> _was;
	QString _signature;
	Ui::Animations::Simple _fade;
	Ui::Animations::Simple _height;
	int _heightFrom = 0;
	int _composeTop = 0;
	int _width = 0;
	int _over = -1;
	int _pressed = -1;
	bool _busy = false;
	bool _instant = false;
	bool _reserveQueued = false;

	base::Timer _clock;
	rpl::lifetime _historyLifetime;

};

} // namespace Safe::Auto
