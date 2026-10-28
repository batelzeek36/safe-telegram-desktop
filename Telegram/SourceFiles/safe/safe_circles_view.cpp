/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_view.h"

#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "main/main_session.h"
#include "safe/safe_circles_dial.h"
#include "safe/safe_circles_model.h"
#include "safe/safe_circles_paint.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/unread_badge_paint.h"
#include "window/window_session_controller.h"

#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_safe_circles.h"

#include <QtGui/QCursor>
#include <QtGui/QWheelEvent>

namespace Safe::Circles {
namespace {

constexpr auto kHeaderHeight = 52;
constexpr auto kDotsHeight = 44; // the dots' rows and targets are 44pt
constexpr auto kDotTarget = 44;
constexpr auto kDotSize = 7;
constexpr auto kDotActiveWidth = 22;
constexpr auto kClickSlop = 4;
constexpr auto kPulse = 0.24; // seconds the tick glows after a chat passes
constexpr auto kLeanFollow = 0.12; // seconds for the leaf to catch the spin
constexpr auto kPageGlide = 0.42;
constexpr auto kBringGlide = 0.6;
constexpr auto kIntroSlots = 2.5;
constexpr auto kIntroSpeed = 9.;
constexpr auto kLoadAhead = 12;
constexpr auto kNoPhaseRelease = crl::time(120);
// Names hide while the dial moves (a pill a detent strobes) and come back
// once it has landed.
constexpr auto kNamesDelay = 0.15;
constexpr auto kNamesFade = 0.2;
// The hub or the card shows the chat under the pointer: its words fade in.
constexpr auto kPreviewFade = 0.16;
constexpr auto kHintDuration = crl::time(150);
constexpr auto kHintLift = 7;

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] double EaseOutCubic(double t) {
	const auto left = 1. - std::clamp(t, 0., 1.);
	return 1. - left * left * left;
}

// A mouse wheel: no scroll phase (a trackpad always has one on macOS; a
// wheel elsewhere brings no pixel delta). It turns one chat a notch, at once,
// never by its pixels: macOS gives a wheel's notch pixels too, and turning by
// them stopped half way and rolled back.
[[nodiscard]] bool DiscreteWheel(not_null<QWheelEvent*> e) {
	if (e->phase() != Qt::NoScrollPhase) {
		return false;
	}
#ifdef Q_OS_MAC
	return true;
#else // Q_OS_MAC
	return e->pixelDelta().isNull();
#endif // Q_OS_MAC
}

// One notch as 120 eighths of a degree. macOS sends one event a notch, its
// size scaled by how fast the wheel turns: each event is one notch there.
[[nodiscard]] QPoint WheelNotches(not_null<QWheelEvent*> e) {
	const auto angle = e->angleDelta();
#ifdef Q_OS_MAC
	const auto sign = [](int value) {
		return (value > 0) ? 120 : (value < 0) ? -120 : 0;
	};
	return QPoint(sign(angle.x()), sign(angle.y()));
#else // Q_OS_MAC
	return angle;
#endif // Q_OS_MAC
}

} // namespace

View::View(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void(not_null<History*>)> open)
: RpWidget(parent)
, _controller(controller)
, _open(std::move(open))
, _model(std::make_unique<Model>(&controller->session()))
, _dial(MakeCrown())
, _scrollIdle([=] { scrollRelease(); })
, _animation([=](crl::time now) { return animationStep(now); }) {
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	refreshCircles();
	_page = _pageRaw = _pageStart = std::clamp(
		CircleIndexOf(
			std::vector<std::int32_t>(begin(_circles), end(_circles)),
			_controller->activeChatsFilterCurrent()),
		0,
		std::max(pageCount() - 1, 0));

	_model->circlesChanges(
	) | rpl::on_next([=] {
		refreshCircles();
		update();
	}, lifetime());

	_model->orderChanges(
	) | rpl::on_next([=] {
		refreshOrder();
		update();
	}, lifetime());

	_model->repaints(
	) | rpl::on_next([=] {
		update();
	}, lifetime());

	_controller->activeChatsFilter(
	) | rpl::on_next([=](FilterId id) {
		followActiveCircle(id);
	}, lifetime());

	// Only a different chat turns the dial: the entry is sent again for the
	// same chat (a jump inside it, a layer opening), and that must not undo
	// a spin he made without opening anything.
	_controller->activeChatEntryValue(
	) | rpl::map([](const Dialogs::RowDescriptor &row) {
		return row.key.history();
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](History *history) {
		followActiveChat(history);
	}, lifetime());
}

View::~View() = default;

