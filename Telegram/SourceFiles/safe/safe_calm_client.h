/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_calm_prompt.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace Safe::Calm {

// One check = the runner on MAINGEAR first (Bearer key, kRunnerTimeoutMs),
// then on any failure the Messages API with Sonnet 5.5 on the picture-check
// key (kApiTimeoutMs), then a failure Calm holds on. No redirects, no cache,
// no cookies; each key goes in its own header and nowhere else.
// Main thread only; `done` runs on the main thread exactly once, unless the
// returned id is cancelled first.
class Client final {
public:
	Client();
	~Client();

	uint64 check(Request request, Fn<void(Result)> done);
	void cancel(uint64 id);

	[[nodiscard]] bool hasRunner() const;
	[[nodiscard]] bool hasApi() const;

private:
	struct Call;

	void askRunner(uint64 id);
	void askApi(uint64 id, QString runnerFailure);
	void post(
		uint64 id,
		bool api,
		Fn<void(int status, QByteArray body, QString failure)> done);
	void finish(uint64 id, Result result);
	[[nodiscard]] not_null<QNetworkAccessManager*> manager();

	QByteArray _runnerUrl;
	QByteArray _runnerKey;
	QByteArray _apiKey;
	std::unique_ptr<QNetworkAccessManager> _manager;
	base::flat_map<uint64, std::unique_ptr<Call>> _calls;
	uint64 _nextId = 0;

};

} // namespace Safe::Calm
