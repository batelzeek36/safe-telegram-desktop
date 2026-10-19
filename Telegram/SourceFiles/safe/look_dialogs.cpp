/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/look_dialogs.h"

#include "dialogs/ui/dialogs_layout.h"
#include "safe/look_style.h"

#include "styles/style_basic.h"
#include "styles/style_dialogs.h"

namespace Safe::Look {

void PaintRowSeparator(
		QPainter &p,
		const Dialogs::Ui::PaintContext &context,
		int rowHeight) {
	if (context.narrow || context.active || rowHeight <= 0) {
		return;
	}
	const auto left = context.st->nameLeft;
	const auto width = context.width - left;
	if (width <= 0) {
		return;
	}
	p.fillRect(
		left,
		rowHeight - st::lineWidth,
		width,
		st::lineWidth,
		SeparatorColor());
}

} // namespace Safe::Look
