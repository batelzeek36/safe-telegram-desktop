/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_media_gate.h"

#include "base/weak_ptr.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_story.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_user.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "main/main_session.h"
#include "safe/safe_media_covers.h"
#include "safe/safe_notices.h"
#include "safe/safe_trust.h"
#include "ui/text/text.h"
#include "ui/userpic_view.h"

namespace Safe {
namespace {

constexpr auto kHeldUserpicKeyMask = uint64(0x5AFE5AFE5AFE5AFEULL);
constexpr auto kHeldPreviewCacheKey = uint64(0x5AFE0000ULL);

const auto kLaunchRefused = u"This picture or video is held, "
	"so it does not open in other apps."_q;

[[nodiscard]] PeerData *Mutable(const PeerData *peer) {
	return const_cast<PeerData*>(peer);
}

[[nodiscard]] bool Trusted(const PeerData *peer) {
	return peer && (TierOf(Mutable(peer)) == Tier::Trusted);
}

[[nodiscard]] bool Allowed(const PeerData *peer) {
	return peer && (TierOf(Mutable(peer)) == Tier::Allowed);
}

// Show, or Judge with a Clear answer.
[[nodiscard]] bool Shows(MediaOutcome outcome, MediaKey key) {
	switch (outcome) {
	case MediaOutcome::Show: return true;
	case MediaOutcome::Hide: return false;
	case MediaOutcome::Judge:
		return key && (Verdict(key) == VerdictState::Clear);
	}
	return false;
}

[[nodiscard]] HeldLook LookFor(
		MediaOutcome outcome,
		MediaKey key,
		bool requestable) {
	if (outcome != MediaOutcome::Judge) {
		return HeldLook::Fog;
	} else if (!key) {
		return HeldLook::Waiting;
	} else if (Verdict(key) == VerdictState::Flagged) {
		return HeldLook::Flagged;
	}
	return (JudgeAvailable() && requestable)
		? HeldLook::Checking
		: HeldLook::Waiting;
}

[[nodiscard]] Image *InlineThumbnail(not_null<PhotoData*> photo) {
	const auto view = photo->activeMediaView();
	return view ? view->thumbnailInline() : nullptr;
}

[[nodiscard]] Image *InlineThumbnail(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	const auto media = item->media();
	if (const auto cover = media ? media->videoCover() : nullptr) {
		return InlineThumbnail(cover);
	}
	const auto view = document->activeMediaView();
	return view ? view->thumbnailInline() : nullptr;
}

[[nodiscard]] bool IsDice(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	return media && (dynamic_cast<const Data::MediaDice*>(media) != nullptr);
}

// A picture or a video by what the document says it is, or by the file
// name. A name of no known kind counts as a picture when `unknownIsPicture`
// (opening it in another app), and as a plain file otherwise (saving it).
[[nodiscard]] bool IsPictureOrVideo(
		not_null<const DocumentData*> document,
		const QString &path,
		bool unknownIsPicture) {
	if (document->isImage()
		|| document->isVideoFile()
		|| document->isAnimation()
		|| document->isGifv()
		|| document->isVideoMessage()
		|| document->isSvgImage()
		|| document->sticker()) {
		return true;
	}
	const auto mime = document->mimeString().toLower();
	if (mime.startsWith(u"image/"_q) || mime.startsWith(u"video/"_q)) {
		return true;
	}
	const auto name = path.isEmpty() ? document->filename() : path;
	switch (Core::DetectNameType(name)) {
	case Core::NameType::Image:
	case Core::NameType::Video: return true;
	case Core::NameType::Unknown: return unknownIsPicture;
	default: return false;
	}
}

// Inside a Trusted chat only the sender counts, and only when he listed
// that person himself.
[[nodiscard]] MediaOutcome InTrustedChat(
		const PeerData *sender,
		not_null<const Main::Session*> session) {
	if (!sender || sender == session->user().get()) {
		return MediaOutcome::Show;
	}
	switch (StatusOf(Mutable(sender))) {
	case Status::Allowed: return MediaOutcome::Judge;
	case Status::OffKnown: return MediaOutcome::Hide;
	default: return MediaOutcome::Show; // Trusted, never listed, asked.
	}
}

} // namespace

MediaOutcome OutcomeOf(not_null<const HistoryItem*> item) {
	const auto history = item->history();
	const auto session = &history->session();
	if (item->isSponsored()) {
		return MediaOutcome::Hide;
	}
	const auto sender = item->out()
		? static_cast<PeerData*>(session->user().get())
		: item->from().get();
	const auto chat = history->peer.get();
	if (Trusted(chat)) {
		// A Trusted chat shows everything in it: forwards from anywhere,
		// inline bot results, shared stories (Zach, 2026-10-19: "if the
		// group is trusted it shows everything irregardless"). The one
		// exception is a person he listed himself: Allowed goes through the
		// picture check, Off stays hidden.
		return InTrustedChat(sender, session);
	}
	if (item->Has<HistoryMessageForwarded>()
		&& !Trusted(item->originalSender())) {
		// An untrusted origin hides it in any chat. A hidden origin (only
		// a name) has no sender and counts as a stranger.
		return MediaOutcome::Hide;
	}
	if (const auto via = item->Get<HistoryMessageVia>()) {
		if (!Trusted(via->bot)) {
			return MediaOutcome::Hide;
		}
	}
	if (const auto media = item->media()) {
		if (const auto story = media->storyId(); story.valid()) {
			if (!Trusted(session->data().peerLoaded(story.peer))) {
				return MediaOutcome::Hide;
			}
		}
	}
	if (Trusted(sender)) {
		return MediaOutcome::Show; // His own pictures, Amber, his bots.
	}
	if (Allowed(chat)) {
		return MediaOutcome::Judge;
	}
	return MediaOutcome::Hide;
}

MediaOutcome OwnerOutcome(not_null<const PeerData*> owner) {
	if (Trusted(owner)) {
		return MediaOutcome::Show;
	} else if (Allowed(owner)) {
		return MediaOutcome::Judge;
	}
	return MediaOutcome::Hide;
}

MediaKey KeyOf(not_null<const PhotoData*> photo) {
	return { .id = uint64(photo->id), .document = false };
}

MediaKey KeyOf(not_null<const DocumentData*> document) {
	return { .id = uint64(document->id), .document = true };
}

MediaKey KeyOf(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return {};
	} else if (const auto document = media->document()) {
		return KeyOf(document);
	} else if (const auto photo = media->photo()) {
		return KeyOf(photo);
	} else if (const auto page = media->webpage()) {
		if (const auto document = page->document) {
			return KeyOf(document);
		} else if (const auto photo = page->photo) {
			return KeyOf(photo);
		}
	}
	return {};
}

