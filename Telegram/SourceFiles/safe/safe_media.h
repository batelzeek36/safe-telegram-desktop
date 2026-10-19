/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Safe {

// What happens to one picture (photo, video / GIF cover, sticker, avatar,
// link preview image, story), the same rule as the phone (Android M3b):
// Show  = everyone who put it there is Trusted (sender or the chat he made,
//         plus a Trusted forward origin / inline bot / story owner).
// Judge = an Allowed-list chat (or its avatar) with nothing untrusted
//         carrying it: blurred until the picture check answers.
// Hide  = anything else: foggy glass, never sent to the check, no label,
//         no tap action, no reveal.
enum class MediaOutcome : uchar {
	Show,
	Judge,
	Hide,
};

// A picture the check knows by Telegram's own id, so it is checked once.
struct MediaKey {
	uint64 id = 0;
	bool document = false; // false = photo id, true = document id.

	friend inline constexpr auto operator<=>(MediaKey, MediaKey) = default;
	friend inline constexpr bool operator==(MediaKey, MediaKey) = default;

	[[nodiscard]] explicit operator bool() const {
		return id != 0;
	}
};

// The picture check's answer for a Judge picture.
enum class VerdictState : uchar {
	Waiting, // No answer yet, the check failed, offline, or no key: blurred.
	Clear, // Shown.
	Flagged, // "Hidden by the picture check" (a refusal counts as flagged).
};

// ---- The picture check (safe_judge.cpp). The media gate calls these. ----

// A key is compiled in (an empty safe_judge_data.h = every Judge picture
// stays Waiting and says it is waiting for the picture check).
[[nodiscard]] bool JudgeAvailable();

// Cached answer only, cheap enough for the paint path. Never queues.
[[nodiscard]] VerdictState Verdict(MediaKey key);

// Queues one check unless it has an answer or is already queued. `cover` is
// the small picture Telegram already decoded for the bubble (the judge scales
// and encodes it); `frames` = up to 3 more frames of a video or GIF, judged
// as one picture. Caption, file name and mime type are untrusted text: they
// can only make a picture worse, never better. Main thread only.
void RequestVerdict(
	not_null<Main::Session*> session,
	MediaKey key,
	QImage cover,
	std::vector<QImage> frames,
	const QString &caption,
	const QString &fileName,
	const QString &mime);

// Fires on the main thread with each key whose answer changed, so the media
// gate repaints what shows it.
[[nodiscard]] rpl::producer<MediaKey> VerdictChanges();

} // namespace Safe
