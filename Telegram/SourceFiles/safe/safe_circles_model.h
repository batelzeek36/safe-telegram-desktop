/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "dialogs/dialogs_common.h"
#include "ui/image/image_location.h"
#include "ui/text/text.h"
#include "ui/userpic_view.h"

class History;
class PeerData;

namespace Main {
class Session;
} // namespace Main

// What the chat circles show, read from Telegram's own chat lists: the
// circles (his folders plus All, in Telegram's order), the chats of each in
// the chat list's order (pinned first), and per chat the face (profile
// photo), names, the last message line, its time and the unread badges.
// Everything follows the session: new messages, reorders, renames, photos
// that finish loading, folders added or removed.
namespace Safe::Circles {

struct CircleUnread {
	int count = 0;
	bool muted = false;
};

class Model final : public base::has_weak_ptr {
public:
	explicit Model(not_null<Main::Session*> session);
	~Model();

	[[nodiscard]] not_null<Main::Session*> session() const;

	// Folder ids; 0 is All.
	[[nodiscard]] const std::vector<FilterId> &circles();
	[[nodiscard]] const Ui::Text::String &circleTitle(FilterId id);
	[[nodiscard]] CircleUnread circleUnread(FilterId id) const;

	// The chats of a circle in the chat list's order (archive and other
	// folder rows are left out), and whether the list is still loading.
	[[nodiscard]] const std::vector<not_null<History*>> &chats(FilterId id);
	[[nodiscard]] bool fullyLoaded(FilterId id) const;
	void requestMore(FilterId id);

	// The circles themselves changed (a folder added, removed or renamed).
	[[nodiscard]] rpl::producer<> circlesChanges() const;
	// The chats of some circle changed order or membership.
	[[nodiscard]] rpl::producer<> orderChanges() const;
	// Something a face or a preview shows changed: repaint.
	[[nodiscard]] rpl::producer<> repaints() const;

	// The face: a circle image `size` device pixels across (Saved Messages
	// and Replies get Telegram's own drawings), kept until the photo
	// changes. Starts the photo's download when needed.
	[[nodiscard]] const QImage &face(not_null<History*> history, int size);

	// The chat's name as the chat list shows it, and the short one (first
	// name) for the label under a face.
	[[nodiscard]] const Ui::Text::String &name(not_null<History*> history);
	[[nodiscard]] const Ui::Text::String &shortName(
		not_null<History*> history);

	// The last message line ("Michelle: see you at 2"), plain text only:
	// no custom emoji, spoilers dotted out, no media thumbnails.
	[[nodiscard]] const Ui::Text::String &preview(not_null<History*> history);
	[[nodiscard]] QString time(not_null<History*> history) const;
	[[nodiscard]] Dialogs::BadgesState badges(
		not_null<History*> history) const;
	[[nodiscard]] bool pinned(not_null<History*> history, FilterId id) const;

	// A spoken label for the accessibility tree.
	[[nodiscard]] QString accessibleName(not_null<History*> history);

private:
	struct Face {
		Ui::PeerUserpicView view;
		InMemoryKey key;
		int size = 0;
		bool special = false;
		QImage image;
	};
	struct Name {
		int version = -1;
		Ui::Text::String text;
	};
	struct Preview {
		HistoryItem *item = nullptr;
		int generation = -1;
		Ui::Text::String text;
	};

	void setupSubscriptions();
	void markOrderDirty();
	void refreshCircles();
	void refreshChats(FilterId id);
	[[nodiscard]] TextWithEntities previewText(
		not_null<History*> history) const;

	const not_null<Main::Session*> _session;
	std::vector<FilterId> _circles;
	bool _circlesDirty = true;
	base::flat_map<FilterId, Ui::Text::String> _titles;
	base::flat_map<FilterId, CircleUnread> _unread;
	base::flat_map<FilterId, std::vector<not_null<History*>>> _chats;
	base::flat_set<FilterId> _chatsDirty;
	base::flat_map<not_null<PeerData*>, Face> _faces;
	base::flat_map<not_null<History*>, Name> _names;
	base::flat_map<not_null<History*>, Name> _shortNames;
	base::flat_map<not_null<History*>, Preview> _previews;
	int _generation = 0;
	bool _orderPosted = false;

	rpl::event_stream<> _circlesChanges;
	rpl::event_stream<> _orderChanges;
	rpl::event_stream<> _repaints;
	rpl::lifetime _unreadLifetime;
	rpl::lifetime _lifetime;

};

} // namespace Safe::Circles
