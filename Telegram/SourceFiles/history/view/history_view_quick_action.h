/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {

enum class DoubleClickQuickAction {
	Reply, // Telegram Desktop's default (Safe Telegram: React).
	React,
	None,
};

[[nodiscard]] DoubleClickQuickAction CurrentQuickAction();

} // namespace HistoryView
