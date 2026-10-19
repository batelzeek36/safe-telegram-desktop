/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_off.h"

#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_key.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_scheduled_section.h"
#include "info/info_content_widget.h"
#include "info/info_memento.h"
#include "logs.h"
#include "main/main_session.h"
#include "media/view/media_view_open_common.h"
#include "ui/layers/show.h"
#include "ui/toast/toast.h"
#include "window/section_memento.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Safe {
namespace {

[[nodiscard]] QString OffNoticeText() {
	return u"This chat is off in Safe Telegram."_q;
}

// Peers whose stories were kept out of the strip, per session, so they can
// be asked for again when a list change allows them.
using StoriesHidden = base::flat_map<
	not_null<Main::Session*>,
	base::flat_set<PeerId>>;

[[nodiscard]] StoriesHidden &HiddenStorySources() {
	static auto result = StoriesHidden();
	return result;
}

[[nodiscard]] std::shared_ptr<Ui::Show> ActiveWindowShow() {
	if (const auto window = Core::App().activeWindow()) {
		if (const auto controller = window->sessionController()) {
			return controller->uiShow();
		}
	}
	return nullptr;
}

void RefreshEntry(not_null<Dialogs::Entry*> entry) {
	// Adds the entry when it should be listed now, then drops it when not.
	entry->updateChatListSortPosition();
	entry->updateChatListExistence();
}

void RefreshPeer(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer) {
	auto &data = session->data();
	if (const auto history = data.historyLoaded(peer)) {
		RefreshEntry(history);
	}
	if (const auto sublist = data.savedMessages().sublistLoaded(peer)) {
		RefreshEntry(sublist);
	}
	if (const auto user = peer->asUser()) {
		if (user->isContact()) {
			// Data::Session rebuilds its contacts lists on this flag.
			session->changes().peerUpdated(
				user,
				Data::PeerUpdate::Flag::IsContact);
		}
	}
}

void RefreshStories(not_null<Main::Session*> session) {
	auto &data = session->data();
	auto &stories = data.stories();
	for (const auto list : {
			Data::StorySourcesList::NotHidden,
			Data::StorySourcesList::Hidden }) {
		const auto ids = stories.sources(list)
			| ranges::views::transform(&Data::StoriesSourceInfo::id)
			| ranges::to_vector;
		for (const auto &id : ids) {
			const auto peer = data.peer(id);
			if (IsOff(peer)) {
				HiddenStorySources()[session].emplace(id);
				stories.apply(peer, nullptr);
			}
		}
	}
	const auto i = HiddenStorySources().find(session);
	if (i == end(HiddenStorySources())) {
		return;
	}
	const auto hidden = base::take(i->second);
	for (const auto &id : hidden) {
		const auto peer = data.peer(id);
		if (IsOff(peer)) {
			i->second.emplace(id);
		} else {
			stories.requestPeerStories(peer);
		}
	}
}

void Refresh(not_null<Main::Session*> session) {
	auto &data = session->data();
	auto peers = std::vector<not_null<PeerData*>>();
	data.enumerateUsers([&](not_null<UserData*> user) {
		peers.push_back(user);
	});
	data.enumerateGroups([&](not_null<PeerData*> peer) {
		peers.push_back(peer);
	});
	data.enumerateBroadcasts([&](not_null<ChannelData*> channel) {
		peers.push_back(channel);
	});
	for (const auto &peer : peers) {
		RefreshPeer(session, peer);
	}
	if (const auto folder = data.folderLoaded(Data::Folder::kId)) {
		folder->updateChatListExistence();
	}
	RefreshStories(session);
	for (const auto &window : session->windows()) {
		const auto thread = window->activeChatCurrent().thread();
		if (thread && IsOffThread(thread)) {
			window->clearSectionStack();
		}
	}
}

} // namespace

bool RefuseOff(
		not_null<PeerData*> peer,
		std::shared_ptr<Ui::Show> show) {
	if (!IsOff(peer)) {
		return false;
	}
	if (!show) {
		show = ActiveWindowShow();
	}
	if (show) {
		show->showToast(OffNoticeText());
	} else {
		Ui::Toast::Show(OffNoticeText());
	}
	return true;
}

bool RefuseOff(
		not_null<PeerData*> peer,
		not_null<Window::SessionNavigation*> navigation) {
	return IsOff(peer) && RefuseOff(peer, navigation->uiShow());
}

bool RefuseOffPeerId(
		not_null<Window::SessionNavigation*> navigation,
		PeerId peerId) {
	return peerId
		&& RefuseOff(navigation->session().data().peer(peerId), navigation);
}

bool RefuseOffThread(
		not_null<Data::Thread*> thread,
		not_null<Window::SessionNavigation*> navigation) {
	if (const auto sublist = thread->asSublist()) {
		const auto other = sublist->sublistPeer();
		if (IsOffSublistPeer(other)) {
			return RefuseOff(other, navigation);
		}
	}
	return RefuseOff(thread->peer(), navigation);
}

