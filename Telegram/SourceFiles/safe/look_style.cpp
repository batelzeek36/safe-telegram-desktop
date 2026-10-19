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

QColor GlyphColor() {
	const auto sub = st::windowSubTextFg->c;
	const auto fg = st::windowFg->c;
	const auto bg = st::dialogsBg->c;
	// Measured on the native app (night theme): idle glyphs #b1c3d5, half
	// way from the theme's secondary text (#708499) to its text (#f5f5f5).
	const auto dark = (bg.lightnessF() < 0.5);
	const auto amount = dark ? 0.5 : 0.1;
	return QColor(
		Push(sub.red(), fg.red(), amount),
		Push(sub.green(), fg.green(), amount),
		Push(sub.blue(), fg.blue(), amount));
}

QString ChatsTitle() {
	return u"Chats"_q;
}

QString ArchivedChatsText() {
	return u"Archived Chats"_q;
}

QString SearchHint() {
	return QString::fromUtf8("Search (\xE2\x8C\x98K)");
}

} // namespace Safe::Look
