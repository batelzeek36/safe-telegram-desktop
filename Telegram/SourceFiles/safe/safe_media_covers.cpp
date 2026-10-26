/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_media_covers.h"

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "ffmpeg/ffmpeg_frame_generator.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "safe/safe_judge_protocol.h"
#include "safe/safe_media_gate.h"
#include "safe/safe_trust.h"
#include "ui/image/image.h"
#include "ui/ui_utility.h"
#include "ui/userpic_view.h"

#include <QtCore/QFile>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>

#include <deque>

namespace Safe::Covers {
namespace {

constexpr auto kPendingLimit = 64; // Newest first wins, like the phone.
constexpr auto kKeptLimit = 64; // Views kept for the Judge wash.
constexpr auto kAskedLimit = 4096;
constexpr auto kWatchedLimit = 2048;
constexpr auto kWatchedPerKey = 8;
constexpr auto kRepaintDelay = crl::time(150);
constexpr auto kMaxFileBytes = int64(16 * 1024 * 1024);
constexpr auto kMaxDecodedFrames = 1200;
constexpr auto kEmojiRetryDelay = crl::time(1500);
constexpr auto kEmojiGrey = 128;

struct Pending {
	base::weak_ptr<Main::Session> session;
	MediaKey key;
	std::shared_ptr<Data::PhotoMedia> photo;
	std::shared_ptr<Data::DocumentMedia> document;
	Ui::PeerUserpicView userpic;
	QString caption;
	QString fileName;
	QString mime;
	bool fromFile = false; // Frames decoded from the downloaded file.
	bool emoji = false; // A custom emoji: see EmojiCover.
};

struct Watched {
	base::weak_ptr<Main::Session> session;
	FullMsgId id;
};

struct State {
	std::deque<Pending> pending;
	std::deque<std::shared_ptr<Data::PhotoMedia>> keptPhotos;
	std::deque<std::shared_ptr<Data::DocumentMedia>> keptDocuments;
	base::flat_set<MediaKey> asked;
	base::flat_map<MediaKey, std::vector<Watched>> watched;
	base::flat_set<not_null<Main::Session*>> sessions;
	base::Timer repaintTimer;
	base::Timer emojiRetryTimer;
	// Custom emoji known only by id, being fetched (see ResolveEmoji).
	base::flat_map<MediaKey, rpl::lifetime> resolving;
	rpl::lifetime lifetime;
};

void RefreshWatched(State &state, std::optional<MediaKey> only);
void ScheduleRepaint(State &state);
void RetryPending();
[[nodiscard]] QString EmojiCaption(not_null<DocumentData*> document);

[[nodiscard]] State &Instance() {
	// Never destroyed: the app may still paint while statics go away.
	static const auto instance = [] {
		const auto result = new State();
		result->repaintTimer.setCallback([] {
			for (const auto widget : QApplication::topLevelWidgets()) {
				if (widget->isVisible()) {
					Ui::ForceFullRepaint(widget);
				}
			}
		});
		// An emoji document can arrive with no download finishing after it.
		result->emojiRetryTimer.setCallback([] {
			RetryPending();
		});
		VerdictChanges(
		) | rpl::on_next([=](MediaKey key) {
			RefreshWatched(*result, key);
			ScheduleRepaint(*result);
		}, result->lifetime);
		Changes(
		) | rpl::on_next([=] {
			RefreshWatched(*result, std::nullopt);
			ScheduleRepaint(*result);
		}, result->lifetime);
		return result;
	}();
	return *instance;
}

void ScheduleRepaint(State &state) {
	if (!state.repaintTimer.isActive()) {
		state.repaintTimer.callOnce(kRepaintDelay);
	}
}

void RefreshWatched(State &state, std::optional<MediaKey> only) {
	auto refresh = std::vector<Watched>();
	if (only) {
		const auto i = state.watched.find(*only);
		if (i == end(state.watched)) {
			return;
		}
		refresh = std::move(i->second);
		state.watched.erase(i);
	} else {
		for (auto &[key, list] : state.watched) {
			for (auto &entry : list) {
				refresh.push_back(std::move(entry));
			}
		}
		state.watched.clear();
	}
	// Each one registers again on its next paint if it is still held.
	for (const auto &entry : refresh) {
		if (const auto session = entry.session.get()) {
			auto &owner = session->data();
			if (const auto item = owner.message(entry.id)) {
				owner.requestItemViewRefresh(item);
				owner.notifyItemDataChange(item);
			}
		}
	}
}

[[nodiscard]] QImage CoverOf(const Pending &entry) {
	const auto original = [](Image *image) {
		return image ? image->original() : QImage();
	};
	if (entry.photo) {
		using Size = Data::PhotoSize;
		for (const auto size : { Size::Thumbnail, Size::Small, Size::Large }) {
			if (auto result = original(entry.photo->image(size))
				; !result.isNull()) {
				return result;
			}
		}
	} else if (entry.document) {
		if (auto result = original(entry.document->thumbnail())
			; !result.isNull()) {
			return result;
		}
		if (entry.document->owner()->sticker()) {
			entry.document->checkStickerLarge();
			return original(entry.document->getStickerSmall());
		}
	} else if (entry.userpic.cloud && !entry.userpic.cloud->isNull()) {
		return *entry.userpic.cloud;
	}
	return QImage();
}

void Keep(State &state, Pending &entry) {
	if (entry.photo) {
		state.keptPhotos.push_back(std::move(entry.photo));
		while (state.keptPhotos.size() > kKeptLimit) {
			state.keptPhotos.pop_front();
		}
	}
	if (entry.document) {
		state.keptDocuments.push_back(std::move(entry.document));
		while (state.keptDocuments.size() > kKeptLimit) {
			state.keptDocuments.pop_front();
		}
	}
}

void Asked(State &state, MediaKey key) {
	if (state.asked.size() >= kAskedLimit) {
		state.asked.clear();
	}
	state.asked.emplace(key);
}

[[nodiscard]] QSize FrameSize(QSize dimensions) {
	const auto side = JudgeProtocol::kMaxSide;
	if (dimensions.isEmpty()) {
		return QSize(side, side);
	} else if (dimensions.width() <= side && dimensions.height() <= side) {
		return dimensions;
	}
	return dimensions.scaled(side, side, Qt::KeepAspectRatio);
}

[[nodiscard]] QByteArray ReadFile(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxFileBytes) {
		return QByteArray();
	}
	return file.readAll();
}

// The first frame and the frames at a quarter, half and three quarters.
[[nodiscard]] std::vector<QImage> DecodeFrames(
		const QByteArray &bytes,
		QSize size) {
	auto result = std::vector<QImage>();
	if (bytes.isEmpty()) {
		return result;
	}
	auto generator = FFmpeg::FrameGenerator(bytes);
	auto count = 0;
	while (count < kMaxDecodedFrames) {
		const auto frame = generator.renderNext(QImage(), QSize(8, 8));
		if (frame.image.isNull()) {
			break;
		}
		++count;
		if (frame.last) {
			break;
		}
	}
	if (!count) {
		return result;
	}
	generator.jumpToStart();
	const auto picks = base::flat_set<int>{
		0,
		count / 4,
		count / 2,
		(count * 3) / 4,
	};
	for (auto index = 0; index != count; ++index) {
		auto frame = generator.renderNext(QImage(), size);
		if (frame.image.isNull()) {
			break;
		} else if (picks.contains(index)) {
			result.push_back(std::move(frame.image));
		}
		if (frame.last) {
			break;
		}
	}
	return result;
}

// A GIF search result: its own frames, not a thumbnail (many have none).
[[nodiscard]] bool TrySendFromFile(State &state, Pending &entry) {
	const auto media = entry.document;
	if (!media || !media->loaded()) {
		return false;
	}
	const auto document = media->owner();
	const auto bytes = media->bytes();
	const auto path = bytes.isEmpty() ? document->filepath(true) : QString();
	if (bytes.isEmpty() && path.isEmpty()) {
		return false;
	}
	const auto size = FrameSize(document->dimensions);
	const auto session = entry.session;
	const auto key = entry.key;
	const auto fileName = entry.fileName;
	const auto mime = entry.mime;
	crl::async([=] {
		auto frames = DecodeFrames(
			bytes.isEmpty() ? ReadFile(path) : bytes,
			size);
		crl::on_main(session, [=, frames = std::move(frames)]() mutable {
			if (frames.empty() || Verdict(key) != VerdictState::Waiting) {
				return;
			}
			auto cover = std::move(frames.front());
			frames.erase(begin(frames));
			RequestVerdict(
				session.get(),
				key,
				std::move(cover),
				std::move(frames),
				QString(),
				fileName,
				mime);
		});
	});
	Asked(state, key);
	Keep(state, entry);
	return true;
}

// A custom emoji thumbnail is about 100 px and often white line art on a
// transparent background: the check gets it at its full side, on mid grey
// (the plain white flattening would leave such art an empty picture).
[[nodiscard]] QImage EmojiCover(const QImage &cover) {
	if (cover.isNull() || cover.width() <= 0 || cover.height() <= 0) {
		return QImage();
	}
	const auto side = JudgeProtocol::kMaxSide;
	auto scaled = cover.scaled(
		QSize(side, side),
		Qt::KeepAspectRatio,
		Qt::SmoothTransformation);
	if (scaled.isNull()) {
		return QImage();
	}
	scaled.setDevicePixelRatio(1.);
	auto result = QImage(scaled.size(), QImage::Format_RGB32);
	result.setDevicePixelRatio(1.);
	result.fill(QColor(kEmojiGrey, kEmojiGrey, kEmojiGrey));
	auto p = QPainter(&result);
	p.drawImage(0, 0, scaled);
	p.end();
	return result;
}

// An emoji document can be filled in (or its thumbnail found) after it was
// queued: ask for the thumbnail each time it is retried.
void WantEmojiThumbnail(const Pending &entry) {
	if (!entry.emoji || !entry.document) {
		return;
	}
	const auto document = entry.document->owner();
	if (document->hasThumbnail() && !entry.document->thumbnail()) {
		document->loadThumbnail(document->stickerSetOrigin());
	}
}

// Only the thumbnail, or a static emoji's own small picture: an animated
// emoji file is never downloaded before it is cleared.
[[nodiscard]] QImage EmojiCoverOf(const Pending &entry) {
	const auto media = entry.document;
	if (!media) {
		return QImage();
	} else if (const auto thumbnail = media->thumbnail()) {
		return thumbnail->original();
	}
	const auto document = media->owner();
	const auto sticker = document->sticker();
	if (!sticker || !sticker->isStatic() || document->hasThumbnail()) {
		return QImage();
	}
	media->checkStickerLarge();
	const auto image = media->getStickerSmall();
	return image ? image->original() : QImage();
}

// True when the entry is finished with (sent, answered or gone).
[[nodiscard]] bool TrySend(State &state, Pending &entry) {
	const auto session = entry.session.get();
	if (!session || Verdict(entry.key) != VerdictState::Waiting) {
		return true;
	} else if (entry.fromFile) {
		return TrySendFromFile(state, entry);
	}
	WantEmojiThumbnail(entry);
	if (entry.emoji && entry.caption.isEmpty() && entry.document) {
		// Queued before the document was fetched: its pack title and
		// alt emoji are known now.
		entry.caption = EmojiCaption(entry.document->owner());
	}
	const auto cover = entry.emoji
		? EmojiCover(EmojiCoverOf(entry))
		: CoverOf(entry);
	if (cover.isNull()) {
		return false;
	}
	// Cover only: no video frames are decoded here (that would mean
	// downloading and decoding the video before it is cleared).
	RequestVerdict(
		session,
		entry.key,
		cover,
		{},
		entry.caption,
		entry.fileName,
		entry.mime);
	Asked(state, entry.key);
	Keep(state, entry);
	return true;
}

void RetryPending() {
	auto &state = Instance();
	auto emoji = false;
	for (auto i = begin(state.pending); i != end(state.pending);) {
		if (TrySend(state, *i)) {
			i = state.pending.erase(i);
		} else {
			emoji = emoji || i->emoji;
			++i;
		}
	}
	if (emoji && !state.emojiRetryTimer.isActive()) {
		state.emojiRetryTimer.callOnce(kEmojiRetryDelay);
	}
}

void WatchSession(State &state, not_null<Main::Session*> session) {
	if (!state.sessions.emplace(session).second) {
		return;
	}
	session->downloaderTaskFinished(
	) | rpl::on_next([] {
		RetryPending();
	}, session->lifetime());
	session->lifetime().add([session] {
		auto &state = Instance();
		state.sessions.remove(session);
		const auto gone = [&](const Pending &entry) {
			const auto strong = entry.session.get();
			return !strong || (strong == session.get());
		};
		state.pending.erase(
			ranges::remove_if(state.pending, gone),
			end(state.pending));
	});
}

[[nodiscard]] bool Known(const State &state, MediaKey key) {
	if (state.asked.contains(key)) {
		return true;
	}
	return ranges::any_of(state.pending, [&](const Pending &entry) {
		return entry.key == key;
	});
}

void Queue(State &state, not_null<Main::Session*> session, Pending entry) {
	if (TrySend(state, entry)) {
		return;
	}
	WatchSession(state, session);
	if (entry.emoji && !state.emojiRetryTimer.isActive()) {
		state.emojiRetryTimer.callOnce(kEmojiRetryDelay);
	}
	state.pending.push_back(std::move(entry));
	while (state.pending.size() > kPendingLimit) {
		state.pending.pop_front();
	}
}

[[nodiscard]] QString CaptionOf(not_null<const HistoryItem*> item) {
	return item->originalText().text;
}

// The pack title and the emoji it stands for: the pack owner's own words.
QString EmojiCaption(not_null<DocumentData*> document) {
	const auto sticker = document->sticker();
	if (!sticker) {
		return QString();
	}
	auto result = QString();
	if (const auto id = sticker->set.id) {
		const auto &sets = document->owner().stickers().sets();
		if (const auto i = sets.find(id); i != end(sets)) {
			result = i->second->title;
		}
	}
	if (!sticker->alt.isEmpty()) {
		result = result.isEmpty() ? sticker->alt : (result + ' ' + sticker->alt);
	}
	return result;
}

// A custom emoji known only by its id (one in a held message's text is
// never painted, so Telegram's own loader never fetches it): fetch the
// document, else it would have no thumbnail to judge and stay held forever.
void ResolveEmoji(State &state, not_null<DocumentData*> document) {
	const auto key = KeyOf(document);
	if (document->sticker() || !key || state.resolving.contains(key)) {
		return;
	}
	const auto finish = [=] {
		// Never inside the lifetime's own callback.
		crl::on_main([=] {
			Instance().resolving.remove(key);
			RetryPending();
		});
	};
	auto &lifetime = state.resolving[key];
	document->owner().customEmojiManager().resolve(
		document->id
	) | rpl::take(1) | rpl::on_next_error([=](not_null<DocumentData*>) {
		finish();
	}, [=] {
		finish();
	}, lifetime);
}

} // namespace

