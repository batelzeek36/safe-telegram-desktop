/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles_model.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_community.h"
#include "data/data_document.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_unread_value.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_item_preview.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "safe/safe_circles_core.h"
#include "ui/empty_userpic.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"

#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "styles/style_safe_circles.h"

#include <QtCore/QUrl>

namespace Safe::Circles {
namespace {

// Previews are one line in the dial; a long message is cut well before it
// could matter for layout.
constexpr auto kPreviewLimit = 200;

[[nodiscard]] not_null<Dialogs::MainList*> ListOf(
		not_null<Main::Session*> session,
		FilterId id) {
	return id
		? session->data().chatsFilters().chatsList(id)
		: session->data().chatsList();
}

[[nodiscard]] bool KeepEntity(EntityType type) {
	switch (type) {
	case EntityType::Colorized:
	case EntityType::Bold:
	case EntityType::Semibold:
	case EntityType::Italic:
		return true;
	default:
		return false;
	}
}

// A link shows its site ("youtube.com"), not "https://yout..." cut short in
// a small hub. Kept entities after a link move with the text; one that runs
// across a link is dropped.
void ShortenLinks(TextWithEntities &text) {
	static const auto expression = QRegularExpression(
		u"https?://\\S+"_q,
		QRegularExpression::CaseInsensitiveOption);
	auto links = std::vector<std::pair<int, int>>();
	auto i = expression.globalMatch(text.text);
	while (i.hasNext()) {
		const auto match = i.next();
		links.emplace_back(match.capturedStart(), match.capturedLength());
	}
	for (auto j = links.rbegin(); j != links.rend(); ++j) {
		const auto [from, length] = *j;
		auto host = QUrl(text.text.mid(from, length)).host();
		if (host.startsWith(u"www."_q)) {
			host = host.mid(4);
		}
		if (host.isEmpty()) {
			continue;
		}
		text.text.replace(from, length, host);
		const auto shrink = length - int(host.size());
		auto kept = EntitiesInText();
		for (auto &entity : text.entities) {
			if (entity.offset() >= from + length) {
				entity.shiftLeft(shrink);
				kept.push_back(std::move(entity));
			} else if (entity.offset() + entity.length() <= from) {
				kept.push_back(std::move(entity));
			}
		}
		text.entities = std::move(kept);
	}
}

// Spoilers become dots, custom emoji keep only their fallback emoji, links
// show their site and every other entity is dropped: the dial shows plain
// words.
[[nodiscard]] TextWithEntities Plain(TextWithEntities text) {
	for (const auto &entity : text.entities) {
		if (entity.type() == EntityType::Spoiler) {
			const auto from = std::clamp(entity.offset(), 0, int(text.text.size()));
			const auto till = std::clamp(
				entity.offset() + entity.length(),
				from,
				int(text.text.size()));
			for (auto i = from; i != till; ++i) {
				if (!text.text[i].isSpace()) {
					text.text[i] = QChar(0x2022);
				}
			}
		}
	}
	auto kept = EntitiesInText();
	for (auto &entity : text.entities) {
		if (KeepEntity(entity.type())) {
			kept.push_back(std::move(entity));
		}
	}
	text.entities = std::move(kept);
	ShortenLinks(text);
	text.text.replace(QChar('\n'), QChar(' '));
	if (text.text.size() > kPreviewLimit) {
		text.text = text.text.mid(0, kPreviewLimit);
		auto inside = EntitiesInText();
		for (auto &entity : text.entities) {
			if (entity.offset() + entity.length() <= kPreviewLimit) {
				inside.push_back(std::move(entity));
			}
		}
		text.entities = std::move(inside);
	}
	return text;
}

// The text without its direction marks and spaces at either end (a preview
// comes wrapped in them, st::wrap_rtl), for comparing what shows.
[[nodiscard]] QString Visible(const QString &text) {
	const auto mark = [](QChar c) {
		const auto u = c.unicode();
		return c.isSpace()
			|| (u == 0x200E) || (u == 0x200F) || (u == 0x061C)
			|| (u >= 0x202A && u <= 0x202E)
			|| (u >= 0x2066 && u <= 0x2069);
	};
	auto from = 0;
	auto till = int(text.size());
	while (from < till && mark(text[from])) {
		++from;
	}
	while (till > from && mark(text[till - 1])) {
		--till;
	}
	return text.mid(from, till - from);
}

// A topic's name as the chat list's topics line shows it: its icon (the
// emoji the custom icon stands for, once known), or "#" for General.
[[nodiscard]] QString TopicTitle(not_null<Data::ForumTopic*> topic) {
	const auto title = topic->title();
	if (topic->isGeneral()) {
		return u"# "_q + title;
	} else if (const auto id = topic->iconId()) {
		const auto sticker = topic->owner().document(id)->sticker();
		if (sticker && !sticker->alt.isEmpty() && !title.startsWith(sticker->alt)) {
			return sticker->alt + QChar(' ') + title;
		}
	}
	return title;
}

// A forum's line, as its row in the chat list reads it: the newest topic (its
// last message's) and that message, "🌊 her pulse: see you at the gate".
[[nodiscard]] TextWithEntities ForumLine(
		not_null<Data::Forum*> forum,
		HistoryItem *item) {
	auto topic = item ? item->topic() : nullptr;
	if (!topic && !forum->recentTopics().empty()) {
		topic = forum->recentTopics().front();
	}
	if (topic && (!item || item->topic() != topic)) {
		topic->chatListPreloadData();
		item = topic->chatListMessage();
	}
	auto message = item
		? Plain(item->toPreview({
			.hideSender = true,
			.generateImages = false,
		}).text)
		: TextWithEntities();
	const auto title = topic ? TopicTitle(topic) : QString();
	const auto opening = Visible(message.text);
	const auto opens = [&](const QString &name) {
		return !name.isEmpty() && opening.startsWith(Visible(name));
	};
	if (title.isEmpty() || opens(title) || opens(topic->title())) {
		// A message that opens with its topic's name says where it is
		// ("🌊 her pulse · Oct 27…", a report posted in "🌊 her pulse").
		return message;
	}
	auto result = Plain(Ui::Text::Colorized(title + QChar(':')));
	if (!message.text.isEmpty()) {
		result.append(' ').append(std::move(message));
	}
	return result;
}

// A community's line, its row's in the chat list: the names of its chats with
// the newest messages ("🌊 her pulse, 💗 her leads and 2 more chats").
[[nodiscard]] TextWithEntities CommunityLine(
		not_null<Data::CommunityInfo*> info) {
	if (info->lastHistories().empty()) {
		return {};
	}
	info->validateListEntryCache();
	return Plain(info->listEntryCache().toTextWithEntities());
}

} // namespace

Model::Model(not_null<Main::Session*> session)
: _session(session) {
	setupSubscriptions();
}

Model::~Model() = default;

not_null<Main::Session*> Model::session() const {
	return _session;
}

void Model::setupSubscriptions() {
	_session->data().chatsFilters().changed(
	) | rpl::on_next([=] {
		_circlesDirty = true;
		_titles.clear();
		for (const auto id : _circles) {
			_chatsDirty.emplace(id);
		}
		_circlesChanges.fire({});
		markOrderDirty();
	}, _lifetime);

	rpl::merge(
		_session->data().chatsListChanges() | rpl::to_empty,
		_session->data().chatsListLoadedEvents() | rpl::to_empty,
		_session->data().chatListEntryRefreshes(
		) | rpl::filter([](const Data::Session::ChatListEntryRefresh &e) {
			return bool(e);
		}) | rpl::to_empty
	) | rpl::on_next([=] {
		markOrderDirty();
	}, _lifetime);

	_session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		_repaints.fire({});
	}, _lifetime);

	using PeerFlag = Data::PeerUpdate::Flag;
	_session->changes().peerUpdates(
		PeerFlag::Name | PeerFlag::Photo
	) | rpl::on_next([=](const Data::PeerUpdate &update) {
		if (update.flags & PeerFlag::Photo) {
			_faces.remove(update.peer);
		}
		_repaints.fire({});
	}, _lifetime);

	rpl::merge(
		_session->changes().entryUpdates(
			Data::EntryUpdate::Flag::Repaint) | rpl::to_empty,
		_session->changes().messageUpdates(
			Data::MessageUpdate::Flag::DialogRowRefresh) | rpl::to_empty,
		_session->changes().historyUpdates(
			Data::HistoryUpdate::Flag::UnreadView
			| Data::HistoryUpdate::Flag::UnreadMentions
			| Data::HistoryUpdate::Flag::UnreadReactions
			| Data::HistoryUpdate::Flag::IsPinned) | rpl::to_empty
	) | rpl::on_next([=] {
		++_generation;
		_repaints.fire({});
	}, _lifetime);
}

