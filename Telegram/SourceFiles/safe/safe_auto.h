/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/weak_ptr.h"
#include "safe/safe_auto_state.h"

class History;
class PeerData;

namespace Main {
class Session;
} // namespace Main

// Full-auto on the Mac (docs/FULL-AUTO.md, build step 4, controls slice):
// the runner on MAINGEAR acts; the app shows its state in the chat's bar and
// turns a hand-back into a macOS notification. One Manager for the app.
//
// Which chats: no chat ids are built in. A chat becomes known the first time
// the runner says full-auto is on there (asked when the chat opens, and for
// the newest chats in the list a little after start), and known chats are
// kept in the data folder (safe_auto.json, 0600) and asked about every 30 s,
// every 10 s while a bar shows them.
namespace Safe::Auto {

class Client;

struct Entry {
	int64 chat = 0;
	PeerId peerId;
	base::weak_ptr<Main::Session> session;
	std::optional<State> state;
	int64 lastAnswer = 0; // unix time of the last good answer
	crl::time lastAsked = 0;
	bool lastFailed = false;
	bool asking = false;
	bool known = false; // full-auto was on here at some point

	// Reply yourself: this hand-back stays aside for a while.
	QString heldKey;
	int64 heldUntil = 0;
	// Let full-auto handle it: "Answering <name> lightly" until he (or
	// full-auto as him) writes in the chat.
	int64 handlingSince = 0;
	QString handlingName;
	// The last hand-back a notification went out for.
	QString notifiedKey;

	std::vector<QPointer<QWidget>> bars;
};

class Manager final : public base::has_weak_ptr {
public:

	[[nodiscard]] static Manager &Instance();

	// Starts the look-around for this account's newest chats (once).
	void watch(not_null<Main::Session*> session);

	// A bar shows this chat (faster asks, and a hand-back for a chat he is
	// looking at needs no notification).
	void showing(not_null<PeerData*> peer, not_null<QWidget*> bar);
	void hidden(not_null<QWidget*> bar);

	[[nodiscard]] const Entry *entry(int64 chat) const;
	[[nodiscard]] rpl::producer<int64> updates() const;

	// Done gets false when the runner did not take the change.
	void change(
		not_null<PeerData*> peer,
		Change change,
		Fn<void(bool)> done);
	// Reply yourself: the hand-back steps aside for kHoldSeconds.
	void hold(int64 chat);

	[[nodiscard]] bool available() const;

private:
	Manager();
	~Manager();

	Entry &ensure(not_null<PeerData*> peer);
	void ask(Entry &entry);
	void answered(int64 chat, std::optional<State> state);
	void apply(Entry &entry, State state);
	void maybeNotify(Entry &entry);
	void tick();
	void lookAround(base::weak_ptr<Main::Session> weak);
	void askNextNew();
	void load();
	void save();
	[[nodiscard]] bool seenNow(const Entry &entry) const;

	std::unique_ptr<Client> _client;
	base::flat_map<int64, Entry> _entries;
	std::vector<std::pair<int64, base::weak_ptr<Main::Session>>> _toAsk;
	bool _askingNew = false;
	base::flat_set<uint64> _watched;
	rpl::event_stream<int64> _updates;
	base::Timer _timer;
	bool _loaded = false;

};

// He chose Reply yourself on the hand-back showing now (it steps aside for
// a while, or until full-auto hands back something else).
[[nodiscard]] bool HandbackHeld(const Entry &entry);

// "Answering Michelle lightly": the first name of whoever sent the message
// a hand-back is about, or empty.
[[nodiscard]] QString HandbackSender(
	not_null<PeerData*> peer,
	const Handback &handback);

// The newest message he (or full-auto as him) sent in this chat, as unix
// time, looking at the loaded messages only.
[[nodiscard]] int64 LastOutgoing(not_null<History*> history);

} // namespace Safe::Auto
