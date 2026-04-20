#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Built-in Manifold sink factories.
///
/// Phase 1 sinks:
///   - spdlog_sink       forward to a named spdlog logger (Phase 0 substrate)
///   - file_sink         direct append to a log file (used by hard-wired Inline routes)
///   - stderr_sink       bootstrap exception path only (doc A §11.2)
///   - observable_sink   stub in Phase 1 — full wiring lands in Phase 2
///   - ring_buffer_sink  bounded in-memory tail (debug/crash context)
///   - test_capture_sink vector-of-messages collector for unit tests
///   - null_sink         discards all messages

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "etil/manifold/sink.hpp"

namespace etil::manifold {

/// Forward messages to a named spdlog logger via the Phase 0 facade.
/// Level selection: tags["level"] ∈ {trace,debug,info,warn,error,critical};
/// default info. Payload stringification: if payload is std::string it
/// is used directly, else the message channel is used as a fallback
/// log line.
std::shared_ptr<ISink> make_spdlog_sink(std::string logger_name);

/// Append each message as one line to the given file path. Used in
/// Inline delivery mode for hard-wired audit/security channels. Not a
/// rotating sink — pair with logrotate externally. Empty payload
/// formats the channel + tags.
std::shared_ptr<ISink> make_file_sink(std::string path);

/// Write to stderr. Reserved for the three bootstrap exceptions in
/// doc A §11.2; not a production sink for normal channels.
std::shared_ptr<ISink> make_stderr_sink();

/// Discard all messages. Used to disable routes without removing them.
std::shared_ptr<ISink> make_null_sink();

/// Collect all accepted messages into an internal vector; unit tests
/// query via captured(). Exposes a richer API than ISink.
class TestCaptureSink : public ISink {
public:
    void accept(const Message& msg) override;
    void flush() override {}

    std::vector<Message> captured() const;
    size_t size() const;
    void clear();

private:
    mutable std::mutex mu_;
    std::vector<Message> storage_;
};

std::shared_ptr<TestCaptureSink> make_test_capture_sink();

/// Bounded in-memory ring. Useful for post-mortem context dumps.
/// drop_first: oldest entries evicted first (default); drop_last
/// discards the newest.
class RingBufferSink : public ISink {
public:
    explicit RingBufferSink(size_t capacity, bool drop_first = true);

    void accept(const Message& msg) override;
    void flush() override {}

    std::vector<Message> snapshot() const;
    size_t size() const;
    size_t dropped_count() const;
    void clear();

private:
    mutable std::mutex mu_;
    std::vector<Message> ring_;
    size_t capacity_;
    size_t head_ = 0;
    size_t count_ = 0;
    size_t dropped_ = 0;
    bool drop_first_ = true;
};

std::shared_ptr<RingBufferSink> make_ring_buffer_sink(size_t capacity,
                                                     bool drop_first = true);

/// Stub observable-sink for Phase 1. Records that the route is
/// observable-backed; Phase 2 replaces this with a HeapObservable
/// emitter via ChannelService::observe. Accepting does nothing in
/// Phase 1 — test_capture_sink is the right choice for Phase 1 tests.
std::shared_ptr<ISink> make_observable_sink_stub();

// ---------------------------------------------------------------------------
// Phase 5a test-sink helpers — see
// docs/claude-design/20260420A-Manifold-Phase-5a-Implementation-Plan.md §5.1
//
// These live in the same header (not a tests/-only header) so the
// Phase 5a.2 async-correctness test suite and the Phase 5a.9 integration
// suite can reach for them without either side needing private headers.
// They compile into the main etil target; each is cheap and exception-
// quiet on its own.
// ---------------------------------------------------------------------------

/// Atomic counter. Use `count()` after `flush_for_tests()` to assert
/// exactly how many deliveries landed. Does not retain messages.
class SubscriberCountingSink : public ISink {
public:
    void accept(const Message& msg) override;
    void flush() override {}

    /// Total number of successful accept() calls since construction.
    uint64_t count() const;

    /// Reset the counter to zero. Useful between sub-scenarios in a
    /// single test.
    void reset();

private:
    std::atomic<uint64_t> count_{0};
};

std::shared_ptr<SubscriberCountingSink> make_subscriber_counting_sink();

/// Blocks inside accept() on an internal semaphore until release() is
/// called. Used by tests that need to observe queue depth while an
/// accept() is in flight. Thread-safe; release() can be called from
/// any thread.
class BlockingSink : public ISink {
public:
    BlockingSink() = default;

    void accept(const Message& msg) override;
    void flush() override {}

    /// Unblock the currently-in-flight accept() and any future ones
    /// until `block()` is called again. Subsequent accept() calls
    /// return immediately until the sink is re-armed via block().
    void release();

    /// Re-arm blocking so the next accept() blocks until the next
    /// release(). Default state after construction is blocking.
    void block();

    uint64_t count() const;
    bool accept_in_progress() const;

    /// Block the calling thread until an accept() is parked on the
    /// semaphore. Returns immediately if one is already parked.
    /// Replaces the poll-and-sleep "let things settle" anti-pattern.
    void wait_until_accept_in_progress();

private:
    std::mutex mu_;
    std::condition_variable cv_;             ///< signals on release() / block()
    std::condition_variable in_progress_cv_; ///< signals on entry/exit of accept()
    bool released_ = false;
    std::atomic<bool> in_progress_{false};
    std::atomic<uint64_t> count_{0};
};

std::shared_ptr<BlockingSink> make_blocking_sink();

/// Throws std::runtime_error on the Nth accept() call (1-indexed);
/// other calls succeed. Used to prove the dispatcher survives sink
/// exceptions without aborting the service.
class ExceptionInjectingSink : public ISink {
public:
    /// `throw_on_call_n` is 1-indexed. Set to 0 to never throw.
    /// `what` is the message on the std::runtime_error.
    ExceptionInjectingSink(uint64_t throw_on_call_n,
                           std::string what = "injected");

    void accept(const Message& msg) override;
    void flush() override {}

    uint64_t count() const;
    uint64_t thrown_count() const;

private:
    uint64_t throw_on_call_n_;
    std::string what_;
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> thrown_count_{0};
};

std::shared_ptr<ExceptionInjectingSink> make_exception_injecting_sink(
    uint64_t throw_on_call_n, std::string what = "injected");

} // namespace etil::manifold
