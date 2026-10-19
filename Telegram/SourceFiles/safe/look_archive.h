/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// Not a forward declaration: Dialogs::Ui must carry its using of ::Ui, or
// files that include this before dialogs_common.h stop compiling.
#include "dialogs/ui/dialogs_layout.h"

namespace Data {
class Folder;
} // namespace Data

namespace Dialogs {
class BasicRow;
} // namespace Dialogs


// The archive the way Telegram for macOS (the Swift app) shows it: an
// outlined "Archived Chats" pill at the top of the chat list, never a full
// row with a photo. Telegram Desktop's own "collapsed archive" bar already
// sits there with about the right height; the native look keeps it
// collapsed and paints it as the pill. "Move to main menu" still hides it.
namespace Safe::Look {

// What the list should treat the "archive collapsed" setting as.
[[nodiscard]] bool ArchiveCollapsed(bool setting);

// Paints a collapsed archive bar as the pill. Returns false (and paints
// nothing) when it does not apply, so the caller paints its own bar: no
// folder, or the narrow (photos only) column.
bool PaintArchivePill(
	QPainter &p,
	const Dialogs::BasicRow &row,
	Data::Folder *folder,
	int unread,
	const Dialogs::Ui::PaintContext &context);

} // namespace Safe::Look
