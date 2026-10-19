/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ChannelData;
class PeerData;
class UserData;

namespace Calls {
struct InviteRequest;
} // namespace Calls

namespace Data {
class GroupCall;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class Show;
} // namespace Ui

namespace Window {
class SessionController;
struct PeerByLinkInfo;
} // namespace Window

namespace Safe {

// The gates on ways into a chat that do not go through opening it from the
// chat list: joining, links, invite links, folder links and calls. Each
// Refuse* returns true when Safe Telegram refused the action; it has then
// shown the Ask to allow box (safe_ask.h) or a toast, and the hook returns.

// Joining a channel or a group (the join bar, a discussion group, a
// community, an Instant View or rich message "join"): refused unless the
// chat is Trusted or Allowed.
[[nodiscard]] bool RefuseJoin(not_null<ChannelData*> channel);

// A t.me link (username, phone, chat link, t.me/c/ id) resolved to `peer`:
// refused when the peer is Off (New people still open). The box names why:
// a join link "join", a bot's Mini App (app, startapp, startattach)
// "miniapp", a game share "game", a bot start (start, startgroup,
// startchannel) "bot", a video chat or livestream "call", else "link".
[[nodiscard]] bool RefuseLink(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer,
	const Window::PeerByLinkInfo &info);

// An invite link for a chat I am not in (chatInvite: Telegram names only a
// title). With a peeked channel behind it: refused unless that channel is
// Trusted or Allowed. Without one the chat is unknown: it goes through only
// when Claude approved this invite hash Trusted or Allowed
// (Safe::InviteTier); otherwise the box says "Already asked" while an
// AskInvite request waits, or offers Ask to allow for the hash.
[[nodiscard]] bool RefuseChatInvite(
	not_null<Window::SessionController*> controller,
	const QString &hash,
	const QString &title,
	bool channel,
	ChannelData *peeked);

// A join through invite `hash` succeeded: the chat it led to (the first chat
// in the join's updates) gets the tier Claude approved for the hash
// (Safe::JoinedByInvite ignores hashes that were not approved).
void BindInviteJoin(
	not_null<Main::Session*> session,
	const QString &hash,
	const MTPUpdates &updates);

// An invite link Telegram resolved to a chat (already a member, or a peek):
// refused unless the chat is Trusted or Allowed.
[[nodiscard]] bool RefuseInvitedChat(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> chat);

// A folder link (or a folder update) that would join `adding`: refused when
// any of them is not Trusted or Allowed, with a toast. The box behind it
// would list those chats by name and picture.
[[nodiscard]] bool RefuseFolderChats(
	not_null<Window::SessionController*> controller,
	const std::vector<not_null<PeerData*>> &adding);

// A one to one call I start: refused only when the person is Off (New
// people and Allowed chats can be called; their video is the media gate's).
[[nodiscard]] bool RefuseCall(not_null<UserData*> user);

// A video chat or livestream in a group or channel I start or join: refused
// only when the chat is Off.
[[nodiscard]] bool RefuseCall(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer);

// People I invite to a conference call (creating it, or inviting into a
// running one): refused unless every one of them is Trusted or Allowed.
[[nodiscard]] bool RefuseCallInvites(
	const std::vector<Calls::InviteRequest> &invites);

// A conference call link or invite, checked before its join box (which shows
// the people in it): refused unless every other loaded participant is
// Trusted or Allowed and the loaded list holds everyone in the call (people
// Safe Telegram cannot see fail closed). An empty call opens.
[[nodiscard]] bool RefuseConferenceCall(
	std::shared_ptr<Ui::Show> show,
	not_null<Data::GroupCall*> call);

} // namespace Safe
