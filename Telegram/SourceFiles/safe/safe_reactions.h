/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flat_map.h"

class DocumentData;

namespace Data {
class DocumentMedia;
struct ReactionId;
} // namespace Data

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Safe {

// Reaction animations play like the native app's (Zach, 2026-10-19: "the
// desktop app isnt showing emoji reaction animations").
//
// Telegram Desktop starts loading a reaction's animation files only when its
// pill is hovered or the reaction picker opens, and a reaction animation
// (Ui::ReactionFlyAnimation) that finds its files not yet in memory is
// dropped on the spot: no pop, no burst. The native app waits for the files
// and then plays. Two hooks close the gap:

// 1. Load every file in the cache as soon as the list arrives (Telegram's own
// reaction animations: picker appear / hover, center icon and burst of every
// active reaction, about 6 MB once and the local cache after that; and the
// generic bursts used for custom emoji reactions).
void PreloadStickerMedia(const base::flat_map<
	not_null<DocumentData*>,
	std::shared_ptr<Data::DocumentMedia>> &cache);

// 2. An unread reaction to his message plays once, when it is first seen.
// When its files are not in memory yet, wait for them (a few seconds at
// most) and play then, instead of losing it. Plays through the view's own
// reaction strip, so whatever the media gate keeps out of the strip (custom
// emoji reactions from strangers, d14) has nothing to play.
void AnimateUnreadReaction(
	not_null<HistoryView::Element*> view,
	const Data::ReactionId &id);

} // namespace Safe
