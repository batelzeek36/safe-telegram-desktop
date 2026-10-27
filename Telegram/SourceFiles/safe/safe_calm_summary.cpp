/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_summary.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Safe::Calm {
namespace {

[[nodiscard]] QString MediaKey(RunMedia media) {
	switch (media) {
	case RunMedia::Gif: return u"gif"_q;
	case RunMedia::Sticker: return u"sticker"_q;
	case RunMedia::Photo: return u"photo"_q;
	case RunMedia::Video: return u"video"_q;
	case RunMedia::None: break;
	}
	return QString();
}

[[nodiscard]] int TextCount(const SummaryRequest &request) {
	return int(ranges::count(
		request.run,
		RunMedia::None,
		&RunLine::media));
}

} // namespace

QByteArray SummaryRunnerBody(const SummaryRequest &request) {
	auto run = QJsonArray();
	const auto from = std::max(int(request.run.size()) - kMaxRunLines, 0);
	for (auto i = from; i < int(request.run.size()); ++i) {
		const auto &line = request.run[i];
		auto item = QJsonObject();
		if (line.media == RunMedia::None) {
			item.insert(u"text"_q, line.text.left(kMaxTextLength));
		} else {
			item.insert(u"media"_q, MediaKey(line.media));
			const auto caption = line.caption.trimmed();
			if (!caption.isEmpty()) {
				item.insert(u"caption"_q, caption.left(kMaxContextText));
			}
		}
		if (!line.ago.isEmpty()) {
			item.insert(u"ago"_q, line.ago.left(kMaxAgo));
		}
		run.push_back(item);
	}
	const auto context = (int(request.context.size()) > kSummaryContext)
		? std::vector<ContextLine>(
			request.context.end() - kSummaryContext,
			request.context.end())
		: request.context;
	auto object = QJsonObject{
		{ u"run"_q, run },
		{ u"context"_q, ContextJson(context) },
	};
	const auto recipient = request.recipient.trimmed().left(64);
	if (!recipient.isEmpty()) {
		object.insert(u"recipient"_q, recipient);
	}
	if (!request.chat.isEmpty()) {
		object.insert(u"chat"_q, request.chat);
	}
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

SummaryResult ParseSummaryRunner(
		int status,
		const QByteArray &body,
		const SummaryRequest &request) {
	if (status != 200) {
		return { .failure = u"runner http %1"_q.arg(status) };
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(body, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return { .failure = u"runner bad json"_q };
	}
	const auto object = document.object();
	auto text = object.value(u"text"_q).toString().trimmed();
	text.replace(QChar(0x2014), QChar('-'));
	const auto count = object.value(u"count"_q).toInt(-1);
	if (text.isEmpty() || text.size() > kMaxTextLength) {
		return { .failure = u"runner bad answer"_q };
	} else if (count != TextCount(request)) {
		// The answer must be for this run, all of its texts.
		return { .failure = u"runner count %1"_q.arg(count) };
	}
	const auto via = object.value(u"via"_q).toString();
	return {
		.text = text,
		.count = count,
		.via = via.isEmpty() ? u"runner"_q : via.left(16),
	};
}

} // namespace Safe::Calm
