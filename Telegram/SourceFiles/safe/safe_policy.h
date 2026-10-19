/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Safe {

// Functions, not constants: a later round reads some of them from the trust store.
[[nodiscard]] inline bool SensitiveLocked() {
	return true;
}

[[nodiscard]] inline bool DiscoveryAllowed() {
	return false;
}

[[nodiscard]] inline bool WebPagesInside() {
	return false;
}

[[nodiscard]] inline bool OfficialUpdatesAllowed() {
	return false;
}

} // namespace Safe
