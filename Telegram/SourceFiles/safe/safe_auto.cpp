/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_auto.h"

#include "base/call_delayed.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "safe/safe_auto_client.h"
#include "safe/safe_calm_files.h"
#include "window/notifications_manager.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Safe::Auto {
namespace {

const auto kFileName = u"safe_auto.json"_q;
constexpr auto kTickMs = crl::time(5000);
constexpr auto kShownAskMs = crl::time(10000);
constexpr auto kKnownAskMs = crl::time(30000);
constexpr auto kOtherAskMs = crl::time(5 * 60 * 1000);
constexpr auto kLookAroundDelayMs = crl::time(15000);
constexpr auto kLookAroundEveryMs = crl::time(6 * 60 * 60 * 1000);
constexpr auto kLookAroundChats = 25;
constexpr auto kHoldSeconds = int64(10 * 60);
constexpr auto kMaxKnown = 64;
constexpr auto kScanMessages = 200;

[[nodiscard]] QString HandbackKey(const Handback &handback) {
	return handback.kind
		+ '|'
		+ QString::number(handback.msg)
		+ '|'
		+ handback.reason;
}

[[nodiscard]] bool Askable(not_null<History*> history) {
	const auto peer = history->peer;
	if (const auto user = peer->asUser()) {
		return !user->isBot() && !user->isSelf() && !user->isServiceUser();
	}
	return peer->isChat() || peer->isMegagroup();
}

void Notify(not_null<PeerData*> peer, const Handback &handback) {
	if (!Core::App().settings().desktopNotify()) {
		return;
	}
	using namespace Window::Notifications;
	auto &manager = Core::App().notifications().manager();
	if (manager.type() != ManagerType::Native) {
		LOG(("Safe Auto: no native notifications for a hand-back."));
		return;
	}
	static_cast<NativeManager&>(manager).showSafeNotification({
		.peer = peer,
		.itemId = MsgId(handback.msg),
		.title = peer->name(),
		.subtitle = u"Handed back to you"_q,
		.message = handback.reason.isEmpty()
			? u"Full-auto left this one for you."_q
			: handback.reason,
		.options = {
			.hideMarkAsRead = true,
			.hideReplyButton = true,
		},
	});
}

} // namespace

Manager::Manager()
: _client(std::make_unique<Client>())
, _timer([=] { tick(); }) {
}

Manager::~Manager() = default;

Manager &Manager::Instance() {
	// Never destroyed: its timer and network calls stay valid until exit.
	static const auto result = new Manager();
	return *result;
}

bool Manager::available() const {
	return _client->available();
}

void Manager::watch(not_null<Main::Session*> session) {
	if (!available()) {
		return;
	}
	load();
	if (!_timer.isActive()) {
		_timer.callEach(kTickMs);
	}
	if (!_watched.emplace(session->uniqueId()).second) {
		return;
	}
	for (auto &[chat, entry] : _entries) {
		if (!entry.session && entry.known) {
			entry.session = session.get();
		}
	}
	const auto weak = base::make_weak(session.get());
	base::call_delayed(kLookAroundDelayMs, this, [=] {
		lookAround(weak);
	});
}

void Manager::lookAround(base::weak_ptr<Main::Session> weak) {
	const auto session = weak.get();
	if (!session) {
		return;
	}
	auto count = 0;
	for (const auto row : *session->data().chatsList()->indexed()) {
		const auto history = row->history();
		if (!history || !Askable(history)) {
			continue;
		}
		const auto chat = RunnerChatId(history->peer);
		if (chat && !_entries.contains(chat)) {
			_toAsk.emplace_back(chat, weak);
		}
		if (++count == kLookAroundChats) {
			break;
		}
	}
	askNextNew();
	base::call_delayed(kLookAroundEveryMs, this, [=] {
		lookAround(weak);
	});
}

void Manager::askNextNew() {
	while (!_askingNew && !_toAsk.empty()) {
		const auto [chat, weak] = _toAsk.front();
		_toAsk.erase(begin(_toAsk));
		const auto session = weak.get();
		if (!session || _entries.contains(chat)) {
			continue;
		}
		auto &entry = _entries[chat];
		entry.chat = chat;
		entry.peerId = PeerFromChatId(chat);
		entry.session = session;
		_askingNew = true;
		entry.asking = true;
		entry.lastAsked = crl::now();
		_client->fetch(chat, crl::guard(this, [=](std::optional<State> s) {
			_askingNew = false;
			answered(chat, std::move(s));
			askNextNew();
		}));
	}
}

