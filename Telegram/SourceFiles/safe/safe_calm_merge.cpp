/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_merge.h"

#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "data/data_drafts.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "safe/safe_calm.h"
#include "safe/safe_calm_queue.h"
#include "safe/safe_calm_run.h"
#include "safe/safe_calm_run_mark.h"
#include "ui/item_text_options.h"

namespace Safe::Calm {
namespace {

struct Pending {
	base::weak_ptr<Main::Session> session;
	SentMerge merge;
	MergeHooks hooks;
	std::unique_ptr<base::Timer> timer;
	bool working = false; // the hold is over, the requests are on their way
};

[[nodiscard]] base::flat_map<PeerId, Pending> &Merges() {
	static auto result = base::flat_map<PeerId, Pending>();
	return result;
}

[[nodiscard]] std::optional<Pending> Take(PeerId peer) {
	auto &merges = Merges();
	const auto i = merges.find(peer);
	if (i == end(merges)) {
		return std::nullopt;
	}
	auto result = std::move(i->second);
	merges.erase(i);
	return result;
}

void Finish(PeerId peer, MergeHooks hooks) {
	Merges().remove(peer);
	SetRunMarkHold(peer, 0);
	if (hooks.finished) {
		hooks.finished();
	}
}

void Abort(PeerId peer, Pending pending, const QString &why) {
	LOG(("Safe Calm: one message not made: %1").arg(why));
	if (pending.hooks.toast) {
		pending.hooks.toast(why);
	}
	if (pending.merge.fromField && pending.hooks.restore) {
		pending.hooks.restore(pending.merge.text);
	}
	Finish(peer, std::move(pending.hooks));
}

[[nodiscard]] TextWithEntities Prepared(
		not_null<History*> history,
		const TextWithTags &text) {
	auto result = TextWithEntities{
		text.text,
		TextUtilities::ConvertTextTagsToEntities(text.tags),
	};
	TextUtilities::PrepareForSending(
		result,
		Ui::ItemTextOptions(history, history->session().user()).flags);
	return result;
}

void Remove(
		not_null<Main::Session*> session,
		const std::vector<not_null<HistoryItem*>> &items) {
	if (items.empty()) {
		return;
	}
	auto ids = MessageIdsList();
	ids.reserve(items.size());
	for (const auto &item : items) {
		ids.push_back(item->fullId());
	}
	session->data().histories().deleteMessages(ids, true);
	session->data().sendHistoryChangeNotifications();
}

// The first text could not be edited: the texts go, the one message is sent
// after the run.
void SendInstead(PeerId peer, Pending pending) {
	const auto session = pending.session.get();
	const auto history = session ? session->data().historyLoaded(peer) : nullptr;
	if (!history) {
		Finish(peer, std::move(pending.hooks));
		return;
	}
	auto texts = std::vector<not_null<HistoryItem*>>();
	for (const auto &id : pending.merge.texts) {
		if (const auto item = session->data().message(id)) {
			if (!CanRemoveForAll(item)) {
				Abort(
					peer,
					std::move(pending),
					u"Telegram won't remove these for everyone, so nothing "
					"changed."_q);
				return;
			}
			texts.push_back(item);
		}
	}
	Remove(session, texts);
	RememberFine(peer, pending.merge.text.text);
	auto message = Api::MessageToSend(Api::SendAction(history));
	message.textWithTags = pending.merge.text;
	session->api().sendMessage(std::move(message));
	Finish(peer, std::move(pending.hooks));
}

void Execute(PeerId peer) {
	auto &merges = Merges();
	const auto i = merges.find(peer);
	if (i == end(merges) || i->second.working) {
		return;
	}
	i->second.working = true;
	i->second.timer = nullptr;
	SetRunMarkHold(peer, 0);
	const auto session = i->second.session.get();
	if (!session) {
		Finish(peer, std::move(i->second.hooks));
		return;
	}
	const auto &merge = i->second.merge;
	auto texts = std::vector<not_null<HistoryItem*>>();
	auto read = false;
	for (const auto &id : merge.items) {
		if (const auto item = session->data().message(id)) {
			if (!RunUnread(item)) {
				read = true;
			}
			if (ranges::contains(merge.texts, id)) {
				texts.push_back(item);
			}
		}
	}
	if (read) {
		Abort(
			peer,
			*Take(peer),
			u"They read some of them first, so nothing changed."_q);
		return;
	} else if (texts.empty()) {
		Finish(peer, std::move(Take(peer)->hooks));
		return;
	}
	const auto first = texts.front();
	const auto others = std::vector<not_null<HistoryItem*>>(
		texts.begin() + 1,
		texts.end());
	for (const auto &item : others) {
		if (!CanRemoveForAll(item)) {
			Abort(
				peer,
				*Take(peer),
				u"Telegram won't remove these for everyone, so nothing "
				"changed."_q);
			return;
		}
	}
	if (!CanEditInPlace(first)) {
		SendInstead(peer, *Take(peer));
		return;
	}
	const auto history = first->history();
	const auto weak = base::make_weak(session);
	const auto otherIds = [&] {
		auto result = std::vector<FullMsgId>();
		for (const auto &item : others) {
			result.push_back(item->fullId());
		}
		return result;
	}();
	const auto removeOthers = [=] {
		const auto session = weak.get();
		auto pending = Take(peer);
		if (!session || !pending) {
			return;
		}
		auto items = std::vector<not_null<HistoryItem*>>();
		for (const auto &id : otherIds) {
			if (const auto item = session->data().message(id)) {
				items.push_back(item);
			}
		}
		Remove(session, items);
		Finish(peer, std::move(pending->hooks));
	};
	RememberFine(peer, merge.text.text);
	Api::EditTextMessage(
		first,
		Prepared(history, merge.text),
		Data::WebPageDraft(),
		Api::SendOptions(),
		[=](mtpRequestId) { removeOthers(); },
		[=](const QString &error, mtpRequestId) {
			if (error == u"MESSAGE_NOT_MODIFIED"_q) {
				removeOthers();
				return;
			}
			LOG(("Safe Calm: the first message would not edit (%1), "
				"sending instead.").arg(error));
			if (auto pending = Take(peer)) {
				SendInstead(peer, std::move(*pending));
			}
		},
		false);
}

} // namespace

void StartSentMerge(
		not_null<History*> history,
		SentMerge merge,
		MergeHooks hooks) {
	const auto peer = history->peer->id;
	if (SentMerging(peer) || merge.texts.empty() || merge.text.empty()) {
		return;
	}
	auto &pending = Merges()[peer];
	pending.session = base::make_weak(&history->session());
	pending.merge = std::move(merge);
	pending.hooks = std::move(hooks);
	pending.timer = std::make_unique<base::Timer>([=] { Execute(peer); });
	pending.timer->callOnce(kHoldMs);
	SetRunMarkHold(peer, crl::now() + kHoldMs);
}

void UndoSentMerge(PeerId peer) {
	auto &merges = Merges();
	const auto i = merges.find(peer);
	if (i == end(merges) || i->second.working) {
		return; // too late: the requests are on their way
	}
	auto pending = Take(peer);
	if (pending->merge.fromField && pending->hooks.restore) {
		pending->hooks.restore(pending->merge.text);
	}
	Finish(peer, std::move(pending->hooks));
}

bool SentMerging(PeerId peer) {
	return Merges().contains(peer);
}

bool CanEditInPlace(not_null<HistoryItem*> item) {
	const auto media = item->media();
	return item->isRegular()
		&& (!media || media->webpage())
		&& item->allowsEdit(base::unixtime::now());
}

bool CanRemoveForAll(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	if (!item->isRegular()) {
		return false;
	} else if (peer->isSelf()) {
		return item->canDelete(); // Saved Messages: nobody else has it
	} else if (peer->isMegagroup()) {
		// A supergroup deletes his own messages for everyone.
		return IsOwn(item) && item->canDelete();
	}
	return item->canDeleteForEveryone(base::unixtime::now());
}

} // namespace Safe::Calm