void View::setLayout(Layout layout) {
	if (layout == Layout::List || layout == _layout) {
		return;
	}
	_layout = layout;
	_dial = (layout == Layout::Orbit) ? MakeOrbit() : MakeCrown();
	_dial->resize(pageRect().size());
	_hovered = -1;
	accessibilityNameChanged();
	update();
}

Layout View::layout() const {
	return _layout;
}

void View::intro() {
	const auto id = circleAt(currentPage());
	if (chatsOf(id).empty()) {
		return;
	}
	auto &spin = spinOf(id);
	const auto target = spin.gliding ? spin.glide.to : std::round(spin.position);
	spin.position = target - kIntroSlots;
	spin.glide = GlideTo(spin.position, target, kIntroSpeed, kMaxGlide);
	spin.glideStart = now();
	spin.gliding = true;
	startAnimation();
}

double View::now() const {
	return crl::now() / 1000.;
}

int View::pageCount() const {
	return int(_circles.size());
}

int View::currentPage() const {
	if (_circles.empty()) {
		return 0;
	}
	return std::clamp(int(std::lround(_page)), 0, pageCount() - 1);
}

FilterId View::circleAt(int page) const {
	if (_circles.empty()) {
		return 0;
	}
	return _circles[std::clamp(page, 0, pageCount() - 1)];
}

View::Spin &View::spinOf(FilterId id) {
	return _spins[id];
}

const std::vector<not_null<History*>> &View::chatsOf(FilterId id) {
	return _model->chats(id);
}

History *View::focusedOf(FilterId id) {
	const auto &chats = chatsOf(id);
	if (chats.empty()) {
		return nullptr;
	}
	const auto &spin = spinOf(id);
	return chats[FocusIndex(spin.position, int(chats.size()))];
}

double View::spinVelocity(FilterId id) {
	const auto &spin = spinOf(id);
	if (spin.gliding) {
		return spin.glide.velocityAt(now() - spin.glideStart);
	} else if ((_drag == Drag::Spin) || (_scroll == Scroll::Spin)) {
		return _tracker.velocity(now());
	}
	return 0.;
}

int View::headerHeight() const {
	return S(kHeaderHeight);
}

int View::dotsHeight() const {
	return (pageCount() > 1) ? S(kDotsHeight) : S(12);
}

QRect View::pageRect() const {
	const auto top = headerHeight();
	return QRect(
		0,
		top,
		width(),
		std::max(height() - top - dotsHeight(), 0));
}

QPointF View::toPage(QPointF point) const {
	return point - QPointF(pageRect().topLeft());
}

void View::moveTo(double position) {
	const auto id = circleAt(currentPage());
	auto &spin = spinOf(id);
	const auto was = spin.position;
	spin.position = position;
	if (Passes(was, position) > 0) {
		_tickAt = now();
		noteFocus(id);
	}
	startAnimation();
}

void View::glideTo(double target, double velocity, double maxDuration) {
	const auto id = circleAt(currentPage());
	auto &spin = spinOf(id);
	spin.glide = GlideTo(spin.position, target, velocity, maxDuration);
	spin.glideStart = now();
	spin.gliding = !spin.glide.finished(0.);
	if (!spin.gliding) {
		spin.position = target;
		settled(id);
	}
	startAnimation();
}

void View::release(double velocity) {
	const auto id = circleAt(currentPage());
	auto &spin = spinOf(id);
	if (chatsOf(id).empty()) {
		spin.position = 0.;
		return;
	}
	const auto glide = Release(spin.position, velocity);
	glideTo(glide.to, velocity, kMaxGlide);
}

void View::step(int delta) {
	const auto id = circleAt(currentPage());
	auto &spin = spinOf(id);
	if (chatsOf(id).empty()) {
		return;
	}
	const auto t = now() - spin.glideStart;
	const auto base = spin.gliding ? spin.glide.to : std::round(spin.position);
	const auto velocity = spin.gliding ? spin.glide.velocityAt(t) : 0.;
	if (spin.gliding) {
		spin.position = spin.glide.positionAt(t);
	}
	glideTo(base + delta, velocity, kStepGlide * 2.);
}

int View::nextUnread() {
	const auto id = circleAt(currentPage());
	const auto &chats = chatsOf(id);
	const auto count = int(chats.size());
	if (!count) {
		return -1;
	}
	const auto i = _spins.find(id);
	const auto focus = FocusIndex(
		(i != end(_spins)) ? i->second.position : 0.,
		count);
	for (auto step = 1; step <= count; ++step) {
		const auto index = (focus + step) % count;
		const auto state = _model->badges(chats[index]);
		if (state.unread || state.mention) {
			return index;
		}
	}
	return -1;
}