Entry &Manager::ensure(not_null<PeerData*> peer) {
	const auto chat = RunnerChatId(peer);
	auto &entry = _entries[chat];
	if (!entry.chat) {
		entry.chat = chat;
		entry.peerId = peer->id;
	}
	if (!entry.session) {
		entry.session = &peer->session();
	}
	return entry;
}

void Manager::showing(not_null<PeerData*> peer, not_null<QWidget*> bar) {
	if (!available() || !RunnerChatId(peer)) {
		return;
	}
	load();
	hidden(bar);
	auto &entry = ensure(peer);
	entry.bars.push_back(bar.get());
	const auto age = crl::now() - entry.lastAsked;
	if (!entry.asking
		&& (!entry.lastAsked || age >= (entry.known ? kShownAskMs : 15000))) {
		ask(entry);
	}
}

void Manager::hidden(not_null<QWidget*> bar) {
	for (auto &[chat, entry] : _entries) {
		entry.bars.erase(
			ranges::remove_if(entry.bars, [&](const QPointer<QWidget> &w) {
				return !w || (w.data() == bar.get());
			}),
			end(entry.bars));
	}
}

const Entry *Manager::entry(int64 chat) const {
	const auto i = _entries.find(chat);
	return (i != end(_entries)) ? &i->second : nullptr;
}

rpl::producer<int64> Manager::updates() const {
	return _updates.events();
}

void Manager::ask(Entry &entry) {
	entry.asking = true;
	entry.lastAsked = crl::now();
	const auto chat = entry.chat;
	_client->fetch(chat, crl::guard(this, [=](std::optional<State> state) {
		answered(chat, std::move(state));
	}));
}

void Manager::answered(int64 chat, std::optional<State> state) {
	const auto i = _entries.find(chat);
	if (i == end(_entries)) {
		return;
	}
	auto &entry = i->second;
	entry.asking = false;
	if (!state) {
		const auto was = entry.lastFailed;
		entry.lastFailed = true;
		if (!was) {
			_updates.fire_copy(chat);
		}
		return;
	}
	apply(entry, std::move(*state));
}

void Manager::apply(Entry &entry, State state) {
	const auto changed = (entry.state != state) || entry.lastFailed;
	entry.lastFailed = false;
	entry.lastAnswer = base::unixtime::now();
	if (state.enabled && !entry.known) {
		entry.known = true;
		save();
	}
	if (!state.enabled) {
		entry.handlingSince = 0;
		entry.heldKey = QString();
	}
	if (state.handback) {
		entry.handlingSince = 0;
	}
	entry.state = std::move(state);
	maybeNotify(entry);
	if (changed) {
		_updates.fire_copy(entry.chat);
	}
}

bool Manager::seenNow(const Entry &entry) const {
	for (const auto &bar : entry.bars) {
		if (const auto widget = bar.data()) {
			if (widget->isVisible() && widget->window()->isActiveWindow()) {
				return true;
			}
		}
	}
	return false;
}

void Manager::maybeNotify(Entry &entry) {
	const auto &state = *entry.state;
	if (!state.enabled || !state.handback) {
		if (!entry.notifiedKey.isEmpty()) {
			entry.notifiedKey = QString();
			save();
		}
		return;
	}
	const auto key = HandbackKey(*state.handback);
	if (entry.notifiedKey == key) {
		return;
	}
	entry.notifiedKey = key;
	save();
	if (seenNow(entry)) {
		return;
	}
	const auto session = entry.session.get();
	if (!session) {
		return;
	}
	if (const auto peer = session->data().peerLoaded(entry.peerId)) {
		Notify(peer, *state.handback);
	}
}