void Remember(not_null<const HistoryItem*> item, MediaKey key) {
	if (!key) {
		return;
	}
	auto &state = Instance();
	const auto id = item->fullId();
	auto &list = state.watched[key];
	const auto session = &item->history()->session();
	const auto same = [&](const Watched &entry) {
		return (entry.id == id) && (entry.session.get() == session);
	};
	if (ranges::any_of(list, same)) {
		return;
	}
	list.push_back({ base::make_weak(session), id });
	if (list.size() > kWatchedPerKey) {
		list.erase(begin(list));
	}
	if (state.watched.size() > kWatchedLimit) {
		// Old entries only miss a repaint; the full repaint still runs.
		state.watched.erase(begin(state.watched));
	}
}

bool WantPhoto(
		not_null<const HistoryItem*> item,
		not_null<PhotoData*> photo) {
	const auto key = KeyOf(photo);
	if (!JudgeAvailable() || !key || photo->isNull()) {
		return false;
	}
	auto &state = Instance();
	if (Known(state, key)) {
		return true;
	}
	const auto session = &item->history()->session();
	auto entry = Pending{
		.session = base::make_weak(session),
		.key = key,
		.photo = photo->createMediaView(),
		.caption = CaptionOf(item),
		.mime = u"image/jpeg"_q,
	};
	photo->load(Data::PhotoSize::Thumbnail, item->fullId());
	Queue(state, session, std::move(entry));
	return true;
}