void Model::markOrderDirty() {
	for (const auto &[id, list] : _chats) {
		_chatsDirty.emplace(id);
	}
	if (_orderPosted) {
		return;
	}
	_orderPosted = true;
	// Telegram moves a chat in several steps for one message; one rebuild.
	Ui::PostponeCall(crl::guard(this, [=] {
		_orderPosted = false;
		_orderChanges.fire({});
	}));
}

const std::vector<FilterId> &Model::circles() {
	if (_circlesDirty) {
		refreshCircles();
	}
	return _circles;
}

void Model::refreshCircles() {
	_circlesDirty = false;
	auto ids = std::vector<std::int32_t>();
	for (const auto &filter : _session->data().chatsFilters().list()) {
		ids.push_back(filter.id());
	}
	_circles.clear();
	for (const auto id : CircleIds(ids)) {
		_circles.push_back(FilterId(id));
	}

	_unreadLifetime.destroy();
	_unread.clear();
	for (const auto id : _circles) {
		rpl::combine(
			Data::UnreadStateValue(_session, id),
			Data::IncludeMutedCounterFoldersValue()
		) | rpl::on_next([=](
				const Dialogs::UnreadState &state,
				bool includeMuted) {
			const auto muted = state.chatsMuted + state.marksMuted;
			const auto count = (state.chats + state.marks)
				- (includeMuted ? 0 : muted);
			_unread[id] = CircleUnread{
				.count = std::max(count, 0),
				.muted = includeMuted && (count == muted),
			};
			_repaints.fire({});
		}, _unreadLifetime);
	}
}

