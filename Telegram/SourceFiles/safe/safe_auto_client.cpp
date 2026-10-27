/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_auto_client.h"

#include "safe/safe_calm_data.h"

#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Safe::Auto {
namespace {

constexpr auto kTimeoutMs = 8000;
constexpr auto kMaxResponseBytes = 64 * 1024;

[[nodiscard]] QNetworkRequest Prepare(const QUrl &url) {
	auto result = QNetworkRequest(url);
	result.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	result.setRawHeader("accept", "application/json");
	result.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	result.setAttribute(
		QNetworkRequest::CacheLoadControlAttribute,
		QNetworkRequest::AlwaysNetwork);
	result.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
	result.setAttribute(
		QNetworkRequest::CookieLoadControlAttribute,
		QNetworkRequest::Manual);
	result.setAttribute(
		QNetworkRequest::CookieSaveControlAttribute,
		QNetworkRequest::Manual);
	result.setTransferTimeout(std::chrono::milliseconds(kTimeoutMs));
	return result;
}

[[nodiscard]] QByteArray Base(QByteArray url) {
	while (url.endsWith('/')) {
		url.chop(1);
	}
	return url;
}

} // namespace

Client::Client()
: _base(Base(QByteArray(kCalmUrl)))
, _key(QByteArray(kCalmKey)) {
}

Client::~Client() {
	for (const auto &reply : base::take(_replies)) {
		if (const auto raw = reply.data()) {
			raw->disconnect();
			raw->abort();
		}
	}
}

bool Client::available() const {
	return !_base.isEmpty() && !_key.isEmpty();
}

not_null<QNetworkAccessManager*> Client::manager() {
	if (!_manager) {
		_manager = std::make_unique<QNetworkAccessManager>();
	}
	return _manager.get();
}

void Client::fetch(int64 chat, Fn<void(std::optional<State>)> done) {
	send(chat, std::nullopt, std::move(done));
}

void Client::change(
		int64 chat,
		Change change,
		Fn<void(std::optional<State>)> done) {
	send(chat, ChangeBody(chat, change), std::move(done));
}

void Client::send(
		int64 chat,
		std::optional<QByteArray> body,
		Fn<void(std::optional<State>)> done) {
	if (!available()) {
		done(std::nullopt);
		return;
	}
	auto url = QUrl(QString::fromLatin1(_base + "/v1/auto/state"));
	if (!body) {
		auto query = QUrlQuery();
		query.addQueryItem(u"chat"_q, QString::number(chat));
		url.setQuery(query);
	}
	auto request = Prepare(url);
	request.setRawHeader("Authorization", "Bearer " + _key);
	const auto reply = body
		? manager()->post(request, *body)
		: manager()->get(request);
	_replies.erase(
		ranges::remove(_replies, nullptr, [](const auto &p) {
			return p.data();
		}),
		end(_replies));
	_replies.push_back(reply);

	struct Flags {
		bool tooLong = false;
		bool finished = false;
	};
	const auto flags = std::make_shared<Flags>();
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		if (!flags->finished && reply->bytesAvailable() > kMaxResponseBytes) {
			flags->tooLong = true;
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		if (flags->finished) {
			return;
		}
		flags->finished = true;
		reply->deleteLater();
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (flags->tooLong || status != 200) {
			if (status != 0 && status != 200) {
				LOG(("Safe Auto: the runner answered %1.").arg(status));
			}
			done(std::nullopt);
			return;
		}
		auto parsed = ParseState(reply->read(kMaxResponseBytes + 1), chat);
		if (!parsed) {
			LOG(("Safe Auto: the runner's state did not parse."));
		}
		done(std::move(parsed));
	});
	QTimer::singleShot(kTimeoutMs + 1000, reply, [=] {
		if (!flags->finished) {
			reply->abort();
		}
	});
}

} // namespace Safe::Auto
