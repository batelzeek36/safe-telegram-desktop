/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "safe/safe_trust_requests.h"

class QFileSystemWatcher;

namespace Safe {

// The Mac's trust state, main thread only: the base list built into the app
// (safe_trust_data.h), the signed approvals and the Ask to allow queue, read
// from the app's data folder (cWorkingDir(), on the Mac release build
// ~/Library/Application Support/Safe Telegram/):
//   approvals.json               written only by tools/mac_approve.py
//   safe_approvals_applied.json  the app's copy of the last file it accepted
//   safe_requests.json           the Ask to allow queue (the app writes it)
//   safe_invite_joins.json       the chat each approved invite led to
// The folder is watched; a changed approvals.json is re-read, and changes()
// fires when the lists or the queue change.
class TrustStore final {
public:
	[[nodiscard]] static TrustStore &Instance();

	// Base list + approvals in force + pending requests.
	[[nodiscard]] const TrustRules::Policy &policy() const;
	[[nodiscard]] std::int64_t serial() const;

	[[nodiscard]] std::optional<TrustRules::Tier> inviteTier(
		const std::string &hash) const;
	[[nodiscard]] bool invitePending(const std::string &hash) const;

	// The request's serial is filled in here. False when the same request
	// is already waiting, or the queue is full.
	bool addRequest(TrustRules::Request request);
	bool addJoin(TrustRules::InviteJoin join);

	// Something outside the store changed a tier (a discussion link).
	void notifyChanged();
	[[nodiscard]] rpl::producer<> changes() const;

private:
	TrustStore();

	[[nodiscard]] QString path(const QString &name) const;
	[[nodiscard]] QByteArray read(const QString &name, bool *exists) const;
	bool write(const QString &name, const QByteArray &bytes) const;

	void loadBase();
	void loadQueue();
	void loadApprovals(bool initial);
	void rebuild();
	void watch();
	void scheduleReload();

	QString _folder;
	TrustRules::Policy _base;
	TrustRules::Policy _policy;
	std::optional<TrustRules::Approvals> _approvals;
	QByteArray _currentBytes;
	bool _currentRead = false;
	std::vector<TrustRules::Request> _requests;
	std::vector<TrustRules::InviteJoin> _joins;
	QFileSystemWatcher *_watcher = nullptr;
	base::Timer _reloadTimer;
	rpl::event_stream<> _changes;

};

} // namespace Safe
