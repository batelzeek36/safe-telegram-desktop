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
#include "data/data_peer.h"
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
constexpr auto kKeptTexts = 256;

struct Note {
	FullMsgId id;
	crl::time asked = 0; // 0: already on Telegram's side when this began
	crl::time again = 0; // the last time it was asked again
	mtpRequestId request = 0; // asked again, the answer not back yet
	bool own = false; // failed before: this hearing asks for it itself
	bool ownFailed = false; // ...and it failed again
	bool noMore = false; // asking again was refused: wait for the update
};

struct Hearing {
	base::weak_ptr<Main::Session> session;
	std::vector<Note> notes;
	Fn<void(int)> listening;
	Fn<void(HearResult)> done;
	crl::time started = 0;
	int waiting = -1; // what `listening` heard last
	int again = 0; // transcripts that came by asking again
	std::unique_ptr<base::Timer> timer;
};

// Transcripts that came by asking again, for a note whose entry in
// Api::Transcribes cannot take them (one that failed before has no
// transcription id there).
struct Kept {
	base::flat_map<FullMsgId, QString> texts;
	std::deque<FullMsgId> order;
};

[[nodiscard]] base::flat_map<uint64, Hearing> &Hearings() {
	static auto result = base::flat_map<uint64, Hearing>();
	return result;
}

[[nodiscard]] Kept &KeptTexts() {
	static auto result = Kept();
	return result;
}

void Keep(FullMsgId id, const QString &text) {
	auto &kept = KeptTexts();
	if (!kept.texts.contains(id)) {
		kept.order.push_back(id);
	}
	kept.texts[id] = text;
	while (int(kept.order.size()) > kKeptTexts) {
		kept.texts.remove(kept.order.front());
		kept.order.pop_front();
	}
}

[[nodiscard]] DocumentData *VoiceDocument(not_null<const HistoryItem*> item) {
	const auto media = item->media();
	const auto document = media ? media->document() : nullptr;
	return (document
		&& (document->isVoiceMessage() || document->isVideoMessage()))
		? document
		: nullptr;
}

[[nodiscard]] Api::Transcribes &TranscribesOf(not_null<HistoryItem*> item) {
	return item->history()->session().api().transcribes();
}

// Asking again costs nothing with Premium (or where the group gives it for
// free); a trial user's tries are never spent on it.
[[nodiscard]] bool MayAskAgain(not_null<HistoryItem*> item) {
	return item->history()->session().premium()
		|| TranscribesOf(item).freeFor(item);
}

[[nodiscard]] QString TextOf(not_null<HistoryItem*> item) {
	const auto &entry = TranscribesOf(item).entry(item);
	if (!entry.pending && !entry.result.trimmed().isEmpty()) {
		return entry.result;
	}
	const auto &kept = KeptTexts().texts;
	const auto i = kept.find(item->fullId());
	return (i != end(kept)) ? i->second : QString();
}

[[nodiscard]] Core::Heard HeardOf(not_null<HistoryItem*> item) {
	if (!item->isRegular()) {
		return Core::Heard::Failed; // Telegram transcribes server messages only
	} else if (!TextOf(item).trimmed().isEmpty()) {
		return Core::Heard::Text;
	}
	const auto &entry = TranscribesOf(item).entry(item);
	if (entry.requestId || entry.pending) {
		return Core::Heard::Asking;
	} else if (entry.failed) {
		return (entry.toolong || !MayAskAgain(item))
			? Core::Heard::Failed
			: Core::Heard::Lost;
	} else if (entry.shown) {
		return Core::Heard::Failed; // transcribed: nothing was said
	}
	return Core::Heard::None;
}

[[nodiscard]] Core::Heard StateOf(
		not_null<Main::Session*> session,
		const Note &note) {
	const auto item = session->data().message(note.id);
	if (!item) {
		return Core::Heard::Failed;
	} else if (!TextOf(item).trimmed().isEmpty()) {
		return Core::Heard::Text;
	} else if (note.own) {
		return note.ownFailed ? Core::Heard::Failed : Core::Heard::Asking;
	}
	return HeardOf(item);
}

void AskAgain(uint64 hearingId, int index);

