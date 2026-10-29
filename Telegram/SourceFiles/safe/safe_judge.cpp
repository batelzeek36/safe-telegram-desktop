/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_media.h"

#include "base/timer.h"
#include "base/unixtime.h"
#include "safe/safe_judge_book.h"
#include "safe/safe_judge_client.h"
#include "safe/safe_judge_data.h"
#include "safe/safe_judge_image.h"
#include "safe/safe_judge_protocol.h"
#include "settings.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QSaveFile>

namespace Safe {
namespace {

const auto kVerdictsFile = u"safe_verdicts.txt"_q;
constexpr auto kSaveDelay = crl::time(2000);
constexpr auto kLogEvery = crl::time(60 * 1000);

[[nodiscard]] JudgeBook::Key BookKey(MediaKey key) {
	return { .id = key.id, .document = key.document, .preview = key.preview };
}

[[nodiscard]] MediaKey FromBookKey(JudgeBook::Key key) {
	return { .id = key.id, .document = key.document, .preview = key.preview };
}

[[nodiscard]] QString Text(std::string_view text) {
	return QString::fromUtf8(text.data(), qsizetype(text.size()));
}

// The picture check for the whole app: photo and document ids are global,
// so one book serves every account. Exists only in a build with a key.
class Judge final {
public:
	[[nodiscard]] static Judge *Instance();

	[[nodiscard]] VerdictState verdict(MediaKey key) const;
	[[nodiscard]] bool previewsAvailable() const { return _previewsAvailable; }
	void request(
		MediaKey key,
		QImage cover,
		std::vector<QImage> frames,
		const QString &caption,
		const QString &fileName,
		const QString &mime);
	[[nodiscard]] rpl::producer<MediaKey> changes() const;

private:
	explicit Judge(QByteArray key);

	void load();
	void scheduleSave();
	void save();
	void prepared(JudgeBook::Key key, JudgeProtocol::Request request);
	void pump();
	void finished(JudgeBook::Key key, JudgeProtocol::Result result);
	void scheduleRetry();
	void retryDue();
	void logFailure(const std::string &failure);

	bool _previewsAvailable = true;
	JudgeBook::Book _book;
	JudgeClient _client;
	QString _path;
	base::Timer _saveTimer;
	base::Timer _retryTimer;
	rpl::event_stream<MediaKey> _changes;
	std::string _lastFailure;
	crl::time _lastFailureLogged = 0;

};

Judge *Judge::Instance() {
	const auto key = std::string_view(kJudgeKey);
	if (!JudgeProtocol::KeyLooksValid(key)) {
		static auto logged = false;
		if (!logged) {
			logged = true;
			LOG(("Safe judge: no key in this build, pictures from Allowed "
				"chats stay blurred and wait for the picture check."));
		}
		return nullptr;
	}
	// Never destroyed: the paint path may ask until the very end of the app.
	static const auto result = new Judge(QByteArray(
		key.data(),
		qsizetype(key.size())));
	return result;
}

Judge::Judge(QByteArray key)
: _book(std::string(JudgeProtocol::kJudgeName), JudgeProtocol::kMaxInFlight)
, _client(std::move(key))
, _path(cWorkingDir().isEmpty() ? QString() : (cWorkingDir() + kVerdictsFile))
, _saveTimer([=] { save(); })
, _retryTimer([=] { retryDue(); }) {
	load();
	if (const auto app = QCoreApplication::instance()) {
		QObject::connect(app, &QCoreApplication::aboutToQuit, [=] {
			if (_saveTimer.isActive()) {
				_saveTimer.cancel();
				save();
			}
		});
	}
}

void Judge::load() {
	if (_path.isEmpty()) {
		LOG(("Safe judge: no data folder, answers last until the app quits."));
		return;
	}
	auto file = QFile(_path);
	if (!file.exists()) {
		return;
	} else if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Safe judge: could not read the stored answers."));
		return;
	}
	const auto bytes = file.read(qint64(JudgeBook::kMaxFileBytes) + 1);
	const auto decoded = JudgeBook::Decode(
		std::string_view(bytes.constData(), std::size_t(bytes.size())));
	if (!decoded.headerOk) {
		LOG(("Safe judge: the stored answers are unreadable, starting empty."));
		return;
	}
	_book.load(decoded.records);
	LOG(("Safe judge: %1 stored answers, %2 lines skipped."
		).arg(_book.storedCount()
		).arg(decoded.skipped));
}

void Judge::scheduleSave() {
	if (!_saveTimer.isActive()) {
		_saveTimer.callOnce(kSaveDelay);
	}
}

void Judge::save() {
	if (_path.isEmpty()) {
		return;
	}
	const auto text = JudgeBook::Encode(_book.records());
	auto file = QSaveFile(_path);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(text.data(), qint64(text.size())) != qint64(text.size())
		|| !file.commit()) {
		LOG(("Safe judge: could not write the stored answers."));
	}
}

