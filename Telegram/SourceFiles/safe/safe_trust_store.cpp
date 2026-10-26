/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_trust_store.h"

#include "logs.h"
#include "safe/safe_trust_data.h"
#include "safe/safe_trust_key.h"
#include "settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QSaveFile>

namespace Safe {
namespace {

using namespace TrustRules;

constexpr auto kReloadDelay = crl::time(300);

const auto kApprovalsFile = u"approvals.json"_q;
const auto kAppliedFile = u"safe_approvals_applied.json"_q;
const auto kRequestsFile = u"safe_requests.json"_q;
const auto kJoinsFile = u"safe_invite_joins.json"_q;

[[nodiscard]] std::string_view View(const QByteArray &bytes) {
	return std::string_view(bytes.constData(), std::size_t(bytes.size()));
}

[[nodiscard]] QString Text(const std::string &value) {
	return QString::fromStdString(value);
}

} // namespace

TrustStore &TrustStore::Instance() {
	// Never destroyed: it may be asked for until the very end of the app.
	static const auto result = new TrustStore();
	return *result;
}

TrustStore::TrustStore()
: _folder(cWorkingDir())
, _base(Policy::FailClosed())
, _policy(_base)
, _reloadTimer([=] { loadApprovals(false); }) {
	loadBase();
	loadQueue();
	loadApprovals(true);
	watch();
}

QString TrustStore::path(const QString &name) const {
	return _folder + name;
}

QByteArray TrustStore::read(const QString &name, bool *exists) const {
	auto file = QFile(path(name));
	if (exists) {
		*exists = file.exists();
	}
	if (_folder.isEmpty() || !file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	} else if (file.size() > qint64(kMaxFileBytes)) {
		// Read one byte too many so the parser reports it as too large.
		return file.read(qint64(kMaxFileBytes) + 1);
	}
	return file.readAll();
}

bool TrustStore::write(const QString &name, const QByteArray &bytes) const {
	if (_folder.isEmpty()) {
		return false;
	}
	// Written next to the target and renamed over it: never half a file.
	auto file = QSaveFile(path(name));
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(bytes) != bytes.size()
		|| !file.commit()) {
		LOG(("Safe trust: could not write %1.").arg(name));
		return false;
	}
	return true;
}

void TrustStore::loadBase() {
	// Filled in CI from the SAFE_TRUST_JSON secret (.github/safe/
	// inject_secrets.py); empty in git, which fails closed.
	const auto json = std::string_view(kTrustListJson);
	if (json.empty()) {
		LOG(("Safe trust: no trust list in this build, "
			"only Saved Messages and Telegram show."));
		return;
	}
	auto parsed = Policy::Parse(json);
	if (!parsed.policy) {
		// The message names the problem and an offset, never content.
		LOG(("Safe trust: the built-in list is broken (%1), "
			"failing closed.").arg(Text(parsed.error)));
		return;
	}
	_base = std::move(*parsed.policy);
	LOG(("Safe trust: built-in list with %1 trusted, %2 allowed, %3 known, "
		"%4 quiet."
		).arg(_base.trustedCount()
		).arg(_base.allowedCount()
		).arg(_base.knownCount()
		).arg(_base.quietMediaCount()));
}

void TrustStore::loadQueue() {
	auto exists = false;
	auto error = std::string();
	const auto requests = read(kRequestsFile, &exists);
	if (exists) {
		_requests = DecodeRequests(View(requests), error);
		if (!error.empty()) {
			LOG(("Safe trust: %1, starting an empty queue."
				).arg(Text(error)));
		}
	}
	error.clear();
	const auto joins = read(kJoinsFile, &exists);
	if (exists) {
		_joins = DecodeJoins(View(joins), error);
		if (!error.empty()) {
			LOG(("Safe trust: %1.").arg(Text(error)));
		}
	}
}