bool HidesItem(not_null<const HistoryItem*> item) {
	return OutcomeOf(item) == MediaOutcome::Hide;
}

bool HoldsItem(not_null<const HistoryItem*> item) {
	return !Shows(OutcomeOf(item), KeyOf(item));
}

bool Holds(
		not_null<const HistoryItem*> item,
		not_null<const PhotoData*> photo) {
	return !Shows(OutcomeOf(item), KeyOf(photo));
}

bool Holds(
		not_null<const HistoryItem*> item,
		not_null<const DocumentData*> document) {
	return !Shows(OutcomeOf(item), KeyOf(document));
}

bool HoldsUserpic(not_null<const PeerData*> peer) {
	if (!peer->hasUserpic()) {
		return false; // The colored initials carry no picture.
	}
	const auto key = MediaKey{ .id = uint64(peer->userpicPhotoId()) };
	return !Shows(OwnerOutcome(peer), key);
}

bool HoldsPeerPhoto(
		not_null<const PeerData*> peer,
		not_null<const PhotoData*> photo) {
	return !Shows(OwnerOutcome(peer), KeyOf(photo));
}

bool HoldsStory(not_null<const Data::Story*> story) {
	// Stories are refused whole unless the owner is Trusted (the phone's
	// rule): a story is a sequence of frames and has no single cover.
	return OwnerOutcome(story->peer()) != MediaOutcome::Show;
}

bool HidesCustomEmoji(not_null<const HistoryItem*> item) {
	return OutcomeOf(item) == MediaOutcome::Hide;
}

bool HoldsInlineResults(const UserData *bot) {
	(void)bot;
	return true;
}

bool HidesStickerSetPreview(bool notInstalled) {
	return notInstalled;
}

bool ShowsLiveVideo(not_null<const PeerData*> peer) {
	return Trusted(peer);
}

