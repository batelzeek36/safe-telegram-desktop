/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_box.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "safe/safe_calm.h"
#include "safe/safe_calm_leaf.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Safe::Calm {
namespace {

constexpr auto kHeroLeaf = 28;

class Hero final : public Ui::RpWidget {
public:
	Hero(QWidget *parent, QString title, QString subtitle);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const QString _title;
	Ui::Text::String _subtitle;

};

Hero::Hero(QWidget *parent, QString title, QString subtitle)
: RpWidget(parent)
, _title(std::move(title))
, _subtitle(st::defaultTextStyle, subtitle) {
}

int Hero::resizeGetHeight(int newWidth) {
	const auto padding = st::boxRowPadding;
	const auto left = padding.left()
		+ style::ConvertScale(kHeroLeaf)
		+ style::ConvertScale(12);
	const auto textWidth = newWidth - left - padding.right();
	return style::ConvertScale(10)
		+ st::semiboldFont->height
		+ style::ConvertScale(2)
		+ _subtitle.countHeight(std::max(textWidth, 1))
		+ style::ConvertScale(10);
}

void Hero::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto padding = st::boxRowPadding;
	const auto leaf = style::ConvertScale(kHeroLeaf);
	const auto top = style::ConvertScale(10);
	PaintLeaf(
		p,
		QRectF(padding.left(), top + style::ConvertScale(2), leaf, leaf),
		leaf,
		JadeColor());
	const auto left = padding.left() + leaf + style::ConvertScale(12);
	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg);
	p.drawText(left, top + st::semiboldFont->ascent, _title);
	p.setPen(st::windowSubTextFg);
	_subtitle.draw(p, {
		.position = {
			left,
			top + st::semiboldFont->height + style::ConvertScale(2),
		},
		.outerWidth = width(),
		.availableWidth = width() - left - padding.right(),
	});
}

[[nodiscard]] QString RuleText(ChatRule rule) {
	switch (rule) {
	case ChatRule::Default: return u"Default"_q;
	case ChatRule::Always: return u"Always"_q;
	case ChatRule::Off: return u"Off"_q;
	}
	Unexpected("Safe::Calm::ChatRule in RuleText.");
}

void AddRadio(
		not_null<Ui::VerticalLayout*> container,
		const std::shared_ptr<Ui::RadioenumGroup<SendMode>> &group,
		SendMode value,
		const QString &title) {
	container->add(
		object_ptr<Ui::Radioenum<SendMode>>(
			container,
			group,
			value,
			title,
			st::settingsPrivacyOption),
		st::settingsSendTypePadding);
}

void AddChatRows(
		not_null<Ui::VerticalLayout*> container,
		not_null<Main::Session*> session) {
	auto any = false;
	for (const auto &[peerId, entry] : Current().chats) {
		const auto peer = session->data().peerLoaded(peerId);
		if (!peer || entry.rule == ChatRule::Default) {
			continue;
		}
		any = true;
		const auto button = container->add(object_ptr<Ui::SettingsButton>(
			container,
			rpl::single(peer->name()),
			st::settingsButtonNoIcon));
		const auto label = Ui::CreateChild<Ui::FlatLabel>(
			button,
			RuleText(entry.rule),
			st::boxDividerLabel);
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
		button->sizeValue() | rpl::on_next([=](QSize size) {
			label->moveToRight(
				st::settingsButtonNoIcon.padding.right(),
				(size.height() - label->height()) / 2);
		}, label->lifetime());
		button->setClickedCallback([=] {
			const auto now = RuleFor(peer->id);
			const auto next = (now == ChatRule::Always)
				? ChatRule::Off
				: ChatRule::Always;
			SetRule(peer->id, next);
			label->setText(RuleText(next));
		});
	}
	if (!any) {
		Ui::AddDividerText(
			container,
			rpl::single(u"No chat has its own rule yet. Set one from a "
				"chat's menu: Calm mode."_q));
	}
}

} // namespace

void SettingsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session) {
	box->setTitle(rpl::single(u"Calm mode"_q));
	box->setWidth(st::boxWideWidth);
	box->addButton(rpl::single(u"Done"_q), [=] { box->closeBox(); });

	const auto container = box->verticalLayout();
	container->add(object_ptr<Hero>(
		container,
		u"Say it hot. Send it calm."_q,
		u"Crash-outs get softened before anyone reads them."_q));

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(u"When you hit Send"_q));
	const auto group = std::make_shared<Ui::RadioenumGroup<SendMode>>(
		Current().send);
	group->setChangedCallback([=](SendMode mode) {
		Update([&](Settings &settings) { settings.send = mode; });
	});
	AddRadio(container, group, SendMode::Off, u"Off: messages go out exactly as typed"_q);
	AddRadio(container, group, SendMode::Review, u"Second look: the calm version waits for one tap"_q);
	AddRadio(container, group, SendMode::Autopilot, u"Autopilot: hot messages go out calm, 4 s to undo"_q);
	Ui::AddSkip(container);
	Ui::AddDivider(container);

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(u"While you type"_q));
	container->add(object_ptr<Ui::SettingsButton>(
		container,
		rpl::single(u"Leaf in the bar"_q),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(Current().typing))->toggledChanges(
	) | rpl::on_next([=](bool on) {
		Update([&](Settings &settings) { settings.typing = on; });
	}, container->lifetime());
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(u"The leaf warms up when a draft runs hot. Click it to "
			"see what reads hot and the calm version."_q));

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(u"Messages to you"_q));
	container->add(object_ptr<Ui::SettingsButton>(
		container,
		rpl::single(u"Soften crash-outs to you"_q),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(Current().incoming))->toggledChanges(
	) | rpl::on_next([=](bool on) {
		Update([&](Settings &settings) { settings.incoming = on; });
	}, container->lifetime());
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(u"In one-on-one chats you only ever see the calm "
			"version, with a small leaf by the time. There is no way to open "
			"the original. Set a group to Always to include it."_q));

	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, rpl::single(u"Chats"_q));
	AddChatRows(container, session);
	Ui::AddSkip(container);
	Ui::AddDividerText(
		container,
		rpl::single(u"Rewrites run on your own Claude account through "
			"MAINGEAR. The other person only ever gets the calm version."_q));
}

rpl::producer<QString> SendModeLabel() {
	return rpl::single(rpl::empty) | rpl::then(
		Changes()
	) | rpl::map([] {
		switch (Current().send) {
		case SendMode::Off: return u"Off"_q;
		case SendMode::Review: return u"Second look"_q;
		case SendMode::Autopilot: return u"Autopilot"_q;
		}
		Unexpected("Safe::Calm::SendMode in SendModeLabel.");
	});
}

QString ChatRuleLabel(not_null<PeerData*> peer) {
	return u"Calm mode: "_q + RuleText(RuleFor(peer->id));
}

void ChatRuleBox(not_null<Ui::GenericBox*> box, not_null<PeerData*> peer) {
	box->setTitle(rpl::single(u"Calm mode in this chat"_q));
	box->addButton(rpl::single(u"Done"_q), [=] { box->closeBox(); });
	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	const auto group = std::make_shared<Ui::RadioenumGroup<ChatRule>>(
		RuleFor(peer->id));
	group->setChangedCallback([=](ChatRule rule) {
		SetRule(peer->id, rule);
	});
	const auto add = [&](ChatRule rule, const QString &title) {
		container->add(
			object_ptr<Ui::Radioenum<ChatRule>>(
				container,
				group,
				rule,
				title,
				st::settingsPrivacyOption),
			st::settingsSendTypePadding);
	};
	add(ChatRule::Default, u"Default: follow Calm mode's settings"_q);
	add(ChatRule::Always, u"Always: check everything in this chat"_q);
	add(ChatRule::Off, u"Off: no Calm in this chat"_q);
	Ui::AddSkip(container);
}

void WhatYouTypedBox(
		not_null<Ui::GenericBox*> box,
		not_null<HistoryItem*> item) {
	box->setTitle(rpl::single(u"What you typed"_q));
	box->addButton(rpl::single(u"Close"_q), [=] { box->closeBox(); });
	const auto original = WhatYouTyped(item).value_or(QString());
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		original,
		st::boxLabel));
	const auto peer = item->history()->peer;
	const auto name = peer->isUser() ? peer->shortName() : peer->name();
	Ui::AddSkip(box->verticalLayout());
	Ui::AddDividerText(
		box->verticalLayout(),
		rpl::single(u"Only you can see this. %1 got the calm one."_q.arg(
			name)));
}

} // namespace Safe::Calm