const Ui::Text::String &Model::circleTitle(FilterId id) {
	auto i = _titles.find(id);
	if (i == end(_titles)) {
		auto text = QString();
		if (!id) {
			text = tr::lng_filters_all_short(tr::now);
		} else {
			for (const auto &filter : _session->data().chatsFilters().list()) {
				if (filter.id() == id) {
					text = filter.title().text.text;
					break;
				}
			}
		}
		auto title = Ui::Text::String();
		title.setText(st::chatUniqueTitle, text);
		i = _titles.emplace(id, std::move(title)).first;
	}
	return i->second;
}

CircleUnread Model::circleUnread(FilterId id) const {
	const auto i = _unread.find(id);
	return (i == end(_unread)) ? CircleUnread() : i->second;
}

const std::vector<not_null<History*>> &Model::chats(FilterId id) {
	const auto i = _chats.find(id);
	if (i == end(_chats) || _chatsDirty.contains(id)) {
		refreshChats(id);
	}
	return _chats[id];
}

void Model::refreshChats(FilterId id) {
	_chatsDirty.remove(id);
	auto &list = _chats[id];
	list.clear();
	const auto main = ListOf(_session, id);
	for (const auto &row : main->indexed()->all()) {
		if (const auto history = row->key().history()) {
			list.push_back(history);
		}
	}
}

bool Model::fullyLoaded(FilterId) const {
	// A folder's chats come from the main list (its own list never says
	// loaded): once every chat is known, every folder is complete.
	return _session->data().chatsList()->loaded();
}

void Model::requestMore(FilterId id) {
	if (!_session->data().chatsList()->loaded()) {
		_session->api().requestDialogs(nullptr);
	}
	if (id) {
		_session->data().chatsFilters().loadNextExceptions(
			_session->data().chatsList()->loaded());
	}
}

rpl::producer<> Model::circlesChanges() const {
	return _circlesChanges.events();
}

rpl::producer<> Model::orderChanges() const {
	return _orderChanges.events();
}

rpl::producer<> Model::repaints() const {
	return _repaints.events();
}

