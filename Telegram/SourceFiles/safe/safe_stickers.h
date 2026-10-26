/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;

namespace Data {
class StickersSet;
} // namespace Data

namespace Safe {

// Stickers from packs he never chose (d26-sticker-guard, 2026-10-26: the
// Stickers tab next to the guarded Emoji tab searched the server with no word
// guard and showed strangers' stickers unjudged). The emoji rule of
// safe_emoji.h, for the sticker panel (search, trending packs, a group's
// pack), the stickers typed-emoji suggestions above the message field and
// the trending list in the stickers settings box:
// - A guarded word (or a guarded emoji typed alone) searches his own packs
//   only: no server sticker search, no server pack search, no stickers by
//   emoji from the server.
// - Free (shown at once): from a pack he chose (IsChosenSet: installed or
//   archived before 2026-10-26 HST) or that is Telegram's own (official); one
//   he sent (recent, recent attached, local recent) or saved as a favorite.
// - Everything else is held: foggy glass (nothing of it loads, no animation
//   starts) until the picture check clears its WebP thumbnail, once ever per
//   document id (the verdict book). No click, preview or menu while held.
// - Flagged: a tile already on screen stays fog; the next search leaves it
//   out (DropsPanelSticker).
// Message effects are Telegram's own and are never held.

// True when this sticker search (or emoji typed alone) is guarded.
[[nodiscard]] bool IsGuardedStickerSearch(const QString &query);

// Shown without a check (see above). False for nullptr.
[[nodiscard]] bool IsFreeSticker(const DocumentData *document);

// Not shown right now: not free and not cleared. False for nullptr.
[[nodiscard]] bool HoldsPanelSticker(const DocumentData *document);

// Not free and flagged: a new result list leaves it out.
[[nodiscard]] bool DropsPanelSticker(not_null<const DocumentData*> document);

// A found pack he did not choose whose title or short name is guarded: left
// out of the search results.
[[nodiscard]] bool DropsStickerSet(not_null<const Data::StickersSet*> set);

// Fog for a held sticker tile (queues its check); true = painted, skip the
// sticker itself (nothing of it is loaded or drawn).
bool PaintHeldPanelSticker(QPainter &p, QRect rect, DocumentData *document);

// A pack's cover in a list of packs (the trending list of the stickers
// settings box): a pack he did not choose shows its cover once the check
// cleared the cover sticker; no cover sticker = fog. True = painted, skip it.
bool PaintHeldSetCover(
	QPainter &p,
	QRect rect,
	not_null<const Data::StickersSet*> set,
	DocumentData *cover);

// A sticker or emoji of an opened pack (StickerSetBox): every one is fog
// while the pack is not installed (PaintHeldSetSticker), and in an installed
// pack he did not choose each one is judged like a panel sticker. No send,
// preview or menu (faved or set as a status it would count as his) while held.
[[nodiscard]] bool HoldsBoxSticker(
	bool notInstalled,
	const DocumentData *document);
bool PaintHeldBoxSticker(
	QPainter &p,
	bool notInstalled,
	DocumentData *document,
	QRect rect);

// "Add" on a pack he did not choose (Zach, 2026-10-26: the Add button of a
// fogged pack installed it, and every sticker in it then showed unjudged).
// Refused while any of its stickers or emoji is still waiting for the picture
// check (the missing checks are queued, a pack not loaded yet is fetched and
// queued) or one was flagged, or its title is guarded; the reason goes to
// notice. A pack he chose (IsChosenSet) is never refused. True = refused,
// send no install request.
bool RefuseSetInstall(
	not_null<Data::StickersSet*> set,
	Fn<void(const QString &)> notice);

} // namespace Safe