void View::bringTo(int index, bool andOpen) {
	const auto id = circleAt(currentPage());
	const auto &chats = chatsOf(id);
	if (index < 0 || index >= int(chats.size())) {
		return;
	}
	const auto history = chats[index];
	auto &spin = spinOf(id);
	const auto t = now() - spin.glideStart;
	const auto velocity = spin.gliding ? spin.glide.velocityAt(t) : 0.;
	if (spin.gliding) {
		spin.position = spin.glide.positionAt(t);
	}
	const auto target = NearestPosition(
		spin.gliding ? spin.glide.to : spin.position,
		index,
		int(chats.size()));
	if (std::abs(target - spin.position) > 1e-6) {
		glideTo(target, velocity, kBringGlide);
	}
	if (andOpen) {
		_open(history);
	}
}

void View::openFocused() {
	if (const auto history = focusedOf(circleAt(currentPage()))) {
		const auto id = circleAt(currentPage());
		auto &spin = spinOf(id);
		if (!spin.gliding && std::abs(spin.position - std::round(spin.position)) > 1e-6) {
			glideTo(std::round(spin.position), 0., kStepGlide);
		}
		_open(not_null<History*>(history));
	}
}

void View::catchSpin() {
	const auto id = circleAt(currentPage());
	auto &spin = spinOf(id);
	_caughtDirection = 0;
	if (spin.gliding) {
		const auto t = now() - spin.glideStart;
		const auto velocity = spin.glide.velocityAt(t);
		_caughtDirection = (velocity > 0.) ? 1 : (velocity < 0.) ? -1 : 0;
		spin.position = spin.glide.positionAt(t);
		spin.gliding = false;
	}
}

void View::settled(FilterId id) {
	noteFocus(id);
	checkLoadMore(id);
}

void View::noteFocus(FilterId id) {
	auto &spin = spinOf(id);
	const auto focused = focusedOf(id);
	if (spin.focused != focused) {
		spin.focused = focused;
		if (id == circleAt(currentPage())) {
			accessibilityValueChanged();
		}
	}
}

void View::goToPage(int page, double velocity) {
	if (_circles.empty()) {
		return;
	}
	page = std::clamp(page, 0, pageCount() - 1);
	_pageStart = page;
	_pageGlide = GlideTo(_page, double(page), velocity, kPageGlide);
	_pageGlideStart = now();
	_pageGliding = !_pageGlide.finished(0.);
	if (!_pageGliding) {
		_page = _pageRaw = page;
	}
	const auto id = circleAt(page);
	if (_controller->activeChatsFilterCurrent() != id) {
		_controller->setActiveChatsFilter(id);
	}
	_hovered = -1;
	accessibilityNameChanged();
	accessibilityValueChanged();
	checkLoadMore(id);
	startAnimation();
}

void View::releasePage(double velocity) {
	const auto offset = _pageRaw - _pageStart;
	goToPage(PickPage(_pageStart, offset, velocity, pageCount()), velocity);
}

void View::movePageTo(double raw) {
	_pageRaw = raw;
	const auto last = double(std::max(pageCount() - 1, 0));
	if (raw < 0.) {
		_page = RubberBand(raw);
	} else if (raw > last) {
		_page = last + RubberBand(raw - last);
	} else {
		_page = raw;
	}
	update();
}

void View::catchPage() {
	if (_pageGliding) {
		_page = _pageRaw = _pageGlide.positionAt(now() - _pageGlideStart);
		_pageGliding = false;
	}
	_pageStart = currentPage();
}

void View::startAnimation() {
	if (!_animation.animating()) {
		_lastFrame = now();
		_animation.start();
	}
	update();
}

void View::settle() {
	const auto id = circleAt(currentPage());
	const auto &spin = spinOf(id);
	if (!spin.gliding
		&& std::abs(spin.position - std::round(spin.position)) > 1e-6
		&& !chatsOf(id).empty()) {
		// A caught spin clicks into the next chat it was heading for,
		// never back the way it came.
		const auto direction = std::exchange(_caughtDirection, 0);
		const auto target = (direction > 0)
			? std::ceil(spin.position)
			: (direction < 0)
			? std::floor(spin.position)
			: std::round(spin.position);
		glideTo(target, 0., kStepGlide * 2.);
	}
	if (!_pageGliding
		&& std::abs(_page - std::round(_page)) > 1e-6) {
		goToPage(currentPage(), 0.);
	}
}

