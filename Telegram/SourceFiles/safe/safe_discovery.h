/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_policy.h"

namespace Safe {

// contacts.search answer with the public `results` dropped and `my_results`
// cut down to users and to chats the account is in.
[[nodiscard]] MTPcontacts_Found KeepOwnResults(const MTPcontacts_Found &found);

template <typename Key, typename Tab>
void DropDiscoveryTabs(std::vector<Key> &keys, std::initializer_list<Tab> tabs) {
	if (DiscoveryAllowed()) {
		return;
	}
	keys.erase(ranges::remove_if(keys, [&](const Key &key) {
		return ranges::contains(tabs, key.tab);
	}), end(keys));
}

} // namespace Safe
