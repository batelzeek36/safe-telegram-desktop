/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_stickers.h"

#include "apiwrap.h"
#include "base/weak_ptr.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "safe/safe_emoji.h"
#include "safe/safe_media.h"
#include "safe/safe_media_covers.h"
#include "safe/safe_media_gate.h"
#include "safe/safe_media_paint.h"
#include "safe/safe_words.h"
#include "main/main_session.h"
#include "settings.h"

namespace Safe {
namespace {

// One of the special lists of his own (sent, attached, local custom).
[[nodiscard]] bool InOwnList(
		const Data::Stickers &stickers,
		uint64 setId,
		const DocumentData *document) {
	const auto &sets = stickers.sets();
	const auto i = sets.find(setId);
	return (i != end(sets))
		&& i->second->stickers.contains(const_cast<DocumentData*>(document));
}

[[nodiscard]] QString CheckingText() {
	return u"Checking this pack first. Try Add again in a moment."_q;
}

[[nodiscard]] QString NotAddedText() {
	return u"This pack can't be added."_q;
}

[[nodiscard]] QString NoCheckText() {
	return u"This pack can't be added while the picture check is off."_q;
}

[[nodiscard]] bool OwnOrFree(not_null<const DocumentData*> document) {
	const auto sticker = document->sticker();
	return (sticker && sticker->setType == Data::StickersType::Emoji)
		? IsFreeEmoji(document)
		: IsFreeSticker(document);
}

// Queues the check of every sticker of a pack that is not his; true when one
// is still waiting, false when all are cleared or flagged.
bool WantSetChecks(not_null<const Data::StickersSet*> set) {
	auto waiting = false;
	for (const auto document : set->stickers) {
		if (OwnOrFree(document)) {
			continue;
		} else if (Verdict(KeyOf(document)) == VerdictState::Waiting) {
			Covers::WantEmoji(document);
			waiting = true;
		}
	}
	return waiting;
}

// A pack seen only by its covers: fetched whole, then every sticker queued.
void LoadAndCheckSet(not_null<Data::StickersSet*> set) {
	const auto session = &set->session();
	const auto weak = base::make_weak(session);
	const auto setId = set->id;
	session->api().request(MTPmessages_GetStickerSet(
		set->mtpInput(),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		const auto strong = weak.get();
		if (!strong) {
			return;
		}
		result.match([&](const MTPDmessages_stickerSet &data) {
			strong->data().stickers().feedSetFull(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
		});
		const auto &sets = strong->data().stickers().sets();
		if (const auto i = sets.find(setId); i != end(sets)) {
			WantSetChecks(i->second.get());
		}
	}).send();
}

// Sent by him or saved as a favorite: his own choice.
[[nodiscard]] bool IsOwnSticker(not_null<const DocumentData*> document) {
	const auto &stickers = document->owner().stickers();
	if (stickers.isFaved(document)
		|| InOwnList(stickers, Data::Stickers::CloudRecentSetId, document)
		|| InOwnList(
			stickers,
			Data::Stickers::CloudRecentAttachedSetId,
			document)
		|| InOwnList(stickers, Data::Stickers::CustomSetId, document)) {
		return true;
	}
	const auto &recent = stickers.getRecentPack();
	return ranges::any_of(recent, [&](const auto &entry) {
		return (entry.first == document);
	});
}

} // namespace

bool IsGuardedStickerSearch(const QString &query) {
	return IsGuardedQuery(query.trimmed());
}

bool IsFreeSticker(const DocumentData *document) {
	if (!document || !document->id) {
		return false;
	}
	return IsInFreeSet(document) || IsOwnSticker(document);
}

bool HoldsPanelSticker(const DocumentData *document) {
	if (!document || IsFreeSticker(document)) {
		return false;
	}
	return Verdict(KeyOf(document)) != VerdictState::Clear;
}

bool DropsPanelSticker(not_null<const DocumentData*> document) {
	return (Verdict(KeyOf(document)) == VerdictState::Flagged)
		&& !IsFreeSticker(document);
}

bool DropsStickerSet(not_null<const Data::StickersSet*> set) {
	// Same rule as a found emoji pack: a pack he did not choose whose own
	// words are guarded.
	return DropsEmojiSet(set);
}

bool PaintHeldPanelSticker(QPainter &p, QRect rect, DocumentData *document) {
	if (!document || !HoldsPanelSticker(document)) {
		return false;
	}
	if (Verdict(KeyOf(document)) == VerdictState::Waiting) {
		// Judged like a custom emoji: its WebP thumbnail (a static sticker
		// with none by its own picture), enlarged on grey, with the pack
		// title and the emoji it stands for as untrusted text.
		Covers::WantEmoji(document);
	}
	// The look of the sticker pack preview of a pack he has not installed
	// (PaintHeldSetSticker): the same foggy glass, the same corners.
	PaintFog(
		p,
		rect,
		HoldShape::Rounded(std::min(rect.width(), rect.height()) / 8));
	return true;
}

bool PaintHeldSetCover(
		QPainter &p,
		QRect rect,
		not_null<const Data::StickersSet*> set,
		DocumentData *cover) {
	if (IsChosenSet(set)) {
		return false;
	} else if (cover && PaintHeldPanelSticker(p, rect, cover)) {
		return true;
	} else if (cover) {
		return false;
	}
	PaintFog(
		p,
		rect,
		HoldShape::Rounded(std::min(rect.width(), rect.height()) / 8));
	return true;
}

bool HoldsBoxSticker(bool notInstalled, const DocumentData *document) {
	return HidesStickerSetPreview(notInstalled)
		|| HoldsPanelSticker(document);
}

bool PaintHeldBoxSticker(
		QPainter &p,
		bool notInstalled,
		DocumentData *document,
		QRect rect) {
	return PaintHeldSetSticker(p, notInstalled, rect)
		|| PaintHeldPanelSticker(p, rect, document);
}

bool RefuseSetInstall(
		not_null<Data::StickersSet*> set,
		Fn<void(const QString &)> notice) {
	if (IsChosenSet(set)) {
		return false;
	}
	const auto refuse = [&](const QString &text) {
		if (notice) {
			notice(text);
		}
		return true;
	};
	if (IsGuardedQuery(set->title) || IsGuardedQuery(set->shortName)) {
		return refuse(NotAddedText());
	} else if (!JudgeAvailable()) {
		return refuse(NoCheckText());
	} else if ((set->flags & Data::StickersSetFlag::NotLoaded)
		|| set->stickers.empty()) {
		LoadAndCheckSet(set);
		return refuse(CheckingText());
	}
	for (const auto document : set->stickers) {
		if (!OwnOrFree(document)
			&& Verdict(KeyOf(document)) == VerdictState::Flagged) {
			return refuse(NotAddedText());
		}
	}
	return WantSetChecks(set) ? refuse(CheckingText()) : false;
}

} // namespace Safe
