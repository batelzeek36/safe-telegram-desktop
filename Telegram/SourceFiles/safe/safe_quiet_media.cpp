/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_quiet_media.h"

#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_game.h"
#include "data/data_groups.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "history/history.h"
#include "history/history_item.h"
#include "safe/safe_trust.h"
#include "safe/safe_trust_store.h"
#include "ui/text/text_utilities.h"

namespace Safe::Quiet {
namespace {

// What a held picture is, in the words the bubble uses.
enum class Kind : uchar {
	Photo,
	Video,
	Gif,
	Sticker,
	VideoMessage,
	Story,
};
constexpr auto kKindCount = 6;

struct Words {
	QString one;
	QString many;
};

[[nodiscard]] const Words &WordsOf(Kind kind) {
	static const auto words = std::array<Words, kKindCount>{ {
		{ u"photo"_q, u"photos"_q },
		{ u"video"_q, u"videos"_q },
		{ u"GIF"_q, u"GIFs"_q },
		{ u"sticker"_q, u"stickers"_q },
		{ u"video message"_q, u"video messages"_q },
		{ u"story"_q, u"stories"_q },
	} };
	return words[int(kind)];
}

[[nodiscard]] bool IsSound(not_null<const DocumentData*> document) {
	return document->isVoiceMessage()
		|| document->isAudioFile()
		|| document->isSong();
}

// A picture or a video by what the document says it is, then by its mime
// type and file name. Anything else (sound, archives, PDFs) has no kind.
[[nodiscard]] std::optional<Kind> KindOf(
		not_null<const DocumentData*> document) {
	if (document->isVideoMessage()) {
		return Kind::VideoMessage;
	} else if (document->sticker()) {
		return Kind::Sticker;
	} else if (document->isAnimation() || document->isGifv()) {
		return Kind::Gif;
	} else if (document->isVideoFile()) {
		return Kind::Video;
	} else if (IsSound(document)) {
		return std::nullopt;
	} else if (document->isImage() || document->isSvgImage()) {
		return Kind::Photo;
	}
	const auto mime = document->mimeString().toLower();
	if (mime.startsWith(u"image/"_q)) {
		return Kind::Photo;
	} else if (mime.startsWith(u"video/"_q)) {
		return Kind::Video;
	}
	switch (Core::DetectNameType(document->filename())) {
	case Core::NameType::Image: return Kind::Photo;
	case Core::NameType::Video: return Kind::Video;
	default: return std::nullopt;
	}
}

// Paid media: each part is a photo, or a video (a preview photo with a
// duration, or a video document once paid).
[[nodiscard]] Kind PaidPartKind(not_null<const Data::Media*> part) {
	if (const auto photo = part->photo()) {
		return photo->extendedMediaVideoDuration().has_value()
			? Kind::Video
			: Kind::Photo;
	} else if (const auto document = part->document()) {
		return KindOf(document).value_or(Kind::Video);
	}
	return Kind::Photo;
}

// The kinds of the message's own media; empty when the bubble keeps its
// media view (a link preview, sound, a plain file, a poll, a location...).
[[nodiscard]] std::vector<Kind> KindsOf(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	if (!media || media->webpage()) {
		return {};
	} else if (media->storyId().valid()) {
		return { Kind::Story };
	} else if (const auto invoice = media->invoice()) {
		auto result = std::vector<Kind>();
		if (invoice->isPaidMedia) {
			for (const auto &part : invoice->extendedMedia) {
				result.push_back(PaidPartKind(part.get()));
			}
		}
		return result;
	} else if (media->photo()) {
		return { Kind::Photo };
	} else if (const auto document = media->document()) {
		if (const auto kind = KindOf(document)) {
			return { *kind };
		}
	}
	return {};
}

// The message carries a picture somewhere (a file's thumbnail included
// when `filesToo`): its own media, a link preview's, a game's, an
// invoice's.
[[nodiscard]] bool CarriesPicture(
		not_null<const HistoryItem*> item,
		bool filesToo) {
	const auto media = item->media();
	if (!media) {
		return false;
	} else if (media->storyId().valid()) {
		return true;
	} else if (const auto page = media->webpage()) {
		return page->photo
			|| page->document
			|| !page->collage.items.empty()
			|| page->stickerSet;
	} else if (const auto invoice = media->invoice()) {
		return invoice->photo || !invoice->extendedMedia.empty();
	} else if (const auto game = media->game()) {
		return game->photo || game->document;
	} else if (media->photo()) {
		return true;
	} else if (const auto document = media->document()) {
		return filesToo
			? !IsSound(document)
			: KindOf(document).has_value();
	}
	return false;
}

[[nodiscard]] const Data::Group *AlbumOf(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	return (media && media->canBeGrouped())
		? item->history()->owner().groups().find(item)
		: nullptr;
}

[[nodiscard]] bool ReplacesOwnMedia(not_null<const HistoryItem*> item) {
	return item->media()
		&& !item->isService()
		&& Item(item)
		&& !KindsOf(item).empty();
}

[[nodiscard]] QString CountedWord(const std::array<int, kKindCount> &counts) {
	auto parts = QStringList();
	auto total = 0;
	for (auto i = 0; i != kKindCount; ++i) {
		total += counts[i];
	}
	for (auto i = 0; i != kKindCount; ++i) {
		const auto count = counts[i];
		if (!count) {
			continue;
		}
		const auto &words = WordsOf(Kind(i));
		parts.push_back((total == 1)
			? words.one
			: (QString::number(count)
				+ ' '
				+ ((count == 1) ? words.one : words.many)));
	}
	return parts.join(u", "_q);
}

} // namespace

bool Chat(not_null<const PeerData*> peer) {
	const auto &policy = TrustStore::Instance().policy();
	return policy.quietMediaCount() && policy.quietMedia(DialogIdOf(peer->id));
}

bool Item(not_null<const HistoryItem*> item) {
	if (item->out() || item->isSponsored()) {
		return false;
	} else if (item->isService()) {
		const auto media = item->media();
		if (!media || !media->storyMention()) {
			return false;
		}
	}
	return Chat(item->history()->peer);
}

bool Holds(
		not_null<const HistoryItem*> item,
		not_null<const PhotoData*> photo) {
	return Item(item);
}

bool Holds(
		not_null<const HistoryItem*> item,
		not_null<const DocumentData*> document) {
	return !IsSound(document) && Item(item);
}

bool HoldsItem(not_null<const HistoryItem*> item) {
	return CarriesPicture(item, true) && Item(item);
}

bool HidesItem(not_null<const HistoryItem*> item) {
	return CarriesPicture(item, false) && Item(item);
}

QString Word(not_null<const PhotoData*> photo) {
	return WordsOf(Kind::Photo).one;
}

QString Word(not_null<const DocumentData*> document) {
	const auto kind = KindOf(document);
	return kind ? WordsOf(*kind).one : u"file"_q;
}

Bubble BubbleOf(not_null<const HistoryItem*> item) {
	if (!ReplacesOwnMedia(item)) {
		return Bubble::Normal;
	} else if (const auto album = AlbumOf(item)) {
		for (const auto &part : album->items) {
			if (!ReplacesOwnMedia(part)) {
				// A mixed album keeps its view: every held part is
				// painted as a quiet tile by the media gate.
				return Bubble::Normal;
			}
		}
		return (album->items.front() == item)
			? Bubble::Word
			: Bubble::HiddenInAlbum;
	}
	return Bubble::Word;
}

bool ReplacesMedia(not_null<const HistoryItem*> item) {
	return BubbleOf(item) == Bubble::Word;
}

not_null<HistoryItem*> CaptionItem(not_null<HistoryItem*> item) {
	const auto album = AlbumOf(item);
	if (!album) {
		return item;
	}
	auto found = (HistoryItem*)nullptr;
	for (const auto &part : album->items) {
		if (!part->emptyText()) {
			if (found) {
				return item; // Several captions: Placeholder joins them.
			}
			found = part;
		}
	}
	return found ? not_null(found) : item;
}

TextWithEntities Placeholder(not_null<HistoryItem*> item) {
	const auto album = AlbumOf(item);
	const auto parts = album ? album->items : HistoryItemsList{ item };
	auto counts = std::array<int, kKindCount>{};
	for (const auto &part : parts) {
		for (const auto kind : KindsOf(part)) {
			++counts[int(kind)];
		}
	}
	auto result = Ui::Text::Italic(CountedWord(counts));
	for (const auto &part : parts) {
		if (!part->emptyText()) {
			result.append(QChar('\n'));
			result.append(part->translatedTextWithLocalEntities());
		}
	}
	return result;
}

bool StripsPreview(not_null<const HistoryItem*> item) {
	return Item(item);
}

bool HidesPreviewImage(not_null<const HistoryItem*> item) {
	return Item(item);
}

} // namespace Safe::Quiet
