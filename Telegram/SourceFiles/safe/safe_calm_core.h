/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// The pure decisions behind Calm's send queue and its one-message offers,
// with no Qt and no Telegram types, so tools/desktopcheck can check them
// (docs/desktop/STATE-SUMMARIZE.md). The app adapts its messages to these.
namespace Safe::Calm::Core {

// Second look on the send queue (Zach 10-27, the Android design "CAP
// HANDOFF 10-27"): one card at a time, for the oldest message waiting for
// his pick, never while a check before it is still running, skipping the
// ones he put away.
enum class CardStep : std::uint8_t {
	Other, // goes out (or waits) without asking him
	Checking, // its check is not back yet
	Review, // hot: waits for Send calm version / Send mine / Edit
	FailedReview, // no answer in Second look: Send as is / Edit
};
struct CardEntry {
	CardStep step = CardStep::Other;
	bool away = false; // he put its card away (Esc); its row offers Open
};
// The index of the entry whose card shows, or -1.
[[nodiscard]] int CardIndex(const std::vector<CardEntry> &entries);

// The hold (Zach 10-27: "what if theres like a 10 second delay when i send
// each message ... so the recipient doesnt see a burst and then a massive
// edit"): his texts wait on his side while he keeps typing, then go out in
// order. Times in milliseconds of a monotonic clock.
struct HoldTimes {
	std::int64_t stop = 12000; // after he stops: field empty, no keystrokes
	std::int64_t pill = 8000; // more while "Send these N as one" is up
	std::int64_t idleDraft = 45000; // a draft left alone stops holding
	std::int64_t cap = 120000; // the first held message waits at most this
};
inline constexpr auto kHoldPillTexts = 3; // texts held together: the pill
inline constexpr auto kWaitsForCard = std::numeric_limits<std::int64_t>::max();

struct HoldGate {
	bool active = false;
	std::int64_t started = 0; // the first held message
	std::int64_t lastActivity = 0; // the last send or keystroke
	std::int64_t lastKey = 0;
	bool fieldBusy = false; // his field has text
	bool pill = false; // kHoldPillTexts or more texts held
	bool card = false; // the one-message card is open: wait for his pick
};

// A text went into the hold (starts it when none was running).
void HoldSent(HoldGate &gate, std::int64_t now, bool fieldBusy);
// A keystroke or a field change in that chat; nothing when no hold runs.
void HoldTyped(HoldGate &gate, std::int64_t now, bool fieldBusy);
// When the held messages go out; kWaitsForCard while the card is open,
// 0 when nothing is held.
[[nodiscard]] std::int64_t HoldReleaseAt(
	const HoldGate &gate,
	const HoldTimes &times = {});
[[nodiscard]] bool HoldDue(
	const HoldGate &gate,
	std::int64_t now,
	const HoldTimes &times = {});

// Merge automatically (Zach 10-27: "maybe there should be a toggle in the
// settings for if i want the merge to be auto or not"), off unless he turns
// it on: when the hold ends with kHoldPillTexts or more texts held, the one
// message is written and goes out in their place after an Undo window; Undo,
// or no one message within kAutoMergeWriteMs, sends them as typed. The
// after-send offer stays tap-only (it changes messages already delivered).
inline constexpr auto kAutoMergeDefault = false;
inline constexpr auto kAutoMergeUndoMs = std::int64_t(4000);
inline constexpr auto kAutoMergeWriteMs = std::int64_t(20000);
enum class HoldEnd : std::uint8_t {
	Wait, // not due yet, or the card (or an automatic merge) is on it
	Release, // they go out as typed
	Merge, // they go as one, after the Undo window
};
[[nodiscard]] HoldEnd DecideHoldEnd(
	const HoldGate &gate,
	std::int64_t now,
	int heldTexts,
	bool autoMerge,
	const HoldTimes &times = {});

// The run that already went out (Zach 10-27: "the AI should detect when im
// mass-sending messages that could overwhelm the reader"; then "keep the
// GIFs"): his own messages at the bottom of the chat, nothing from anyone
// else between, all on the server and unread. Texts get merged; his GIFs,
// stickers, photos and videos count toward the run and stay where they are.
// Anything else (a forward, a reply to someone else, a file, a voice note,
// a service message) ends the run.
enum class RunKind : std::uint8_t {
	Text,
	Media,
	Breaks,
};
struct RunEntry {
	RunKind kind = RunKind::Breaks;
	bool own = false;
	bool sent = true; // on the server; a local one at the bottom = no run
	bool unread = false;
	std::int64_t date = 0; // unixtime
	std::int64_t id = 0;
};
struct RunScan {
	int count = 0; // texts and media
	int texts = 0;
	std::int64_t first = 0; // the oldest message's id: which run it is
	std::int64_t oldest = 0;
	std::int64_t newest = 0;
};
inline constexpr auto kMaxRun = 60;

// `newestFirst`: the chat's messages from the bottom up.
[[nodiscard]] RunScan ScanRun(
	const std::vector<RunEntry> &newestFirst,
	int max = kMaxRun);

struct RunRules {
	int min = 5; // messages in a row
	int minTexts = 2; // one text has nothing to merge
	std::int64_t pause = 20; // seconds since his last message
	std::int64_t fresh = 30 * 60; // the oldest under 30 minutes old
	int regrow = 5; // after Keep them, this many more offer again
};
struct Kept {
	std::int64_t first = 0; // the run he kept
	int count = 0; // how long it was then
};
enum class Offer : std::uint8_t {
	None,
	Later, // wait for his pause
	Ask, // ask the runner for the one message, then offer it
};
struct OfferDecision {
	Offer offer = Offer::None;
	std::int64_t retryIn = 0; // seconds, for Later
};
[[nodiscard]] OfferDecision DecideOffer(
	const RunScan &scan,
	std::int64_t now,
	const Kept *kept,
	const RunRules &rules = {});

// Voice notes in reply ideas (Zach 10-27: "suggest a reply doesnt work for
// voice messages"): before asking, Telegram's own transcription (Premium,
// the voice bubble's button) of the newest voice and video notes in the
// chat; each goes to the model as "[voice note] <what was said>", or
// "[voice note, not heard]" so the model never pretends to know.
inline constexpr auto kMaxHeard = 5;
enum class Heard : std::uint8_t {
	None, // never asked
	Asking, // asked, not back yet (or still being transcribed)
	Text,
	Failed, // refused for good: too long, no Premium, a local message
	Lost, // failed for another reason (the network, a busy server)
};
struct VoiceSlot {
	bool voice = false; // a voice note or a round video
	Heard heard = Heard::None;
	bool picked = false; // the message he asked ideas for: always heard
};
struct HearPlan {
	std::vector<int> ask; // indexes of the notes to ask Telegram for now
	std::vector<int> retry; // Lost: asked once more
	bool waiting = false; // some of the notes are still on their way
};

// How long the ideas wait for Telegram (10-27: two real tries timed out at
// 12 s). Telegram answers "pending" at once and sends the finished text
// once, as an update (5-8 s for notes of 5-63 s, measured 10-27); the app
// misses that update while it catches up on updates (Updates drops every
// batch during getDifference), so a note still pending is asked again: the
// same request returns the finished text once Telegram has it.
inline constexpr auto kHearWaitMs = std::int64_t(45000);
inline constexpr auto kHearAskAgainAfterMs = std::int64_t(4000);
inline constexpr auto kHearAskAgainEveryMs = std::int64_t(3000);
// `asked`: when Telegram was asked for it (0: before this hearing began),
// `lastAgain`: the last time it was asked again (0: never).
[[nodiscard]] bool HearAskAgain(
	std::int64_t now,
	std::int64_t asked,
	std::int64_t lastAgain);
// `newestFirst`: the context window from the bottom up.
[[nodiscard]] HearPlan PlanHearing(
	const std::vector<VoiceSlot> &newestFirst,
	int max = kMaxHeard);
[[nodiscard]] std::string VoiceLine(
	bool round,
	Heard heard,
	const std::string &text);

} // namespace Safe::Calm::Core