bool ShowsPeerLook(not_null<const PeerData*> peer) {
	return Trusted(peer);
}

bool PaintHeld(
		QPainter &p,
		not_null<const HistoryItem*> item,
		not_null<PhotoData*> photo,
		QRect rect,
		const HoldShape &shape) {
	const auto outcome = OutcomeOf(item);
	const auto key = KeyOf(photo);
	if (Shows(outcome, key)) {
		return false;
	}
	Covers::Remember(item, key);
	if (outcome == MediaOutcome::Hide) {
		PaintFog(p, rect, shape);
		return true;
	}
	const auto requestable = (Verdict(key) == VerdictState::Waiting)
		&& Covers::WantPhoto(item, photo);
	PaintJudged(
		p,
		rect,
		shape,
		LookFor(outcome, key, requestable),
		key.id,
		InlineThumbnail(photo));
	return true;
}

bool PaintHeld(
		QPainter &p,
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document,
		QRect rect,
		const HoldShape &shape) {
	const auto outcome = OutcomeOf(item);
	const auto key = KeyOf(document);
	if (Shows(outcome, key)) {
		return false;
	}
	Covers::Remember(item, key);
	if (outcome == MediaOutcome::Hide) {
		PaintFog(p, rect, shape);
		return true;
	}
	const auto requestable = (Verdict(key) == VerdictState::Waiting)
		&& Covers::WantDocument(item, document);
	PaintJudged(
		p,
		rect,
		shape,
		LookFor(outcome, key, requestable),
		key.id,
		InlineThumbnail(item, document));
	return true;
}

bool PaintHeldItem(
		QPainter &p,
		not_null<const HistoryItem*> item,
		QRect rect,
		const HoldShape &shape) {
	const auto media = item->media();
	const auto page = media ? media->webpage() : nullptr;
	if (const auto document = media ? media->document() : nullptr) {
		return PaintHeld(p, item, document, rect, shape);
	} else if (const auto photo = media ? media->photo() : nullptr) {
		return PaintHeld(p, item, photo, rect, shape);
	} else if (const auto document = page ? page->document : nullptr) {
		return PaintHeld(p, item, document, rect, shape);
	} else if (const auto photo = page ? page->photo : nullptr) {
		return PaintHeld(p, item, photo, rect, shape);
	} else if (HoldsItem(item)) {
		PaintFog(p, rect, shape);
		return true;
	}
	return false;
}

bool PaintHeldSticker(
		QPainter &p,
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document,
		QRect rect,
		bool customEmojiPart,
		bool emojiSticker) {
	if (emojiSticker || item->isService() || IsDice(item)) {
		return false;
	}
	const auto shape = HoldShape::Rounded(
		std::min(rect.width(), rect.height()) / 8);
	if (customEmojiPart) {
		if (!HidesCustomEmoji(item)) {
			return false;
		}
		PaintFog(p, rect, shape);
		return true;
	}
	return PaintHeld(p, item, document, rect, shape);
}

bool PaintHeldInlineResult(QPainter &p, const UserData *bot, QRect rect) {
	if (!HoldsInlineResults(bot)) {
		return false;
	}
	PaintFog(p, rect, HoldShape::Small());
	return true;
}

bool PaintHeldSetSticker(QPainter &p, bool notInstalled, QRect rect) {
	if (!HidesStickerSetPreview(notInstalled)) {
		return false;
	}
	PaintFog(
		p,
		rect,
		HoldShape::Rounded(std::min(rect.width(), rect.height()) / 8));
	return true;
}

QImage *HeldUserpic(
		not_null<const PeerData*> peer,
		Ui::PeerUserpicView &view) {
	if (!peer->hasUserpic()) {
		return nullptr;
	}
	const auto outcome = OwnerOutcome(peer);
	const auto key = MediaKey{ .id = uint64(peer->userpicPhotoId()) };
	if (Shows(outcome, key)) {
		return nullptr;
	}
	if (outcome == MediaOutcome::Judge
		&& key
		&& Verdict(key) == VerdictState::Waiting) {
		Covers::WantUserpic(peer, key);
	}
	if (view.cloud) {
		// Drop the real one: once it shows, the view is rebuilt from the
		// cloud image (the userpic code sees a stale view and reloads).
		view.cloud = nullptr;
		view.cached = QImage();
	}
	return FogUserpicImage();
}

