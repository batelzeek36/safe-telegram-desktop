/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_caps.h"

#include "data/data_peer.h"
#include "safe/safe_calm_files.h"
#include "safe/safe_caps_core.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Safe::Caps {
namespace {

const auto kFileName = u"safe_caps.json"_q;
constexpr auto kMaxChats = 5000;

struct Store {
	bool loaded = false;
	base::flat_set<PeerId> off;
};

[[nodiscard]] Store &Instance() {
	static auto result = Store();
	return result;
}

void Load(Store &store) {
	if (store.loaded) {
		return;
	}
	store.loaded = true;
	const auto bytes = Calm::ReadPrivateFile(kFileName);
	if (bytes.isEmpty()) {
		return;
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(bytes, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Safe Caps: %1 did not parse, on in every chat.").arg(kFileName));
		return;
	}
	for (const auto &value : document.object().value(u"off"_q).toArray()) {
		const auto raw = value.toString().toULongLong();
		if (raw && int(store.off.size()) < kMaxChats) {
			store.off.emplace(PeerId(raw));
		}
	}
}

void Save(const Store &store) {
	auto off = QJsonArray();
	for (const auto &peer : store.off) {
		off.append(QString::number(peer.value));
	}
	auto root = QJsonObject();
	root.insert(u"off"_q, off);
	Calm::WritePrivateFile(
		kFileName,
		QJsonDocument(root).toJson(QJsonDocument::Compact));
}

[[nodiscard]] bool Untouchable(EntityType type) {
	switch (type) {
	case EntityType::Url:
	case EntityType::CustomUrl:
	case EntityType::Email:
	case EntityType::Hashtag:
	case EntityType::Cashtag:
	case EntityType::Mention:
	case EntityType::MentionName:
	case EntityType::BotCommand:
	case EntityType::Phone:
	case EntityType::BankCard:
	case EntityType::TonAddress:
	case EntityType::Code:
	case EntityType::Pre:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] CharKind KindOf(const QString &text, int i) {
	const auto ch = text[i];
	if (ch.isSurrogate()) {
		return CharKind::Other; // emoji and other astral symbols
	} else if (ch.isUpper() || ch.isTitleCase()) {
		return CharKind::Upper;
	} else if (ch.isLower()) {
		return CharKind::Lower;
	} else if (ch.isLetterOrNumber()) {
		return CharKind::Uncased;
	} else if ((ch == QChar('\'') || ch == QChar(0x2019))
		&& i > 0
		&& i + 1 < text.size()
		&& text[i - 1].isLetter()
		&& text[i + 1].isLetter()) {
		return CharKind::Uncased; // DON'T stays one word
	}
	return CharKind::Other;
}

[[nodiscard]] std::vector<CharKind> Kinds(const TextWithEntities &text) {
	const auto size = int(text.text.size());
	auto result = std::vector<CharKind>(size, CharKind::Other);
	for (auto i = 0; i != size; ++i) {
		result[i] = KindOf(text.text, i);
	}
	for (const auto &entity : text.entities) {
		if (!Untouchable(entity.type())) {
			continue;
		}
		const auto from = std::clamp(entity.offset(), 0, size);
		const auto till = std::clamp(from + entity.length(), from, size);
		std::fill(begin(result) + from, begin(result) + till, CharKind::Break);
	}
	return result;
}

[[nodiscard]] bool AlreadyItalic(const TextWithEntities &text, Range run) {
	return ranges::any_of(text.entities, [&](const EntityInText &entity) {
		return (entity.type() == EntityType::Italic)
			&& (entity.offset() <= run.from)
			&& (entity.offset() + entity.length() >= run.till);
	});
}

} // namespace

bool EnabledFor(PeerId peer) {
	auto &store = Instance();
	Load(store);
	return !store.off.contains(peer);
}

void SetEnabled(PeerId peer, bool enabled) {
	auto &store = Instance();
	Load(store);
	if (enabled == !store.off.contains(peer)) {
		return;
	}
	if (enabled) {
		store.off.remove(peer);
	} else {
		store.off.emplace(peer);
	}
	Save(store);
}

TextWithEntities Soften(TextWithEntities text) {
	const auto runs = ShoutRuns(Kinds(text));
	if (runs.empty()) {
		return text;
	}
	for (const auto &run : runs) {
		// one QChar for one QChar, so every entity offset stays right
		for (auto i = run.from; i != run.till; ++i) {
			const auto ch = text.text[i];
			if (!ch.isSurrogate() && (ch.isUpper() || ch.isTitleCase())) {
				const auto lower = ch.toLower();
				if (!lower.isSurrogate()) {
					text.text[i] = lower;
				}
			}
		}
		if (!AlreadyItalic(text, run)) {
			text.entities.push_back(EntityInText(
				EntityType::Italic,
				run.from,
				run.till - run.from));
		}
	}
	std::stable_sort(
		text.entities.begin(),
		text.entities.end(),
		[](const EntityInText &a, const EntityInText &b) {
			return a.offset() < b.offset();
		});
	return text;
}

void Apply(PeerId peer, TextWithEntities &text) {
	if (!peer || !EnabledFor(peer)) {
		return;
	}
	text = Soften(std::move(text));
}

void AddMenuAction(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const Ui::Menu::MenuCallback &addAction) {
	const auto id = peer->id;
	const auto on = EnabledFor(id);
	addAction(
		on ? u"ALL CAPS as italics: On"_q : u"ALL CAPS as italics: Off"_q,
		[=] {
			SetEnabled(id, !on);
			controller->showToast(on
				? u"ALL CAPS goes out as you type it in this chat."_q
				: u"ALL CAPS goes out in italics in this chat."_q);
		},
		&st::menuIconFont);
}

} // namespace Safe::Caps
