/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_auto_box.h"

#include "data/data_peer.h"
#include "safe/safe_auto.h"
#include "safe/safe_auto_mark.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/toast/toast.h"
#include "ui/vertical_list.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Safe::Auto {
namespace {

constexpr auto kMenuIcon = 24;
constexpr auto kMenuDisc = 19;

enum class Mode : uchar {
	On,
	Off,
};

// The menu row with full-auto's own mark where the icon goes.
class DiscAction final : public Ui::Menu::Action {
public:
	using Action::Action;

protected:
	void paintEvent(QPaintEvent *e) override;

};

void DiscAction::paintEvent(QPaintEvent *e) {
	Action::paintEvent(e);
	auto p = QPainter(this);
	const auto box = QRectF(
		st().itemIconPosition,
		QSizeF(style::ConvertScale(kMenuIcon), style::ConvertScale(kMenuIcon)));
	PaintDisc(p, box, style::ConvertScale(kMenuDisc), VioletColor());
}

void Apply(not_null<PeerData*> peer, Change change) {
	Manager::Instance().change(peer, std::move(change), [](bool ok) {
		if (!ok) {
			Ui::Toast::Show(
				u"Couldn't reach full-auto. Try again in a moment."_q);
		}
	});
}

} // namespace

void AddMenuAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Ui::Menu::MenuCallback &addAction) {
	const auto chat = RunnerChatId(peer);
	const auto entry = chat ? Manager::Instance().entry(chat) : nullptr;
	if (!entry || !entry->known || !entry->state) {
		return;
	}
	const auto text = entry->state->enabled
		? u"Full-auto: On"_q
		: u"Full-auto: Off"_q;
	const auto open = [=] {
		controller->show(Box(SettingsBox, peer));
	};
	addAction({
		.text = text,
		.handler = open,
		.make = [=](not_null<Ui::PopupMenu*> menu) {
			return base::make_unique_q<DiscAction>(
				menu->menu(),
				menu->st().menu,
				Ui::Menu::CreateAction(menu->menu().get(), text, open),
				nullptr,
				nullptr);
		},
	});
}

void SettingsBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	const auto chat = RunnerChatId(peer);
	const auto entry = Manager::Instance().entry(chat);
	const auto state = (entry && entry->state)
		? *entry->state
		: State{ .chat = chat };

	box->setTitle(rpl::single(u"Full-auto in this chat"_q));
	const auto container = box->verticalLayout();
	Ui::AddSkip(container);

	const auto mode = std::make_shared<Ui::RadioenumGroup<Mode>>(
		state.enabled ? Mode::On : Mode::Off);
	// Labels wrap instead of ending in "..." (Zach 10-27: "i cant read
	// this - it gets cut off").
	const auto addMode = [&](Mode value, const QString &title) {
		container->add(
			object_ptr<Ui::Radioenum<Mode>>(
				container,
				mode,
				value,
				title,
				st::settingsPrivacyOption),
			st::settingsSendTypePadding)->setAllowTextLines();
	};
	addMode(Mode::On, u"On: reacts and replies as you"_q);
	addMode(Mode::Off, u"Off: you answer everything yourself"_q);

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(u"Pace"_q));
	const auto pace = std::make_shared<Ui::RadioenumGroup<Pace>>(state.pace);
	auto paceRows = std::vector<not_null<Ui::Radioenum<Pace>*>>();
	const auto addPace = [&](Pace value, const QString &title) {
		paceRows.push_back(container->add(
			object_ptr<Ui::Radioenum<Pace>>(
				container,
				pace,
				value,
				title,
				st::settingsPrivacyOption),
			st::settingsSendTypePadding));
		paceRows.back()->setAllowTextLines();
	};
	addPace(Pace::LikeMe, u"Like me: reacts within 1 min, answers within 3"_q);
	addPace(Pace::Calmer, u"Calmer: reacts within 3 min, answers about a third within 6"_q);
	addPace(Pace::Slow, u"Slow: mostly reactions within 10 min, answers within 15"_q);
	const auto dimPace = [=](Mode value) {
		for (const auto row : paceRows) {
			row->setDisabled(value == Mode::Off);
		}
	};
	dimPace(mode->current());
	mode->setChangedCallback(dimPace);

	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(u"Anything only you can answer comes back to you, "
			"with a notification."_q));

	box->addButton(rpl::single(u"Done"_q), [=] {
		const auto enabled = (mode->current() == Mode::On);
		auto change = Change();
		if (enabled != state.enabled) {
			change.enabled = enabled;
		}
		if (enabled && pace->current() != state.pace) {
			change.pace = pace->current();
		}
		box->closeBox();
		if (change.enabled || change.pace) {
			Apply(peer, std::move(change));
		}
	});
	box->addButton(rpl::single(u"Cancel"_q), [=] { box->closeBox(); });
}

} // namespace Safe::Auto