bool View::animationStep(crl::time ms) {
	const auto t = ms / 1000.;
	const auto dt = std::clamp(t - _lastFrame, 0., 0.1);
	_lastFrame = t;
	auto busy = false;

	if (_pageGliding) {
		const auto elapsed = t - _pageGlideStart;
		_page = _pageRaw = _pageGlide.positionAt(elapsed);
		if (_pageGlide.finished(elapsed)) {
			_pageGliding = false;
			_page = _pageRaw = _pageGlide.to;
		} else {
			busy = true;
		}
	}

	const auto id = circleAt(currentPage());
	for (auto &[circle, spin] : _spins) {
		if (!spin.gliding) {
			continue;
		}
		const auto elapsed = t - spin.glideStart;
		const auto was = spin.position;
		spin.position = spin.glide.positionAt(elapsed);
		if (circle == id && Passes(was, spin.position) > 0) {
			_tickAt = t;
			noteFocus(circle);
		}
		if (spin.glide.finished(elapsed)) {
			spin.gliding = false;
			spin.position = spin.glide.to;
			settled(circle);
		} else {
			busy = true;
		}
	}

	const auto chats = int(chatsOf(id).size());
	const auto target = LeanFor(spinVelocity(id), SlotCount(chats));
	_lean += (target - _lean) * (1. - std::exp(-dt / kLeanFollow));
	if (std::abs(_lean) > 0.05 || std::abs(target) > 0.05) {
		busy = true;
	} else {
		_lean = 0.;
	}
	if (t - _tickAt < kPulse) {
		busy = true;
	}
	if (_drag == Drag::Spin || _scroll == Scroll::Spin) {
		busy = true;
	}
	if (busy && (_pageGliding
		|| spinOf(id).gliding
		|| _drag == Drag::Spin
		|| _drag == Drag::Page
		|| _scroll == Scroll::Spin
		|| _scroll == Scroll::Page)) {
		_movedAt = t;
	}
	if (t - _movedAt < kNamesDelay + kNamesFade) {
		busy = true;
	}
	if (t - _previewAt < kPreviewFade) {
		busy = true;
	}
	update();
	return busy;
}

void View::refreshCircles() {
	const auto was = circleAt(currentPage());
	_circles = _model->circles();
	const auto index = CircleIndexOf(
		std::vector<std::int32_t>(begin(_circles), end(_circles)),
		was);
	_page = _pageRaw = _pageStart = std::clamp(
		index,
		0,
		std::max(pageCount() - 1, 0));
	_pageGliding = false;
	_dial->resize(pageRect().size());
	accessibilityNameChanged();
}

void View::refreshOrder() {
	for (auto &[id, spin] : _spins) {
		const auto focused = spin.focused;
		const auto &chats = chatsOf(id);
		if (!focused || chats.empty()) {
			continue;
		}
		const auto i = ranges::find(chats, not_null<History*>(focused));
		if (i == end(chats)) {
			noteFocus(id);
			continue;
		}
		const auto index = int(i - begin(chats));
		const auto rounded = std::round(spin.position);
		const auto moved = NearestPosition(rounded, index, int(chats.size()))
			- rounded;
		if (std::abs(moved) < 1e-9) {
			continue;
		}
		spin.position += moved;
		if (spin.gliding) {
			spin.glide.from += moved;
			spin.glide.to += moved;
		}
	}
	checkLoadMore(circleAt(currentPage()));
}

void View::followActiveChat(History *history) {
	if (!history || _drag == Drag::Spin || _scroll == Scroll::Spin) {
		return;
	}
	const auto id = circleAt(currentPage());
	const auto &chats = chatsOf(id);
	const auto i = ranges::find(chats, not_null<History*>(history));
	if (i == end(chats)) {
		return;
	}
	const auto index = int(i - begin(chats));
	auto &spin = spinOf(id);
	const auto at = spin.gliding ? spin.glide.to : spin.position;
	if (FocusIndex(at, int(chats.size())) == index
		&& std::abs(at - std::round(at)) < 1e-6) {
		return;
	}
	if (!isVisible()) {
		spin.position = NearestPosition(spin.position, index, int(chats.size()));
		spin.gliding = false;
		noteFocus(id);
		return;
	}
	bringTo(index, false);
}

void View::followActiveCircle(FilterId id) {
	if (_circles.empty() || _drag == Drag::Page || _scroll == Scroll::Page) {
		return;
	}
	const auto page = CircleIndexOf(
		std::vector<std::int32_t>(begin(_circles), end(_circles)),
		id);
	if (_pageGliding ? (_pageStart == page) : (currentPage() == page)) {
		return;
	}
	if (!isVisible()) {
		_page = _pageRaw = _pageStart = page;
		return;
	}
	goToPage(page, 0.);
}

