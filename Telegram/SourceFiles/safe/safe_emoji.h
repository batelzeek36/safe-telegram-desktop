/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class HistoryItem;

namespace Data {
class StickersSet;
} // namespace Data

namespace Safe {

// Custom emoji from packs he never chose (d25-emoji-guard, Zach 2026-10-26:
// "emojis are not being checked by haiku on JADELEAF": a search for "booty"
// in the emoji panel showed stripper, pole-heel and thong emoji from strangers'
// packs). The phone's patch 25 rule, in the emoji panel, the reaction picker,
// the status picker and the userpic builder (one widget, EmojiListWidget):
// - A guarded word (IsGuardedQuery, the GIF search guard) searches his own
//   packs only: no server emoji search, no server pack search, no featured
//   packs in the results.
// - Free (shown at once): from a pack he installed, archived or that is
//   Telegram's own (official); one of his recent emoji, recent or top
//   reactions, recent or default emoji statuses.
// - Everything else is held: foggy glass (nothing of it loads) until the
//   picture check clears its WebP thumbnail, once ever per document id (the
//   verdict book). No click, no long-press preview, no menu while held.
// - Flagged: a tile already on screen stays fog (no reshuffle under the
//   pointer); the next list leaves it out (DropsEmoji).
//
// In messages (the Allowed-chat rule, Zach: "Allowed chats are judged,
// Trusted chats show everything, everything else is blurred"): custom emoji
// in the text and custom reactions others set in an Allowed chat are held the
// same way until the check clears them (Trusted senders, Trusted chats and
// free emoji show; elsewhere the media gate already hides or drops them).

// True when the words typed in the emoji search are guarded.
[[nodiscard]] bool IsGuardedEmojiSearch(const std::vector<QString> &query);

// Shown without a check (see above). False for nullptr.
[[nodiscard]] bool IsFreeEmoji(const DocumentData *document);

// Not shown right now: not free and not cleared. False for nullptr.
[[nodiscard]] bool HoldsEmoji(const DocumentData *document);

// Not free and flagged: a new result list leaves it out.
[[nodiscard]] bool DropsEmoji(not_null<const DocumentData*> document);

// A pack a server search found: left out when it is not his and its title or
// short name is guarded.
[[nodiscard]] bool DropsEmojiSet(not_null<const Data::StickersSet*> set);

// Fog for a held emoji tile (queues its check); true = painted, skip the
// emoji itself (nothing of it is loaded or drawn).
bool PaintHeldEmoji(QPainter &p, QRect rect, DocumentData *document);

// ---- In messages (Allowed chats). ----

// A custom emoji in this message's text (or an emoji-only message) is held:
// Hide = always, Judge = until its picture check clears it (queued here),
// Show = never.
[[nodiscard]] bool HoldsMessageEmoji(
	not_null<const HistoryItem*> item,
	DocumentId id);

// For the message's reaction bar: true when a custom reaction is held. In an
// Allowed chat, custom reactions he did not set himself wait for the check;
// GateReactions already dropped them everywhere but Trusted and Allowed
// chats. Null for a chat where nothing is held.
[[nodiscard]] Fn<bool(DocumentId)> ReactionEmojiHolds(
	not_null<const HistoryItem*> item);

} // namespace Safe