VerdictState Judge::verdict(MediaKey key) const {
	switch (_book.state(BookKey(key))) {
	case JudgeBook::State::Clear: return VerdictState::Clear;
	case JudgeBook::State::Flagged: return VerdictState::Flagged;
	case JudgeBook::State::Waiting: return VerdictState::Waiting;
	}
	return VerdictState::Waiting;
}

void Judge::request(
		MediaKey key,
		QImage cover,
		std::vector<QImage> frames,
		const QString &caption,
		const QString &fileName,
		const QString &mime) {
	const auto bookKey = BookKey(key);
	if (!key || cover.isNull()) {
		return;
	} else if (_book.touch(bookKey)) {
		pump();
		return;
	} else if (!_book.wants(bookKey, crl::now())) {
		return;
	}
	_book.preparing(bookKey);
	crl::async([=,
			cover = std::move(cover),
			frames = std::move(frames)]() mutable {
		auto prepared = JudgeImage::Prepare(
			key.document,
			cover,
			frames,
			caption,
			fileName,
			mime);
		crl::on_main([=, prepared = std::move(prepared)]() mutable {
			Judge::Instance()->prepared(bookKey, std::move(prepared));
		});
	});
}

void Judge::prepared(JudgeBook::Key key, JudgeProtocol::Request request) {
	if (request.cover.empty()) {
		_book.unprepared(key, crl::now());
		logFailure("no picture bytes");
		return;
	}
	_book.prepared(key, std::move(request));
	pump();
}

void Judge::pump() {
	while (auto job = _book.next()) {
		const auto key = job->key;
		_client.call(job->request, [=](JudgeProtocol::Result result) {
			finished(key, std::move(result));
		});
	}
}

void Judge::finished(JudgeBook::Key key, JudgeProtocol::Result result) {
	using JudgeProtocol::Verdict;
	const auto available = (result.verdict == Verdict::Clear)
		|| (result.verdict == Verdict::Flag);
	const auto availabilityChanged = (_previewsAvailable != available);
	_previewsAvailable = available;
	if (result.verdict == Verdict::Clear || result.verdict == Verdict::Flag) {
		const auto answer = (result.verdict == Verdict::Clear)
			? JudgeBook::Answer::Clear
			: JudgeBook::Answer::Flag;
		if (_book.complete(key, answer, base::unixtime::now())) {
			scheduleSave();
			_changes.fire(FromBookKey(key));
		}
	} else {
		_book.fail(key, crl::now());
		logFailure(result.failure);
		scheduleRetry();
	}
	if (availabilityChanged) {
		_changes.fire(MediaKey());
	}
	pump();
}

void Judge::scheduleRetry() {
	const auto at = _book.nextRetryAt();
	if (!at) {
		_retryTimer.cancel();
		return;
	}
	_retryTimer.callOnce(std::max(*at - crl::now(), crl::time(0)) + 1);
}

void Judge::retryDue() {
	_book.requeueDue(crl::now());
	pump();
	scheduleRetry();
}

void Judge::logFailure(const std::string &failure) {
	const auto now = crl::now();
	if (failure == _lastFailure && now - _lastFailureLogged < kLogEvery) {
		return;
	}
	_lastFailure = failure;
	_lastFailureLogged = now;
	LOG(("Safe judge: no verdict this time (%1), will ask again later."
		).arg(Text(failure)));
}

rpl::producer<MediaKey> Judge::changes() const {
	return _changes.events();
}

} // namespace

bool JudgeAvailable() {
	return JudgeProtocol::KeyLooksValid(std::string_view(kJudgeKey));
}

bool PreviewCheckAvailable() {
	const auto judge = Judge::Instance();
	return judge && judge->previewsAvailable();
}

VerdictState Verdict(MediaKey key) {
	const auto judge = Judge::Instance();
	return judge ? judge->verdict(key) : VerdictState::Waiting;
}

void RequestVerdict(
		not_null<Main::Session*>,
		MediaKey key,
		QImage cover,
		std::vector<QImage> frames,
		const QString &caption,
		const QString &fileName,
		const QString &mime) {
	if (const auto judge = Judge::Instance()) {
		judge->request(
			key,
			std::move(cover),
			std::move(frames),
			caption,
			fileName,
			mime);
	}
}

rpl::producer<MediaKey> VerdictChanges() {
	const auto judge = Judge::Instance();
	return judge ? judge->changes() : rpl::never<MediaKey>();
}

} // namespace Safe
