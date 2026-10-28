/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_core.h"

#include <algorithm>
#include <cmath>

namespace Safe::Circles {
namespace {

constexpr auto kEpsilon = 1e-9;

// A glide that starts from rest: about a third of a second for one chat.
constexpr auto kRestBase = 0.16;
constexpr auto kRestPerChat = 0.2;

// How strongly the leaf leans: degrees per degree-a-second of spin.
constexpr auto kLeanPerSpeed = 0.06;

// The rubber band past the first and last circle (UIScrollView's constant).
constexpr auto kRubber = 0.55;

[[nodiscard]] double Sign(double value) {
	return (value > 0.) ? 1. : (value < 0.) ? -1. : 0.;
}

// The cubic Hermite step from 0 to 1 with start slope m and end slope 0.
[[nodiscard]] double Hermite(double s, double m) {
	const auto s2 = s * s;
	const auto s3 = s2 * s;
	return m * (s3 - 2. * s2 + s) + (3. * s2 - 2. * s3);
}

[[nodiscard]] double HermiteSlope(double s, double m) {
	return (1. - s) * (m + s * (6. - 3. * m));
}

} // namespace

Layout ParseLayout(std::string_view text) {
	if (text == "crown") {
		return Layout::Crown;
	} else if (text == "orbit") {
		return Layout::Orbit;
	}
	return Layout::List;
}

std::string_view LayoutKey(Layout layout) {
	switch (layout) {
	case Layout::List: return "list";
	case Layout::Crown: return "crown";
	case Layout::Orbit: return "orbit";
	}
	return "list";
}

int SlotCount(int count) {
	return std::clamp(count, 0, kMaxSlots);
}

int Wrap(long long index, int count) {
	if (count <= 0) {
		return 0;
	}
	const auto result = index % count;
	return int((result < 0) ? (result + count) : result);
}

int FocusIndex(double position, int count) {
	return Wrap((long long)std::llround(position), count);
}

std::vector<Slot> VisibleSlots(double position, int count) {
	const auto slots = SlotCount(count);
	auto result = std::vector<Slot>();
	if (!slots) {
		return result;
	}
	const auto half = slots / 2.;
	if (count > slots) {
		// An endless dial. At rest the ring is full: offsets -half + 1 ..
		// half, the face at the far point fully drawn. While it turns, the
		// chat leaving at one end and the one arriving at the other share the
		// far slot (offsets o and o + slots are the same place on the ring)
		// and cross-fade, their alphas adding up to one.
		result.reserve(slots + 1);
		auto j = (long long)std::floor(position - half);
		for (;; ++j) {
			const auto offset = double(j) - position;
			if (offset >= half + 1. - kEpsilon) {
				break;
			}
			const auto alpha = std::clamp(
				std::min(half + 1. - offset, half + offset),
				0.,
				1.);
			if (alpha <= kEpsilon) {
				continue;
			}
			result.push_back({
				.index = Wrap(j, count),
				.offset = offset,
				.alpha = alpha,
			});
		}
		return result;
	}
	// A short circle: the half-open window (position - half, position + half]
	// holds exactly `slots` whole numbers, so every chat shows once.
	result.reserve(slots);
	auto first = (long long)std::floor(position - half) + 1;
	if (double(first) - position <= -half) {
		++first;
	}
	for (auto j = first; result.size() < size_t(slots); ++j) {
		const auto offset = double(j) - position;
		if (offset > half + kEpsilon) {
			break;
		}
		result.push_back({
			.index = Wrap(j, count),
			.offset = offset,
		});
	}
	return result;
}

double NearestPosition(double from, int index, int count) {
	if (count <= 0) {
		return from;
	}
	const auto base = std::floor((from - index) / count + 0.5);
	auto result = index + base * count;
	// Two candidates can tie half a turn away; keep the forward one.
	if (result - from > count / 2.) {
		result -= count;
	} else if (from - result > count / 2.) {
		result += count;
	}
	return result;
}

int Passes(double before, double after) {
	return int(std::llabs(std::llround(after) - std::llround(before)));
}

double Glide::positionAt(double t) const {
	if (duration <= 0. || t >= duration) {
		return to;
	} else if (t <= 0.) {
		return from;
	}
	return from + (to - from) * Hermite(t / duration, slope);
}

double Glide::velocityAt(double t) const {
	if (duration <= 0. || t >= duration || t < 0.) {
		return 0.;
	}
	return (to - from) / duration * HermiteSlope(t / duration, slope);
}

bool Glide::finished(double t) const {
	return (duration <= 0.) || (t >= duration);
}

double PickTarget(double position, double velocity) {
	const auto speed = std::clamp(velocity, -kMaxSpeed, kMaxSpeed);
	auto target = std::round(position + speed * kProjection);
	if (speed >= kAheadSpeed) {
		target = std::max(target, std::floor(position + kEpsilon) + 1.);
	} else if (speed <= -kAheadSpeed) {
		target = std::min(target, std::ceil(position - kEpsilon) - 1.);
	}
	return target;
}

Glide GlideTo(double from, double to, double velocity, double maxDuration) {
	auto result = Glide{ .from = from, .to = to };
	const auto distance = to - from;
	if (std::abs(distance) < kEpsilon) {
		return result;
	}
	const auto speed = std::clamp(velocity, -kMaxSpeed, kMaxSpeed);
	if (speed * distance > 0.) {
		// Slope 3 is a pure ease-out: the release speed decays to zero.
		auto duration = 3. * distance / speed;
		duration = std::min(duration, maxDuration);
		result.duration = duration;
		result.slope = std::clamp(speed * duration / distance, 0., 3.);
	} else {
		const auto rest = kRestBase + kRestPerChat * std::sqrt(std::abs(distance));
		result.duration = std::min(rest, maxDuration);
		result.slope = 0.;
	}
	return result;
}

Glide Release(double position, double velocity) {
	return GlideTo(position, PickTarget(position, velocity), velocity);
}

void Tracker::reset(double t, double value) {
	_samples.clear();
	_samples.push_back({ t, value });
}

void Tracker::add(double t, double value) {
	if (!_samples.empty() && t < _samples.back().t) {
		t = _samples.back().t;
	}
	_samples.push_back({ t, value });
	while (_samples.size() > 2 && (t - _samples.front().t) > kWindow) {
		_samples.erase(begin(_samples));
	}
}

double Tracker::velocity(double now) const {
	if (_samples.size() < 2) {
		return 0.;
	}
	const auto &last = _samples.back();
	if (now - last.t > kStillAfter) {
		return 0.;
	}
	const auto &first = _samples.front();
	const auto span = last.t - first.t;
	if (span < 0.008) {
		return 0.;
	}
	return (last.value - first.value) / span;
}

Axis LockAxis(double dx, double dy) {
	const auto ax = std::abs(dx);
	const auto ay = std::abs(dy);
	if (std::max(ax, ay) < kAxisLock) {
		return Axis::None;
	}
	return (ax > ay * 1.2) ? Axis::Page : Axis::Spin;
}

int PickPage(int current, double offset, double velocity, int count) {
	if (count <= 0) {
		return 0;
	}
	// A flick goes to the neighbour on its side of where the page is now
	// (back to `current` when it was dragged past and flicked back).
	const auto position = current + std::clamp(offset, -1., 1.);
	auto target = 0;
	if (velocity >= kPageFlick) {
		target = int(std::floor(position + kEpsilon)) + 1;
	} else if (velocity <= -kPageFlick) {
		target = int(std::ceil(position - kEpsilon)) - 1;
	} else {
		target = int(std::lround(position));
	}
	target = std::clamp(target, current - 1, current + 1);
	return std::clamp(target, 0, count - 1);
}

double RubberBand(double over) {
	const auto amount = std::abs(over);
	return Sign(over) * (1. - 1. / (amount * kRubber + 1.));
}

double LeanFor(double velocity, int slots) {
	if (slots <= 0) {
		return 0.;
	}
	const auto degreesPerSecond = velocity * 360. / slots;
	return std::clamp(degreesPerSecond * kLeanPerSpeed, -kMaxLean, kMaxLean);
}

double BadgeAngle(double outward, double focus) {
	constexpr auto kFullTurn = 8. * kBadgeShoulder; // 2 pi
	const auto turn = std::remainder(kBadgeShoulder - outward, kFullTurn);
	return outward + turn * std::clamp(focus, 0., 1.);
}

double BadgeFade(double alpha) {
	const auto t = std::clamp(
		(alpha - kBadgeFadeFrom) / (1. - kBadgeFadeFrom),
		0.,
		1.);
	return t * t * (3. - 2. * t);
}

double OrbitOutward(double angle, double rx, double ry, double named) {
	if (rx <= 0. || ry <= 0.) {
		return 0.;
	}
	const auto x = std::cos(angle) / rx;
	const auto y = std::sin(angle) / ry; // down
	const auto normal = std::atan2(x, -y);
	const auto lifted = std::clamp(
		normal,
		-kOrbitBadgeLowest,
		kOrbitBadgeLowest);
	return normal + (lifted - normal) * std::clamp(named, 0., 1.);
}

double BadgeReach(
		double dx,
		double dy,
		double angle,
		double rim,
		double width,
		double height,
		double limit) {
	if (limit <= 0. || rim <= 0.) {
		return rim;
	}
	const auto ux = std::sin(angle);
	const auto uy = -std::cos(angle);
	const auto half = std::max(width - height, 0.) / 2.;
	// The farthest point of the pill from the dial's centre: one of its
	// round ends.
	const auto far = [&](double t) {
		const auto x = dx + ux * t;
		const auto y = dy + uy * t;
		return std::max(std::hypot(x - half, y), std::hypot(x + half, y))
			+ height / 2.;
	};
	if (far(rim) <= limit) {
		return rim;
	} else if (far(0.) > limit) {
		return 0.;
	}
	auto low = 0.;
	auto high = rim;
	for (auto i = 0; i != 40; ++i) {
		const auto middle = (low + high) / 2.;
		if (far(middle) <= limit) {
			low = middle;
		} else {
			high = middle;
		}
	}
	return low;
}

double NeighbourSpread(
		double ring,
		double reach,
		double neighbour,
		double clear,
		int slots) {
	if (slots < 4 || ring <= 0.) {
		return 0.;
	}
	const auto pi = 4. * kBadgeShoulder;
	const auto slot = 2. * pi / slots;
	const auto apart = reach + neighbour + clear;
	const auto needed = (apart >= 2. * ring)
		? pi
		: 2. * std::asin(apart / (2. * ring));
	return std::clamp(needed - slot, 0., slot * kMaxSpread);
}

double NeighbourSpreadShaped(
		double ring,
		double reachHalf,
		double reachCorner,
		double neighbourHalf,
		double neighbourCorner,
		double clear,
		int slots) {
	if (reachCorner >= reachHalf && neighbourCorner >= neighbourHalf) {
		return NeighbourSpread(ring, reachHalf, neighbourHalf, clear, slots);
	} else if (slots < 4 || ring <= 0.) {
		return 0.;
	}
	const auto slot = 2. * (4. * kBadgeShoulder) / slots;
	const auto most = slot * kMaxSpread;
	// The room left between the two outlines, measured along the line
	// between the centres, when they are `spread` further apart than a slot:
	// that line leaves twelve o'clock at half the angle between them.
	const auto room = [&](double spread) {
		const auto apart = slot + spread;
		const auto chord = apart / 2.;
		return 2. * ring * std::sin(apart / 2.)
			- RimDistance(reachHalf, reachCorner, chord)
			- RimDistance(neighbourHalf, neighbourCorner, chord)
			- clear;
	};
	if (room(0.) >= 0.) {
		return 0.;
	} else if (room(most) < 0.) {
		return most;
	}
	// The least spread that leaves the room.
	auto low = 0.;
	auto high = most;
	for (auto i = 0; i != 60; ++i) {
		const auto middle = (low + high) / 2.;
		if (room(middle) >= 0.) {
			high = middle;
		} else {
			low = middle;
		}
	}
	return high;
}

double RimDistance(double half, double corner, double angle) {
	if (half <= 0.) {
		return 0.;
	}
	corner = std::clamp(corner, 0., half);
	const auto u = std::abs(std::cos(angle));
	const auto v = std::abs(std::sin(angle));
	const auto flat = half - corner; // each side's straight half
	// On a straight side: the side the ray meets first.
	if (u >= v) {
		if (half * v <= flat * u) {
			return half / u;
		}
	} else if (half * u <= flat * v) {
		return half / v;
	}
	// On a corner's arc, centred at (flat, flat): the far root of
	// |t (u, v) - (flat, flat)| = corner.
	const auto b = flat * (u + v);
	const auto c = 2. * flat * flat - corner * corner;
	return b + std::sqrt(std::max(b * b - c, 0.));
}

double RimFarthest(double half, double corner) {
	if (half <= 0.) {
		return 0.;
	}
	corner = std::clamp(corner, 0., half);
	return std::sqrt(2.) * (half - corner) + corner;
}

double RimAcross(double half, double corner, double y) {
	if (half <= 0.) {
		return 0.;
	}
	corner = std::clamp(corner, 0., half);
	const auto flat = half - corner;
	const auto up = std::abs(y);
	if (up >= half) {
		return 0.;
	} else if (up <= flat) {
		return half;
	}
	const auto into = up - flat;
	return flat + std::sqrt(std::max(corner * corner - into * into, 0.));
}

double SpreadAngle(double offset, int slots, double spread) {
	const auto pi = 4. * kBadgeShoulder;
	const auto slot = 2. * pi / std::max(slots, 1);
	const auto base = offset * slot;
	if (spread <= 0. || slots < 4) {
		return base;
	}
	const auto half = slots / 2.;
	const auto distance = std::abs(offset);
	const auto share = (distance <= 1.)
		? distance
		: std::clamp((half - distance) / (half - 1.), 0., 1.);
	return base + Sign(offset) * spread * share;
}

std::vector<std::int32_t> CircleIds(const std::vector<std::int32_t> &filters) {
	auto result = std::vector<std::int32_t>();
	result.reserve(filters.size() + 1);
	for (const auto id : filters) {
		if (std::find(begin(result), end(result), id) == end(result)) {
			result.push_back(id);
		}
	}
	if (std::find(begin(result), end(result), 0) == end(result)) {
		result.insert(begin(result), 0);
	}
	return result;
}

int CircleIndexOf(const std::vector<std::int32_t> &circles, std::int32_t id) {
	const auto i = std::find(begin(circles), end(circles), id);
	return (i == end(circles)) ? 0 : int(i - begin(circles));
}

} // namespace Safe::Circles
