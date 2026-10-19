/*
This file is part of Safe Telegram, a locked build of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

// The picture check's verdict book and request queue. Standard C++ only (no
// Qt, no tdesktop types, time passed in), so tools/judgecheck-desktop checks
// this exact file on a plain Mac. Same rules as the phone's SafeVerdictBook:
//   stored Flag (any judge name)          -> Flagged (final: never re-sent,
//                                            a later Clear never replaces it)
//   stored Clear under the current name   -> Clear (never re-sent)
//   anything else                         -> Waiting
// A Clear stored under another judge name never shows a picture: it is
// dropped on load and the picture is checked again under the current name.
// Queue: newest first (what is on screen now), at most kMaxQueued (the
// oldest drop and ask again when drawn again), at most maxInFlight handed out
// at a time. A failed check backs off (30 s doubling to 30 min); the first
// kAutoRetries failures keep the picture and retry on their own, later ones
// wait until the picture is asked for again.
// The file (Encode / Decode): one line per answer, keyed by media id + judge.

#include "safe/safe_judge_protocol.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Safe::JudgeBook {

inline constexpr std::size_t kMaxRecords = 20000;
inline constexpr std::size_t kMaxQueued = 256;
inline constexpr std::int64_t kBackoffFirstMs = 30 * 1000;
inline constexpr std::int64_t kBackoffMaxMs = 30 * 60 * 1000;
inline constexpr int kAutoRetries = 6;
inline constexpr std::size_t kMaxFileBytes = 4 * 1024 * 1024;
inline constexpr std::string_view kFileHeader = "safe-verdicts 1";

struct Key {
	std::uint64_t id = 0;
	bool document = false;

	friend inline auto operator<=>(const Key &, const Key &) = default;
	friend inline bool operator==(const Key &, const Key &) = default;
};

enum class Answer : unsigned char {
	Clear,
	Flag,
};

enum class State : unsigned char {
	Waiting,
	Clear,
	Flagged,
};

struct Record {
	Key key;
	Answer answer = Answer::Flag;
	std::string judge;
	std::int64_t at = 0; // Unix seconds.
};

// [a-z0-9-]{1,32}, the phone's rule for a source name.
[[nodiscard]] bool JudgeNameValid(std::string_view name);

[[nodiscard]] std::string Encode(const std::vector<Record> &records);

struct Decoded {
	std::vector<Record> records;
	int skipped = 0; // Lines that did not read as an answer.
	bool headerOk = false;
};
[[nodiscard]] Decoded Decode(std::string_view text);

struct Job {
	Key key;
	JudgeProtocol::Request request;
};

class Book final {
public:
	Book(std::string judge, int maxInFlight);

	// Flag wins over Clear for a key; Clears of other judges are dropped.
	void load(const std::vector<Record> &records);

	[[nodiscard]] State state(Key key) const;

	// No answer, not queued, not running, not backing off.
	[[nodiscard]] bool wants(Key key, std::int64_t nowMs) const;

	// Asked again while queued: it becomes the newest. False if not queued.
	bool touch(Key key);

	// wants() -> preparing() -> prepared() (or unprepared() when there was
	// nothing to send) -> next() -> complete() or fail().
	void preparing(Key key);
	void prepared(Key key, JudgeProtocol::Request request);
	void unprepared(Key key, std::int64_t nowMs);
	[[nodiscard]] std::optional<Job> next();

	// True when the key's state changed (the gate repaints it).
	bool complete(Key key, Answer answer, std::int64_t at);
	void fail(Key key, std::int64_t nowMs);

	// Held retries whose time came go back into the queue; returns how many.
	int requeueDue(std::int64_t nowMs);
	[[nodiscard]] std::optional<std::int64_t> nextRetryAt() const;

	[[nodiscard]] std::vector<Record> records() const;
	[[nodiscard]] std::size_t storedCount() const;
	[[nodiscard]] std::size_t queuedCount() const;
	[[nodiscard]] std::size_t inFlightCount() const;
	[[nodiscard]] std::size_t heldCount() const;
	[[nodiscard]] const std::string &judge() const;

private:
	struct Entry {
		Answer answer = Answer::Flag;
		std::string judge;
		std::int64_t at = 0;
	};
	struct Held {
		JudgeProtocol::Request request;
		std::int64_t retryAt = 0;
		std::uint64_t order = 0;
	};

	[[nodiscard]] bool busy(Key key) const;
	bool put(Key key, Entry entry);
	void enqueue(Key key, JudgeProtocol::Request request);
	void forget(Key key);
	std::pair<int, std::int64_t> backoff(Key key, std::int64_t nowMs);
	void trimRecords();
	void trimFailures();
	void trimHeld();

	std::string _judge;
	int _maxInFlight = 1;
	std::map<Key, Entry> _records;
	std::vector<std::pair<Key, JudgeProtocol::Request>> _queue; // Newest last.
	std::map<Key, JudgeProtocol::Request> _inFlight;
	std::set<Key> _preparing;
	std::map<Key, Held> _held;
	std::map<Key, std::int64_t> _retryAt;
	std::map<Key, int> _failures;
	std::uint64_t _heldOrder = 0;

};

} // namespace Safe::JudgeBook
