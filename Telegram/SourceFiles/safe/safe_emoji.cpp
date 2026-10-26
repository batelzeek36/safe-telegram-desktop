/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_emoji.h"

#include "base/weak_ptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_emoji_statuses.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_stickers_set.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "safe/safe_media.h"
#include "safe/safe_media_covers.h"
#include "safe/safe_media_gate.h"
#include "safe/safe_media_paint.h"
#include "safe/safe_trust.h"
#include "safe/safe_words.h"

namespace Safe {
namespace {

using SetFlag = Data::StickersSetFlag;

[[nodiscard]] bool FreeSet(
		not_null<Data::Session*> owner,
		const StickerSetIdentifier &identifier) {
	if (!identifier.id) {
		return false;
	}
	const auto &sets = owner->stickers().sets();
	const auto i = sets.find(identifier.id);
	if (i == end(sets)) {
		return false;
	}
	const auto flags = i->second->flags;
	return (flags & SetFlag::Installed)
		|| (flags & SetFlag::Archived)
		|| (flags & SetFlag::Official);
}

[[nodiscard]] bool InRecentEmoji(DocumentId id) {
	const auto &recent = Core::App().settings().recentEmoji();
	return ranges::any_of(recent, [&](const Core::RecentEmoji &entry) {
		const auto document = std::get_if<Core::RecentEmojiDocument>(
			&entry.id.data);
		return document && (document->id == id);
	});
}

[[nodiscard]] bool InReactions(
		not_null<Data::Session*> owner,
		DocumentId id) {
	using Type = Data::Reactions::Type;
	const auto &reactions = owner->reactions();
	const auto has = [&](Type type) {
		return ranges::any_of(
			reactions.list(type),
			[&](const Data::Reaction &reaction) {
				return reaction.id.custom() == id;
			});
	};
	return has(Type::Recent) || has(Type::Top);
}

[[nodiscard]] bool InStatuses(
		not_null<Data::Session*> owner,
		DocumentId id) {
	using Type = Data::EmojiStatuses::Type;
	const auto &statuses = owner->emojiStatuses();
	const auto has = [&](Type type) {
		return ranges::any_of(
			statuses.list(type),
			[&](const EmojiStatusId &status) {
				return status.documentId == id;
			});
	};
	return has(Type::Recent)
		|| has(Type::Default)
		|| has(Type::Colored)
		|| has(Type::ChannelDefault)
		|| has(Type::ChannelColored);
}

// His own use, by id (the document itself may not be loaded yet).
[[nodiscard]] bool IsMine(not_null<Data::Session*> owner, DocumentId id) {
	return InRecentEmoji(id) || InReactions(owner, id) || InStatuses(owner, id);
}

[[nodiscard]] bool MyReaction(
		not_null<const HistoryItem*> item,
		DocumentId id) {
	const auto &list = item->reactions();
	return ranges::any_of(list, [&](const Data::MessageReaction &reaction) {
		return reaction.my && (reaction.id.custom() == id);
	});
}

// Judge rule for one custom emoji of a message in an Allowed chat.
[[nodiscard]] bool HoldsJudged(not_null<DocumentData*> document) {
	if (IsFreeEmoji(document)) {
		return false;
	}
	const auto key = KeyOf(document);
	const auto verdict = Verdict(key);
	if (verdict == VerdictState::Waiting) {
		Covers::WantEmoji(document);
	}
	return (verdict != VerdictState::Clear);
}

} // namespace

bool IsGuardedEmojiSearch(const std::vector<QString> &query) {
	auto text = QString();
	for (const auto &word : query) {
		if (!text.isEmpty()) {
			text.append(QChar(' '));
		}
		text.append(word);
	}
	return IsGuardedQuery(text.trimmed());
}

bool IsFreeEmoji(const DocumentData *document) {
	if (!document || !document->id) {
		return false;
	}
	const auto owner = &document->owner();
	if (const auto sticker = document->sticker()) {
		if (FreeSet(owner, sticker->set)) {
			return true;
		}
	}
	return IsMine(owner, document->id);
}

bool HoldsEmoji(const DocumentData *document) {
	if (!document || IsFreeEmoji(document)) {
		return false;
	}
	return Verdict(KeyOf(document)) != VerdictState::Clear;
}

bool DropsEmoji(not_null<const DocumentData*> document) {
	return (Verdict(KeyOf(document)) == VerdictState::Flagged)
		&& !IsFreeEmoji(document);
}

bool DropsEmojiSet(not_null<const Data::StickersSet*> set) {
	if ((set->flags & SetFlag::Installed)
		|| (set->flags & SetFlag::Archived)
		|| (set->flags & SetFlag::Official)) {
		return false;
	}
	return IsGuardedQuery(set->title)
		|| IsGuardedQuery(set->shortName);
}

bool PaintHeldEmoji(QPainter &p, QRect rect, DocumentData *document) {
	if (!document || !HoldsEmoji(document)) {
		return false;
	}
	if (Verdict(KeyOf(document)) == VerdictState::Waiting) {
		Covers::WantEmoji(document);
	}
	const auto side = std::min(rect.width(), rect.height());
	PaintFog(p, rect, HoldShape::Rounded(side / 4));
	return true;
}

bool HoldsMessageEmoji(not_null<const HistoryItem*> item, DocumentId id) {
	if (!id) {
		return false;
	}
	switch (OutcomeOf(item)) {
	case MediaOutcome::Show: return false;
	case MediaOutcome::Hide: return true;
	case MediaOutcome::Judge: break;
	}
	return HoldsJudged(item->history()->owner().document(id));
}

Fn<bool(DocumentId)> ReactionEmojiHolds(not_null<const HistoryItem*> item) {
	const auto chat = item->history()->peer.get();
	if (TierOf(chat) != Tier::Allowed) {
		return nullptr;
	}
	const auto session = &item->history()->session();
	const auto weak = base::make_weak(session);
	const auto id = item->fullId();
	return [=](DocumentId documentId) {
		const auto strong = weak.get();
		const auto found = strong ? strong->data().message(id) : nullptr;
		if (!found) {
			return true;
		} else if (!documentId || MyReaction(found, documentId)) {
			return false;
		}
		return HoldsJudged(strong->data().document(documentId));
	};
}

} // namespace Safe
