/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Chat circles (d35-circles, Zach 2026-10-27: "instead of the chats being
// listed vertically, they should be listed in a circle and i can swipe
// between circles", "the circle is also a dial i can spin/flick with my thumb
// just for fun", "let crown and orbit be options in the menu").
//
// The pure part: which layout he picked, how a dial position maps to faces
// (an endless dial past 12 chats), how a flick lands (once, no roll-back),
// how a sideways swipe picks a circle, and which folders become circles.
// Plain C++20, no Qt and no Telegram types: tools/desktopcheck checks it.
namespace Safe::Circles {

enum class Layout {
	List,
	Crown,
	Orbit,
};

// The stored word for each layout ("list", "crown", "orbit"). Anything else
// read back (a hand-edited file, a newer build's word) is the List.
[[nodiscard]] Layout ParseLayout(std::string_view text);
[[nodiscard]] std::string_view LayoutKey(Layout layout);

// ---- The ring ----
//
// A dial position is counted in chats: at position p the chat with index
// round(p) (wrapped) is at the focus, p + 1 brings the next (older) chat
// there. Up to kMaxSlots chats share the ring; a circle with more chats is an
// endless dial: the ring shows kMaxSlots of them around the focus and faces
// slide in at the far side as it turns. Indices wrap at both ends.
inline constexpr int kMaxSlots = 12;

[[nodiscard]] int SlotCount(int count);
[[nodiscard]] int Wrap(long long index, int count);
[[nodiscard]] int FocusIndex(double position, int count);

struct Slot {
	int index = 0; // the chat, 0 .. count - 1
	double offset = 0.; // slots from the focus, in (-slots / 2, slots / 2 + 1)
	double alpha = 1.; // the far slot of a turning endless dial cross-fades
};

// The faces on the ring at `position`, each chat at most once: exactly
// SlotCount(count) of them at rest (a full ring), one more while an endless
// dial turns (two chats share the far slot, cross-fading).
[[nodiscard]] std::vector<Slot> VisibleSlots(double position, int count);

// The position nearest to `from` that puts chat `index` at the focus (the
// shorter way round).
[[nodiscard]] double NearestPosition(double from, int index, int count);

// How many detents a move from `before` to `after` crossed: the visible tick
// fires once for each chat that reaches the focus.
[[nodiscard]] int Passes(double before, double after);

// ---- Motion ----
//
// One planned glide from `from` to `to`: a cubic that starts at the release
// speed and stops exactly on `to` with no overshoot and no roll-back (its
// slope never changes sign for 0 <= slope <= 3).
struct Glide {
	double from = 0.;
	double to = 0.;
	double duration = 0.; // seconds; 0 = already there
	double slope = 0.; // start speed as a share of (to - from) / duration

	[[nodiscard]] double positionAt(double t) const;
	[[nodiscard]] double velocityAt(double t) const;
	[[nodiscard]] bool finished(double t) const;
};

// Release speeds: a flick coasts about kProjection seconds' worth of its
// speed, capped at kMaxSpeed chats a second. A move slower than kAheadSpeed
// may settle back to the nearest chat; a faster one always lands ahead.
inline constexpr double kProjection = 0.42;
inline constexpr double kMaxSpeed = 40.;
inline constexpr double kAheadSpeed = 0.6;
inline constexpr double kMaxGlide = 1.8;
inline constexpr double kStepGlide = 0.26;

// Where a flick at `velocity` (chats a second) released at `position` lands:
// a whole chat, and for a real flick (kAheadSpeed or more) always the next
// chat or further along its direction, never back where it came from.
[[nodiscard]] double PickTarget(double position, double velocity);

// A glide to `to` that keeps the speed it starts with when that speed heads
// for `to` (lasting at most `maxDuration`), else eases in and out from rest.
[[nodiscard]] Glide GlideTo(
	double from,
	double to,
	double velocity,
	double maxDuration = kMaxGlide);

// The landing after a drag or a flick is let go: PickTarget, then GlideTo.
[[nodiscard]] Glide Release(double position, double velocity);

// ---- Pointer speed ----
//
// The speed of the last ~0.1 s of a drag or a two finger scroll. A pointer
// that stopped before it was let go (nothing for kStillAfter) has no speed.
inline constexpr double kWindow = 0.1;
inline constexpr double kStillAfter = 0.07;

class Tracker final {
public:
	void reset(double t, double value);
	void add(double t, double value);
	[[nodiscard]] double velocity(double now) const;

private:
	struct Sample {
		double t = 0.;
		double value = 0.;
	};
	std::vector<Sample> _samples;

};

// ---- Gestures ----
//
// A two finger scroll locks to one axis once it moved kAxisLock pixels:
// mostly sideways pages between circles, anything else spins the dial.
enum class Axis {
	None,
	Spin,
	Page,
};
inline constexpr double kAxisLock = 6.;
[[nodiscard]] Axis LockAxis(double dx, double dy);

// The circle a sideways swipe lands on. `offset` is how far it was dragged
// (in pages, + toward the next circle), `velocity` its speed in pages a
// second at release. A quick swipe changes circle however short it was (and
// one dragged past and flicked back stays); a slow one changes only past
// half a page. At most one circle along, never past either end.
inline constexpr double kPageFlick = 0.45;
[[nodiscard]] int PickPage(
	int current,
	double offset,
	double velocity,
	int count);

// A drag past the first or last circle moves less and less (the rubber
// band): `over` pages past the end are drawn this far.
[[nodiscard]] double RubberBand(double over);

// ---- Look ----
//
// The leaf in the middle leans with the spin: degrees for a dial turning at
// `velocity` chats a second with `slots` faces round it (+ = clockwise),
// at most kMaxLean either way.
inline constexpr double kMaxLean = 24.;
[[nodiscard]] double LeanFor(double velocity, int slots);

// ---- Badges ----
//
// Where a face wears its unread badge (or a far face its dot), as an angle
// clockwise from twelve o'clock in radians: on its outer side, away from
// the ring's centre. The faces either side lie along the ring, so a badge
// there keeps clear of them.
//
// Crown: `outward` is the face's own angle round the hub. The focus has the
// notch straight above it, so a face coming into the focus (`focus` 0 .. 1)
// turns its badge to the top-right shoulder, the shorter way round.
inline constexpr double kBadgeShoulder = 0.7853981633974483; // 45 degrees
[[nodiscard]] double BadgeAngle(double outward, double focus);

// Orbit: the ellipse's outer normal at ellipse angle `angle` (x = rx cos,
// y = ry sin, y down, so the front face, pi / 2, wears it underneath, in
// its pool of light, between the names). A face whose name pill shows under
// it (`named` 0 .. 1, the focus's neighbours) lifts it to at most
// kOrbitBadgeLowest from twelve o'clock, above the name.
inline constexpr double kOrbitBadgeLowest = 1.9198621771937625; // 110 degrees
[[nodiscard]] double OrbitOutward(
	double angle,
	double rx,
	double ry,
	double named);

// How far from its face's centre, along its angle, a badge sits: `rim`
// (on the face's rim) unless some of it would then reach past `limit`
// from the dial's centre (the Crown's bezel ticks, less a clear gap),
// else pulled in toward the face's centre until all of it is inside.
// `dx`, `dy` = the face's centre from the dial's centre (y down); the
// badge is a level pill `width` x `height` with round ends. No limit
// (0) = the rim.
[[nodiscard]] double BadgeReach(
	double dx,
	double dy,
	double angle,
	double rim,
	double width,
	double height,
	double limit);

// ---- The Crown's spacing ----
//
// The focused face is larger and wears a jade ring; on a full ring the
// faces either side of it would come within a hair of that ring. They are
// moved out by the least angle that keeps `clear` between the ring's
// outer edge and each of them, and the faces further round close up a
// little to make the room, back to even spacing at the far side.
// `ring` = the radius the faces' centres sit on, `reach` = the focused
// face's radius with its ring, `neighbour` = the radius of the faces
// either side. At most kMaxSpread of a slot.
inline constexpr double kMaxSpread = 0.25;
[[nodiscard]] double NeighbourSpread(
	double ring,
	double reach,
	double neighbour,
	double clear,
	int slots);

// Where the face (or bezel tick) `offset` slots from the focus sits, as
// an angle clockwise from twelve: offset * slot, plus `spread` growing
// with the offset to the faces either side of the focus, then easing
// back to none at the far side (offset slots / 2), so nothing jumps as
// the dial turns and the order round the ring never changes.
[[nodiscard]] double SpreadAngle(double offset, int slots, double spread);

// ---- Folders ----
//
// The circles are his chat folders in Telegram's order, plus the "All
// chats" circle (filter id 0): where his folder list has it, as he ordered
// it; first when it does not (no folders at all = one circle, All).
// Duplicates are dropped.
[[nodiscard]] std::vector<std::int32_t> CircleIds(
	const std::vector<std::int32_t> &filters);

// The page of circle `id`, or 0 (the first circle) when it is not one.
[[nodiscard]] int CircleIndexOf(
	const std::vector<std::int32_t> &circles,
	std::int32_t id);

} // namespace Safe::Circles
