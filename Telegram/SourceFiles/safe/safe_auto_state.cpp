/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_auto_state.h"

#include "data/data_peer.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Safe::Auto {
namespace {

constexpr auto kChannelShift = 1000000000000LL;
constexpr auto kMaxReason = 600;
constexpr auto kMaxKind = 64;
// Integers above this lose precision in a JSON double.
constexpr auto kMaxExact = 9007199254740991LL;

[[nodiscard]] std::optional<int64> Integer(const QJsonValue &value) {
	if (!value.isDouble()) {
		return std::nullopt;
	}
	const auto number = value.toDouble();
	if (number != std::floor(number)
		|| number > double(kMaxExact)
		|| number < -double(kMaxExact)) {
		return std::nullopt;
	}
	return int64(number);
}

[[nodiscard]] std::optional<Pace> PaceFromKey(const QString &key) {
	if (key == u"like_me"_q) {
		return Pace::LikeMe;
	} else if (key == u"calmer"_q) {
		return Pace::Calmer;
	} else if (key == u"slow"_q) {
		return Pace::Slow;
	}
	return std::nullopt;
}

[[nodiscard]] QString Clean(QString text, int limit) {
	text = text.simplified();
	if (text.size() > limit) {
		text = text.left(limit - 1) + QChar(0x2026);
	}
	return text;
}

} // namespace

std::optional<State> ParseState(const QByteArray &json, int64 chat) {
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}
	const auto object = document.object();
	if (Integer(object.value(u"chat"_q)) != chat
		|| !object.value(u"enabled"_q).isBool()) {
		return std::nullopt;
	}
	auto result = State{ .chat = chat };
	result.enabled = object.value(u"enabled"_q).toBool();

	const auto pace = object.value(u"pace"_q);
	if (pace.isString()) {
		result.pace = PaceFromKey(pace.toString()).value_or(Pace::Calmer);
	}

	const auto paused = object.value(u"paused_until"_q);
	if (!paused.isNull() && !paused.isUndefined()) {
		const auto until = paused.isDouble()
			? std::optional<int64>(int64(paused.toDouble()))
			: std::nullopt;
		if (!until || *until < 0) {
			return std::nullopt;
		}
		result.pausedUntil = *until;
	}

	const auto handback = object.value(u"handback"_q);
	if (handback.isObject()) {
		const auto data = handback.toObject();
		const auto kind = data.value(u"kind"_q);
		const auto reason = data.value(u"reason"_q);
		if (!kind.isString() || !reason.isString()) {
			return std::nullopt;
		}
		const auto msg = Integer(data.value(u"msg"_q)).value_or(0);
		result.handback = Handback{
			.kind = Clean(kind.toString(), kMaxKind),
			.reason = Clean(reason.toString(), kMaxReason),
			.msg = (msg > 0) ? msg : 0,
		};
	} else if (!handback.isNull() && !handback.isUndefined()) {
		return std::nullopt;
	}

	const auto quiet = object.value(u"quiet"_q);
	if (quiet.isString()) {
		const auto key = quiet.toString();
		result.quiet = (key == u"night"_q)
			? QuietKind::Night
			: (key == u"max_out"_q)
			? QuietKind::MaxOut
			: QuietKind::None;
	} else if (!quiet.isNull() && !quiet.isUndefined()) {
		return std::nullopt;
	}
	return result;
}

QByteArray ChangeBody(int64 chat, const Change &change) {
	auto result = "{\"chat\":" + QByteArray::number(chat);
	if (change.enabled) {
		result += ",\"enabled\":";
		result += *change.enabled ? "true" : "false";
	}
	if (change.pace) {
		result += ",\"pace\":\"" + PaceKey(*change.pace).toLatin1() + '"';
	}
	if (change.resume) {
		result += ",\"resume\":true";
	}
	if (change.unpause) {
		result += ",\"unpause\":true";
	}
	return result + '}';
}

QString PaceKey(Pace pace) {
	switch (pace) {
	case Pace::LikeMe: return u"like_me"_q;
	case Pace::Calmer: return u"calmer"_q;
	case Pace::Slow: return u"slow"_q;
	}
	return u"calmer"_q;
}

QString PaceLine(Pace pace) {
	switch (pace) {
	case Pace::LikeMe: return u"Like-me pace"_q;
	case Pace::Calmer: return u"Calmer pace"_q;
	case Pace::Slow: return u"Slow pace"_q;
	}
	return u"Calmer pace"_q;
}

int64 RunnerChatId(not_null<PeerData*> peer) {
	const auto id = peer->id;
	if (peerIsUser(id)) {
		return int64(peerToUser(id).bare);
	} else if (peerIsChat(id)) {
		return -int64(peerToChat(id).bare);
	} else if (peerIsChannel(id)) {
		return -(kChannelShift + int64(peerToChannel(id).bare));
	}
	return 0;
}

PeerId PeerFromChatId(int64 chat) {
	if (chat > 0) {
		return peerFromUser(UserId(BareId(chat)));
	} else if (chat < -kChannelShift) {
		return peerFromChannel(ChannelId(BareId(-chat - kChannelShift)));
	} else if (chat < 0) {
		return peerFromChat(::ChatId(BareId(-chat)));
	}
	return PeerId();
}

} // namespace Safe::Auto
