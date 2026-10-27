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

RunScan ScanRun(const std::vector<RunEntry> &newestFirst, int max) {
	auto result = RunScan();
	for (const auto &entry : newestFirst) {
		if (!entry.sent) {
			// Something of his is still on its way at the bottom.
			return result.count ? result : RunScan();
		} else if (!entry.own
			|| !entry.unread
			|| entry.kind == RunKind::Breaks
			|| result.count == max) {
			break;
		}
		if (!result.count) {
			result.newest = entry.date;
		}
		++result.count;
		if (entry.kind == RunKind::Text) {
			++result.texts;
		}
		result.first = entry.id;
		result.oldest = entry.date;
	}
	return result;
}

OfferDecision DecideOffer(
		const RunScan &scan,
		std::int64_t now,
		const Kept *kept,
		const RunRules &rules) {
	if (scan.count < rules.min || scan.texts < rules.minTexts) {
		return {};
	} else if (kept
		&& kept->first == scan.first
		&& scan.count < kept->count + rules.regrow) {
		return {};
	}
	const auto quiet = now - scan.newest;
	if (quiet < rules.pause) {
		return { .offer = Offer::Later, .retryIn = rules.pause - quiet };
	} else if (now - scan.oldest >= rules.fresh) {
		return {};
	}
	return { .offer = Offer::Ask };
}

} // namespace Safe::Calm::Core
