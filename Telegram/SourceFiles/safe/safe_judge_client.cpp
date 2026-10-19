/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_judge_client.h"

#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Safe {
namespace {

using JudgeProtocol::Failed;
using JudgeProtocol::Result;

struct CallState {
	bool timedOut = false;
	bool tooLong = false;
	bool finished = false;
};

[[nodiscard]] QNetworkRequest Prepare(const QByteArray &key) {
	auto result = QNetworkRequest(QUrl(QString::fromLatin1(
		JudgeProtocol::kEndpoint.data(),
		qsizetype(JudgeProtocol::kEndpoint.size()))));
	result.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	result.setRawHeader("accept", "application/json");
	result.setRawHeader("anthropic-version", QByteArray(
		JudgeProtocol::kApiVersion.data(),
		qsizetype(JudgeProtocol::kApiVersion.size())));
	result.setRawHeader("x-api-key", key);
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
	result.setTransferTimeout(
		std::chrono::milliseconds(JudgeProtocol::kTimeoutMs));
	return result;
}

[[nodiscard]] Result ResultOf(
		not_null<QNetworkReply*> reply,
		const CallState &state) {
	if (state.tooLong) {
		return Failed("answer too long");
	} else if (state.timedOut) {
		return Failed("timeout");
	}
	const auto status = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (status > 0) {
		const auto body = reply->read(
			qint64(JudgeProtocol::kMaxResponseBytes) + 1);
		if (std::size_t(body.size()) > JudgeProtocol::kMaxResponseBytes) {
			return Failed("answer too long");
		}
		return JudgeProtocol::Parse(
			status,
			std::string_view(body.constData(), std::size_t(body.size())));
	}
	switch (reply->error()) {
	case QNetworkReply::TimeoutError:
	case QNetworkReply::OperationCanceledError:
		return Failed("timeout");
	default:
		return Failed("network");
	}
}

} // namespace

JudgeClient::JudgeClient(QByteArray key) : _key(std::move(key)) {
}

JudgeClient::~JudgeClient() = default;

not_null<QNetworkAccessManager*> JudgeClient::manager() {
	if (!_manager) {
		_manager = std::make_unique<QNetworkAccessManager>();
	}
	return _manager.get();
}

void JudgeClient::call(
		const JudgeProtocol::Request &request,
		Fn<void(Result)> done) {
	const auto body = JudgeProtocol::RequestBody(request);
	if (_key.isEmpty() || !body) {
		const auto failure = _key.isEmpty() ? "no key" : "no picture bytes";
		crl::on_main([=] {
			done(Failed(failure));
		});
		return;
	}
	const auto reply = manager()->post(
		Prepare(_key),
		QByteArray::fromStdString(*body));
	const auto state = std::make_shared<CallState>();
	const auto finish = [=] {
		if (state->finished) {
			return;
		}
		state->finished = true;
		auto result = ResultOf(reply, *state);
		reply->deleteLater();
		done(std::move(result));
	};
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		const auto limit = qint64(JudgeProtocol::kMaxResponseBytes);
		if (!state->finished && reply->bytesAvailable() > limit) {
			state->tooLong = true;
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, finish);
	QTimer::singleShot(2 * JudgeProtocol::kTimeoutMs, reply, [=] {
		if (!state->finished) {
			state->timedOut = true;
			reply->abort();
		}
	});
}

} // namespace Safe