void Check(uint64 id) {
	auto &hearings = Hearings();
	const auto i = hearings.find(id);
	if (i == end(hearings)) {
		return;
	}
	auto &hearing = i->second;
	const auto session = hearing.session.get();
	const auto now = crl::now();
	const auto timedOut = (now - hearing.started) >= Core::kHearWaitMs;
	auto waiting = 0;
	auto heard = 0;
	if (session) {
		for (auto n = 0; n != int(hearing.notes.size()); ++n) {
			auto &note = hearing.notes[n];
			const auto state = StateOf(session, note);
			if (state == Core::Heard::Text) {
				++heard;
			} else if (state == Core::Heard::Asking) {
				++waiting;
				const auto item = session->data().message(note.id);
				const auto sending = item
					&& TranscribesOf(item).entry(item).requestId;
				if (!timedOut
					&& item
					&& !note.request
					&& !note.noMore
					&& !sending
					&& (note.own || MayAskAgain(item))
					&& Core::HearAskAgain(now, note.asked, note.again)) {
					AskAgain(id, n);
				}
			}
		}
	}
	const auto finished = !session || !waiting || timedOut;
	if (!finished) {
		if (hearing.waiting != waiting) {
			hearing.waiting = waiting;
			if (hearing.listening) {
				hearing.listening(waiting);
			}
		}
		return;
	}
	const auto elapsed = now - hearing.started;
	if (waiting) {
		LOG(("Safe Calm: %1 of %2 voice notes still being transcribed after "
			"%3 s, asking without them."
			).arg(waiting
			).arg(hearing.notes.size()
			).arg(elapsed / 1000));
	} else {
		LOG(("Safe Calm: %1 of %2 voice notes heard in %3 ms (%4 by asking "
			"again)."
			).arg(heard
			).arg(hearing.notes.size()
			).arg(elapsed
			).arg(hearing.again));
	}
	auto done = std::move(hearing.done);
	auto listening = std::move(hearing.listening);
	const auto result = HearResult{
		.notes = int(hearing.notes.size()),
		.heard = heard,
		.complete = !waiting,
	};
	// Not destroyed inside its own callback.
	const auto timer = hearing.timer.release();
	timer->cancel();
	crl::on_main([=] { delete timer; });
	hearings.erase(i);
	if (listening && result.notes) {
		listening(0);
	}
	if (done) {
		done(result);
	}
}

[[nodiscard]] Note *FindNote(uint64 hearingId, FullMsgId id) {
	auto &hearings = Hearings();
	const auto i = hearings.find(hearingId);
	if (i == end(hearings)) {
		return nullptr;
	}
	for (auto &note : i->second.notes) {
		if (note.id == id) {
			return &note;
		}
	}
	return nullptr;
}

// Telegram answers "pending" until it has the text, then the text; the same
// transcription id as the first ask, so the bubble's own entry takes it.
void AskAgain(uint64 hearingId, int index) {
	auto &hearing = Hearings()[hearingId];
	auto &note = hearing.notes[index];
	const auto session = hearing.session.get();
	const auto item = session ? session->data().message(note.id) : nullptr;
	if (!item) {
		return;
	}
	const auto id = note.id;
	const auto weak = hearing.session;
	note.again = crl::now();
	note.request = session->api().request(MTPmessages_TranscribeAudio(
		item->history()->peer->input(),
		MTP_int(id.msg)
	)).done([=](const MTPmessages_TranscribedAudio &result) {
		const auto &data = result.data();
		const auto pending = data.is_pending();
		const auto text = qs(data.vtext());
		if (!pending) {
			Keep(id, text);
			if (const auto session = weak.get()) {
				auto update = MTP_updateTranscribedAudio(
					MTP_flags(MTPDupdateTranscribedAudio::Flag(0)),
					peerToMTP(id.peer),
					MTP_int(id.msg),
					data.vtranscription_id(),
					data.vtext());
				session->api().transcribes().apply(
					update.c_updateTranscribedAudio());
			}
		}
		if (const auto note = FindNote(hearingId, id)) {
			note->request = 0; // still pending: asked again in a while
			if (!pending) {
				++Hearings()[hearingId].again;
				if (note->own && text.trimmed().isEmpty()) {
					note->ownFailed = true; // transcribed: nothing was said
				}
			}
		}
	}).fail([=](const MTP::Error &error) {
		LOG(("Safe Calm: asking again for a voice note failed (%1)."
			).arg(error.type()));
		if (const auto note = FindNote(hearingId, id)) {
			note->request = 0;
			if (note->own) {
				note->ownFailed = true;
			} else {
				note->noMore = true; // its update can still come
			}
		}
	}).send();
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
		? TextOf(item).left(kMaxContextText)
		: QString();
	return QString::fromStdString(Core::VoiceLine(
		document->isVideoMessage(),
		heard,
		text.toStdString()));
}

void HearVoices(
		not_null<History*> history,
		HistoryItem *picked,
		Fn<void(int)> listening,
		Fn<void(HearResult)> done) {
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
	if (!plan.waiting) {
		crl::on_main([done = std::move(done)] { done({}); });
		return;
	}
	auto &transcribes = history->session().api().transcribes();
	const auto now = crl::now();
	auto notes = std::vector<Note>();
	for (auto i = 0; i != int(slots.size()); ++i) {
		const auto &slot = slots[i];
		const auto asked = ranges::contains(plan.ask, i);
		const auto retry = ranges::contains(plan.retry, i);
		if (!asked && !retry && slot.heard != Core::Heard::Asking) {
			continue;
		}
		notes.push_back({
			.id = items[i]->fullId(),
			.asked = asked ? now : 0,
			.own = retry,
		});
	}
	// All at once: Telegram transcribes them side by side.
	for (const auto index : plan.ask) {
		transcribes.toggle(items[index]);
	}
	static auto nextId = uint64();
	const auto id = ++nextId;
	auto &hearing = Hearings()[id];
	hearing.session = base::make_weak(&history->session());
	hearing.notes = std::move(notes);
	hearing.listening = std::move(listening);
	hearing.done = std::move(done);
	hearing.started = now;
	hearing.timer = std::make_unique<base::Timer>([=] { Check(id); });
	hearing.timer->callEach(kPoll);
	crl::on_main([=] { Check(id); }); // the retries go now
}

} // namespace Safe::Calm
