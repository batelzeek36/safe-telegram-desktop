/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_judge_protocol.h"

namespace Safe::JudgeImage {

// A picture as the check sends it: JPEG, longest side at most
// JudgeProtocol::kMaxSide, never enlarged, transparency flattened onto white.
// Empty for a null image or when encoding fails. Safe off the main thread.
[[nodiscard]] std::string Encode(const QImage &image);

// The first JudgeProtocol::kMaxTextUnits UTF-16 units (as on the phone),
// never splitting a surrogate pair, as UTF-8.
[[nodiscard]] std::string ClipText(const QString &text);

// Everything RequestVerdict() was handed, ready for the queue. An empty
// `cover` in the result means there is nothing to send.
[[nodiscard]] JudgeProtocol::Request Prepare(
	bool document,
	const QImage &cover,
	const std::vector<QImage> &frames,
	const QString &caption,
	const QString &fileName,
	const QString &mime);

} // namespace Safe::JudgeImage
