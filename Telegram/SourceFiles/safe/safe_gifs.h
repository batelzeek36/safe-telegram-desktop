/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;

namespace InlineBots::Layout {
class ItemBase;
} // namespace InlineBots::Layout

namespace Safe {

// GIF search in the GIF panel (d22-gif-search, Zach 2026-10-25: "tenor gif
// is supposed to be the built-in gif picker"). Telegram's own GIF bot
// (help.getConfig gif_search_username) still answers; Google closing the
// Tenor API on 2026-06-30 did not empty the panel, d13's bot gate did (every
// search whose bot was not Trusted or Allowed was dropped).
//
// Now the configured GIF bot may search unless the query is guarded
// (IsGuardedQuery: nothing is sent, the panel says no results). Each result
// is held as a Judge picture: washed-out stand-in until the picture check
// clears its cover (cached by document id), no link, no preview, no menu,
// no send, and no autoplay download. Saved GIFs are his own and show.

// True when the GIF panel may send `query` to `bot`.
[[nodiscard]] bool AllowsGifSearch(
	not_null<UserData*> bot,
	const QString &query);

// A GIF panel tile that is a search result not cleared yet.
[[nodiscard]] bool HoldsGifTile(const InlineBots::Layout::ItemBase *item);

// The stand-in for a held tile (queues its check); true = painted, skip the
// picture.
bool PaintHeldGifTile(
	QPainter &p,
	not_null<const InlineBots::Layout::ItemBase*> item);

} // namespace Safe
