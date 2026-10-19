/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_trust_approvals.h"

namespace Safe::TrustRules {

// The public half of the approvals key (raw Ed25519, 32 bytes). The private
// half lives only on Zach's Mac (~/.config/safe-telegram/
// approvals-ed25519.pem, mode 600, never in git); tools/mac_approve.py in the
// control repo signs with it. A new key pair means a new build.
inline constexpr unsigned char kApprovalsPublicKey[kPublicKeySize] = {
	0x7d, 0xd1, 0x92, 0xbb, 0xd9, 0xc6, 0xc5, 0x81,
	0xfc, 0xf8, 0x8a, 0x24, 0x55, 0x60, 0xbb, 0xd7,
	0x8b, 0x08, 0xe6, 0x98, 0x2c, 0xfb, 0x83, 0x68,
	0xae, 0xee, 0x35, 0x95, 0x6a, 0x67, 0xda, 0xf4,
};

} // namespace Safe::TrustRules
