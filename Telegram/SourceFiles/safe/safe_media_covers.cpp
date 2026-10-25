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
	rpl::lifetime lifetime;
};

void RefreshWatched(State &state, std::optional<MediaKey> only);
void ScheduleRepaint(State &state);

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

// True when the entry is finished with (sent, answered or gone).
[[nodiscard]] bool TrySend(State &state, Pending &entry) {
	const auto session = entry.session.get();
	if (!session || Verdict(entry.key) != VerdictState::Waiting) {
		return true;
	} else if (entry.fromFile) {
		return TrySendFromFile(state, entry);
	}
	const auto cover = CoverOf(entry);
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
	for (auto i = begin(state.pending); i != end(state.pending);) {
		if (TrySend(state, *i)) {
			i = state.pending.erase(i);
		} else {
			++i;
		}
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
	state.pending.push_back(std::move(entry));
	while (state.pending.size() > kPendingLimit) {
		state.pending.pop_front();
	}
}

[[nodiscard]] QString CaptionOf(not_null<const HistoryItem*> item) {
	return item->originalText().text;
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

bool WantSearchedGif(not_null<DocumentData*> document) {
	const auto key = KeyOf(document);
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

} // namespace Safe::Covers