void View::checkLoadMore(FilterId id) {
	const auto count = int(chatsOf(id).size());
	if (_model->fullyLoaded(id)) {
		return;
	}
	const auto focus = count ? FocusIndex(spinOf(id).position, count) : 0;
	if (!count || focus + kLoadAhead >= count || count - focus <= kLoadAhead) {
		_model->requestMore(id);
	}
}

int View::headerItemAt(QPointF point) const {
	if (point.y() < 0 || point.y() >= headerHeight() || _circles.empty()) {
		return -1;
	}
	const auto current = currentPage();
	for (const auto page : { current - 1, current + 1, current }) {
		if (page < 0 || page >= pageCount()) {
			continue;
		}
		if (headerItemRect(page).contains(point.toPoint())) {
			return page;
		}
	}
	return -1;
}

double View::headerSpacing() const {
	return width() * 0.44;
}

QRect View::headerItemRect(int page) const {
	const auto center = width() / 2. + (page - _page) * headerSpacing();
	const auto half = int(headerSpacing() / 2.);
	const auto targetHeight = std::max(S(kDotTarget), headerHeight());
	return QRect(
		int(center) - half,
		(headerHeight() - targetHeight) / 2,
		half * 2,
		targetHeight);
}

int View::dotAt(QPointF point) const {
	const auto count = pageCount();
	if (count < 2 || point.y() < height() - dotsHeight()) {
		return -1;
	}
	const auto gap = std::min(
		double(S(kDotTarget)),
		(width() - S(32)) / double(count));
	const auto left = (width() - gap * count) / 2.;
	const auto index = int(std::floor((point.x() - left) / gap));
	return (index >= 0 && index < count) ? index : -1;
}

void View::updateHover(QPointF point) {
	auto hovered = -1;
	auto clickable = false;
	const auto page = pageRect();
	const auto header = headerItemAt(point);
	const auto dot = dotAt(point);
	if (page.contains(point.toPoint()) && !_pageGliding) {
		const auto inside = toPage(point);
		const auto id = circleAt(currentPage());
		const auto count = int(chatsOf(id).size());
		hovered = _dial->faceAt(inside, spinOf(id).position, count);
		clickable = (hovered >= 0) || (_dial->opens(inside) && count > 0);
	}
	if ((header >= 0 && header != currentPage())
		|| (header >= 0 && header == currentPage() && nextUnread() >= 0)
		|| dot >= 0) {
		clickable = true;
	}
	setCursor(clickable ? style::cur_pointer : style::cur_default);
	showHeaderHint(header >= 0 && header == currentPage() && nextUnread() >= 0);
	if (_hovered != hovered || _hoveredHeader != header || _hoveredDot != dot) {
		_hovered = hovered;
		_hoveredHeader = header;
		_hoveredDot = dot;
		update();
	}
}

void View::showHeaderHint(bool shown) {
	if (_headerHintShown == shown) {
		return;
	}
	_headerHintShown = shown;
	_headerHint.start(
		[=] { update(0, 0, width(), headerHeight()); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		kHintDuration,
		anim::easeOutCubic);
}

void View::resizeEvent(QResizeEvent *e) {
	_dial->resize(pageRect().size());
}

void View::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (const auto parent = parentWidget()) {
		parent->setFocus();
	}
	catchSpin();
	catchPage();
	const auto point = e->position();
	_press = Press{ .point = point };
	_press.header = headerItemAt(point);
	_press.dot = dotAt(point);
	if (pageRect().contains(point.toPoint())) {
		const auto inside = toPage(point);
		const auto id = circleAt(currentPage());
		const auto count = int(chatsOf(id).size());
		_press.face = _dial->faceAt(inside, spinOf(id).position, count);
		_press.opens = _dial->opens(inside) && count > 0;
		_press.grabs = _dial->grabs(inside) && count > 0;
	}
	_drag = Drag::Pending;
	_last = point;
	_tracker.reset(now(), spinOf(circleAt(currentPage())).position);
}

void View::mouseMoveEvent(QMouseEvent *e) {
	const auto point = e->position();
	if (_drag == Drag::None) {
		updateHover(point);
		return;
	}
	if (_drag == Drag::Pending) {
		const auto d = point - _press.point;
		if (std::max(std::abs(d.x()), std::abs(d.y())) < S(kClickSlop)) {
			return;
		}
		if (_press.grabs) {
			_drag = Drag::Spin;
			_tracker.reset(now(), spinOf(circleAt(currentPage())).position);
		} else if (std::abs(d.x()) > std::abs(d.y()) && pageCount() > 0) {
			// With one circle the swipe still answers: it pulls against
			// the rubber band and springs back.
			_drag = Drag::Page;
			_pageStart = currentPage();
			_pageFrom = _pageRaw;
			_tracker.reset(now(), _pageRaw);
		} else {
			_drag = Drag::Ignored;
		}
		setCursor(style::cur_default);
		_hovered = -1;
	}
	if (_drag == Drag::Spin) {
		const auto id = circleAt(currentPage());
		const auto count = int(chatsOf(id).size());
		const auto turned = _dial->turn(toPage(_last), toPage(point), count);
		moveTo(spinOf(id).position + turned);
		_tracker.add(now(), spinOf(id).position);
	} else if (_drag == Drag::Page) {
		const auto shift = -(point.x() - _press.point.x()) / std::max(width(), 1);
		movePageTo(_pageFrom + shift);
		_tracker.add(now(), _pageRaw);
	}
	_last = point;
}