void Manager::change(
		not_null<PeerData*> peer,
		Change change,
		Fn<void(bool)> done) {
	auto &entry = ensure(peer);
	const auto chat = entry.chat;
	const auto handback = (entry.state && entry.state->handback)
		? entry.state->handback
		: std::nullopt;
	const auto name = (change.resume && handback)
		? HandbackSender(peer, *handback)
		: QString();
	const auto resume = change.resume;
	_client->change(chat, std::move(change), crl::guard(this, [=](
			std::optional<State> state) {
		const auto i = _entries.find(chat);
		if (!state || i == end(_entries)) {
			done(false);
			return;
		}
		auto &entry = i->second;
		if (resume && !state->handback) {
			entry.handlingSince = base::unixtime::now();
			entry.handlingName = name;
		}
		entry.heldKey = QString();
		apply(entry, std::move(*state));
		_updates.fire_copy(chat);
		done(true);
	}));
}

void Manager::hold(int64 chat) {
	const auto i = _entries.find(chat);
	if (i == end(_entries)
		|| !i->second.state
		|| !i->second.state->handback) {
		return;
	}
	i->second.heldKey = HandbackKey(*i->second.state->handback);
	i->second.heldUntil = base::unixtime::now() + kHoldSeconds;
	_updates.fire_copy(chat);
}

void Manager::tick() {
	const auto now = crl::now();
	const auto unix = int64(base::unixtime::now());
	auto released = std::vector<int64>();
	for (auto &[chat, entry] : _entries) {
		if (!entry.heldKey.isEmpty() && entry.heldUntil <= unix) {
			entry.heldKey = QString();
			released.push_back(chat);
		}
		if (entry.asking || !entry.session) {
			continue;
		}
		const auto shown = ranges::any_of(entry.bars, [](const auto &w) {
			return w && w->isVisible();
		});
		const auto every = shown
			? (entry.known ? kShownAskMs : kOtherAskMs)
			: (entry.known ? kKnownAskMs : 0);
		if (every && now - entry.lastAsked >= every) {
			ask(entry);
		}
	}
	for (const auto chat : released) {
		_updates.fire_copy(chat);
	}
}

void Manager::load() {
	if (_loaded) {
		return;
	}
	_loaded = true;
	const auto bytes = Calm::ReadPrivateFile(kFileName);
	if (bytes.isEmpty()) {
		return;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Safe Auto: %1 did not parse, starting empty.").arg(kFileName));
		return;
	}
	const auto chats = document.object().value(u"chats"_q).toArray();
	for (const auto &value : chats) {
		const auto object = value.toObject();
		const auto chat = object.value(u"chat"_q).toString().toLongLong();
		if (!chat || !PeerFromChatId(chat) || _entries.size() >= kMaxKnown) {
			continue;
		}
		auto &entry = _entries[chat];
		entry.chat = chat;
		entry.peerId = PeerFromChatId(chat);
		entry.known = true;
		entry.notifiedKey = object.value(u"notified"_q).toString();
	}
}

void Manager::save() {
	auto chats = QJsonArray();
	for (const auto &[chat, entry] : _entries) {
		if (!entry.known) {
			continue;
		}
		auto object = QJsonObject();
		object.insert(u"chat"_q, QString::number(chat));
		if (!entry.notifiedKey.isEmpty()) {
			object.insert(u"notified"_q, entry.notifiedKey);
		}
		chats.append(object);
	}
	auto root = QJsonObject();
	root.insert(u"chats"_q, chats);
	Calm::WritePrivateFile(
		kFileName,
		QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool HandbackHeld(const Entry &entry) {
	return !entry.heldKey.isEmpty()
		&& entry.state
		&& entry.state->handback
		&& (entry.heldKey == HandbackKey(*entry.state->handback));
}

QString HandbackSender(
		not_null<PeerData*> peer,
		const Handback &handback) {
	if (handback.msg <= 0) {
		return QString();
	}
	const auto item = peer->owner().message(peer, MsgId(handback.msg));
	if (!item || item->out()) {
		return QString();
	}
	const auto from = item->from();
	const auto name = from->isUser() ? from->shortName() : from->name();
	return name.trimmed();
}

int64 LastOutgoing(not_null<History*> history) {
	auto scanned = 0;
	for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			const auto item = (*m)->data();
			if (item->out() && !item->isService()) {
				return int64(item->date());
			}
			if (++scanned == kScanMessages) {
				return 0;
			}
		}
	}
	return 0;
}

} // namespace Safe::Auto
