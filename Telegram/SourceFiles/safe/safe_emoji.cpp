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

// The day the pack guard began, 2026-10-26 00:00 HST. Zach, 10-26: the
// "Add" button of a fogged pack installed it, and an installed pack showed
// unjudged; a pack added from now on stays judged sticker by sticker.
constexpr auto kChosenBefore = TimeId(1793008800);

[[nodiscard]] bool FreeSet(
		not_null<Data::Session*> owner,
		const StickerSetIdentifier &identifier) {
	if (!identifier.id) {
		return false;
	}
	const auto &sets = owner->stickers().sets();
	const auto i = sets.find(identifier.id);
	return (i != end(sets)) && IsChosenSet(i->second.get());
}

[[nodiscard]] bool InRecentEmoji(DocumentId id) {
	const auto &recent = Core::App().settings().recentEmoji();
	return ranges::any_of(recent, [&](const Core::RecentEmoji &entry) {
		const auto document = std::get_if<Core::RecentEmojiDocument>(
			&entry.id.data);
		return document && (document->id == id);
	});
}

// His recent, top and tag reactions, and every animation of Telegram's own
// standard reactions (the reaction picker shows those by document id).
[[nodiscard]] bool InReactions(
		not_null<Data::Session*> owner,
		DocumentId id) {
	using Type = Data::Reactions::Type;
	const auto &reactions = owner->reactions();
	const auto telegrams = [&](const Data::Reaction &reaction) {
		const auto is = [&](const DocumentData *document) {
			return document && (document->id == id);
		};
		return is(reaction.selectAnimation)
			|| is(reaction.appearAnimation)
			|| is(reaction.centerIcon)
			|| is(reaction.aroundAnimation);
	};
	const auto has = [&](Type type, bool customToo) {
		return ranges::any_of(
			reactions.list(type),
			[&](const Data::Reaction &reaction) {
				if (const auto custom = reaction.id.custom()) {
					return customToo && (custom == id);
				}
				return telegrams(reaction);
			});
	};
	return has(Type::Recent, true)
		|| has(Type::Top, true)
		|| has(Type::MyTags, true)
		|| has(Type::Tags, true)
		|| has(Type::Active, false)
		|| has(Type::All, false);
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
	return IsInFreeSet(document) || IsMine(&document->owner(), document->id);
}

bool IsInFreeSet(const DocumentData *document) {
	if (!document || !document->id) {
		return false;
	}
	const auto sticker = document->sticker();
	return sticker && FreeSet(&document->owner(), sticker->set);
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

bool IsChosenSet(const Data::StickersSet *set) {
	if (!set) {
		return false;
	} else if (set->flags & SetFlag::Official) {
		return true;
	} else if (!(set->flags & SetFlag::Installed)
		&& !(set->flags & SetFlag::Archived)) {
		return false;
	}
	// Telegram dates every installed pack (installed_date); a pack this app
	// installs gets "now", one read from an old local cache without a date
	// gets 1. No date at all: not his choice as far as this app can tell.
	return (set->installDate > 0) && (set->installDate < kChosenBefore);
}

bool DropsEmojiSet(not_null<const Data::StickersSet*> set) {
	if (IsChosenSet(set)) {
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
