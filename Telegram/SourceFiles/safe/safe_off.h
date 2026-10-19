/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_trust.h"

class History;
class HistoryItem;
class UserData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace Data {
class Thread;
} // namespace Data

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Window {
class SectionMemento;
class SessionNavigation;
} // namespace Window

namespace Media::View {
struct OpenRequest;
} // namespace Media::View

namespace Safe {

// Off chats (Safe::IsOff) are hidden from every list and cannot be opened.
// The lists are kept clean where they are built (chat list membership,
// contacts, top / recent peers, search answers, stories strip), so the
// pickers and boxes that read them need no hooks of their own.

// The one refusal point: true when `peer` is Off. The request is dropped
// and the person sees why. `show` may be null (the active window's is used).
bool RefuseOff(
	not_null<PeerData*> peer,
	std::shared_ptr<Ui::Show> show = nullptr);

// The same, with the show of a window (built only when refusing).
bool RefuseOff(
	not_null<PeerData*> peer,
	not_null<Window::SessionNavigation*> navigation);

// Wrappers that find the chat and call RefuseOff. A zero id is never refused.
bool RefuseOffPeerId(
	not_null<Window::SessionNavigation*> navigation,
	PeerId peerId);
bool RefuseOffThread(
	not_null<Data::Thread*> thread,
	not_null<Window::SessionNavigation*> navigation);

// Any section: profile, shared media, stories, gifts, topic, replies,
// saved sublist, pinned, scheduled, admin log.
bool RefuseOffSection(
	Window::SectionMemento *memento,
	not_null<Window::SessionNavigation*> navigation);

// The media viewer: stories, message media and profile pictures.
bool RefuseOffMedia(const Media::View::OpenRequest &request);

// A topic, a chat, or a saved / channel-messages sublist whose other side is
// Off. The "hidden author" sublist of Saved Messages is never Off.
[[nodiscard]] bool IsOffThread(not_null<Data::Thread*> thread);
[[nodiscard]] bool IsOffEntry(not_null<Dialogs::Entry*> entry);
[[nodiscard]] bool IsOffSublistPeer(not_null<PeerData*> peer);
[[nodiscard]] bool IsOffItem(not_null<const HistoryItem*> item);

// Notifications: the chat is Off, or the reaction / poll vote came from an
// Off person.
[[nodiscard]] bool SkipOffNotification(
	not_null<HistoryItem*> item,
	UserData *reactionOrVoteSender);

// Incoming calls and conference invites from Off people: no ringing, no
// call window, the caller hears it ring out.
[[nodiscard]] bool IgnoreCallFrom(UserData *user);

// False for an Off peer (kept out of the stories strip, so never preloaded).
// Remembered, so the stories come back when the chat is allowed later.
[[nodiscard]] bool KeepStoriesInStrip(not_null<PeerData*> peer);

// The archive's server-side unread numbers count Off chats too, so they are
// never shown: the archive counts only the chats it has loaded.
[[nodiscard]] inline bool CloudUnreadCountsAllowed() {
	return false;
}

void DropOffItems(
	std::vector<not_null<HistoryItem*>> &items,
	HistoryItem *&inject);

[[nodiscard]] std::vector<not_null<PeerData*>> WithoutOff(
	std::vector<not_null<PeerData*>> peers);
[[nodiscard]] base::flat_set<not_null<History*>> WithoutOff(
	const base::flat_set<not_null<History*>> &histories);

// Adds back the Off chats of `was` that a picker could not show, so saving
// the picker does not drop them from a folder.
void KeepOff(
	base::flat_set<not_null<History*>> &chosen,
	const base::flat_set<not_null<History*>> &was);

template <typename Peers>
void DropOffPeers(Peers &peers) {
	peers.erase(ranges::remove_if(peers, [](const auto &peer) {
		return IsOff(peer);
	}), end(peers));
}

// Rows that carry a `user` (mention and inline bot suggestions).
template <typename Rows>
void DropOffUserRows(Rows &rows) {
	rows.erase(ranges::remove_if(rows, [](const auto &row) {
		return IsOff(row.user);
	}), end(rows));
}

// Re-checks every loaded chat, contact, saved sublist and story source of
// the session each time the lists change (Safe::Changes), so newly allowed
// chats appear and newly Off chats vanish, and closes an open Off chat.
void WatchOff(not_null<Main::Session*> session);

} // namespace Safe