InMemoryKey UserpicKey(not_null<const PeerData*> peer, InMemoryKey key) {
	if (HoldsUserpic(peer)) {
		key.first ^= kHeldUserpicKeyMask;
	}
	return key;
}

Ui::Text::MarkedContext GateCustomEmoji(
		not_null<const HistoryItem*> item,
		Ui::Text::MarkedContext context) {
	if (!context.customEmojiFactory) {
		return context;
	}
	const auto session = &item->history()->session();
	const auto weak = base::make_weak(session);
	const auto id = item->fullId();
	auto hidden = [=] {
		const auto strong = weak.get();
		const auto found = strong ? strong->data().message(id) : nullptr;
		return !found || HidesCustomEmoji(found);
	};
	auto original = std::move(context.customEmojiFactory);
	context.customEmojiFactory = [=](
			QStringView data,
			const Ui::Text::MarkedContext &inner)
	-> std::unique_ptr<Ui::Text::CustomEmoji> {
		auto result = original(data, inner);
		// Only real custom emoji (a document id). The text's own helper
		// icons (the quote mark, inline images) are the app's, not a
		// sender's, and keep showing.
		if (result && Data::ParseCustomEmojiData(data) != 0) {
			return MakeGatedEmoji(std::move(result), hidden);
		}
		return result;
	};
	return context;
}

QImage HeldPreviewImage(int size, bool ellipse) {
	return FogPreviewImage(size, ellipse);
}

uint64 HeldPreviewCacheKey(bool ellipse) {
	return kHeldPreviewCacheKey | (ellipse ? 1 : 0);
}

bool ViewerRefuses(
		HistoryItem *item,
		PeerData *peer,
		PhotoData *photo,
		DocumentData *document,
		Data::Story *story) {
	if (story) {
		return HoldsStory(story);
	} else if (!photo && !document) {
		return false; // Streams and themes: not a picture of a message.
	} else if (item) {
		return photo
			? Holds(item, photo)
			: Holds(item, document);
	} else if (peer) {
		return photo
			? HoldsPeerPhoto(peer, photo)
			: (OwnerOutcome(peer) != MediaOutcome::Show);
	}
	return true; // No context to say who put it there: fail closed.
}

bool ViewerHolds(
		HistoryItem *item,
		PeerData *peer,
		const ViewerData &data) {
	if (const auto photo = std::get_if<not_null<PhotoData*>>(&data)) {
		return ViewerRefuses(item, peer, *photo, nullptr, nullptr);
	} else if (const auto document
			= std::get_if<not_null<DocumentData*>>(&data)) {
		return ViewerRefuses(item, peer, nullptr, *document, nullptr);
	}
	return false;
}

bool RefusesPlay(not_null<DocumentData*> document, FullMsgId contextId) {
	if (!document->isVideoMessage()) {
		return false;
	}
	const auto item = document->owner().message(contextId);
	return !item || Holds(item, document);
}

bool RefusesSaving(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return false;
	} else if (const auto photo = media->photo()) {
		return Holds(item, photo);
	} else if (const auto document = media->document()) {
		return IsPictureOrVideo(document, QString(), false)
			&& Holds(item, document);
	}
	return false;
}

bool RefusesLaunch(
		not_null<DocumentData*> document,
		HistoryItem *item,
		const QString &path) {
	if (!IsPictureOrVideo(document, path, true)
		|| (item && !Holds(item, document))) {
		return false;
	}
	ShowNotice(QVariant(), kLaunchRefused);
	return true;
}

bool RefusesLaunch(
		not_null<DocumentData*> document,
		const Data::FileOrigin &origin,
		const QString &path) {
	const auto id = std::get_if<Data::FileOriginMessage>(&origin.data);
	const auto item = id ? document->owner().message(*id) : nullptr;
	return RefusesLaunch(document, item, path);
}

std::vector<Data::MessageReaction> GateReactions(
		not_null<const HistoryItem*> item,
		std::vector<Data::MessageReaction> reactions) {
	const auto chat = item->history()->peer.get();
	if (Trusted(chat) || Allowed(chat)) {
		return reactions;
	}
	reactions.erase(
		ranges::remove_if(reactions, [](const Data::MessageReaction &r) {
			return r.id.custom() && !r.my;
		}),
		end(reactions));
	return reactions;
}

} // namespace Safe
