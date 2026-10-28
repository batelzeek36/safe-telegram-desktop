/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "safe/safe_circles_core.h"
#include "ui/effects/animations.h"
#include "ui/rp_widget.h"

class History;
class Painter;

namespace Window {
class SessionController;
} // namespace Window

namespace Safe::Circles {

class Dial;
class Model;

// The chat circles in place of the chat list: a header with the circle's
// name (and its neighbours peeking in), one dial per circle, page dots.
//
// Spin: drag round the dial, or scroll with two fingers; let go and it
// lands once on a chat (Release in safe_circles_core.h). Arrow keys step,
// Return opens, Page Up / Page Down change circle. Change circle: swipe
// sideways with two fingers (velocity counts, not only distance), drag
// sideways off the dial, click a neighbour's name or a dot. Clicking a face
// brings it round and opens the chat; clicking the hub (Crown) or the card
// (Orbit) opens the focused chat. The circle shown is Telegram's active
// folder, so the folder shortcuts and the list underneath stay in step.
class View final : public Ui::RpWidget {
public:
	View(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void(not_null<History*>)> open);
	~View();

	void setLayout(Layout layout);
	[[nodiscard]] Layout layout() const;

	// A short wind-up when a layout is first picked: the dial swings in
	// and lands on the chat it shows.
	void intro();

	// Keys the chat list gets while the circles show: true when used.
	bool handleKey(not_null<QKeyEvent*> e);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void leaveEventHook(QEvent *e) override;

	QAccessible::Role accessibilityRole() override;
	QString accessibilityName() override;
	QString accessibilityValue() const override;

private:
	struct Spin {
		double position = 0.;
		Glide glide;
		double glideStart = 0.;
		bool gliding = false;
		History *focused = nullptr;
	};
	enum class Drag {
		None,
		Pending,
		Spin,
		Page,
		Ignored,
	};
	enum class Scroll {
		None,
		Undecided,
		Spin,
		Page,
		Released,
	};
	struct Press {
		QPointF point;
		int face = -1;
		int header = -1;
		int dot = -1;
		bool opens = false;
		bool grabs = false;
	};

	[[nodiscard]] double now() const;
	[[nodiscard]] int pageCount() const;
	[[nodiscard]] int currentPage() const;
	[[nodiscard]] FilterId circleAt(int page) const;
	[[nodiscard]] Spin &spinOf(FilterId id);
	[[nodiscard]] const std::vector<not_null<History*>> &chatsOf(FilterId id);
	[[nodiscard]] History *focusedOf(FilterId id);
	[[nodiscard]] double spinVelocity(FilterId id);

	[[nodiscard]] int headerHeight() const;
	[[nodiscard]] int dotsHeight() const;
	[[nodiscard]] QRect pageRect() const;
	[[nodiscard]] QPointF toPage(QPointF point) const;

	// The dial of the current circle.
	void moveTo(double position);
	void glideTo(double target, double velocity, double maxDuration);
	void release(double velocity);
	void step(int delta);
	void bringTo(int index, bool andOpen);
	void openFocused();
	// The next chat with unread messages after the focus (wrapping), or -1.
	[[nodiscard]] int nextUnread();
	void catchSpin();
	void settle();
	void settled(FilterId id);
	void noteFocus(FilterId id);

	// The circles.
	void goToPage(int page, double velocity);
	void releasePage(double velocity);
	void movePageTo(double raw);
	void catchPage();

	void startAnimation();
	bool animationStep(crl::time now);

	void refreshCircles();
	void refreshOrder();
	void followActiveChat(History *history);
	void followActiveCircle(FilterId id);
	void checkLoadMore(FilterId id);

	void updateHover(QPointF point);
	[[nodiscard]] int headerItemAt(QPointF point) const;
	[[nodiscard]] int dotAt(QPointF point) const;
	[[nodiscard]] QRect headerItemRect(int page) const;
	[[nodiscard]] double headerSpacing() const;

	void scrollUpdate(QPointF delta);
	void scrollRelease();

	void paintPage(Painter &p, int page, double shift);
	void paintHeader(Painter &p);
	void paintDots(Painter &p);

	const not_null<Window::SessionController*> _controller;
	const Fn<void(not_null<History*>)> _open;
	const std::unique_ptr<Model> _model;
	std::unique_ptr<Dial> _dial;
	Layout _layout = Layout::Crown;

	std::vector<FilterId> _circles;
	base::flat_map<FilterId, Spin> _spins;

	double _page = 0.;
	double _pageRaw = 0.;
	int _pageStart = 0;
	double _pageFrom = 0.;
	Glide _pageGlide;
	double _pageGlideStart = 0.;
	bool _pageGliding = false;

	Drag _drag = Drag::None;
	Press _press;
	QPointF _last;
	Tracker _tracker;
	int _caughtDirection = 0;

	Scroll _scroll = Scroll::None;
	QPointF _scrolled;
	double _scrollStartPosition = 0.;
	base::Timer _scrollIdle;
	int _wheelNotches = 0;
	int _wheelNotchesX = 0;

	double _lean = 0.;
	double _tickAt = -10.;
	double _movedAt = -10.; // the last frame the dial or the page moved
	double _lastFrame = 0.;
	int _hovered = -1;
	int _hoveredHeader = -1;
	int _hoveredDot = -1;

	Ui::Animations::Basic _animation;

};

} // namespace Safe::Circles
