/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_calm_store.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "safe/safe_calm_files.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <xxhash.h> // XXH64.

namespace Safe::Calm {
namespace {

const auto kFileName = u"safe_calm_marks.json"_q;
constexpr auto kSaveDelay = crl::time(1500);
constexpr auto kMaxSent = 3000;
constexpr auto kMaxIncoming = 8000;

struct SentRecord {
	QString original;
	TimeId at = 0;
};

using SentKey = std::pair<PeerId, uint64>;

class Store final {
public:
	Store();

	void markSent(PeerId peer, const QString &calm, const QString &original);
	[[nodiscard]] std::optional<QString> sentOriginal(
		PeerId peer,
		const QString &shown) const;

	[[nodiscard]] const IncomingRecord *incoming(FullMsgId id) const;
	void setIncoming(FullMsgId id, IncomingRecord record);

private:
	void load();
	void scheduleSave();
	void save();
	void trim();

	base::flat_map<SentKey, SentRecord> _sent;
	base::flat_map<FullMsgId, IncomingRecord> _incoming;
	base::Timer _saveTimer;

};

[[nodiscard]] Store &Instance() {
	static auto result = Store();
	return result;
}

[[nodiscard]] QString Hex(uint64 value) {
	return QString::number(value, 16);
}

[[nodiscard]] uint64 FromHex(const QString &value) {
	return value.toULongLong(nullptr, 16);
}

Store::Store() : _saveTimer([=] { save(); }) {
	load();
	QObject::connect(
		QCoreApplication::instance(),
		&QCoreApplication::aboutToQuit,
		[=] {
			if (_saveTimer.isActive()) {
				_saveTimer.cancel();
				save();
			}
		});
}

void Store::markSent(
		PeerId peer,
		const QString &calm,
		const QString &original) {
	_sent[SentKey(peer, TextHash(calm.trimmed()))] = SentRecord{
		.original = original,
		.at = base::unixtime::now(),
	};
	scheduleSave();
}

std::optional<QString> Store::sentOriginal(
		PeerId peer,
		const QString &shown) const {
	const auto i = _sent.find(SentKey(peer, TextHash(shown.trimmed())));
	return (i != end(_sent))
		? std::make_optional(i->second.original)
		: std::nullopt;
}

const IncomingRecord *Store::incoming(FullMsgId id) const {
	const auto i = _incoming.find(id);
	return (i != end(_incoming)) ? &i->second : nullptr;
}

void Store::setIncoming(FullMsgId id, IncomingRecord record) {
	_incoming[id] = std::move(record);
	scheduleSave();
}

void Store::scheduleSave() {
	if (!_saveTimer.isActive()) {
		_saveTimer.callOnce(kSaveDelay);
	}
}

void Store::trim() {
	const auto drop = [](auto &map, int max) {
		if (int(map.size()) <= max) {
			return;
		}
		auto dates = std::vector<TimeId>();
		dates.reserve(map.size());
		for (const auto &[key, value] : map) {
			dates.push_back(value.at);
		}
		ranges::sort(dates);
		const auto cut = dates[dates.size() - max];
		for (auto i = map.begin(); i != map.end();) {
			if (i->second.at < cut) {
				i = map.erase(i);
			} else {
				++i;
			}
		}
	};
	drop(_sent, kMaxSent);
	drop(_incoming, kMaxIncoming);
}

void Store::save() {
	trim();
	auto sent = QJsonArray();
	for (const auto &[key, record] : _sent) {
		sent.push_back(QJsonObject{
			{ u"p"_q, QString::number(key.first.value) },
			{ u"h"_q, Hex(key.second) },
			{ u"o"_q, record.original },
			{ u"d"_q, double(record.at) },
		});
	}
	auto incoming = QJsonArray();
	for (const auto &[id, record] : _incoming) {
		auto object = QJsonObject{
			{ u"p"_q, QString::number(id.peer.value) },
			{ u"m"_q, QString::number(id.msg.bare) },
			{ u"h"_q, Hex(record.hash) },
			{ u"s"_q, record.soften },
			{ u"d"_q, double(record.at) },
		};
		if (record.soften) {
			object.insert(u"t"_q, record.calm);
		}
		incoming.push_back(object);
	}
	WritePrivateFile(kFileName, QJsonDocument(QJsonObject{
		{ u"sent"_q, sent },
		{ u"incoming"_q, incoming },
	}).toJson(QJsonDocument::Compact));
}

void Store::load() {
	const auto document = QJsonDocument::fromJson(ReadPrivateFile(kFileName));
	if (!document.isObject()) {
		return;
	}
	const auto object = document.object();
	const auto peerOf = [](const QJsonObject &item) {
		return PeerId(item.value(u"p"_q).toString().toULongLong());
	};
	for (const auto &value : object.value(u"sent"_q).toArray()) {
		const auto item = value.toObject();
		const auto peer = peerOf(item);
		const auto hash = FromHex(item.value(u"h"_q).toString());
		if (!peer || !hash) {
			continue;
		}
		_sent[SentKey(peer, hash)] = SentRecord{
			.original = item.value(u"o"_q).toString(),
			.at = TimeId(item.value(u"d"_q).toDouble()),
		};
	}
	for (const auto &value : object.value(u"incoming"_q).toArray()) {
		const auto item = value.toObject();
		const auto peer = peerOf(item);
		const auto msg = item.value(u"m"_q).toString().toLongLong();
		const auto hash = FromHex(item.value(u"h"_q).toString());
		const auto soften = item.value(u"s"_q).toBool();
		const auto calm = item.value(u"t"_q).toString();
		if (!peer || msg <= 0 || !hash || (soften && calm.isEmpty())) {
			continue;
		}
		_incoming[FullMsgId(peer, MsgId(msg))] = IncomingRecord{
			.hash = hash,
			.soften = soften,
			.calm = calm,
			.at = TimeId(item.value(u"d"_q).toDouble()),
		};
	}
}

} // namespace

uint64 TextHash(const QString &text) {
	const auto utf8 = text.toUtf8();
	return XXH64(utf8.constData(), utf8.size(), 0x5AFEC0A1ULL);
}

void MarkSent(PeerId peer, const QString &calm, const QString &original) {
	Instance().markSent(peer, calm, original);
}

std::optional<QString> SentOriginal(PeerId peer, const QString &shown) {
	return Instance().sentOriginal(peer, shown);
}

const IncomingRecord *Incoming(FullMsgId id) {
	return Instance().incoming(id);
}

void SetIncoming(FullMsgId id, IncomingRecord record) {
	Instance().setIncoming(id, std::move(record));
}

} // namespace Safe::Calm
