/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/variant.h"
#include "data/data_message_reaction_id.h"
#include "data/data_msg_id.h"
#include "safe/safe_media.h"
#include "safe/safe_media_paint.h"
#include "ui/image/image_location.h"

class HistoryItem;
class PeerData;
class PhotoData;
class DocumentData;
class UserData;

namespace Data {
class Story;
struct FileOrigin;
} // namespace Data

namespace Ui {
struct PeerUserpicView;
} // namespace Ui

namespace Ui::Text {
struct MarkedContext;
} // namespace Ui::Text

namespace Safe {

// ---- Who put a picture there (the phone's SafeMediaPolicy, M3b). ----

// A message's pictures: an untrusted forward origin, inline bot or story
// owner hides them in any chat; a Trusted sender or a Trusted chat shows
// them; an Allowed chat judges them; anything else hides them.
[[nodiscard]] MediaOutcome OutcomeOf(not_null<const HistoryItem*> item);

// An avatar, profile photo or video, story: its owner decides.
[[nodiscard]] MediaOutcome OwnerOutcome(not_null<const PeerData*> owner);

[[nodiscard]] MediaKey KeyOf(not_null<const PhotoData*> photo);
[[nodiscard]] MediaKey KeyOf(not_null<const DocumentData*> document);
// The key of the picture the message carries (photo, file, link preview).
[[nodiscard]] MediaKey KeyOf(not_null<const HistoryItem*> item);

// Hide: nothing of it is downloaded (thumbnails included).
[[nodiscard]] bool HidesItem(not_null<const HistoryItem*> item);

// Not shown right now: Hide, or Judge without a Clear verdict.
[[nodiscard]] bool HoldsItem(not_null<const HistoryItem*> item);
[[nodiscard]] bool Holds(
	not_null<const HistoryItem*> item,
	not_null<const PhotoData*> photo);
[[nodiscard]] bool Holds(
	not_null<const HistoryItem*> item,
	not_null<const DocumentData*> document);
[[nodiscard]] bool HoldsUserpic(not_null<const PeerData*> peer);
[[nodiscard]] bool HoldsPeerPhoto(
	not_null<const PeerData*> peer,
	not_null<const PhotoData*> photo);
[[nodiscard]] bool HoldsStory(not_null<const Data::Story*> story);

// Custom emoji in a message's text: hidden from senders who are not Trusted
// outside Trusted and Allowed chats (the outcome is Hide).
[[nodiscard]] bool HidesCustomEmoji(not_null<const HistoryItem*> item);

// Inline-bot results (@gif and the like): held for every bot, Trusted bots
// included (the phone's rule: the results come from the open web through
// the bot, not from the bot's owner). Held = foggy tiles, no hover
// preview, no thumbnail preloads. The bot is kept in the signature so the
// call sites name what they gate.
[[nodiscard]] bool HoldsInlineResults(const UserData *bot);

// A sticker or emoji set that is not installed: its previews are foggy glass.
[[nodiscard]] bool HidesStickerSetPreview(bool notInstalled);

// Live video (calls, video chats, live streams): Trusted people only.
[[nodiscard]] bool ShowsLiveVideo(not_null<const PeerData*> peer);

// A wallpaper, chat theme or emoji status someone set: Trusted only.
[[nodiscard]] bool ShowsPeerLook(not_null<const PeerData*> peer);

// ---- Paint hooks: true = the stand-in was painted, skip the picture. ----

bool PaintHeld(
	QPainter &p,
	not_null<const HistoryItem*> item,
	not_null<PhotoData*> photo,
	QRect rect,
	const HoldShape &shape);
bool PaintHeld(
	QPainter &p,
	not_null<const HistoryItem*> item,
	not_null<DocumentData*> document,
	QRect rect,
	const HoldShape &shape);
// The message's own picture (whatever KeyOf(item) finds).
bool PaintHeldItem(
	QPainter &p,
	not_null<const HistoryItem*> item,
	QRect rect,
	const HoldShape &shape);
// Stickers in messages. Animated emoji and dice always show; a sticker made
// of custom emoji follows the custom emoji rule; gift and other service
// stickers are Telegram's own and show.
bool PaintHeldSticker(
	QPainter &p,
	not_null<const HistoryItem*> item,
	not_null<DocumentData*> document,
	QRect rect,
	bool customEmojiPart,
	bool emojiSticker);
bool PaintHeldInlineResult(QPainter &p, const UserData *bot, QRect rect);
bool PaintHeldSetSticker(QPainter &p, bool notInstalled, QRect rect);

// ---- Userpics: one choke point, PeerData::userpicCloudImage. ----

// Foggy glass instead of the avatar (never downloaded for Hide), or nullptr
// when it shows. Resets the view so the real one is fetched once it shows.
[[nodiscard]] QImage *HeldUserpic(
	not_null<const PeerData*> peer,
	Ui::PeerUserpicView &view);
// A different cache key while held, so caches keyed by it refresh.
[[nodiscard]] InMemoryKey UserpicKey(
	not_null<const PeerData*> peer,
	InMemoryKey key);

// ---- Message text. ----

[[nodiscard]] Ui::Text::MarkedContext GateCustomEmoji(
	not_null<const HistoryItem*> item,
	Ui::Text::MarkedContext context);

// ---- Chat list, reply and pinned bar previews. ----

[[nodiscard]] QImage HeldPreviewImage(int size, bool ellipse);
[[nodiscard]] uint64 HeldPreviewCacheKey(bool ellipse);

// ---- Media viewer. ----

// Hide = refuses to open; Judge = refuses until Clear (the bubble shows the
// blurred stand-in meanwhile). Stories refuse unless the owner is Trusted.
[[nodiscard]] bool ViewerRefuses(
	HistoryItem *item,
	PeerData *peer,
	PhotoData *photo,
	DocumentData *document,
	Data::Story *story);
using ViewerData = std::variant<
	v::null_t,
	not_null<PhotoData*>,
	not_null<DocumentData*>>;
// Flipping through a chat's media skips held items; preloading skips them.
[[nodiscard]] bool ViewerHolds(
	HistoryItem *item,
	PeerData *peer,
	const ViewerData &data);

// ---- Players. ----

// Round video messages (they play with sound in the bubble and in sequence).
[[nodiscard]] bool RefusesPlay(
	not_null<DocumentData*> document,
	FullMsgId contextId);

// ---- Leaving the app. ----

// A held picture or video counts as protected content (Telegram's own "no
// saving" rule): no Save as, Save image, Copy image, Save GIF, download
// buttons, and the external media viewer option does not take it. Other
// files (documents, archives, music) are not pictures and stay savable.
[[nodiscard]] bool RefusesSaving(not_null<const HistoryItem*> item);

// Opening a held picture or video file in another app ("Open with", the
// system viewer for pictures the in-app viewer cannot show, videos it cannot
// play). Refused with a notice; no message to say who sent it = refused.
// `path` is the local file (its extension names pictures and videos that
// the document's own type does not); unknown kinds count as pictures.
bool RefusesLaunch(
	not_null<DocumentData*> document,
	HistoryItem *item,
	const QString &path);
bool RefusesLaunch(
	not_null<DocumentData*> document,
	const Data::FileOrigin &origin,
	const QString &path);

// ---- Reactions. ----

// Custom emoji reactions show in Trusted and Allowed chats (the custom emoji
// rule). Anywhere else anyone may have set them, so the custom ones he did
// not pick himself are dropped (not fogged, like emoji statuses).
[[nodiscard]] std::vector<Data::MessageReaction> GateReactions(
	not_null<const HistoryItem*> item,
	std::vector<Data::MessageReaction> reactions);

} // namespace Safe
