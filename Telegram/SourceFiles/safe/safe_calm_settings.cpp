/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_settings.h"

#include "base/unixtime.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "safe/safe_calm_files.h"
#include "safe/safe_calm_core.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Safe::Calm {
namespace {

const auto kFileName = u"safe_calm.json"_q;

static_assert(!Core::kAutoMergeDefault, "Settings::autoMerge starts off");

struct State {
	Settings settings;
	rpl::event_stream<> changes;
	bool loaded = false;
};

[[nodiscard]] State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString SendModeKey(SendMode mode) {
	switch (mode) {
	case SendMode::Off: return u"off"_q;
	case SendMode::Review: return u"review"_q;
	case SendMode::Autopilot: return u"autopilot"_q;
	}
	Unexpected("Safe::Calm::SendMode in SendModeKey.");
}

[[nodiscard]] SendMode SendModeFrom(const QString &key) {
	return (key == u"off"_q)
		? SendMode::Off
		: (key == u"autopilot"_q)
		? SendMode::Autopilot
		: SendMode::Review;
}

[[nodiscard]] QString RuleKey(ChatRule rule) {
	switch (rule) {
	case ChatRule::Default: return u"default"_q;
	case ChatRule::Always: return u"always"_q;
	case ChatRule::Off: return u"off"_q;
	}
	Unexpected("Safe::Calm::ChatRule in RuleKey.");
}

[[nodiscard]] ChatRule RuleFrom(const QString &key) {
	return (key == u"always"_q)
		? ChatRule::Always
		: (key == u"off"_q)
		? ChatRule::Off
		: ChatRule::Default;
}

void Save(const Settings &settings) {
	auto chats = QJsonObject();
	for (const auto &[peer, entry] : settings.chats) {
		if (entry.rule == ChatRule::Default) {
			continue;
		}
		chats.insert(QString::number(peer.value), QJsonObject{
			{ u"rule"_q, RuleKey(entry.rule) },
			{ u"since"_q, double(entry.since) },
		});
	}
	WritePrivateFile(kFileName, QJsonDocument(QJsonObject{
		{ u"send"_q, SendModeKey(settings.send) },
		{ u"typing"_q, settings.typing },
		{ u"incoming"_q, settings.incoming },
		{ u"incomingSince"_q, double(settings.incomingSince) },
		{ u"suggest"_q, settings.suggest },
		{ u"ready"_q, settings.ready },
		{ u"merge"_q, settings.merge },
		{ u"hold"_q, settings.hold },
		{ u"autoMerge"_q, settings.autoMerge },
		{ u"chats"_q, chats },
	}).toJson(QJsonDocument::Indented));
}

void Load(State &state) {
	state.loaded = true;
	auto &settings = state.settings;
	const auto raw = ReadPrivateFile(kFileName);
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(raw, &error);
	if (raw.isEmpty()
		|| error.error != QJsonParseError::NoError
		|| !document.isObject()) {
		// First run (or an unreadable file): their side counts from now.
		settings = Settings{ .incomingSince = base::unixtime::now() };
		Save(settings);
		return;
	}
	const auto object = document.object();
	settings.send = SendModeFrom(object.value(u"send"_q).toString());
	settings.typing = object.value(u"typing"_q).toBool(true);
	settings.incoming = object.value(u"incoming"_q).toBool(true);
	settings.suggest = object.value(u"suggest"_q).toBool(true);
	settings.ready = object.value(u"ready"_q).toBool(false);
	settings.merge = object.value(u"merge"_q).toBool(true);
	settings.hold = object.value(u"hold"_q).toBool(true);
	settings.autoMerge = object.value(u"autoMerge"_q).toBool(
		Core::kAutoMergeDefault);
	settings.incomingSince = TimeId(
		object.value(u"incomingSince"_q).toDouble());
	if (settings.incoming && settings.incomingSince <= 0) {
		settings.incomingSince = base::unixtime::now();
	}
	const auto chats = object.value(u"chats"_q).toObject();
	for (auto i = chats.begin(); i != chats.end(); ++i) {
		auto ok = false;
		const auto value = i.key().toULongLong(&ok);
		const auto entry = i.value().toObject();
		const auto rule = RuleFrom(entry.value(u"rule"_q).toString());
		if (!ok || !value || rule == ChatRule::Default) {
			continue;
		}
		settings.chats.emplace(PeerId(value), ChatEntry{
			.rule = rule,
			.since = TimeId(entry.value(u"since"_q).toDouble()),
		});
	}
}

[[nodiscard]] State &Loaded() {
	auto &state = Instance();
	if (!state.loaded) {
		Load(state);
	}
	return state;
}

[[nodiscard]] bool DefaultKind(not_null<PeerData*> peer) {
	if (peer->isSelf() || peer->isServiceUser() || peer->isRepliesChat()) {
		return false;
	} else if (const auto user = peer->asUser()) {
		return !user->isBot() && !user->isInaccessible();
	}
	return peer->isChat() || peer->isMegagroup();
}

} // namespace

const Settings &Current() {
	return Loaded().settings;
}

rpl::producer<> Changes() {
	return Loaded().changes.events();
}

void Update(Fn<void(Settings&)> change) {
	auto &state = Loaded();
	const auto wasIncoming = state.settings.incoming;
	change(state.settings);
	if (state.settings.incoming && !wasIncoming) {
		state.settings.incomingSince = base::unixtime::now();
	}
	Save(state.settings);
	state.changes.fire({});
}

ChatRule RuleFor(PeerId peer) {
	const auto &chats = Current().chats;
	const auto i = chats.find(peer);
	return (i != end(chats)) ? i->second.rule : ChatRule::Default;
}

void SetRule(PeerId peer, ChatRule rule) {
	if (RuleFor(peer) == rule) {
		return;
	}
	Update([&](Settings &settings) {
		if (rule == ChatRule::Default) {
			settings.chats.remove(peer);
		} else {
			settings.chats[peer] = ChatEntry{
				.rule = rule,
				.since = base::unixtime::now(),
			};
		}
	});
}

Modes ModesFor(not_null<PeerData*> peer) {
	const auto &settings = Current();
	const auto i = settings.chats.find(peer->id);
	const auto rule = (i != end(settings.chats))
		? i->second.rule
		: ChatRule::Default;
	switch (rule) {
	case ChatRule::Off: return {};
	case ChatRule::Always: return {
		.send = (settings.send == SendMode::Off)
			? SendMode::Review
			: settings.send,
		.typing = true,
		.incoming = true,
		.incomingSince = settings.incoming
			? std::min(settings.incomingSince, i->second.since)
			: i->second.since,
		.suggest = settings.suggest,
		.ready = settings.suggest && settings.ready,
		.merge = settings.merge,
		.hold = settings.hold,
		.autoMerge = settings.hold && settings.autoMerge,
	};
	case ChatRule::Default: break;
	}
	if (!DefaultKind(peer)) {
		return {};
	}
	// Groups only by Always: their traffic is not aimed at Zach and would
	// spend the runner's caps.
	return {
		.send = settings.send,
		.typing = settings.typing,
		.incoming = settings.incoming && peer->isUser(),
		.incomingSince = settings.incomingSince,
		.suggest = settings.suggest,
		.ready = settings.suggest && settings.ready && peer->isUser(),
		.merge = settings.merge,
		.hold = settings.hold,
		.autoMerge = settings.hold && settings.autoMerge,
	};
}

} // namespace Safe::Calm
