/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class PeerData;
class PhotoData;
class DocumentData;
struct TextWithEntities;

namespace Safe::Quiet {

// Quiet chats (Zach, 2026-10-26: "make it so that JADELEAF doesnt display
// media from link previews or any incoming media like images/videos but only
// in the Michelle and Tausin group ... it can apply to my chats with Jade and
// Michelle privately too"): the family group and his private chats with
// Michelle and with Jade. Nothing incoming there that is a picture or a video
// is displayed: photos, videos, GIFs, round video messages, stickers,
// stories, paid media, file thumbnails and the picture or video of a link
// preview (its words stay). His own messages, voice notes and music stay as
// they are. There is no reveal, no hover or press preview, the media viewer
// refuses them, and saving them is refused like any held picture.
//
// The chats come from the private trust list ("quiet_media_dialogs", see
// safe_trust_policy.h), never from this public source. The rule beats
// "a Trusted chat shows everything" for these chats only.
//
// Display only: nothing is deleted, refused or marked viewed on the
// account, nothing changes on the server, and the media stays in the chat
// for anything else that reads it (the full-auto reader on MAINGEAR has its
// own session). The pictures are simply not fetched for display here.

// One of the quiet chats.
[[nodiscard]] bool Chat(not_null<const PeerData*> peer);

// A message in a quiet chat that is not his own (a story mention counts;
// other service messages do not).
[[nodiscard]] bool Item(not_null<const HistoryItem*> item);

// ---- The media gate's side (safe_media_gate.cpp asks these first). ----

// Every photo a quiet message carries (its own, a link preview's, a game's
// or an invoice's) is held.
[[nodiscard]] bool Holds(
	not_null<const HistoryItem*> item,
	not_null<const PhotoData*> photo);
// A document is held unless it is sound (voice notes, music, audio files):
// pictures and videos never show, other files lose only their thumbnail.
[[nodiscard]] bool Holds(
	not_null<const HistoryItem*> item,
	not_null<const DocumentData*> document);
// The message's own picture (or its link preview's) is held.
[[nodiscard]] bool HoldsItem(not_null<const HistoryItem*> item);
// Held and not even fetched (thumbnails included): the message is a
// picture, a video or a link preview with one. Files keep loading.
[[nodiscard]] bool HidesItem(not_null<const HistoryItem*> item);

// The word a held picture is called where it is painted in place (shared
// media tiles): "photo", "video", "GIF", "sticker", "video message".
[[nodiscard]] QString Word(not_null<const PhotoData*> photo);
[[nodiscard]] QString Word(not_null<const DocumentData*> document);

// ---- The message bubble (history_view_element.cpp, d27 hooks). ----

enum class Bubble : uchar {
	Normal, // Not a quiet picture message: the usual media view.
	Word, // No media view: one short italic line with the kind word, then
	      // the caption.
	HiddenInAlbum, // A later part of a quiet album: the first part speaks
	               // for all of it ("3 photos").
};

[[nodiscard]] Bubble BubbleOf(not_null<const HistoryItem*> item);

// BubbleOf(item) == Bubble::Word.
[[nodiscard]] bool ReplacesMedia(not_null<const HistoryItem*> item);

// The item whose text the bubble carries: an album's caption, else itself.
[[nodiscard]] not_null<HistoryItem*> CaptionItem(
	not_null<HistoryItem*> item);

// The bubble's text: the kind word in italics ("photo", "3 photos",
// "2 photos, 1 video"), then the caption on the next line.
[[nodiscard]] TextWithEntities Placeholder(not_null<HistoryItem*> item);

// ---- Elsewhere. ----

// Link preview: its words stay, its picture, video, collage and sticker
// pack previews are not built.
[[nodiscard]] bool StripsPreview(not_null<const HistoryItem*> item);

// Chat list, search results, reply and pinned bars: no small picture.
[[nodiscard]] bool HidesPreviewImage(not_null<const HistoryItem*> item);

} // namespace Safe::Quiet