// One image a chat, at the largest size asked for so far: a face and the
// Orbit card ask for different sizes every frame, and a smaller draw scales
// the larger image instead of making a new one each time.
const QImage &Model::face(not_null<History*> history, int size) {
	const auto peer = history->peer;
	auto &face = _faces[peer];
	const auto special = peer->isSelf() || peer->isRepliesChat();
	if (special) {
		if (!face.special || face.size < size || face.image.isNull()) {
			face.special = true;
			face.size = size;
			face.image = peer->isSelf()
				? Ui::EmptyUserpic::GenerateSavedMessages(size)
				: Ui::EmptyUserpic::GenerateRepliesMessages(size);
		}
		return face.image;
	}
	peer->loadUserpic();
	const auto key = peer->userpicUniqueKey(face.view);
	if (face.image.isNull() || face.key != key || face.size < size) {
		face.key = key;
		face.size = size;
		face.image = PeerData::GenerateUserpicImage(peer, face.view, size);
	}
	return face.image;
}

const Ui::Text::String &Model::name(not_null<History*> history) {
	auto &entry = _names[history];
	const auto version = history->chatListNameVersion();
	if (entry.version != version) {
		entry.version = version;
		const auto peer = history->peer;
		const auto text = peer->isSelf()
			? tr::lng_saved_messages(tr::now)
			: peer->isRepliesChat()
			? tr::lng_replies_messages(tr::now)
			: history->chatListName();
		entry.text.setText(st::circlesCardName, text);
	}
	return entry.text;
}

const Ui::Text::String &Model::shortName(not_null<History*> history) {
	auto &entry = _shortNames[history];
	const auto version = history->chatListNameVersion();
	if (entry.version != version) {
		entry.version = version;
		const auto peer = history->peer;
		const auto text = peer->isSelf()
			? tr::lng_saved_messages(tr::now)
			: peer->isRepliesChat()
			? tr::lng_replies_messages(tr::now)
			: peer->shortName();
		entry.text.setText(st::chatSimilarName, text);
	}
	return entry.text;
}

TextWithEntities Model::previewText(not_null<History*> history) const {
	const auto item = history->chatListMessage();
	const auto channel = history->peer->asChannel();
	if (const auto forum = history->peer->forum()) {
		return ForumLine(forum, item);
	} else if (const auto info = (channel && channel->isCommunity())
			? channel->communityInfo()
			: nullptr) {
		return CommunityLine(info);
	} else if (!item) {
		return {};
	}
	return Plain(item->toPreview({
		.generateImages = false,
		.ignoreGroup = false,
	}).text);
}

Model::Preview &Model::previewEntry(not_null<History*> history) {
	auto &entry = _previews[history];
	// As the list's row does before it reads the line: a chat whose last
	// message is not known yet (a forum the list never painted) asks for it.
	history->chatListPreloadData();
	const auto item = history->chatListMessage();
	if (entry.item != item || entry.generation != _generation) {
		entry.item = item;
		entry.generation = _generation;
		entry.marked = previewText(history);
		entry.text.setMarkedText(
			st::circlesCardText,
			entry.marked,
			kMarkupTextOptions);
	}
	return entry;
}

const Ui::Text::String &Model::preview(not_null<History*> history) {
	return previewEntry(history).text;
}

const TextWithEntities &Model::previewMarked(not_null<History*> history) {
	return previewEntry(history).marked;
}

QString Model::time(not_null<History*> history) const {
	const auto id = history->chatListTimeId();
	return id ? Ui::FormatDialogsDate(base::unixtime::parse(id)) : QString();
}

Dialogs::BadgesState Model::badges(not_null<History*> history) const {
	return history->chatListBadgesState();
}

bool Model::pinned(not_null<History*> history, FilterId id) const {
	return history->isPinnedDialog(id);
}

QString Model::accessibleName(not_null<History*> history) {
	auto result = name(history).toString();
	const auto state = badges(history);
	if (state.unreadCounter > 0) {
		result += u", %1 unread"_q.arg(state.unreadCounter);
	} else if (state.unread) {
		result += u", unread"_q;
	}
	const auto line = preview(history).toString();
	if (!line.isEmpty()) {
		result += u", "_q + line;
	}
	return result;
}

} // namespace Safe::Circles
