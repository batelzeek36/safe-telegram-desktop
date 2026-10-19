/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "safe/safe_judge_book.h"

#include <algorithm>

namespace Safe::JudgeBook {
namespace {

constexpr auto kMaxUnixSeconds = std::int64_t(1'000'000'000'000);

[[nodiscard]] std::vector<std::string_view> Split(
		std::string_view text,
		char separator) {
	auto result = std::vector<std::string_view>();
	auto start = std::size_t(0);
	while (true) {
		const auto end = text.find(separator, start);
		if (end == std::string_view::npos) {
			result.push_back(text.substr(start));
			return result;
		}
		result.push_back(text.substr(start, end - start));
		start = end + 1;
	}
}

// Plain decimal digits, no sign, no leading zero, no overflow.
[[nodiscard]] std::optional<std::uint64_t> Decimal(std::string_view text) {
	if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0')) {
		return std::nullopt;
	}
	auto result = std::uint64_t(0);
	for (const auto c : text) {
		if (c < '0' || c > '9') {
			return std::nullopt;
		}
		const auto digit = std::uint64_t(c - '0');
		if (result > (UINT64_MAX - digit) / 10) {
			return std::nullopt;
		}
		result = result * 10 + digit;
	}
	return result;
}

[[nodiscard]] std::optional<Record> DecodeLine(std::string_view line) {
	const auto fields = Split(line, ' ');
	if (fields.size() != 5) {
		return std::nullopt;
	}
	auto result = Record();
	if (fields[0] == "p") {
		result.key.document = false;
	} else if (fields[0] == "d") {
		result.key.document = true;
	} else {
		return std::nullopt;
	}
	const auto id = Decimal(fields[1]);
	if (!id || !*id) {
		return std::nullopt;
	}
	result.key.id = *id;
	if (fields[2] == "clear") {
		result.answer = Answer::Clear;
	} else if (fields[2] == "flag") {
		result.answer = Answer::Flag;
	} else {
		return std::nullopt;
	}
	if (!JudgeNameValid(fields[3])) {
		return std::nullopt;
	}
	result.judge = std::string(fields[3]);
	const auto at = Decimal(fields[4]);
	if (!at || *at > std::uint64_t(kMaxUnixSeconds)) {
		return std::nullopt;
	}
	result.at = std::int64_t(*at);
	return result;
}

} // namespace

bool JudgeNameValid(std::string_view name) {
	if (name.empty() || name.size() > 32) {
		return false;
	}
	return std::all_of(name.begin(), name.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
	});
}

std::string Encode(const std::vector<Record> &records) {
	auto result = std::string(kFileHeader);
	result.reserve(result.size() + records.size() * 48);
	result.push_back('\n');
	for (const auto &record : records) {
		if (!record.key.id || !JudgeNameValid(record.judge)) {
			continue;
		}
		result.append(record.key.document ? "d " : "p ");
		result.append(std::to_string(record.key.id));
		result.append(record.answer == Answer::Clear ? " clear " : " flag ");
		result.append(record.judge);
		result.push_back(' ');
		result.append(std::to_string(std::clamp(
			record.at,
			std::int64_t(0),
			kMaxUnixSeconds)));
		result.push_back('\n');
	}
	return result;
}

Decoded Decode(std::string_view text) {
	auto result = Decoded();
	if (text.size() > kMaxFileBytes) {
		return result;
	}
	const auto lines = Split(text, '\n');
	if (lines.empty() || lines.front() != kFileHeader) {
		return result;
	}
	result.headerOk = true;
	for (auto i = std::size_t(1); i != lines.size(); ++i) {
		const auto line = lines[i];
		if (line.empty()) {
			continue;
		} else if (auto record = DecodeLine(line)) {
			result.records.push_back(std::move(*record));
		} else {
			++result.skipped;
		}
	}
	return result;
}

Book::Book(std::string judge, int maxInFlight)
: _judge(std::move(judge))
, _maxInFlight(std::max(1, std::min(8, maxInFlight))) {
}

