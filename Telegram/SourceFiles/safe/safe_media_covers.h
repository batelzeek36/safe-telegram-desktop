/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "safe/safe_media.h"

class HistoryItem;
class PeerData;
class PhotoData;
class DocumentData;

namespace Safe::Covers {

// The media gate's side of the picture check: it fetches the small cover
// Telegram decodes for the bubble anyway (never the full picture), queues
// one check per key, and repaints what shows a key when its answer lands
// or when the trust lists change. Main thread only.

// This message draws a held picture with this key: repaint it on an answer.
void Remember(not_null<const HistoryItem*> item, MediaKey key);
// An avatar or anything else that is not a message: a full repaint follows
// every answer, so nothing needs remembering for those.

// Queue a Judge picture. False when there is nothing to send (no key for
// the check, no media key, or no thumbnail at all), so it says "Waiting".
bool WantPhoto(not_null<const HistoryItem*> item, not_null<PhotoData*> photo);
bool WantDocument(
	not_null<const HistoryItem*> item,
	not_null<DocumentData*> document);
bool WantUserpic(not_null<const PeerData*> peer, MediaKey key);

} // namespace Safe::Covers
