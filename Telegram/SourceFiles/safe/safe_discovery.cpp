/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_discovery.h"

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

[[nodiscard]] uint64 ChatIdOf(const MTPChat &chat) {
	return chat.match([](const auto &data) {
		return uint64(data.vid().v);
	});
}

} // namespace

MTPcontacts_Found KeepOwnResults(const MTPcontacts_Found &found) {
	const auto &data = found.data();
	if (DiscoveryAllowed()) {
		return found;
	}
	auto chatsIn = base::flat_set<uint64>();
	for (const auto &chat : data.vchats().v) {
		if (AmInChat(chat)) {
			chatsIn.emplace(ChatIdOf(chat));
		}
	}
	auto keepUsers = base::flat_set<uint64>();
	auto keepChats = base::flat_set<uint64>();
	auto mine = QVector<MTPPeer>();
	for (const auto &peer : data.vmy_results().v) {
		peer.match([&](const MTPDpeerUser &data) {
			keepUsers.emplace(uint64(data.vuser_id().v));
			mine.push_back(peer);
		}, [&](const MTPDpeerChat &data) {
			const auto id = uint64(data.vchat_id().v);
			if (chatsIn.contains(id)) {
				keepChats.emplace(id);
				mine.push_back(peer);
			}
		}, [&](const MTPDpeerChannel &data) {
			const auto id = uint64(data.vchannel_id().v);
			if (chatsIn.contains(id)) {
				keepChats.emplace(id);
				mine.push_back(peer);
			}
		});
	}
	auto chats = QVector<MTPChat>();
	for (const auto &chat : data.vchats().v) {
		if (keepChats.contains(ChatIdOf(chat))) {
			chats.push_back(chat);
		}
	}
	auto users = QVector<MTPUser>();
	for (const auto &user : data.vusers().v) {
		const auto id = user.match([](const auto &data) {
			return uint64(data.vid().v);
		});
		if (keepUsers.contains(id)) {
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