void Book::load(const std::vector<Record> &records) {
	for (const auto &record : records) {
		if (!record.key.id || !JudgeNameValid(record.judge)) {
			continue;
		} else if (record.answer == Answer::Clear && record.judge != _judge) {
			continue;
		}
		put(record.key, Entry{
			.answer = record.answer,
			.judge = record.judge,
			.at = record.at,
		});
	}
	trimRecords();
}

State Book::state(Key key) const {
	const auto i = _records.find(key);
	if (i == end(_records)) {
		return State::Waiting;
	} else if (i->second.answer == Answer::Flag) {
		return State::Flagged;
	}
	return (i->second.judge == _judge) ? State::Clear : State::Waiting;
}

bool Book::wants(Key key, std::int64_t nowMs) const {
	if (!key.id || state(key) != State::Waiting || busy(key)) {
		return false;
	}
	const auto retry = _retryAt.find(key);
	return (retry == end(_retryAt)) || (nowMs >= retry->second);
}

bool Book::touch(Key key) {
	const auto i = std::find_if(begin(_queue), end(_queue), [&](
			const auto &entry) {
		return entry.first == key;
	});
	if (i == end(_queue)) {
		return false;
	}
	auto entry = std::move(*i);
	_queue.erase(i);
	_queue.push_back(std::move(entry));
	return true;
}

void Book::preparing(Key key) {
	_preparing.insert(key);
}

void Book::prepared(Key key, JudgeProtocol::Request request) {
	_preparing.erase(key);
	if (state(key) != State::Waiting
		|| _inFlight.contains(key)
		|| _held.contains(key)) {
		return;
	}
	enqueue(key, std::move(request));
}

void Book::unprepared(Key key, std::int64_t nowMs) {
	_preparing.erase(key);
	backoff(key, nowMs);
}

std::optional<Job> Book::next() {
	if (_queue.empty() || int(_inFlight.size()) >= _maxInFlight) {
		return std::nullopt;
	}
	auto entry = std::move(_queue.back());
	_queue.pop_back();
	auto job = Job{ .key = entry.first, .request = entry.second };
	_inFlight.emplace(entry.first, std::move(entry.second));
	return job;
}

bool Book::complete(Key key, Answer answer, std::int64_t at) {
	const auto before = state(key);
	forget(key);
	put(key, Entry{ .answer = answer, .judge = _judge, .at = at });
	trimRecords();
	return state(key) != before;
}

void Book::fail(Key key, std::int64_t nowMs) {
	auto request = std::optional<JudgeProtocol::Request>();
	if (const auto i = _inFlight.find(key); i != end(_inFlight)) {
		request = std::move(i->second);
		_inFlight.erase(i);
	}
	const auto [failures, retryAt] = backoff(key, nowMs);
	if (request && failures <= kAutoRetries) {
		_held[key] = Held{
			.request = std::move(*request),
			.retryAt = retryAt,
			.order = ++_heldOrder,
		};
		trimHeld();
	}
}

int Book::requeueDue(std::int64_t nowMs) {
	auto due = std::vector<std::pair<std::uint64_t, Key>>();
	for (const auto &[key, held] : _held) {
		if (held.retryAt <= nowMs) {
			due.emplace_back(held.order, key);
		}
	}
	std::sort(begin(due), end(due));
	auto requeued = 0;
	for (const auto &[order, key] : due) {
		auto node = _held.extract(key);
		if (state(key) != State::Waiting) {
			continue;
		}
		// Behind everything already queued: what is on screen goes first.
		_queue.insert(begin(_queue), { key, std::move(node.mapped().request) });
		++requeued;
	}
	while (_queue.size() > kMaxQueued) {
		_queue.erase(begin(_queue));
	}
	return requeued;
}