void View::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto drag = std::exchange(_drag, Drag::None);
	const auto point = e->position();
	if (drag == Drag::Pending) {
		if (_press.header >= 0 && _press.header == headerItemAt(point)) {
			if (_press.header != currentPage()) {
				goToPage(_press.header, 0.);
			} else if (const auto unread = nextUnread(); unread >= 0) {
				// The circle's name and unread count: turn to the next
				// unread chat, the way back to chats that left the ring.
				bringTo(unread, false);
			}
		} else if (_press.dot >= 0 && _press.dot == dotAt(point)) {
			goToPage(_press.dot, 0.);
		} else if (_press.face >= 0) {
			bringTo(_press.face, true);
		} else if (_press.opens) {
			openFocused();
		} else {
			settle();
		}
	} else if (drag == Drag::Spin) {
		release(_tracker.velocity(now()));
	} else if (drag == Drag::Page) {
		releasePage(_tracker.velocity(now()));
	} else {
		settle();
	}
	updateHover(point);
}

void View::leaveEventHook(QEvent *e) {
	showHeaderHint(false);
	if (_hovered >= 0 || _hoveredHeader >= 0 || _hoveredDot >= 0) {
		_hovered = _hoveredHeader = _hoveredDot = -1;
		update();
	}
}

void View::scrollUpdate(QPointF delta) {
	_scrolled += delta;
	if (_scroll == Scroll::Undecided) {
		const auto axis = LockAxis(_scrolled.x(), _scrolled.y());
		if (axis == Axis::None) {
			return;
		} else if (axis == Axis::Page && pageCount() > 0) {
			_scroll = Scroll::Page;
			_pageStart = currentPage();
			_tracker.reset(now(), _pageRaw);
			delta = _scrolled;
		} else {
			_scroll = Scroll::Spin;
			const auto id = circleAt(currentPage());
			_scrollStartPosition = spinOf(id).position;
			_tracker.reset(now(), _scrollStartPosition);
			delta = _scrolled;
		}
	}
	if (_scroll == Scroll::Spin) {
		const auto id = circleAt(currentPage());
		if (chatsOf(id).empty()) {
			return;
		}
		const auto chats = -delta.y() / _dial->pixelsPerChat();
		moveTo(spinOf(id).position + chats);
		_tracker.add(now(), spinOf(id).position);
	} else if (_scroll == Scroll::Page) {
		const auto pages = -delta.x() / std::max(width(), 1);
		movePageTo(_pageRaw + pages);
		_tracker.add(now(), _pageRaw);
	}
}

void View::scrollRelease() {
	_scrollIdle.cancel();
	const auto scroll = std::exchange(_scroll, Scroll::Released);
	if (scroll == Scroll::Spin) {
		release(_tracker.velocity(now()));
	} else if (scroll == Scroll::Page) {
		releasePage(_tracker.velocity(now()));
	} else {
		// Fingers that only touched still caught the dial: let it land.
		settle();
	}
}

