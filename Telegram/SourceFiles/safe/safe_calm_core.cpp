/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_core.h"

#include <algorithm>

namespace Safe::Calm::Core {

int CardIndex(const std::vector<CardEntry> &entries) {
	for (auto i = 0; i != int(entries.size()); ++i) {
		const auto &entry = entries[i];
		switch (entry.step) {
		case CardStep::Checking:
			return -1; // cards come in order: wait for this answer
		case CardStep::Review:
		case CardStep::FailedReview:
			if (!entry.away) {
				return i;
			}
			break;
		case CardStep::Other:
			break;
		}
	}
	return -1;
}

void HoldSent(HoldGate &gate, std::int64_t now, bool fieldBusy) {
	if (!gate.active) {
		gate = HoldGate{ .active = true, .started = now };
	}
	gate.lastActivity = now;
	gate.fieldBusy = fieldBusy;
}

void HoldTyped(HoldGate &gate, std::int64_t now, bool fieldBusy) {
	if (!gate.active) {
		return;
	}
	gate.lastActivity = now;
	gate.lastKey = now;
	gate.fieldBusy = fieldBusy;
}

std::int64_t HoldReleaseAt(const HoldGate &gate, const HoldTimes &times) {
	if (!gate.active) {
		return 0;
	} else if (gate.card) {
		return kWaitsForCard;
	}
	auto at = gate.lastActivity + times.stop + (gate.pill ? times.pill : 0);
	if (gate.fieldBusy) {
		at = std::max(at, gate.lastKey + times.idleDraft);
	}
	return std::min(at, gate.started + times.cap);
}

bool HoldDue(
		const HoldGate &gate,
		std::int64_t now,
		const HoldTimes &times) {
	return gate.active && (now >= HoldReleaseAt(gate, times));
}

HoldEnd DecideHoldEnd(
		const HoldGate &gate,
		std::int64_t now,
		int heldTexts,
		int sentTexts,
		bool autoMerge,
		const HoldTimes &times) {
	if (!HoldDue(gate, now, times)) {
		return HoldEnd::Wait;
	}
	return (autoMerge && heldTexts >= kRunTexts && !sentTexts)
		? HoldEnd::Merge
		: HoldEnd::Release;
}

RunScan ScanRun(const std::vector<RunEntry> &newestFirst, int max) {
	auto result = RunScan();
	for (const auto &entry : newestFirst) {
		const auto ends = !entry.own
			|| entry.kind == RunKind::Breaks
			|| entry.failed
			|| (!entry.held && entry.sent && !entry.unread)
			|| (result.count == max);
		if (ends) {
			result.anchor = entry.id;
			break;
		} else if (!entry.held && !entry.sent) {
			return RunScan{ .settling = true };
		}
		++result.count;
		if (entry.kind == RunKind::Text) {
			++result.texts;
			if (entry.held) {
				++result.heldTexts;
			}
		}
	}
	return result;
}

bool OfferRun(const RunScan &scan, const Kept *kept) {
	if (scan.settling || scan.texts < kRunTexts) {
		return false;
	}
	return !kept
		|| (kept->anchor != scan.anchor)
		|| (kept->texts != scan.texts);
}

MergeDecision DecideMerge(const MergeFacts &facts) {
	using Step = MergeStep;
	using Why = MergeWhy;
	if (facts.anyRead) {
		return { Step::Abort, Why::Read };
	} else if (facts.heldLeft) {
		return { Step::Abort, Why::Changed };
	} else if (!facts.sentTexts) {
		// Every sent text was deleted meanwhile.
		return { Step::Abort, facts.heldTexts ? Why::Changed : Why::None };
	} else if (!facts.othersRemovable) {
		return { Step::Abort, Why::WontRemove };
	} else if (facts.firstEditable) {
		return { Step::EditFirst };
	} else if (facts.firstRemovable) {
		return { Step::SendAfter }; // the 48 h edit limit, say
	}
	return { Step::Abort, Why::WontRemove };
}

HearPlan PlanHearing(const std::vector<VoiceSlot> &newestFirst, int max) {
	auto result = HearPlan();
	auto counted = 0;
	for (auto i = 0; i != int(newestFirst.size()); ++i) {
		const auto &slot = newestFirst[i];
		if (!slot.voice) {
			continue;
		}
		const auto inWindow = (counted < max);
		if (inWindow) {
			++counted;
		}
		if (!inWindow && !slot.picked) {
			continue;
		}
		if (slot.heard == Heard::None) {
			result.ask.push_back(i);
			result.waiting = true;
		} else if (slot.heard == Heard::Lost) {
			result.retry.push_back(i);
			result.waiting = true;
		} else if (slot.heard == Heard::Asking) {
			result.waiting = true;
		}
	}
	return result;
}

bool HearAskAgain(
		std::int64_t now,
		std::int64_t asked,
		std::int64_t lastAgain) {
	if (lastAgain) {
		return (now - lastAgain) >= kHearAskAgainEveryMs;
	}
	return !asked || (now - asked) >= kHearAskAgainAfterMs;
}

std::string VoiceLine(bool round, Heard heard, const std::string &text) {
	const auto label = std::string(round ? "video note" : "voice note");
	auto trimmed = text;
	while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\n')) {
		trimmed.pop_back();
	}
	while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\n')) {
		trimmed.erase(trimmed.begin());
	}
	if (heard == Heard::Text && !trimmed.empty()) {
		return "[" + label + "] " + trimmed;
	}
	return "[" + label + ", not heard]";
}

} // namespace Safe::Calm::Core
