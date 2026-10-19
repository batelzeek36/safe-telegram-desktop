/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_reactions.h"

#include "safe/look_chat.h"
#include "ui/chat/chat_style.h"

namespace Safe::Look {
namespace {

[[nodiscard]] bool DarkBubble(const Ui::MessageStyle &stm) {
	return stm.msgBg->c.lightnessF() < 0.5;
}

} // namespace

QColor ReactionBg(
		QColor upstream,
		const Ui::MessageStyle &stm,
		bool outbg,
		bool chosen) {
	if (!FloatingChatBars() || !outbg) {
		return upstream;
	}
	// Measured on the native app (night theme): your own reaction in an
	// outgoing bubble is a near white pill (#eef5fb on a #2b5278 bubble).
	// Not chosen: the same color, faint (Telegram Desktop's opacity).
	return DarkBubble(stm) ? stm.historyTextFg->c : stm.msgDateFg->c;
}

QPen ReactionChosenFg(
		QPen upstream,
		const Ui::MessageStyle &stm,
		bool outbg) {
	if (!FloatingChatBars() || !outbg) {
		return upstream;
	}
	return QPen(stm.msgBg->c);
}

} // namespace Safe::Look
