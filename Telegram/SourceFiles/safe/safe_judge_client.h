/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_judge_protocol.h"

class QNetworkAccessManager;

namespace Safe {

// One POST to the Messages API per call, no retry (the book backs off), no
// redirects, no cache, no cookies, JudgeProtocol::kTimeoutMs without a byte
// and twice that in all. The key goes in one header and nowhere else.
// Main thread only; `done` runs on the main thread exactly once.
class JudgeClient final {
public:
	explicit JudgeClient(QByteArray key);
	~JudgeClient();

	void call(
		const JudgeProtocol::Request &request,
		Fn<void(JudgeProtocol::Result)> done);

private:
	[[nodiscard]] not_null<QNetworkAccessManager*> manager();

	QByteArray _key;
	std::unique_ptr<QNetworkAccessManager> _manager;

};

} // namespace Safe