bool WantDocument(
		not_null<const HistoryItem*> item,
		not_null<DocumentData*> document) {
	const auto key = KeyOf(document);
	if (!JudgeAvailable() || !key) {
		return false;
	}
	auto &state = Instance();
	if (Known(state, key)) {
		return true;
	}
	const auto session = &item->history()->session();
	const auto origin = Data::FileOrigin(item->fullId());
	auto entry = Pending{
		.session = base::make_weak(session),
		.key = key,
		.caption = CaptionOf(item),
		.fileName = document->filename(),
		.mime = document->mimeString(),
	};
	const auto media = item->media();
	if (const auto cover = media ? media->videoCover() : nullptr) {
		entry.photo = cover->createMediaView();
		cover->load(Data::PhotoSize::Thumbnail, origin);
	} else if (document->hasThumbnail()) {
		entry.document = document->createMediaView();
		document->loadThumbnail(origin);
	} else if (const auto sticker = document->sticker()
		; sticker && sticker->isStatic()) {
		entry.document = document->createMediaView();
		entry.document->checkStickerLarge();
	} else {
		return false; // Nothing small to judge it by: it waits.
	}
	Queue(state, session, std::move(entry));
	return true;
}

bool WantUserpic(not_null<const PeerData*> peer, MediaKey key) {
	if (!JudgeAvailable() || !key) {
		return false;
	}
	auto &state = Instance();
	if (Known(state, key)) {
		return true;
	}
	const auto mutablePeer = const_cast<PeerData*>(peer.get());
	const auto session = &mutablePeer->session();
	auto entry = Pending{
		.session = base::make_weak(session),
		.key = key,
		.userpic = mutablePeer->createUserpicView(),
		.mime = u"image/jpeg"_q,
	};
	Queue(state, session, std::move(entry));
	return true;
}

