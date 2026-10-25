/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_client.h"

#include "safe/safe_calm_data.h"
#include "safe/safe_judge_data.h"

#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Safe::Calm {
namespace {

[[nodiscard]] QNetworkRequest Prepare(const QUrl &url, int timeoutMs) {
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
	result.setTransferTimeout(std::chrono::milliseconds(timeoutMs));
	return result;
}

[[nodiscard]] QByteArray RunnerBase(QByteArray base) {
	while (base.endsWith('/')) {
		base.chop(1);
	}
	return base;
}

} // namespace

struct Client::Call {
	Request request;
	Fn<void(Result)> done;
	std::optional<SuggestRequest> suggest; // a suggestions call when set
	Fn<void(SuggestResult)> suggested;
	QPointer<QNetworkReply> reply;
	bool finished = false;
};

Client::Client()
: _runnerUrl(RunnerBase(QByteArray(kCalmUrl)))
, _runnerKey(QByteArray(kCalmKey))
, _apiKey(QByteArray(kJudgeKey)) {
}

Client::~Client() {
	for (const auto &[id, call] : base::take(_calls)) {
		call->finished = true;
		if (const auto reply = call->reply.data()) {
			reply->abort();
		}
	}
}

bool Client::hasRunner() const {
	return !_runnerUrl.isEmpty() && !_runnerKey.isEmpty();
}

bool Client::hasApi() const {
	return !_apiKey.isEmpty();
}

not_null<QNetworkAccessManager*> Client::manager() {
	if (!_manager) {
		_manager = std::make_unique<QNetworkAccessManager>();
	}
	return _manager.get();
}

uint64 Client::check(Request request, Fn<void(Result)> done) {
	auto call = std::make_unique<Call>();
	call->request = std::move(request);
	call->done = std::move(done);
	return start(std::move(call));
}

uint64 Client::suggest(
		SuggestRequest request,
		Fn<void(SuggestResult)> done) {
	auto call = std::make_unique<Call>();
	call->suggest = std::move(request);
	call->suggested = std::move(done);
	return start(std::move(call));
}

uint64 Client::start(std::unique_ptr<Call> call) {
	const auto id = ++_nextId;
	_calls.emplace(id, std::move(call));
	if (hasRunner()) {
		askRunner(id);
	} else {
		askApi(id, u"no runner"_q);
	}
	return id;
}

void Client::cancel(uint64 id) {
	const auto i = _calls.find(id);
	if (i == end(_calls)) {
		return;
	}
	const auto call = std::move(i->second);
	_calls.erase(i);
	call->finished = true;
	if (const auto reply = call->reply.data()) {
		reply->abort();
	}
}

void Client::askRunner(uint64 id) {
	post(id, false, [=](int status, QByteArray body, QString failure) {
		const auto i = _calls.find(id);
		if (i == end(_calls)) {
			return;
		}
		if (i->second->suggest) {
			auto result = failure.isEmpty()
				? ParseSuggestRunner(status, body)
				: SuggestResult{ .failure = u"runner "_q + failure };
			if (!result.list.empty()) {
				finishSuggest(id, std::move(result));
			} else {
				askApi(id, result.failure);
			}
			return;
		}
		auto result = failure.isEmpty()
			? ParseRunner(status, body, i->second->request)
			: Result{ .failure = u"runner "_q + failure };
		if (result.verdict) {
			finish(id, std::move(result));
		} else {
			askApi(id, result.failure);
		}
	});
}

void Client::askApi(uint64 id, QString runnerFailure) {
	const auto i = _calls.find(id);
	if (i == end(_calls)) {
		return;
	}
	const auto suggesting = i->second->suggest.has_value();
	if (!hasApi()) {
		const auto failure = runnerFailure + u", no api key"_q;
		if (suggesting) {
			finishSuggest(id, { .failure = failure });
		} else {
			finish(id, { .failure = failure });
		}
		return;
	}
	post(id, true, [=](int status, QByteArray body, QString failure) {
		const auto i = _calls.find(id);
		if (i == end(_calls)) {
			return;
		}
		if (suggesting) {
			auto result = failure.isEmpty()
				? ParseSuggestApi(status, body)
				: SuggestResult{ .failure = u"api "_q + failure };
			if (result.list.empty()) {
				result.failure = runnerFailure + u", "_q + result.failure;
			}
			finishSuggest(id, std::move(result));
			return;
		}
		auto result = failure.isEmpty()
			? ParseApi(status, body, i->second->request)
			: Result{ .failure = u"api "_q + failure };
		if (!result.verdict) {
			result.failure = runnerFailure + u", "_q + result.failure;
		}
		finish(id, std::move(result));
	});
}

void Client::post(
		uint64 id,
		bool api,
		Fn<void(int status, QByteArray body, QString failure)> done) {
	const auto i = _calls.find(id);
	if (i == end(_calls)) {
		return;
	}
	const auto call = i->second.get();
	const auto timeoutMs = api ? kApiTimeoutMs : kRunnerTimeoutMs;
	const auto runnerUrl = _runnerUrl
		+ (call->suggest ? "/v1/suggest" : "/v1/check");
	auto request = Prepare(
		QUrl(QString::fromLatin1(api ? QByteArray(kApiEndpoint) : runnerUrl)),
		timeoutMs);
	if (api) {
		request.setRawHeader("x-api-key", _apiKey);
		request.setRawHeader("anthropic-version", QByteArray(kApiVersion));
	} else {
		request.setRawHeader("Authorization", "Bearer " + _runnerKey);
	}
	const auto body = call->suggest
		? (api
			? SuggestApiBody(*call->suggest)
			: SuggestRunnerBody(*call->suggest))
		: api
		? ApiBody(call->request)
		: RunnerBody(call->request);
	const auto reply = manager()->post(request, body);
	call->reply = reply;

	struct State {
		bool timedOut = false;
		bool tooLong = false;
		bool finished = false;
	};
	const auto state = std::make_shared<State>();
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		if (!state->finished && reply->bytesAvailable() > kMaxResponseBytes) {
			state->tooLong = true;
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		if (state->finished) {
			return;
		}
		state->finished = true;
		reply->deleteLater();
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (state->tooLong) {
			done(0, {}, u"answer too long"_q);
		} else if (state->timedOut) {
			done(0, {}, u"timeout"_q);
		} else if (status > 0) {
			done(status, reply->read(kMaxResponseBytes + 1), QString());
		} else if (reply->error() == QNetworkReply::TimeoutError
			|| reply->error() == QNetworkReply::OperationCanceledError) {
			done(0, {}, u"timeout"_q);
		} else {
			done(0, {}, u"network"_q);
		}
	});
	QTimer::singleShot(timeoutMs, reply, [=] {
		if (!state->finished) {
			state->timedOut = true;
			reply->abort();
		}
	});
}

void Client::finishSuggest(uint64 id, SuggestResult result) {
	const auto i = _calls.find(id);
	if (i == end(_calls)) {
		return;
	}
	const auto call = std::move(i->second);
	_calls.erase(i);
	if (!call->finished) {
		call->finished = true;
		call->suggested(std::move(result));
	}
}

void Client::finish(uint64 id, Result result) {
	const auto i = _calls.find(id);
	if (i == end(_calls)) {
		return;
	}
	const auto call = std::move(i->second);
	_calls.erase(i);
	if (!call->finished) {
		call->finished = true;
		call->done(std::move(result));
	}
}

} // namespace Safe::Calm
