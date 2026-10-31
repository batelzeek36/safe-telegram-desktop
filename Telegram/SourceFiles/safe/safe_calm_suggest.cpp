/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_suggest.h"

#include "data/data_peer.h"
#include "history/history.h"
#include "history/history_item.h"
#include "safe/safe_calm.h"
#include "safe/safe_calm_client.h"
#include "safe/safe_calm_shared.h"
#include "safe/safe_calm_voice.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Safe::Calm {
namespace {

constexpr auto kMaxRemembered = 16;

// The chat as it was asked about: its newest message and the one picked.
struct Key {
	PeerId peer = 0;
	MsgId last = 0;
	MsgId replyTo = 0;

	friend inline auto operator<=>(const Key&, const Key&) = default;
	friend inline bool operator==(const Key&, const Key&) = default;
};

struct Remembered {
	base::flat_map<Key, SuggestResult> answers;
	std::deque<Key> order;
	base::flat_map<uint64, uint64> calls; // our id -> the client's
	uint64 nextId = 0;
};

[[nodiscard]] Remembered &State() {
	static auto result = Remembered();
	return result;
}

void Remember(const Key &key, const SuggestResult &result) {
	auto &state = State();
	if (!state.answers.contains(key)) {
		state.order.push_back(key);
	}
	state.answers[key] = result;
	while (int(state.order.size()) > kMaxRemembered) {
		state.answers.remove(state.order.front());
		state.order.pop_front();
	}
}

[[nodiscard]] MsgId LastId(not_null<History*> history) {
	const auto last = history->lastMessage();
	return last ? last->id : MsgId();
}

void Ask(
	uint64 id,
	Key key,
	not_null<History*> history,
	HistoryItem *replyTo,
	bool remember,
	QString thought,
	Fn<void(const SuggestResult&)> done);

} // namespace

uint64 Suggest(
		not_null<History*> history,
		HistoryItem *replyTo,
		bool fresh,
		QString thought,
		Fn<void(int)> listening,
		Fn<void(const SuggestResult&)> done) {
	auto &state = State();
	const auto id = ++state.nextId;
	const auto key = Key{
		.peer = history->peer->id,
		.last = LastId(history),
		.replyTo = replyTo ? replyTo->id : MsgId(),
	};
	if (!fresh && thought.isEmpty()) {
		if (const auto i = state.answers.find(key); i != end(state.answers)) {
			auto result = i->second;
			state.calls.emplace(id, 0);
			crl::on_main([=, done = std::move(done)] {
				if (State().calls.remove(id)) {
					done(result);
				}
			});
			return id;
		}
	}
	// Registered first: with no runner and no key the answer comes at once.
	state.calls.emplace(id, 0);
	// Voice notes first: Telegram transcribes them, then the model reads what
	// was said (Zach 10-27: "suggest a reply doesnt work for voice messages").
	const auto weak = base::make_weak(&history->session());
	const auto peer = history->peer->id;
	const auto replyId = replyTo ? replyTo->fullId() : FullMsgId();
	auto heard = [=, done = std::move(done)](HearResult hearing) mutable {
		const auto session = weak.get();
		const auto history = session
			? session->data().historyLoaded(peer)
			: nullptr;
		const auto replyTo = (session && replyId)
			? session->data().message(replyId)
			: nullptr;
		if (!State().calls.contains(id)) {
			return; // cancelled while the notes were heard
		} else if (!history || (replyId && !replyTo)) {
			State().calls.remove(id);
			done({ .failure = u"chat gone"_q });
			return;
		}
		// Ideas made without a note still being transcribed are asked for
		// again next time, when its words may be there.
		Ask(
			id,
			key,
			history,
			replyTo,
			hearing.complete && thought.isEmpty(),
			thought,
			std::move(done));
	};
	HearVoices(
		history,
		replyTo,
		[=](int waiting) {
			if (listening && State().calls.contains(id)) {
				listening(waiting);
			}
		},
		std::move(heard));
	return id;
}

namespace {

void Ask(
		uint64 id,
		Key key,
		not_null<History*> history,
		HistoryItem *replyTo,
		bool remember,
		QString thought,
		Fn<void(const SuggestResult&)> done) {
	auto &state = State();
	auto request = SuggestRequest{
		.recipient = DisplayName(history->peer),
		.context = MakeRequest(history, Direction::Out, QString()).context,
		.chat = ChatKey(history->peer),
		.thought = std::move(thought),
	};
	if (replyTo) {
		auto text = replyTo->originalText().text;
		if (IsVoiceNote(replyTo)) {
			const auto voice = VoiceContextText(replyTo);
			text = text.trimmed().isEmpty() ? voice : (voice + u"\n"_q + text);
		}
		request.replyTo = ContextLine{
			.from = IsOwn(replyTo)
				? OwnerName()
				: DisplayName(replyTo->from()),
			.text = text,
		};
	}
	const auto call = SharedClient().suggest(
		std::move(request),
		[=](SuggestResult result) {
			if (!State().calls.remove(id)) {
				return;
			}
			if (remember && !result.list.empty()) {
				Remember(key, result);
			}
			done(result);
		});
	if (const auto i = state.calls.find(id); i != end(state.calls)) {
		i->second = call;
	}
}

} // namespace

void CancelSuggest(uint64 id) {
	auto &calls = State().calls;
	const auto i = calls.find(id);
	if (i == end(calls)) {
		return;
	}
	const auto call = i->second;
	calls.erase(i);
	if (call) {
		SharedClient().cancel(call);
	}
}

} // namespace Safe::Calm
