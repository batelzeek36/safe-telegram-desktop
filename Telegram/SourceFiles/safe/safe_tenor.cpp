/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_tenor.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "data/business/data_shortcut_messages.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_helpers.h"
#include "inline_bots/inline_bot_result.h"
#include "main/main_session.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Safe::Tenor {
namespace {

constexpr auto kQueryId = uint64(0x74656E6F72474946); // "tenorGIF"
constexpr auto kMaxPageBytes = qint64(8 * 1024 * 1024);
constexpr auto kFetchTimeout = std::chrono::milliseconds(15000);
constexpr auto kUserAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; "
	"rv:159.0) Gecko/20100101 Firefox/159.0";

struct Clip {
	QString id;
	QString title;
	QString pageUrl;
	QString mp4;
	int mp4Size = 0;
	QSize size;
	float64 duration = 0.;
	QString preview;
	int previewSize = 0;
	QSize previewDimensions;
};

[[nodiscard]] not_null<QNetworkAccessManager*> Manager() {
	static const auto result = new QNetworkAccessManager();
	return result;
}

[[nodiscard]] QUrl SearchUrl(const QString &query) {
	auto words = QStringList();
	for (const auto &word : query.simplified().split(' ', Qt::SkipEmptyParts)) {
		words.push_back(QString::fromLatin1(QUrl::toPercentEncoding(word)));
	}
	return QUrl(u"https://tenor.com/search/"_q
		+ words.join('-')
		+ u"-gifs"_q);
}

[[nodiscard]] QSize SizeOf(const QJsonValue &dims) {
	const auto list = dims.toArray();
	return (list.size() == 2)
		? QSize(list.at(0).toInt(), list.at(1).toInt())
		: QSize();
}

[[nodiscard]] bool IsTenorMedia(const QString &url) {
	const auto host = QUrl(url).host().toLower();
	return (QUrl(url).scheme() == u"https"_q)
		&& (host == u"tenor.com"_q || host.endsWith(u".tenor.com"_q));
}

[[nodiscard]] std::optional<Clip> ClipOf(const QJsonObject &result) {
	const auto formats = result.value(u"media_formats"_q).toObject();
	const auto mp4 = formats.value(u"mp4"_q).toObject();
	const auto preview = formats.value(u"gifpreview"_q).toObject();
	auto clip = Clip{
		.id = result.value(u"id"_q).toString(),
		.title = result.value(u"content_description"_q).toString(),
		.pageUrl = result.value(u"itemurl"_q).toString(),
		.mp4 = mp4.value(u"url"_q).toString(),
		.mp4Size = mp4.value(u"size"_q).toInt(),
		.size = SizeOf(mp4.value(u"dims"_q)),
		.duration = mp4.value(u"duration"_q).toDouble(),
		.preview = preview.value(u"url"_q).toString(),
		.previewSize = preview.value(u"size"_q).toInt(),
		.previewDimensions = SizeOf(preview.value(u"dims"_q)),
	};
	auto valid = false;
	clip.id.toULongLong(&valid);
	if (!valid
		|| clip.size.isEmpty()
		|| !IsTenorMedia(clip.mp4)
		|| !IsTenorMedia(clip.preview)) {
		return std::nullopt;
	}
	return clip;
}

[[nodiscard]] std::vector<Clip> ParsePage(const QByteArray &page) {
	const auto open = page.indexOf(R"(<script id="store-cache")");
	const auto start = (open >= 0) ? page.indexOf('>', open) : -1;
	const auto end = (start >= 0) ? page.indexOf("</script>", start) : -1;
	if (end < 0) {
		return {};
	}
	const auto json = QJsonDocument::fromJson(
		page.mid(start + 1, end - start - 1));
	const auto searches = json.object()
		.value(u"universal"_q).toObject()
		.value(u"search"_q).toObject();
	auto result = std::vector<Clip>();
	for (const auto &search : searches) {
		for (const auto &value : search.toObject()
				.value(u"results"_q).toArray()) {
			if (auto clip = ClipOf(value.toObject())) {
				result.push_back(std::move(*clip));
			}
		}
		break;
	}
	return result;
}

[[nodiscard]] MTPWebDocument WebDocument(
		const QString &url,
		int size,
		const QString &mime,
		MTPDocumentAttribute attribute) {
	return MTP_webDocumentNoProxy(
		MTP_string(url),
		MTP_int(size),
		MTP_string(mime),
		MTP_vector<MTPDocumentAttribute>(1, std::move(attribute)));
}

[[nodiscard]] MTPBotInlineResult ResultOf(const Clip &clip) {
	using Flag = MTPDbotInlineResult::Flag;
	using VideoFlag = MTPDdocumentAttributeVideo::Flag;
	const auto preview = clip.previewDimensions.isEmpty()
		? clip.size
		: clip.previewDimensions;
	return MTP_botInlineResult(
		MTP_flags(Flag::f_title
			| Flag::f_url
			| Flag::f_thumb
			| Flag::f_content),
		MTP_string(clip.id),
		MTP_string("gif"),
		MTP_string(clip.title),
		MTPstring(),
		MTP_string(clip.pageUrl),
		WebDocument(
			clip.preview,
			clip.previewSize,
			u"image/png"_q,
			MTP_documentAttributeImageSize(
				MTP_int(preview.width()),
				MTP_int(preview.height()))),
		WebDocument(
			clip.mp4,
			clip.mp4Size,
			u"video/mp4"_q,
			MTP_documentAttributeVideo(
				MTP_flags(VideoFlag::f_nosound),
				MTP_double(clip.duration),
				MTP_int(clip.size.width()),
				MTP_int(clip.size.height()),
				MTPint(),
				MTPdouble(),
				MTPstring())),
		MTP_botInlineMessageMediaAuto(
			MTP_flags(0),
			MTPstring(),
			MTPVector<MTPMessageEntity>(),
			MTPReplyMarkup()));
}

[[nodiscard]] MTPmessages_BotResults AnswerOf(const std::vector<Clip> &clips) {
	auto results = QVector<MTPBotInlineResult>();
	results.reserve(clips.size());
	for (const auto &clip : clips) {
		results.push_back(ResultOf(clip));
	}
	return MTP_messages_botResults(
		MTP_flags(0),
		MTP_long(kQueryId),
		MTPstring(),
		MTPInlineBotSwitchPM(),
		MTPInlineBotWebView(),
		MTP_vector<MTPBotInlineResult>(std::move(results)),
		MTP_int(0),
		MTP_vector<MTPUser>());
}

[[nodiscard]] QNetworkRequest Prepare(const QUrl &url) {
	auto result = QNetworkRequest(url);
	result.setRawHeader("User-Agent", kUserAgent);
	result.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	result.setAttribute(
		QNetworkRequest::CookieLoadControlAttribute,
		QNetworkRequest::Manual);
	result.setAttribute(
		QNetworkRequest::CookieSaveControlAttribute,
		QNetworkRequest::Manual);
	result.setTransferTimeout(kFetchTimeout);
	return result;
}

[[nodiscard]] bool IsOurs(not_null<const InlineBots::Result*> result) {
	return (result->getQueryId() == kQueryId)
		&& IsTenorMedia(result->contentUrl());
}

} // namespace