void TrustStore::loadApprovals(bool initial) {
	auto currentPresent = false;
	const auto currentBytes = read(kApprovalsFile, &currentPresent);
	if (!initial && _currentRead && currentBytes == _currentBytes) {
		watch();
		return;
	}
	_currentRead = true;
	_currentBytes = currentBytes;

	// The copy is re-verified at every load: editing it by hand breaks it.
	auto appliedPresent = false;
	const auto appliedBytes = read(kAppliedFile, &appliedPresent);
	const auto applied = appliedPresent
		? ReadSignedApprovals(View(appliedBytes), kApprovalsPublicKey)
		: ApprovalsResult();
	const auto current = currentPresent
		? ReadSignedApprovals(View(currentBytes), kApprovalsPublicKey)
		: ApprovalsResult();
	if (appliedPresent && !applied.approvals) {
		LOG(("Safe trust: the applied copy does not verify (%1)."
			).arg(Text(applied.error)));
	}
	const auto choice = ChooseApprovals(applied, currentPresent, current);
	if (choice.currentBroken) {
		LOG(("Safe trust: approvals.json ignored (%1).").arg(
			Text(current.error)));
	} else if (choice.currentIgnored) {
		LOG(("Safe trust: approvals.json with serial %1 is older than the "
			"applied serial %2, ignored."
			).arg(current.approvals->serial
			).arg(applied.approvals->serial));
	}

	using Source = ApprovalsChoice::Source;
	auto next = std::optional<Approvals>();
	if (choice.source == Source::Current) {
		next = current.approvals;
		if (currentBytes != appliedBytes) {
			write(kAppliedFile, currentBytes);
		}
	} else if (choice.source == Source::Applied) {
		next = applied.approvals;
	}
	const auto serialOf = [](const std::optional<Approvals> &value) {
		return value ? value->serial : std::int64_t(0);
	};
	const auto changed = initial
		|| (serialOf(next) != serialOf(_approvals));
	_approvals = std::move(next);
	if (changed) {
		LOG(("Safe trust: approvals serial %1 in force, %2 chats, "
			"%3 invites."
			).arg(serialOf(_approvals)
			).arg(_approvals ? _approvals->dialogs.size() : 0
			).arg(_approvals ? _approvals->invites.size() : 0));
	}
	const auto answered = DropAnswered(_requests, _approvals);
	if (answered) {
		write(kRequestsFile, QByteArray::fromStdString(
			EncodeRequests(_requests)));
	}
	if (changed || answered) {
		rebuild();
		if (!initial) {
			_changes.fire({});
		}
	}
	watch();
}

void TrustStore::rebuild() {
	_policy = _base.withOverrides(
		EffectiveOverrides(_approvals, _joins)
	).withRequests(PendingDialogIds(_requests, _approvals));
}

void TrustStore::watch() {
	if (_folder.isEmpty()) {
		return;
	} else if (!_watcher) {
		// Owned by the application object, so it goes away with it.
		_watcher = new QFileSystemWatcher(QCoreApplication::instance());
		QObject::connect(
			_watcher,
			&QFileSystemWatcher::directoryChanged,
			[=] { scheduleReload(); });
		QObject::connect(
			_watcher,
			&QFileSystemWatcher::fileChanged,
			[=] { scheduleReload(); });
	}
	const auto folder = _folder.endsWith('/') ? _folder.chopped(1) : _folder;
	if (!_watcher->directories().contains(folder)) {
		_watcher->addPath(folder);
	}
	// A rename over the file drops its watch: add it back every time.
	const auto file = path(kApprovalsFile);
	if (QFile::exists(file) && !_watcher->files().contains(file)) {
		_watcher->addPath(file);
	}
}

void TrustStore::scheduleReload() {
	_reloadTimer.callOnce(kReloadDelay);
}

const Policy &TrustStore::policy() const {
	return _policy;
}

std::int64_t TrustStore::serial() const {
	return _approvals ? _approvals->serial : 0;
}

std::optional<Tier> TrustStore::inviteTier(const std::string &hash) const {
	if (!_approvals) {
		return std::nullopt;
	}
	const auto i = _approvals->invites.find(hash);
	if (i == end(_approvals->invites)) {
		return std::nullopt;
	} else if (!_policy.loaded() && i->second != Tier::Off) {
		// The built-in list failed closed: approvals may only close.
		return std::nullopt;
	}
	return i->second;
}

bool TrustStore::invitePending(const std::string &hash) const {
	return InvitePending(_requests, hash, _approvals);
}

bool TrustStore::addRequest(Request request) {
	request.serial = serial();
	if (!AddRequest(_requests, std::move(request), _approvals)) {
		return false;
	}
	write(kRequestsFile, QByteArray::fromStdString(EncodeRequests(_requests)));
	LOG(("Safe trust: Ask to allow queued, %1 waiting."
		).arg(_requests.size()));
	rebuild();
	_changes.fire({});
	return true;
}

bool TrustStore::addJoin(InviteJoin join) {
	if (!AddJoin(_joins, std::move(join))) {
		return false;
	}
	write(kJoinsFile, QByteArray::fromStdString(EncodeJoins(_joins)));
	rebuild();
	_changes.fire({});
	return true;
}

void TrustStore::notifyChanged() {
	_changes.fire({});
}

rpl::producer<> TrustStore::changes() const {
	return _changes.events();
}

} // namespace Safe