std::optional<std::int64_t> Book::nextRetryAt() const {
	auto result = std::optional<std::int64_t>();
	for (const auto &[key, held] : _held) {
		if (!result || held.retryAt < *result) {
			result = held.retryAt;
		}
	}
	return result;
}

std::vector<Record> Book::records() const {
	auto result = std::vector<Record>();
	result.reserve(_records.size());
	for (const auto &[key, entry] : _records) {
		result.push_back(Record{
			.key = key,
			.answer = entry.answer,
			.judge = entry.judge,
			.at = entry.at,
		});
	}
	return result;
}

std::size_t Book::storedCount() const {
	return _records.size();
}

std::size_t Book::queuedCount() const {
	return _queue.size();
}

std::size_t Book::inFlightCount() const {
	return _inFlight.size();
}

std::size_t Book::heldCount() const {
	return _held.size();
}

const std::string &Book::judge() const {
	return _judge;
}

bool Book::busy(Key key) const {
	return _preparing.contains(key)
		|| _inFlight.contains(key)
		|| _held.contains(key)
		|| std::any_of(begin(_queue), end(_queue), [&](const auto &entry) {
			return entry.first == key;
		});
}

bool Book::put(Key key, Entry entry) {
	const auto i = _records.find(key);
	if (i == end(_records)) {
		_records.emplace(key, std::move(entry));
		return true;
	}
	auto &existing = i->second;
	if (existing.answer == Answer::Flag && entry.answer == Answer::Clear) {
		return false; // A flag is final.
	} else if (existing.answer == entry.answer && existing.judge == entry.judge) {
		existing.at = std::max(existing.at, entry.at);
		return false;
	}
	existing = std::move(entry);
	return true;
}

void Book::enqueue(Key key, JudgeProtocol::Request request) {
	const auto i = std::find_if(begin(_queue), end(_queue), [&](
			const auto &entry) {
		return entry.first == key;
	});
	if (i != end(_queue)) {
		_queue.erase(i);
	}
	_queue.emplace_back(key, std::move(request));
	while (_queue.size() > kMaxQueued) {
		_queue.erase(begin(_queue));
	}
}

void Book::forget(Key key) {
	_preparing.erase(key);
	_inFlight.erase(key);
	_held.erase(key);
	_retryAt.erase(key);
	_failures.erase(key);
	const auto i = std::find_if(begin(_queue), end(_queue), [&](
			const auto &entry) {
		return entry.first == key;
	});
	if (i != end(_queue)) {
		_queue.erase(i);
	}
}

void Book::trimRecords() {
	if (_records.size() <= kMaxRecords) {
		return;
	}
	auto ages = std::vector<std::pair<std::int64_t, Key>>();
	ages.reserve(_records.size());
	for (const auto &[key, entry] : _records) {
		ages.emplace_back(entry.at, key);
	}
	const auto drop = _records.size() - (kMaxRecords * 9 / 10);
	std::nth_element(begin(ages), begin(ages) + drop, end(ages));
	for (auto i = std::size_t(0); i != drop; ++i) {
		_records.erase(ages[i].second);
	}
}

std::pair<int, std::int64_t> Book::backoff(Key key, std::int64_t nowMs) {
	const auto failures = ++_failures[key];
	const auto shift = std::min(failures - 1, 6);
	const auto retryAt = nowMs
		+ std::min(kBackoffFirstMs << shift, kBackoffMaxMs);
	_retryAt[key] = retryAt;
	trimFailures();
	return { failures, retryAt };
}

void Book::trimFailures() {
	if (_retryAt.size() > kMaxRecords || _failures.size() > kMaxRecords) {
		_retryAt.clear();
		_failures.clear();
	}
}

void Book::trimHeld() {
	while (_held.size() > kMaxQueued) {
		auto oldest = begin(_held);
		for (auto i = begin(_held); i != end(_held); ++i) {
			if (i->second.order < oldest->second.order) {
				oldest = i;
			}
		}
		_held.erase(oldest);
	}
}

} // namespace Safe::JudgeBook
