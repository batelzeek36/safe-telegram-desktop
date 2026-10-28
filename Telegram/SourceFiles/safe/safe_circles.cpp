/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_circles.h"

#include "safe/safe_circles_settings.h"
#include "safe/safe_circles_view.h"
#include "ui/rp_widget.h"
#include "window/window_session_controller.h"

namespace Safe::Circles {
namespace {

struct Entry {
	not_null<Ui::RpWidget*> list;
	not_null<Window::SessionController*> controller;
	Host host;
	base::unique_qptr<View> view;
	Layout layout = Layout::List;
	bool tabsHidden = false;
	bool syncing = false;
	bool introPending = false;
};

[[nodiscard]] base::flat_map<not_null<QWidget*>, std::unique_ptr<Entry>> &Entries() {
	static auto result = base::flat_map<
		not_null<QWidget*>,
		std::unique_ptr<Entry>>();
	return result;
}

[[nodiscard]] Entry *Find(not_null<QWidget*> list) {
	auto &entries = Entries();
	const auto i = entries.find(list);
	return (i == end(entries)) ? nullptr : i->second.get();
}

// Hiding is repeated on every sync: Telegram creates its folder tabs when
// the folders arrive and shows them itself. Showing them again happens once.
void SetTabsHidden(Entry &entry, bool hidden) {
	if (!entry.host.hideFolderTabs) {
		return;
	} else if (hidden || entry.tabsHidden) {
		entry.tabsHidden = hidden;
		entry.host.hideFolderTabs(hidden);
	}
}

void SyncEntry(Entry &entry) {
	if (entry.syncing) {
		return;
	}
	entry.syncing = true;
	const auto guard = gsl::finally([&] { entry.syncing = false; });

	const auto layout = CurrentLayout();
	const auto wanted = (layout != Layout::List) && entry.host.shown();
	if (layout == Layout::List) {
		entry.view = nullptr;
	}
	if (!wanted) {
		if (entry.view) {
			entry.view->hide();
		}
		SetTabsHidden(entry, false);
		return;
	}
	if (!entry.view) {
		const auto raw = &entry;
		entry.view = base::make_unique_q<View>(
			entry.list.get(),
			entry.controller,
			[=](not_null<History*> history) { raw->host.open(history); });
	}
	entry.view->setLayout(layout);
	entry.view->setGeometry(entry.host.area());
	SetTabsHidden(entry, true);
	if (entry.view->isHidden()) {
		entry.view->show();
	}
	entry.view->raise();
	if (entry.host.raiseOverlays) {
		entry.host.raiseOverlays();
	}
	if (std::exchange(entry.introPending, false)) {
		entry.view->intro();
	}
}

} // namespace

void Attach(
		not_null<Ui::RpWidget*> list,
		not_null<Window::SessionController*> controller,
		Host host) {
	auto &entries = Entries();
	if (entries.contains(list)) {
		return;
	}
	auto entry = std::make_unique<Entry>(Entry{
		.list = list,
		.controller = controller,
		.host = std::move(host),
		.layout = CurrentLayout(),
	});
	const auto raw = entry.get();
	entries.emplace(list, std::move(entry));

	LayoutValue(
	) | rpl::on_next([=](Layout layout) {
		const auto was = std::exchange(raw->layout, layout);
		if (was != layout && layout != Layout::List) {
			raw->introPending = true;
		}
		SyncEntry(*raw);
	}, list->lifetime());

	list->lifetime().add([=] {
		Entries().remove(list);
	});
}

void Sync(not_null<QWidget*> list) {
	if (const auto entry = Find(list)) {
		SyncEntry(*entry);
	}
}

bool HandleKey(not_null<QWidget*> list, not_null<QKeyEvent*> e) {
	const auto entry = Find(list);
	if (!entry || !entry->view || entry->view->isHidden()) {
		return false;
	}
	return entry->view->handleKey(e);
}

} // namespace Safe::Circles
