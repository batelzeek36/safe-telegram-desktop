/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class History;
class HistoryItem;

// Voice notes and round videos for Calm's model (Zach 10-27: "suggest a reply
// doesnt work for voice messages"). Telegram's own transcription only (the
// Premium transcribe of the voice bubble's button, Api::Transcribes): no
// audio ever goes to the runner or the paid API. Asking for one shows its
// text under the bubble, the same as tapping that button.
namespace Safe::Calm {

[[nodiscard]] bool IsVoiceNote(not_null<const HistoryItem*> item);

// What a voice or video note says to the model, from the transcript Telegram
// already gave: "[voice note] <what was said>" or "[voice note, not heard]".
[[nodiscard]] QString VoiceContextText(not_null<HistoryItem*> item);

// Asks Telegram to transcribe the newest voice notes of `history` that were
// never transcribed (Core::kMaxHeard of them, newest first) and `picked` (the
// message he wants ideas for), then calls `done` once all are back or after
// kHearTimeoutMs. `done` runs on the main thread, at once when nothing waits.
inline constexpr auto kHearTimeoutMs = crl::time(12000);
void HearVoices(
	not_null<History*> history,
	HistoryItem *picked,
	Fn<void()> done);

} // namespace Safe::Calm