void View::wheelEvent(QWheelEvent *e) {
	e->accept();
	const auto pixels = e->pixelDelta();
	const auto phase = e->phase();
	if (DiscreteWheel(e)) {
		// A mouse wheel: one chat (or one circle) a notch.
		const auto notches = WheelNotches(e);
		_wheelNotches += notches.y();
		_wheelNotchesX += notches.x();
		while (std::abs(_wheelNotches) >= 120) {
			const auto down = (_wheelNotches < 0);
			_wheelNotches += down ? 120 : -120;
			step(down ? 1 : -1);
		}
		while (std::abs(_wheelNotchesX) >= 120) {
			const auto left = (_wheelNotchesX < 0);
			_wheelNotchesX += left ? 120 : -120;
			goToPage(currentPage() + (left ? 1 : -1), 0.);
		}
		return;
	}
	switch (phase) {
	case Qt::ScrollBegin:
		catchSpin();
		catchPage();
		_scroll = Scroll::Undecided;
		_scrolled = QPointF();
		scrollUpdate(QPointF(pixels));
		break;
	case Qt::ScrollUpdate:
		if (_scroll == Scroll::Released) {
			return;
		} else if (_scroll == Scroll::None) {
			catchSpin();
			catchPage();
			_scroll = Scroll::Undecided;
			_scrolled = QPointF();
		}
		scrollUpdate(QPointF(pixels));
		break;
	case Qt::ScrollMomentum:
		// The fingers are off: land now, the system's momentum is ignored.
		if (_scroll != Scroll::Released && _scroll != Scroll::None) {
			scrollRelease();
		}
		break;
	case Qt::ScrollEnd:
		if (_scroll != Scroll::Released && _scroll != Scroll::None) {
			scrollRelease();
		}
		_scroll = Scroll::None;
		break;
	case Qt::NoScrollPhase:
		if (_scroll == Scroll::None || _scroll == Scroll::Released) {
			catchSpin();
			catchPage();
			_scroll = Scroll::Undecided;
			_scrolled = QPointF();
		}
		scrollUpdate(QPointF(pixels));
		_scrollIdle.callOnce(kNoPhaseRelease);
		break;
	}
}

bool View::handleKey(not_null<QKeyEvent*> e) {
	const auto key = e->key();
	const auto modifiers = e->modifiers()
		& ~(Qt::KeypadModifier | Qt::ShiftModifier);
	if (modifiers != Qt::NoModifier) {
		return false;
	}
	switch (key) {
	case Qt::Key_Right:
	case Qt::Key_Down:
		step(1);
		return true;
	case Qt::Key_Left:
	case Qt::Key_Up:
		step(-1);
		return true;
	case Qt::Key_Return:
	case Qt::Key_Enter:
		openFocused();
		return true;
	case Qt::Key_PageDown:
		goToPage(currentPage() + 1, 0.);
		return true;
	case Qt::Key_PageUp:
		goToPage(currentPage() - 1, 0.);
		return true;
	case Qt::Key_Home:
		bringTo(0, false);
		return true;
	}
	return false;
}

QAccessible::Role View::accessibilityRole() {
	return QAccessible::Dial;
}

QString View::accessibilityName() {
	const auto id = circleAt(currentPage());
	return u"Chats in a dial, %1 circle, %2 of %3"_q
		.arg(_model->circleTitle(id).toString())
		.arg(currentPage() + 1)
		.arg(std::max(pageCount(), 1));
}

QString View::accessibilityValue() const {
	const auto id = circleAt(currentPage());
	const auto &chats = _model->chats(id);
	if (chats.empty()) {
		return QString();
	}
	const auto i = _spins.find(id);
	const auto position = (i != end(_spins)) ? i->second.position : 0.;
	return _model->accessibleName(chats[FocusIndex(position, int(chats.size()))]);
}

void View::paintPage(Painter &p, int page, double shift) {
	const auto id = circleAt(page);
	const auto &chats = chatsOf(id);
	const auto &spin = spinOf(id);
	const auto current = (page == currentPage());
	const auto count = int(chats.size());
	// The dial turns and resizes under a still pointer: name a face only
	// while the pointer is still on the chat it last moved onto.
	const auto hovered = [&] {
		if (!current || _drag != Drag::None || _hovered < 0 || !underMouse()) {
			return -1;
		}
		const auto inside = toPage(QPointF(mapFromGlobal(QCursor::pos())));
		const auto under = _dial->faceAt(inside, spin.position, count);
		return (under == _hovered) ? under : -1;
	}();
	const auto focusIndex = count ? FocusIndex(spin.position, count) : -1;
	const auto pointed = (current && hovered >= 0 && hovered < count && hovered != focusIndex)
		? chats[hovered].get()
		: nullptr;
	if (current && pointed != _previewHover) {
		_previewHover = pointed;
		_previewAt = now();
		startAnimation();
	}
	auto frame = DialFrame{
		.position = spin.position,
		.count = count,
		.lean = current ? _lean : 0.,
		.pulse = current
			? (1. - EaseOutCubic((now() - _tickAt) / kPulse))
			: 0.,
		.hovered = hovered,
		.names = std::clamp(
			(now() - _movedAt - kNamesDelay) / kNamesFade,
			0.,
			1.),
		.focused = count ? chats[focusIndex].get() : nullptr,
		.loading = !count && !_model->fullyLoaded(id),
	};
	frame.previewed = pointed ? pointed : frame.focused;
	frame.previewAlpha = current
		? EaseOutCubic((now() - _previewAt) / kPreviewFade)
		: 1.;
	const auto rect = pageRect();
	p.save();
	p.translate(rect.x() + shift, rect.y());
	_dial->paint(p, *_model, chats, frame);
	p.restore();
}

