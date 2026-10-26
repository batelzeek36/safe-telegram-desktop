/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "safe/safe_calm_client.h"
#include "safe/safe_calm_marks.h"
#include "safe/safe_calm_shared.h"
#include "safe/safe_calm_store.h"
#include "window/notifications_manager.h"

namespace Safe::Calm {
namespace {

constexpr auto kRetryEvery = crl::time(30000);
constexpr auto kMaxTries = 20;

enum class Stage : uchar {
	Asking,
	Failed, // shows the waiting text, asks again every kRetryEvery
	Fine,
	Soft,
};

struct Held {
	base::weak_ptr<Main::Session> session;
	TextWithEntities original;
	uint64 hash = 0;
	QString shown;
	Stage stage = Stage::Asking;
	Request request;
	uint64 call = 0;
	int tries = 0;
};

// Zach 10-25: the held bubble shows a soft shimmer, not the word "softening"
// (the chat list and notifications keep the placeholder text).
constexpr auto kShimmerFrame = crl::time(40);

void Ask(FullMsgId id);
void RetryFailed();
void ShimmerTick();

struct State {
	State()
	: retry([] { RetryFailed(); })
	, shimmer([] { ShimmerTick(); }) {
	}

	base::flat_map<FullMsgId, Held> items;
	base::Timer retry;
	base::Timer shimmer;
};

[[nodiscard]] State &Instance() {
	static auto result = State();
	return result;
}

[[nodiscard]] TextWithEntities Placeholder(bool failed) {
	const auto text = failed
		? QString::fromUtf8("\xF0\x9F\x8D\x83 waiting to be softened")
		: QString::fromUtf8("\xF0\x9F\x8D\x83 softening\xE2\x80\xA6");
	return {
		text,
		{ EntityInText(EntityType::Italic, 0, int(text.size())) },
	};
}

[[nodiscard]] TextWithEntities Shown(const Held &held) {
	switch (held.stage) {
	case Stage::Asking: return Placeholder(false);
	case Stage::Failed: return Placeholder(true);
	case Stage::Fine: return held.original;
	case Stage::Soft: return { held.shown };
	}
	Unexpected("Safe::Calm::Stage in Shown.");
}

[[nodiscard]] bool Eligible(
		not_null<HistoryItem*> item,
		const TextWithEntities &text) {
	if (item->out()
		|| !item->isRegular()
		|| item->isService()
		|| item->isSponsored()
		|| item->isLegacyMessage()) {
		return false;
	}
	const auto history = item->history();
	const auto from = item->from();
	if (from->isSelf() || from->isServiceUser()) {
		return false;
	} else if (const auto user = from->asUser()) {
		if (user->isBot() && RuleFor(history->peer->id) != ChatRule::Always) {
			return false;
		}
	}
	const auto modes = ModesFor(history->peer);
	return modes.incoming
		&& (item->date() >= modes.incomingSince)
		&& Checkable(text.text);
}

// Shows the current stage everywhere the message renders.
void Apply(FullMsgId id) {
	auto &state = Instance();
	const auto i = state.items.find(id);
	if (i == end(state.items)) {
		return;
	}
	const auto session = i->second.session.get();
	const auto item = session ? session->data().message(id) : nullptr;
	if (!item) {
		state.items.erase(i);
		return;
	}
	auto original = i->second.original;
	const auto fine = (i->second.stage == Stage::Fine);
	i->second.shown = Shown(i->second).text;
	item->setText(std::move(original));
	if (fine) {
		state.items.remove(id); // the store answers from now on
	}
	session->data().requestItemViewRefresh(item);
	item->invalidateChatListEntry();
	Core::App().notifications().checkDelayed();
}

void Done(FullMsgId id, const Result &result) {
	auto &state = Instance();
	const auto i = state.items.find(id);
	if (i == end(state.items)) {
		return;
	}
	auto &held = i->second;
	held.call = 0;
	if (!result.verdict) {
		LOG(("Safe Calm: no verdict for an incoming message (%1).").arg(
			result.failure));
		const auto wasFailed = (held.stage == Stage::Failed);
		held.stage = Stage::Failed;
		if (++held.tries < kMaxTries && !state.retry.isActive()) {
			state.retry.callOnce(kRetryEvery);
		}
		if (!wasFailed) {
			Apply(id);
		}
		return;
	}
	const auto &verdict = *result.verdict;
	held.stage = verdict.soften ? Stage::Soft : Stage::Fine;
	held.shown = verdict.soften ? verdict.text : held.original.text;
	SetIncoming(id, IncomingRecord{
		.hash = held.hash,
		.soften = verdict.soften,
		.calm = verdict.soften ? verdict.text : QString(),
		.at = base::unixtime::now(),
	});
	Apply(id);
}

void Ask(FullMsgId id) {
	auto &state = Instance();
	const auto i = state.items.find(id);
	if (i == end(state.items) || i->second.call) {
		return;
	}
	i->second.call = SharedClient().check(i->second.request, [=](
			Result result) {
		Done(id, result);
	});
}

void RetryFailed() {
	auto failed = std::vector<FullMsgId>();
	for (const auto &[id, held] : Instance().items) {
		if (held.stage == Stage::Failed && !held.call) {
			failed.push_back(id);
		}
	}
	for (const auto &id : failed) {
		Ask(id);
	}
}

[[nodiscard]] const Held *Find(not_null<const HistoryItem*> item) {
	const auto &items = Instance().items;
	const auto i = items.find(item->fullId());
	return (i != end(items)) ? &i->second : nullptr;
}

// Repaints every bubble still being softened so its shimmer moves; stops
// once none is left.
void ShimmerTick() {
	auto &state = Instance();
	auto any = false;
	for (const auto &[id, held] : state.items) {
		if (held.stage != Stage::Asking) {
			continue;
		}
		const auto session = held.session.get();
		if (const auto item = session ? session->data().message(id) : nullptr) {
			session->data().requestItemRepaint(item);
			any = true;
		}
	}
	if (any) {
		state.shimmer.callOnce(kShimmerFrame);
	}
}

} // namespace

TextWithEntities FilterIncoming(
		not_null<HistoryItem*> item,
		TextWithEntities text) {
	if (text.text.isEmpty()) {
		return text;
	}
	auto &state = Instance();
	const auto id = item->fullId();
	if (const auto i = state.items.find(id); i != end(state.items)) {
		auto &held = i->second;
		if (text.text == held.shown) {
			return text;
		} else if (TextHash(text.text) == held.hash) {
			return Shown(held);
		}
		// Edited: the new text is checked from scratch.
		SharedClient().cancel(held.call);
		state.items.erase(i);
	}
	if (!Eligible(item, text)) {
		return text;
	}
	const auto hash = TextHash(text.text);
	auto held = Held{
		.session = base::make_weak(&item->history()->session()),
		.original = text,
		.hash = hash,
	};
	if (const auto record = Incoming(id); record && record->hash == hash) {
		if (!record->soften) {
			return text;
		}
		held.stage = Stage::Soft;
		held.shown = record->calm;
		state.items.emplace(id, std::move(held));
		return { record->calm };
	}
	const auto from = item->from();
	held.request = MakeRequest(
		item->history(),
		Direction::In,
		text.text,
		item->id);
	held.request.sender = from->isUser() ? from->shortName() : from->name();
	auto shown = Placeholder(false);
	held.shown = shown.text;
	state.items.emplace(id, std::move(held));
	Ask(id);
	if (!state.shimmer.isActive()) {
		state.shimmer.callOnce(kShimmerFrame);
	}
	return shown;
}

bool HoldsNotification(not_null<const HistoryItem*> item) {
	const auto held = Find(item);
	return held && (held->stage == Stage::Asking);
}

bool IsHeld(not_null<const HistoryItem*> item) {
	const auto held = Find(item);
	return held
		&& (held->stage == Stage::Asking || held->stage == Stage::Failed);
}

bool PaintHeldText(
		QPainter &p,
		not_null<const HistoryItem*> item,
		QRect textRect,
		int textWidth,
		int lineHeight,
		const QColor &color) {
	const auto held = Find(item);
	if (!held || held->stage != Stage::Asking) {
		return false;
	}
	const auto height = lineHeight * 0.5;
	const auto width = std::min(textWidth, textRect.width());
	PaintShimmerBar(
		p,
		QRectF(
			textRect.x(),
			textRect.y() + (lineHeight - height) / 2.,
			width,
			height),
		color);
	return true;
}

bool IsSoftenedIncoming(not_null<const HistoryItem*> item) {
	const auto held = Find(item);
	return held && (held->stage == Stage::Soft);
}

} // namespace Safe::Calm
