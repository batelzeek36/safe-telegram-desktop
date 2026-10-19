/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_style.h"

#include "styles/style_basic.h"
#include "styles/style_dialogs.h"

namespace Safe::Look {
namespace {

// Measured on the native app (night theme): list #18222d, separator #213040.
constexpr auto kSeparatorPush = 1.5;

[[nodiscard]] int Push(int from, int to, double amount) {
	return std::clamp(
		int(base::SafeRound(from + (to - from) * amount)),
		0,
		255);
}

} // namespace

QColor SeparatorColor() {
	const auto bg = st::dialogsBg->c;
	const auto over = st::dialogsBgOver->c;
	return QColor(
		Push(bg.red(), over.red(), kSeparatorPush),
		Push(bg.green(), over.green(), kSeparatorPush),
		Push(bg.blue(), over.blue(), kSeparatorPush));
}

int MediaInset() {
	return st::lineWidth;
}

} // namespace Safe::Look