void View::paintHeader(Painter &p) {
	const auto current = currentPage();
	const auto spacing = headerSpacing();
	const auto height = headerHeight();
	const auto &st = st::chatUniqueTitle;
	for (auto page = current - 2; page <= current + 2; ++page) {
		if (page < 0 || page >= pageCount()) {
			continue;
		}
		const auto distance = std::abs(page - _page);
		if (distance > 1.6) {
			continue;
		}
		const auto id = circleAt(page);
		const auto &title = _model->circleTitle(id);
		const auto unread = _model->circleUnread(id);
		auto badge = Ui::UnreadBadgeStyle();
		badge.muted = unread.muted;
		const auto badgeText = QString::number(unread.count);
		const auto badgeSize = unread.count
			? Ui::CountUnreadBadgeSize(badgeText, badge)
			: QSize();
		const auto badgeGap = unread.count ? S(7) : 0;
		const auto available = int(spacing) - S(24) - badgeSize.width() - badgeGap;
		const auto titleWidth = std::min(title.maxWidth(), std::max(available, 0));
		const auto full = titleWidth + badgeGap + badgeSize.width();
		const auto center = width() / 2. + (page - _page) * spacing;
		const auto left = int(std::round(center - full / 2.));
		const auto hint = (page == current)
			? _headerHint.value(_headerHintShown ? 1. : 0.)
			: 0.;
		const auto top = (height - st.font->height) / 2
			- int(std::round(S(kHintLift) * hint));
		const auto hover = (page == _hoveredHeader && page != current);
		const auto alpha = std::clamp(1. - 0.5 * distance, 0.3, 1.)
			* (hover ? 1.25 : 1.);
		p.setOpacity(std::min(alpha, 1.));
		p.setPen(Mix(SubTextColor(), TextColor(), 1. - std::min(distance, 1.)));
		title.drawElided(p, left, top, titleWidth, 1);
		if (unread.count) {
			badge.align = style::al_left;
			Ui::PaintUnreadBadge(
				p,
				badgeText,
				left + titleWidth + badgeGap,
				top + (st.font->height - badgeSize.height()) / 2,
				badge);
		}
		if (hint > 0.01) {
			// Clicking the name turns the dial to the next unread chat.
			const auto &font = st::circlesHintFont;
			const auto text = u"Next unread"_q;
			p.setOpacity(hint);
			p.setFont(font);
			p.setPen(SubTextColor());
			p.drawText(
				QPointF(
					center - font->width(text) / 2.,
					top + st.font->height + S(1) + font->ascent),
				text);
		}
	}
	p.setOpacity(1.);
	p.fillRect(
		QRect(S(16), height - st::lineWidth, width() - S(32), st::lineWidth),
		LineColor());
}

void View::paintDots(Painter &p) {
	const auto count = pageCount();
	if (count < 2) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto gap = std::min(
		double(S(kDotTarget)),
		(width() - S(32)) / double(count));
	const auto left = (width() - gap * count) / 2.;
	const auto y = height() - dotsHeight() / 2.;
	const auto dot = S(kDotSize) / 2.;
	p.setPen(Qt::NoPen);
	for (auto i = 0; i != count; ++i) {
		const auto x = left + gap * (i + 0.5);
		const auto hover = (i == _hoveredDot);
		p.setBrush(Faded(GlyphColor(), hover ? 0.8 : 0.4));
		p.drawEllipse(QPointF(x, y), dot, dot);
	}
	const auto position = std::clamp(_page, 0., double(count - 1));
	const auto x = left + gap * (position + 0.5);
	const auto stretch = 1. - std::abs(position - std::round(position)) * 0.6;
	const auto active = S(kDotActiveWidth) * stretch;
	p.setBrush(JadeColor());
	p.drawRoundedRect(
		QRectF(x - active / 2., y - dot, active, dot * 2.),
		dot,
		dot);
}

void View::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(e->rect(), BackgroundColor());

	const auto rect = pageRect();
	p.save();
	p.setClipRect(rect);
	const auto base = int(std::floor(_page));
	for (const auto page : { base, base + 1 }) {
		if (page < 0 || page >= pageCount()) {
			continue;
		}
		const auto shift = (page - _page) * width();
		if (std::abs(shift) >= width()) {
			continue;
		}
		paintPage(p, page, shift);
	}
	if (_circles.empty()) {
		PaintEmpty(p, rect, true);
	}
	p.restore();

	paintHeader(p);
	paintDots(p);
}

} // namespace Safe::Circles
