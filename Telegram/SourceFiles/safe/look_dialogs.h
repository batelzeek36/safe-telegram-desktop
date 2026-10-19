/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Dialogs::Ui {
struct PaintContext;
} // namespace Dialogs::Ui

namespace Safe::Look {

// The native app's hairline under a chat list row: it starts at the text
// column (not under the photo) and runs to the right edge. None in the
// narrow (photos only) column and none under the open chat's highlight.
void PaintRowSeparator(
	QPainter &p,
	const Dialogs::Ui::PaintContext &context,
	int rowHeight);

} // namespace Safe::Look