bool WantSearchedGif(not_null<DocumentData*> document, MediaKey key) {
	if (!JudgeAvailable() || !key || document->size > kMaxFileBytes) {
		return false;
	}
	auto &state = Instance();
	if (Known(state, key)) {
		return true;
	}
	const auto session = &document->session();
	auto entry = Pending{
		.session = base::make_weak(session),
		.key = key,
		.document = document->createMediaView(),
		.fileName = document->filename(),
		.mime = document->mimeString(),
		.fromFile = true,
	};
	document->save(
		Data::FileOrigin(),
		QString(),
		LoadFromCloudOrLocal,
		true);
	Queue(state, session, std::move(entry));
	return true;
}

bool WantEmoji(not_null<DocumentData*> document) {
	const auto key = KeyOf(document);
	if (!JudgeAvailable() || !key) {
		return false;
	}
	auto &state = Instance();
	ResolveEmoji(state, document);
	if (Known(state, key)) {
		return true;
	}
	const auto session = &document->session();
	auto entry = Pending{
		.session = base::make_weak(session),
		.key = key,
		.document = document->createMediaView(),
		.caption = EmojiCaption(document),
		// The picture judged is the emoji's WebP thumbnail: a sticker, as on
		// the phone (patch 25), whatever the emoji file itself is.
		.mime = u"image/webp"_q,
		.emoji = true,
	};
	Queue(state, session, std::move(entry));
	return true;
}

} // namespace Safe::Covers
