/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_voice.h"

#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "base/timer.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "safe/safe_calm_core.h"
#include "safe/safe_calm_prompt.h"

namespace Safe::Calm {
namespace {

constexpr auto kPoll = crl::time(250);

struct Hearing {
	base::weak_ptr<Main::Session> session;
	std::vector<FullMsgId> notes;
	Fn<void()> done;
	crl::time started = 0;
	std::unique_ptr<base::Timer> timer;
};

[[nodiscard]] base::flat_map<uint64, Hearing> &Hearings() {
	static auto result = base::flat_map<uint64, Hearing>();
	return result;
}

[[nodiscard]] DocumentData *VoiceDocument(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return (document
		&& (document->isVoiceMessage() || document->isVideoMessage()))
		? document
		: nullptr;
}

[[nodiscard]] Core::Heard HeardOf(not_null<HistoryItem*> item) {
	if (!item->isRegular()) {
		return Core::Heard::Failed; // Telegram transcribes server messages only
	}
	const auto &entry = item->history()->session().api().transcribes().entry(
		item);
	if (entry.requestId || entry.pending) {
		return Core::Heard::Asking;
	} else if (entry.failed) {
		return Core::Heard::Failed;
	} else if (!entry.result.trimmed().isEmpty()) {
		return Core::Heard::Text;
	}
	return Core::Heard::None;
}

[[nodiscard]] bool StillWaiting(const Hearing &hearing) {
	const auto session = hearing.session.get();
	if (!session) {
		return false;
	}
	for (const auto &id : hearing.notes) {
		if (const auto item = session->data().message(id)) {
			if (HeardOf(item) == Core::Heard::Asking) {
				return true;
			}
		}
	}
	return false;
}

void Check(uint64 id) {
	auto &hearings = Hearings();
	const auto i = hearings.find(id);
	if (i == end(hearings)) {
		return;
	}
	const auto timedOut = (crl::now() - i->second.started)
		>= kHearTimeoutMs;
	if (StillWaiting(i->second) && !timedOut) {
		return;
	}
	if (timedOut) {
		LOG(("Safe Calm: voice notes still being transcribed, asking without "
			"them."));
	}
	auto done = std::move(i->second.done);
	// Not destroyed inside its own callback.
	const auto timer = i->second.timer.release();
	timer->cancel();
	crl::on_main([=] { delete timer; });
	hearings.erase(i);
	if (done) {
		done();
	}
}

} // namespace

bool IsVoiceNote(not_null<const HistoryItem*> item) {
	return VoiceDocument(item) != nullptr;
}

QString VoiceContextText(not_null<HistoryItem*> item) {
	const auto document = VoiceDocument(item);
	if (!document) {
		return QString();
	}
	const auto heard = HeardOf(item);
	const auto text = (heard == Core::Heard::Text)
		? item->history()->session().api().transcribes().entry(
			item).result.left(kMaxContextText)
		: QString();
	return QString::fromStdString(Core::VoiceLine(
		document->isVideoMessage(),
		heard,
		text.toStdString()));
}

void HearVoices(
		not_null<History*> history,
		HistoryItem *picked,
		Fn<void()> done) {
	auto items = std::vector<not_null<HistoryItem*>>();
	auto slots = std::vector<Core::VoiceSlot>();
	const auto add = [&](not_null<HistoryItem*> item, bool isPicked) {
		items.push_back(item);
		slots.push_back({
			.voice = IsVoiceNote(item),
			.heard = IsVoiceNote(item) ? HeardOf(item) : Core::Heard::None,
			.picked = isPicked,
		});
	};
	if (picked && IsVoiceNote(picked)) {
		add(picked, true);
	}
	auto seen = 0;
	for (auto b = history->blocks.rbegin(); b != history->blocks.rend(); ++b) {
		const auto &messages = (*b)->messages;
		for (auto m = messages.rbegin(); m != messages.rend(); ++m) {
			const auto item = (*m)->data();
			if (item.get() != picked) {
				add(item, false);
			}
			if (++seen >= kMaxContext) {
				break;
			}
		}
		if (seen >= kMaxContext) {
			break;
		}
	}
	const auto plan = Core::PlanHearing(slots);
	auto &transcribes = history->session().api().transcribes();
	auto notes = std::vector<FullMsgId>();
	for (const auto index : plan.ask) {
		transcribes.toggle(items[index]);
	}
	for (auto i = 0; i != int(slots.size()); ++i) {
		if (slots[i].voice) {
			notes.push_back(items[i]->fullId());
		}
	}
	if (!plan.waiting) {
		crl::on_main([done = std::move(done)] { done(); });
		return;
	}
	static auto nextId = uint64();
	const auto id = ++nextId;
	auto &hearing = Hearings()[id];
	hearing.session = base::make_weak(&history->session());
	hearing.notes = std::move(notes);
	hearing.done = std::move(done);
	hearing.started = crl::now();
	hearing.timer = std::make_unique<base::Timer>([=] { Check(id); });
	hearing.timer->callEach(kPoll);
}

} // namespace Safe::Calm