void Search(
		not_null<Main::Session*> session,
		const QString &query,
		Fn<void(const MTPmessages_BotResults&)> done) {
	const auto reply = Manager()->get(Prepare(SearchUrl(query)));
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		if (reply->bytesAvailable() > kMaxPageBytes) {
			reply->abort();
		}
	});
	const auto weak = base::make_weak(session);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (!weak) {
			return;
		}
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto page = (reply->error() == QNetworkReply::NoError
			&& status == 200)
			? reply->read(kMaxPageBytes)
			: QByteArray();
		done(AnswerOf(ParsePage(page)));
	});
}

QString ClipUrl(not_null<const InlineBots::Result*> result) {
	return IsOurs(result) ? result->contentUrl() : QString();
}

MediaKey KeyOf(not_null<const InlineBots::Result*> result) {
	return IsOurs(result)
		? MediaKey{ .id = result->getId().toULongLong(), .document = true }
		: MediaKey();
}

bool Send(
		not_null<ApiWrap*> api,
		not_null<InlineBots::Result*> result,
		const Api::SendAction &action,
		uint64 randomId,
		FullMsgId localId,
		int64 starsPaid,
		Fn<void(bool)> done) {
	const auto url = ClipUrl(result);
	if (url.isEmpty()) {
		return false;
	}
	const auto history = action.history;
	const auto peer = history->peer;
	const auto topicRootId = action.replyTo.messageId
		? action.replyTo.topicRootId
		: 0;
	const auto monoforumPeerId = action.replyTo.monoforumPeerId;
	using Flag = MTPmessages_SendMedia::Flag;
	auto flags = Flag::f_clear_draft | Flag();
	if (action.replyTo) {
		flags |= Flag::f_reply_to;
	}
	if (ShouldSendSilent(peer, action.options)) {
		flags |= Flag::f_silent;
	}
	if (action.options.scheduled) {
		flags |= Flag::f_schedule_date;
		if (action.options.scheduleRepeatPeriod) {
			flags |= Flag::f_schedule_repeat_period;
		}
	}
	if (action.options.shortcutId) {
		flags |= Flag::f_quick_reply_shortcut;
	}
	if (starsPaid) {
		flags |= Flag::f_allow_paid_stars;
	}
	const auto sendAs = action.options.sendAs;
	if (sendAs) {
		flags |= Flag::f_send_as;
	}
	const auto finish = [=](const MTP::Response &response) {
		history->finishSavingCloudDraft(
			topicRootId,
			monoforumPeerId,
			Api::UnixtimeFromMsgId(response.outerMsgId));
	};
	history->owner().histories().sendPreparedMessage(
		history,
		action.replyTo,
		randomId,
		Data::Histories::PrepareMessage<MTPmessages_SendMedia>(
			MTP_flags(flags),
			peer->input(),
			Data::Histories::ReplyToPlaceholder(),
			MTP_inputMediaDocumentExternal(
				MTP_flags(0),
				MTP_string(url),
				MTPint(), // ttl_seconds
				MTPInputPhoto(), // video_cover
				MTPint()), // video_timestamp
			MTPstring(),
			MTP_long(randomId),
			MTPReplyMarkup(),
			MTPvector<MTPMessageEntity>(),
			MTP_int(action.options.scheduled),
			MTP_int(action.options.scheduleRepeatPeriod),
			(sendAs ? sendAs->input() : MTP_inputPeerEmpty()),
			Data::ShortcutIdToMTP(&history->session(), action.options.shortcutId),
			MTP_long(0),
			MTP_long(starsPaid),
			Api::SuggestToMTP({})
		), [=](const MTPUpdates &, const MTP::Response &response) {
		finish(response);
		if (done) {
			done(true);
		}
	}, [=](const MTP::Error &error, const MTP::Response &response) {
		api->sendMessageFail(error, peer, randomId, localId);
		finish(response);
		if (done) {
			done(false);
		}
	});
	return true;
}

} // namespace Safe::Tenor
