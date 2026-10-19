/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_reactions.h"

#include "base/call_delayed.h"
#include "base/weak_ptr.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_message_reactions.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "ui/effects/reaction_fly_animation.h"

namespace Safe {
namespace {

// How long an unread reaction waits for its files before it plays with
// whatever is there (the stock behaviour). Long enough for the first
// download on a slow link, short enough that it still reads as "just now".
constexpr auto kWaitForFiles = crl::time(6000);

struct Pending {
	base::weak_ptr<HistoryView::Element> view;
	Data::ReactionId id;
	crl::time until = 0;
};

using PendingMap = base::flat_map<
	not_null<Main::Session*>,
	std::vector<Pending>>;

[[nodiscard]] PendingMap &AllPending() {
	static auto result = PendingMap();
	return result;
}

[[nodiscard]] bool Loaded(DocumentData *document) {
	if (!document) {
		return true;
	}
	const auto media = document->activeMediaView();
	return media && media->loaded();
}

// What Ui::ReactionFlyAnimation needs in memory to play: the center icon (or
// the select animation standing in for it) and the burst around it. Custom
// emoji reactions draw their own emoji and pick a loaded generic burst;
// paid reactions use files bundled with the app.
[[nodiscard]] bool FilesReady(
		not_null<Data::Reactions*> reactions,
		const Data::ReactionId &id) {
	if (id.custom() || id.paid()) {
		return true;
	}
	const auto &list = reactions->list(Data::Reactions::Type::All);
	const auto i = ranges::find(list, id, &Data::Reaction::id);
	if (i == end(list)) {
		return false; // The reactions list itself has not arrived yet.
	}
	const auto center = i->centerIcon
		? i->centerIcon
		: i->selectAnimation.get();
	return Loaded(center) && Loaded(i->aroundAnimation);
}

void Play(const Pending &entry) {
	if (const auto view = entry.view.get()) {
		view->animateReaction({ .id = entry.id });
	}
}

void CheckPending(not_null<Main::Session*> session) {
	auto &all = AllPending();
	const auto i = all.find(session);
	if (i == end(all) || i->second.empty()) {
		return;
	}
	const auto reactions = &session->data().reactions();
	const auto now = crl::now();
	auto ready = std::vector<Pending>();
	auto &list = i->second;
	for (auto j = begin(list); j != end(list);) {
		if (!j->view) {
			j = list.erase(j);
		} else if (j->until <= now || FilesReady(reactions, j->id)) {
			ready.push_back(std::move(*j));
			j = list.erase(j);
		} else {
			++j;
		}
	}
	// Played after the list is settled: animateReaction only repaints, but
	// nothing here may depend on that.
	for (const auto &entry : ready) {
		Play(entry);
	}
}

[[nodiscard]] std::vector<Pending> &PendingFor(
		not_null<Main::Session*> session) {
	auto &all = AllPending();
	if (const auto i = all.find(session); i != end(all)) {
		return i->second;
	}
	// One watcher per session, for the session's whole life: every finished
	// download (network or local cache) and every new reactions list can
	// make a waiting reaction ready.
	session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		CheckPending(session);
	}, session->lifetime());
	session->data().reactions().defaultUpdates(
	) | rpl::on_next([=] {
		CheckPending(session);
	}, session->lifetime());
	session->lifetime().add([=] {
		AllPending().remove(session);
	});
	return all.emplace(session, std::vector<Pending>()).first->second;
}

} // namespace

void PreloadStickerMedia(const base::flat_map<
		not_null<DocumentData*>,
		std::shared_ptr<Data::DocumentMedia>> &cache) {
	for (const auto &[document, media] : cache) {
		media->checkStickerLarge();
	}
}

void AnimateUnreadReaction(
		not_null<HistoryView::Element*> view,
		const Data::ReactionId &id) {
	const auto session = &view->history()->session();
	const auto reactions = &session->data().reactions();
	if (FilesReady(reactions, id)) {
		view->animateReaction({ .id = id });
		return;
	}
	// Start whatever is missing: the center icon (kept by the reactions
	// image cache) and the burst (kept by the reactions icon cache).
	reactions->preloadReactionImageFor(id);
	reactions->preloadAnimationsFor(id);
	PendingFor(session).push_back({
		.view = base::make_weak(view),
		.id = id,
		.until = crl::now() + kWaitForFiles,
	});
	base::call_delayed(kWaitForFiles, base::make_weak(session), [=] {
		CheckPending(session);
	});
}

} // namespace Safe
