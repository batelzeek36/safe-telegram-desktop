/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_dial.h"

namespace Safe::Circles {

double Smooth(double t) {
	const auto x = std::clamp(t, 0., 1.);
	return x * x * (3. - 2. * x);
}

double FocusAmount(double offset) {
	return Smooth(1. - std::abs(offset));
}

// Full for the chats near the focus, fading in as a face leaves the focus and
// out past `reach` (1.5 = the chat either side).
double NameAlpha(double offset, double reach) {
	const auto distance = std::abs(offset);
	const auto in = std::clamp((distance - 0.45) * 2.5, 0., 1.);
	const auto out = std::clamp((reach - distance) * 2., 0., 1.);
	return in * out;
}

int HoveredFace(
		const std::vector<FacePlace> &faces,
		int hovered,
		int count) {
	if (hovered < 0 || hovered >= count) {
		return -1;
	}
	for (auto i = 0; i != int(faces.size()); ++i) {
		if (faces[i].index == hovered && faces[i].focus < 0.5) {
			return i;
		}
	}
	return -1;
}

bool Behind(const std::vector<FacePlace> &faces, int i, int hover) {
	if (hover < 0
		|| hover >= int(faces.size())
		|| i < 0
		|| i >= int(faces.size())
		|| i == hover) {
		return false;
	}
	return (faces[i].depth <= faces[hover].depth);
}

int Dial::faceAt(QPointF point, double position, int count) const {
	const auto faces = place(position, count);
	for (auto i = faces.rbegin(); i != faces.rend(); ++i) {
		if (i->alpha < 0.5) {
			continue;
		}
		const auto reach = i->size / 2. + style::ConvertScale(3);
		const auto d = point - i->center;
		if (d.x() * d.x() + d.y() * d.y() <= reach * reach) {
			return i->index;
		}
	}
	return -1;
}

} // namespace Safe::Circles
