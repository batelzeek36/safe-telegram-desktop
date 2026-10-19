/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_trust.h"

#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "safe/safe_trust_invites.h"
#include "safe/safe_trust_store.h"

#include <QtCore/QDateTime>

namespace Safe {
namespace {

namespace Rules = TrustRules;

[[nodiscard]] Status FromRules(Rules::Status status) {
	switch (status) {
	case Rules::Status::Trusted: return Status::Trusted;
	case Rules::Status::Allowed: return Status::Allowed;
	case Rules::Status::New: return Status::New;
	case Rules::Status::OffKnown: return Status::OffKnown;
	case Rules::Status::OffNew: return Status::OffNew;
	case Rules::Status::OffRequested: return Status::OffRequested;
	}
	return Status::OffNew;
}

[[nodiscard]] Tier FromRules(Rules::Tier tier) {
	switch (tier) {
	case Rules::Tier::Trusted: return Tier::Trusted;
	case Rules::Tier::Allowed: return Tier::Allowed;
	case Rules::Tier::New: return Tier::New;
	case Rules::Tier::Off: return Tier::Off;
	}
	return Tier::Off;
}

[[nodiscard]] Rules::PeerKind KindOf(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		// Not loaded yet: the bot flag is unknown, so it counts as a bot.
		if (user->loadedStatus() == PeerData::LoadedStatus::Not) {
			return Rules::PeerKind::Unknown;
		}
		return user->isBot() ? Rules::PeerKind::Bot : Rules::PeerKind::User;
	}
	return Rules::PeerKind::Chat;
}

[[nodiscard]] std::int64_t SelfOf(not_null<PeerData*> peer) {
	return DialogIdOf(peer->session().userPeerId());
}

// The broadcast channel whose discussion group `peer` is, or nullptr.
[[nodiscard]] ChannelData *DiscussionChannelOf(not_null<PeerData*> peer) {
	const auto group = peer->asMegagroup();
	if (!group) {
		return nullptr;
	}
	const auto linked = group->discussionLink();
	return (linked && linked->isBroadcast()) ? linked : nullptr;
}

// A discussion link learned after a tier was asked for changes that tier:
// tell everything that caches tiers.
void WatchDiscussionLinks(not_null<Main::Session*> session) {
	static auto watched = base::flat_set<not_null<Main::Session*>>();
	if (!watched.emplace(session).second) {
		return;
	}
	session->changes().peerUpdates(
		Data::PeerUpdate::Flag::DiscussionLink
	) | rpl::filter([](const Data::PeerUpdate &update) {
		return DiscussionChannelOf(update.peer) != nullptr;
	}) | rpl::on_next([](const Data::PeerUpdate &) {
		TrustStore::Instance().notifyChanged();
	}, session->lifetime());
	session->lifetime().add([=] {
		watched.remove(session);
	});
}

[[nodiscard]] std::string PeerWord(not_null<PeerData*> peer) {
	return peer->isUser() ? "user" : peer->isChat() ? "chat" : "channel";
}

[[nodiscard]] std::string KindWord(not_null<PeerData*> peer) {
	if (peer->isUser()) {
		return (KindOf(peer) == Rules::PeerKind::User) ? "person" : "bot";
	}
	return peer->isBroadcast() ? "channel" : "group";
}

} // namespace

Tier TierFromStatus(Status status) {
	switch (status) {
	case Status::Trusted: return Tier::Trusted;
	case Status::Allowed: return Tier::Allowed;
	case Status::New: return Tier::New;
	case Status::OffKnown:
	case Status::OffNew:
	case Status::OffRequested: return Tier::Off;
	}
	return Tier::Off;
}

int64 DialogIdOf(PeerId peer) {
	if (const auto user = peerToUser(peer)) {
		return int64(user.bare);
	} else if (const auto chat = peerToChat(peer)) {
		return -int64(chat.bare);
	} else if (const auto channel = peerToChannel(peer)) {
		return -int64(channel.bare);
	}
	return 0;
}

Status StatusOf(not_null<PeerData*> peer) {
	WatchDiscussionLinks(&peer->session());
	const auto &policy = TrustStore::Instance().policy();
	const auto id = DialogIdOf(peer->id);
	const auto self = SelfOf(peer);
	if (const auto channel = DiscussionChannelOf(peer)) {
		return FromRules(policy.discussionStatus(
			id,
			DialogIdOf(channel->id),
			self));
	}
	return FromRules(policy.statusOf(id, KindOf(peer), self));
}

Tier TierOf(not_null<PeerData*> peer) {
	return TierFromStatus(StatusOf(peer));
}

bool Ask(not_null<PeerData*> peer, const QString &reason) {
	const auto id = DialogIdOf(peer->id);
	if (!id || Rules::Policy::IsFixed(id, SelfOf(peer))) {
		return false;
	}
	auto request = Rules::Request();
	request.key = Rules::KeyForDialog(id);
	request.dialogId = id;
	request.peer = PeerWord(peer);
	request.kind = KindWord(peer);
	request.title = peer->name().toStdString();
	request.username = peer->username().toStdString();
	request.reason = reason.toStdString();
	request.firstSeen = QDateTime::currentMSecsSinceEpoch();
	return TrustStore::Instance().addRequest(std::move(request));
}

bool AskInvite(
		const QString &hash,
		const QString &title,
		const QString &reason) {
	const auto key = Rules::KeyForInvite(hash.toStdString());
	if (!key) {
		return false;
	}
	auto request = Rules::Request();
	request.key = *key;
	request.peer = "invite";
	request.kind = "chat";
	request.title = title.toStdString();
	request.reason = reason.toStdString();
	request.firstSeen = QDateTime::currentMSecsSinceEpoch();
	return TrustStore::Instance().addRequest(std::move(request));
}

rpl::producer<> Changes() {
	return TrustStore::Instance().changes();
}

std::optional<Tier> InviteTier(const QString &hash) {
	const auto tier = TrustStore::Instance().inviteTier(hash.toStdString());
	return tier ? std::make_optional(FromRules(*tier)) : std::nullopt;
}

bool InviteRequested(const QString &hash) {
	return TrustStore::Instance().invitePending(hash.toStdString());
}

void JoinedByInvite(const QString &hash, not_null<PeerData*> peer) {
	const auto tier = InviteTier(hash);
	if (!tier || !IsOpenTier(*tier) || peer->isUser()) {
		return;
	}
	auto join = Rules::InviteJoin();
	join.hash = hash.toStdString();
	join.dialogId = DialogIdOf(peer->id);
	join.peer = PeerWord(peer);
	join.title = peer->name().toStdString();
	join.time = QDateTime::currentMSecsSinceEpoch();
	TrustStore::Instance().addJoin(std::move(join));
}

} // namespace Safe
