/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_gate.h"

#include "calls/group/calls_group_common.h"
#include "data/data_channel.h"
#include "data/data_group_call.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "safe/safe_ask.h"
#include "safe/safe_identity.h"
#include "safe/safe_trust.h"
#include "safe/safe_trust_invites.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"

namespace Safe {
namespace {

// User-visible text (C++ constants: cloud language packs cannot change them).
[[nodiscard]] QString FolderRefusedText() {
	return u"Folder links can only add chats that are allowed in "_q
		+ Safe::AppName()
		+ u"."_q;
}

[[nodiscard]] QString CallNotAllowedText() {
	return u"This call has people in it who are not allowed in "_q
		+ Safe::AppName()
		+ u"."_q;
}

[[nodiscard]] QString CallUncheckedText() {
	return u"This call has more people in it than "_q
		+ Safe::AppName()
		+ u" can check, so it stays closed."_q;
}

// The reason words safe_trust.h lists for Safe::Ask.
[[nodiscard]] QString JoinReason() {
	return u"join"_q;
}

[[nodiscard]] QString InviteReason() {
	return u"invite"_q;
}

[[nodiscard]] QString LinkReason() {
	return u"link"_q;
}

[[nodiscard]] QString BotReason() {
	return u"bot"_q;
}

[[nodiscard]] QString MiniAppReason() {
	return u"miniapp"_q;
}

[[nodiscard]] QString GameReason() {
	return u"game"_q;
}

[[nodiscard]] QString CallReason() {
	return u"call"_q;
}

[[nodiscard]] std::shared_ptr<Ui::Show> ShowOf(
		not_null<Window::SessionController*> controller) {
	return std::shared_ptr<Ui::Show>(controller->uiShow());
}

// What a t.me link to `peer` was going to do, in the words of Safe::Ask.
[[nodiscard]] QString ReasonOfLink(
		not_null<PeerData*> peer,
		const Window::PeerByLinkInfo &info) {
	using Type = Window::ResolveType;
	if (info.voicechatHash) {
		return CallReason();
	} else if (info.joinChannel) {
		return JoinReason();
	}
	const auto user = peer->asUser();
	if (!user || !user->isBot()) {
		return LinkReason();
	}
	switch (info.resolveType) {
	case Type::ShareGame:
		return GameReason();
	case Type::BotApp:
		return MiniAppReason();
	case Type::BotStart:
	case Type::AddToGroup:
	case Type::AddToChannel:
		return BotReason();
	case Type::Default:
	case Type::HashtagSearch:
	case Type::Mention:
	case Type::Boost:
	case Type::ChannelDirect:
	case Type::Profile:
		break;
	}
	if (!info.botAppName.isEmpty()
		|| info.attachBotMainOpen
		|| info.attachBotToggleCommand.has_value()) {
		return MiniAppReason();
	} else if (info.startAutoSubmit || !info.startToken.isEmpty()) {
		return BotReason();
	}
	return LinkReason();
}

} // namespace

bool RefuseJoin(not_null<ChannelData*> channel) {
	if (IsOpen(channel)) {
		return false;
	}
	ShowAskBox(channel, JoinReason());
	return true;
}

bool RefuseLink(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		const Window::PeerByLinkInfo &info) {
	if (!IsOff(peer)) {
		return false;
	}
	ShowAskBox(std::move(show), peer, ReasonOfLink(peer, info));
	return true;
}

bool RefuseChatInvite(
		not_null<Window::SessionController*> controller,
		const QString &hash,
		const QString &title,
		bool channel,
		ChannelData *peeked) {
	if (peeked) {
		if (IsOpen(peeked)) {
			return false;
		}
		ShowAskBox(controller, peeked, InviteReason());
		return true;
	}
	const auto approved = InviteTier(hash);
	if (approved && IsOpenTier(*approved)) {
		return false;
	}
	ShowAskInviteBox(ShowOf(controller), hash, title, channel, InviteReason());
	return true;
}

void BindInviteJoin(
		not_null<Main::Session*> session,
		const QString &hash,
		const MTPUpdates &updates) {
	const auto bind = [&](const MTPVector<MTPChat> &chats) {
		if (chats.v.isEmpty()) {
			return;
		}
		const auto peerId = chats.v[0].match([](const MTPDchat &data) {
			return peerFromChat(data.vid().v);
		}, [](const MTPDchannel &data) {
			return peerFromChannel(data.vid().v);
		}, [](const auto &) {
			return PeerId(0);
		});
		if (const auto peer = session->data().peerLoaded(peerId)) {
			JoinedByInvite(hash, peer);
		}
	};
	updates.match([&](const MTPDupdates &data) {
		bind(data.vchats());
	}, [&](const MTPDupdatesCombined &data) {
		bind(data.vchats());
	}, [](const auto &) {
	});
}

bool RefuseInvitedChat(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> chat) {
	if (IsOpen(chat)) {
		return false;
	}
	ShowAskBox(controller, chat, InviteReason());
	return true;
}

bool RefuseFolderChats(
		not_null<Window::SessionController*> controller,
		const std::vector<not_null<PeerData*>> &adding) {
	const auto closed = ranges::any_of(adding, [](not_null<PeerData*> peer) {
		return !IsOpen(peer);
	});
	if (!closed) {
		return false;
	}
	ShowRefusalToast(ShowOf(controller), FolderRefusedText());
	return true;
}

bool RefuseCall(not_null<UserData*> user) {
	if (!IsOff(user)) {
		return false;
	}
	ShowAskBox(user, CallReason());
	return true;
}

bool RefuseCall(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer) {
	if (!IsOff(peer)) {
		return false;
	}
	ShowAskBox(std::move(show), peer, CallReason());
	return true;
}

bool RefuseCallInvites(const std::vector<Calls::InviteRequest> &invites) {
	for (const auto &invite : invites) {
		if (!IsOpen(invite.user)) {
			ShowAskBox(invite.user, CallReason());
			return true;
		}
	}
	return false;
}

bool RefuseConferenceCall(
		std::shared_ptr<Ui::Show> show,
		not_null<Data::GroupCall*> call) {
	const auto &participants = call->participants();
	const auto closed = ranges::any_of(participants, [](const auto &p) {
		return !p.peer->isSelf() && !IsOpen(p.peer);
	});
	if (closed) {
		ShowRefusalToast(std::move(show), CallNotAllowedText());
		return true;
	} else if (call->fullCount() > int(participants.size())) {
		ShowRefusalToast(std::move(show), CallUncheckedText());
		return true;
	}
	return false;
}

} // namespace Safe
