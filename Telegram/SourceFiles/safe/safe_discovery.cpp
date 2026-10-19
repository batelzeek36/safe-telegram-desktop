/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_discovery.h"

#include "data/data_peer_id.h"

namespace Safe {
namespace {

[[nodiscard]] bool AmInChat(const MTPChat &chat) {
	return chat.match([](const MTPDchat &data) {
		return !data.is_left() && !data.is_deactivated();
	}, [](const MTPDchannel &data) {
		return !data.is_left() && !data.is_min();
	}, [](const auto &) {
		return false;
	});
}

// Basic groups and channels number their ids separately, so they are
// compared as PeerId values (which carry the type).
[[nodiscard]] PeerId PeerIdOf(const MTPChat &chat) {
	return chat.match([](const MTPDchannel &data) {
		return peerFromChannel(ChannelId(data.vid()));
	}, [](const MTPDchannelForbidden &data) {
		return peerFromChannel(ChannelId(data.vid()));
	}, [](const auto &data) {
		return peerFromChat(ChatId(data.vid()));
	});
}

[[nodiscard]] PeerId PeerIdOf(const MTPUser &user) {
	return user.match([](const auto &data) {
		return peerFromUser(UserId(data.vid()));
	});
}

} // namespace

MTPcontacts_Found KeepOwnResults(const MTPcontacts_Found &found) {
	if (DiscoveryAllowed()) {
		return found;
	}
	const auto &data = found.data();
	auto chatsIn = base::flat_set<PeerId>();
	for (const auto &chat : data.vchats().v) {
		if (AmInChat(chat)) {
			chatsIn.emplace(PeerIdOf(chat));
		}
	}
	auto keep = base::flat_set<PeerId>();
	auto mine = QVector<MTPPeer>();
	for (const auto &peer : data.vmy_results().v) {
		const auto id = peerFromMTP(peer);
		if (peerIsUser(id) || chatsIn.contains(id)) {
			keep.emplace(id);
			mine.push_back(peer);
		}
	}
	auto chats = QVector<MTPChat>();
	for (const auto &chat : data.vchats().v) {
		if (keep.contains(PeerIdOf(chat))) {
			chats.push_back(chat);
		}
	}
	auto users = QVector<MTPUser>();
	for (const auto &user : data.vusers().v) {
		if (keep.contains(PeerIdOf(user))) {
			users.push_back(user);
		}
	}
	return MTP_contacts_found(
		MTP_vector<MTPPeer>(std::move(mine)),
		MTP_vector<MTPPeer>(),
		MTP_vector<MTPChat>(std::move(chats)),
		MTP_vector<MTPUser>(std::move(users)));
}

} // namespace Safe