bool RefuseOffSection(
		Window::SectionMemento *memento,
		not_null<Window::SessionNavigation*> navigation) {
	const auto refuse = [&](PeerData *peer) {
		return peer && RefuseOff(not_null(peer), navigation);
	};
	if (!memento) {
		return false;
	} else if (const auto info = dynamic_cast<Info::Memento*>(memento)) {
		if (!info->stackSize()) {
			return false;
		}
		const auto content = info->content();
		if (const auto topic = content->topic()) {
			return RefuseOffThread(topic, navigation);
		} else if (const auto sublist = content->sublist()) {
			return RefuseOffThread(sublist, navigation);
		}
		return refuse(content->peer())
			|| refuse(content->storiesPeer())
			|| refuse(content->musicPeer())
			|| refuse(content->giftsPeer())
			|| refuse(content->starrefPeer());
	} else if (const auto chat
			= dynamic_cast<HistoryView::ChatMemento*>(memento)) {
		const auto id = chat->id();
		if (const auto sublist = id.sublist) {
			return RefuseOffThread(sublist, navigation);
		}
		return RefuseOff(id.history->peer, navigation);
	} else if (const auto pinned
			= dynamic_cast<HistoryView::PinnedMemento*>(memento)) {
		return RefuseOffThread(pinned->getThread(), navigation);
	} else if (const auto scheduled
			= dynamic_cast<HistoryView::ScheduledMemento*>(memento)) {
		return RefuseOff(scheduled->getHistory()->peer, navigation);
	} else if (const auto log
			= dynamic_cast<AdminLog::SectionMemento*>(memento)) {
		return RefuseOff(log->getChannel(), navigation);
	}
	return false;
}

bool RefuseOffMedia(const Media::View::OpenRequest &request) {
	if (const auto story = request.story()) {
		return RefuseOff(story->peer());
	} else if (const auto item = request.item()) {
		return RefuseOff(item->history()->peer);
	} else if (const auto peer = request.peer()) {
		return RefuseOff(not_null(peer));
	}
	return false;
}

bool IsOffSublistPeer(not_null<PeerData*> peer) {
	return !peer->isSavedHiddenAuthor() && IsOff(peer);
}

bool IsOffThread(not_null<Data::Thread*> thread) {
	if (IsOff(thread->peer())) {
		return true;
	} else if (const auto sublist = thread->asSublist()) {
		return IsOffSublistPeer(sublist->sublistPeer());
	}
	return false;
}

bool IsOffEntry(not_null<Dialogs::Entry*> entry) {
	const auto thread = entry->asThread();
	return thread && IsOffThread(thread);
}

bool IsOffItem(not_null<const HistoryItem*> item) {
	return IsOff(item->history()->peer);
}

bool SkipOffNotification(
		not_null<HistoryItem*> item,
		UserData *reactionOrVoteSender) {
	return IsOffItem(item)
		|| (reactionOrVoteSender && IsOff(reactionOrVoteSender));
}

bool IgnoreCallFrom(UserData *user) {
	if (!user || !IsOff(user)) {
		return false;
	}
	LOG(("Safe: a call from an off chat was ignored."));
	return true;
}

bool KeepStoriesInStrip(not_null<PeerData*> peer) {
	if (!IsOff(peer)) {
		return true;
	}
	HiddenStorySources()[&peer->session()].emplace(peer->id);
	return false;
}

void DropOffItems(
		std::vector<not_null<HistoryItem*>> &items,
		HistoryItem *&inject) {
	if (inject && IsOffItem(inject)) {
		inject = nullptr;
	}
	items.erase(ranges::remove_if(items, [](not_null<HistoryItem*> item) {
		return IsOffItem(item);
	}), end(items));
}

std::vector<not_null<PeerData*>> WithoutOff(
		std::vector<not_null<PeerData*>> peers) {
	DropOffPeers(peers);
	return peers;
}

base::flat_set<not_null<History*>> WithoutOff(
		const base::flat_set<not_null<History*>> &histories) {
	auto result = base::flat_set<not_null<History*>>();
	for (const auto &history : histories) {
		if (!IsOff(history->peer)) {
			result.emplace(history);
		}
	}
	return result;
}

void KeepOff(
		base::flat_set<not_null<History*>> &chosen,
		const base::flat_set<not_null<History*>> &was) {
	for (const auto &history : was) {
		if (IsOff(history->peer)) {
			chosen.emplace(history);
		}
	}
}

void WatchOff(not_null<Main::Session*> session) {
	Changes() | rpl::on_next([=] {
		Refresh(session);
	}, session->lifetime());
	session->lifetime().add([=] {
		HiddenStorySources().remove(session);
	});
}

} // namespace Safe
