/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_auto_state.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace Safe::Auto {

// Full-auto's endpoints on the Calm runner (same address and key as Calm,
// safe_calm_data.h): read a chat's state, change it. No redirects, no cache,
// no cookies, the key in its own header and nowhere else. Main thread only;
// `done` runs exactly once, with nothing when there was no good answer.
class Client final {
public:
	Client();
	~Client();

	[[nodiscard]] bool available() const;

	void fetch(int64 chat, Fn<void(std::optional<State>)> done);
	void change(
		int64 chat,
		Change change,
		Fn<void(std::optional<State>)> done);

private:
	void send(
		int64 chat,
		std::optional<QByteArray> body,
		Fn<void(std::optional<State>)> done);
	[[nodiscard]] not_null<QNetworkAccessManager*> manager();

	QByteArray _base;
	QByteArray _key;
	std::unique_ptr<QNetworkAccessManager> _manager;
	std::vector<QPointer<QNetworkReply>> _replies;

};

} // namespace Safe::Auto
