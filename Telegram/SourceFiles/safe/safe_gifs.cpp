/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_gifs.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_user.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "inline_bots/inline_bot_result.h"
#include "main/main_session.h"
#include "mtproto/mtproto_config.h"
#include "safe/safe_bots.h"
#include "safe/safe_media.h"
#include "safe/safe_media_covers.h"
#include "safe/safe_media_gate.h"
#include "safe/safe_media_paint.h"
#include "safe/safe_tenor.h"
#include "safe/safe_words.h"

namespace Safe {
namespace {

[[nodiscard]] bool IsGifBot(not_null<UserData*> bot) {
	const auto &configured = bot->session().serverConfig().gifSearchUsername;
	return !configured.isEmpty()
		&& !bot->username().compare(configured, Qt::CaseInsensitive);
}

// Tenor's post id for a Tenor tile (its web document id is new each time).
[[nodiscard]] MediaKey KeyFor(
		not_null<const InlineBots::Layout::ItemBase*> item) {
	if (const auto result = item->getResult()) {
		if (const auto key = Tenor::KeyOf(result.get())) {
			return key;
		}
	}
	const auto document = item->getPreviewDocument();
	return document ? KeyOf(document) : MediaKey();
}

[[nodiscard]] HeldLook LookOf(MediaKey key, bool requestable) {
	if (Verdict(key) == VerdictState::Flagged) {
		return HeldLook::Flagged;
	}
	return (JudgeAvailable() && requestable)
		? HeldLook::Checking
		: HeldLook::Waiting;
}

[[nodiscard]] Image *InlineThumbnail(not_null<DocumentData*> document) {
	const auto view = document->activeMediaView();
	return view ? view->thumbnailInline() : nullptr;
}

} // namespace

bool AllowsGifSearch(not_null<UserData*> bot, const QString &query) {
	return !IsGuardedQuery(query) && (IsGifBot(bot) || IsOpenBot(bot));
}

bool HoldsGifTile(const InlineBots::Layout::ItemBase *item) {
	if (!item || !item->getResult()) {
		return false;
	}
	const auto key = KeyFor(item);
	return !key || (Verdict(key) != VerdictState::Clear);
}

bool PaintHeldGifTile(
		QPainter &p,
		not_null<const InlineBots::Layout::ItemBase*> item) {
	if (!HoldsGifTile(item)) {
		return false;
	}
	const auto rect = QRect(0, 0, item->width(), item->height());
	const auto shape = HoldShape::Small();
	const auto document = item->getPreviewDocument();
	const auto key = KeyFor(item);
	if (!document || !key) {
		PaintFog(p, rect, shape);
		return true;
	}
	const auto requestable = (Verdict(key) == VerdictState::Waiting)
		&& Covers::WantSearchedGif(document, key);
	PaintJudged(
		p,
		rect,
		shape,
		LookOf(key, requestable),
		key.id,
		InlineThumbnail(document));
	return true;
}

} // namespace Safe
