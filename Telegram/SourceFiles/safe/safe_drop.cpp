/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_drop.h"

#include "core/mime_type.h"
#include "media/media_video_encode.h"
#include "ui/toast/toast.h"

#ifdef Q_OS_MAC
#include "safe/safe_drop_mac.h"
#endif // Q_OS_MAC

#include <QtCore/QMimeData>
#include <QtCore/QRegularExpression>
#include <QtCore/QTemporaryFile>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Safe {
namespace {

constexpr auto kMaxGifBytes = qint64(20 * 1024 * 1024);
constexpr auto kFetchTimeout = std::chrono::milliseconds(15000);

struct TenorGif {
	QString id;
	QString slug;
};

[[nodiscard]] bool IsRemote(const QUrl &url) {
	const auto scheme = url.scheme().toLower();
	return (scheme == u"http"_q) || (scheme == u"https"_q);
}

#ifdef Q_OS_MAC
[[nodiscard]] QList<QUrl> BrowserFiles(const QUrl &link) {
	return Mac::BrowserFiles(link);
}
#else // Q_OS_MAC
[[nodiscard]] QList<QUrl> BrowserFiles(const QUrl &) {
	return {};
}
#endif // Q_OS_MAC

[[nodiscard]] std::optional<TenorGif> FindTenorGif(const QString &text) {
	// Media links are <11-char gif id><5-char rendition>/<slug>.<ext>.
	static const auto expression = QRegularExpression(
		u"https?://(?:media\\d*|c)\\.tenor\\.com/(?:m/)?([A-Za-z0-9_-]{11})[A-Za-z0-9_-]{5}/([A-Za-z0-9._~%-]+?)\\.(?:gif|webp|mp4|webm|png|jpe?g)"_q);
	const auto match = expression.match(text);
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	return TenorGif{ .id = match.captured(1), .slug = match.captured(2) };
}

[[nodiscard]] std::optional<TenorGif> TenorGifIn(
		not_null<const QMimeData*> data) {
	for (const auto &url : data->urls()) {
		if (const auto found = FindTenorGif(url.toString())) {
			return found;
		}
	}
	if (data->hasHtml()) {
		if (const auto found = FindTenorGif(data->html())) {
			return found;
		}
	}
	return data->hasText() ? FindTenorGif(data->text()) : std::nullopt;
}

[[nodiscard]] QUrl Mp4Url(const TenorGif &gif) {
	return QUrl(u"https://media.tenor.com/"_q
		+ gif.id
		+ u"AAAPo/"_q
		+ gif.slug
		+ u".mp4"_q);
}

[[nodiscard]] not_null<QNetworkAccessManager*> Manager() {
	static const auto result = new QNetworkAccessManager();
	return result;
}

[[nodiscard]] bool LooksLikeMp4(const QByteArray &bytes) {
	return (bytes.size() > 12) && (bytes.mid(4, 4) == "ftyp");
}

[[nodiscard]] QString SaveTemporary(const QByteArray &bytes) {
	auto file = QTemporaryFile(Media::Encode::TempFileTemplate(u"mp4"_q));
	file.setAutoRemove(false);
	if (!file.open()) {
		return QString();
	} else if (file.write(bytes) != bytes.size()) {
		file.remove();
		return QString();
	}
	file.close();
	return file.fileName();
}

[[nodiscard]] QStringList LocalFiles(not_null<const QMimeData*> data) {
	auto result = QStringList();
	for (const auto &url : Core::ReadMimeUrls(data)) {
		if (url.isLocalFile()) {
			result.push_back(url.toLocalFile());
		}
	}
	return result;
}

[[nodiscard]] QByteArray ReadMp4(not_null<QNetworkReply*> reply) {
	const auto status = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (reply->error() != QNetworkReply::NoError || status != 200) {
		return QByteArray();
	}
	auto result = reply->read(kMaxGifBytes + 1);
	return (result.size() <= kMaxGifBytes && LooksLikeMp4(result))
		? result
		: QByteArray();
}

void Fetch(
		const TenorGif &gif,
		QStringList fallback,
		Fn<void(QStringList)> send) {
	auto request = QNetworkRequest(Mp4Url(gif));
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setAttribute(
		QNetworkRequest::CookieLoadControlAttribute,
		QNetworkRequest::Manual);
	request.setAttribute(
		QNetworkRequest::CookieSaveControlAttribute,
		QNetworkRequest::Manual);
	request.setTransferTimeout(kFetchTimeout);
	const auto reply = Manager()->get(request);
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		if (reply->bytesAvailable() > kMaxGifBytes) {
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		const auto path = SaveTemporary(ReadMp4(reply));
		if (!path.isEmpty()) {
			send({ path });
		} else if (!fallback.isEmpty()) {
			send(fallback);
		} else {
			Ui::Toast::Show(u"Could not get this GIF from Tenor."_q);
		}
	});
}

} // namespace

QList<QUrl> PreferBrowserFiles(
		not_null<const QMimeData*> data,
		QList<QUrl> urls) {
	if (urls.isEmpty()
		|| !data->hasImage()
		|| !ranges::all_of(urls, IsRemote)) {
		return urls;
	}
	auto files = BrowserFiles(urls.front());
	return files.isEmpty() ? urls : files;
}

bool TakeHostedGif(
		not_null<const QMimeData*> data,
		Fn<void(QStringList)> send) {
	if (!data->hasImage()) {
		return false;
	}
	const auto gif = TenorGifIn(data);
	if (!gif) {
		return false;
	}
	Fetch(*gif, LocalFiles(data), std::move(send));
	return true;
}

} // namespace Safe
