/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_media_gate.h"
#include "webrtc/webrtc_video_track.h"

namespace Safe {

// A video chat / live stream tile's frame, through ShowsLiveVideo (which shows
// everyone since 2026-10-19). A refused tile gets "no frame", which every
// renderer draws as the person's blurred userpic. Audio is untouched.
[[nodiscard]] inline Webrtc::FrameWithInfo LiveFrame(
		not_null<PeerData*> peer,
		not_null<Webrtc::VideoTrack*> track,
		bool requireARGB32) {
	return ShowsLiveVideo(peer)
		? track->frameWithInfo(requireARGB32)
		: Webrtc::FrameWithInfo();
}

} // namespace Safe
