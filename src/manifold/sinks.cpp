// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/sinks.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <typeindex>

#include <spdlog/spdlog.h>

#include "etil/core/logging.hpp"

namespace etil::manifold {

namespace {

/// Best-effort payload → string. Dominant case is std::string.
std::string payload_to_string(const Message& m) {
    if (m.payload.type() == typeid(std::string)) {
        try {
            return std::any_cast<std::string>(m.payload);
        } catch (...) {}
    }
    if (m.payload.type() == typeid(const char*)) {
        try {
            return std::any_cast<const char*>(m.payload);
        } catch (...) {}
    }
    // Fallback: stringify from channel + tags so the line is not empty.
    std::ostringstream os;
    os << "[" << m.channel << "]";
    for (auto& [k, v] : m.tags) {
        os << " " << k << "=" << v;
    }
    return os.str();
}

spdlog::level::level_enum level_from_tag(const Message& m) {
    auto it = m.tags.find("level");
    if (it == m.tags.end()) return spdlog::level::info;
    return etil::core::logging::level_from_string(it->second);
}

class SpdlogSink : public ISink {
public:
    explicit SpdlogSink(std::string logger_name)
        : logger_(etil::core::logging::get(logger_name)) {}

    void accept(const Message& msg) override {
        if (!logger_) return;
        logger_->log(level_from_tag(msg), "{}", payload_to_string(msg));
    }

    void flush() override {
        if (logger_) logger_->flush();
    }

private:
    std::shared_ptr<spdlog::logger> logger_;
};

class FileSink : public ISink {
public:
    explicit FileSink(std::string path) : path_(std::move(path)) {
        // Defer open until first accept — permissions may not be
        // settled at construction time (e.g. dir just created).
    }

    void accept(const Message& msg) override {
        std::lock_guard<std::mutex> lock(mu_);
        if (!open_attempted_) {
            out_.open(path_, std::ios::app);
            open_attempted_ = true;
        }
        if (!out_.is_open()) return;
        out_ << "[" << msg.channel << "] "
             << payload_to_string(msg) << "\n";
        out_.flush();
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mu_);
        if (out_.is_open()) out_.flush();
    }

private:
    std::string path_;
    std::ofstream out_;
    bool open_attempted_ = false;
    std::mutex mu_;
};

class StderrSink : public ISink {
public:
    void accept(const Message& msg) override {
        // NOTE: This sink exists ONLY for the three bootstrap exceptions
        // in doc A §11.2. Non-bootstrap code paths must use spdlog_sink
        // or a Manifold channel with a non-stderr sink.
        std::lock_guard<std::mutex> lock(mu_);
        std::fprintf(stderr, "[%s] %s\n",
                     msg.channel.c_str(),
                     payload_to_string(msg).c_str());
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mu_);
        std::fflush(stderr);
    }

private:
    std::mutex mu_;
};

class NullSink : public ISink {
public:
    void accept(const Message&) override {}
};

class ObservableSinkStub : public ISink {
public:
    void accept(const Message&) override {
        // Phase 1 stub — Phase 2 replaces with a HeapObservable emitter.
    }
};

} // namespace

std::shared_ptr<ISink> make_spdlog_sink(std::string logger_name) {
    return std::make_shared<SpdlogSink>(std::move(logger_name));
}

std::shared_ptr<ISink> make_file_sink(std::string path) {
    return std::make_shared<FileSink>(std::move(path));
}

std::shared_ptr<ISink> make_stderr_sink() {
    return std::make_shared<StderrSink>();
}

std::shared_ptr<ISink> make_null_sink() {
    return std::make_shared<NullSink>();
}

std::shared_ptr<ISink> make_observable_sink_stub() {
    return std::make_shared<ObservableSinkStub>();
}

// --- TestCaptureSink ---------------------------------------------------

void TestCaptureSink::accept(const Message& msg) {
    std::lock_guard<std::mutex> lock(mu_);
    storage_.push_back(msg);
}

std::vector<Message> TestCaptureSink::captured() const {
    std::lock_guard<std::mutex> lock(mu_);
    return storage_;
}

size_t TestCaptureSink::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return storage_.size();
}

void TestCaptureSink::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    storage_.clear();
}

std::shared_ptr<TestCaptureSink> make_test_capture_sink() {
    return std::make_shared<TestCaptureSink>();
}

// --- RingBufferSink ----------------------------------------------------

RingBufferSink::RingBufferSink(size_t capacity, bool drop_first)
    : capacity_(capacity == 0 ? 1 : capacity),
      drop_first_(drop_first) {
    ring_.resize(capacity_);
}

void RingBufferSink::accept(const Message& msg) {
    std::lock_guard<std::mutex> lock(mu_);
    if (count_ < capacity_) {
        size_t idx = (head_ + count_) % capacity_;
        ring_[idx] = msg;
        ++count_;
    } else if (drop_first_) {
        ring_[head_] = msg;
        head_ = (head_ + 1) % capacity_;
        ++dropped_;
    } else {
        // drop_last: refuse to accept; count the drop.
        ++dropped_;
    }
}

std::vector<Message> RingBufferSink::snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<Message> out;
    out.reserve(count_);
    for (size_t i = 0; i < count_; ++i) {
        out.push_back(ring_[(head_ + i) % capacity_]);
    }
    return out;
}

size_t RingBufferSink::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return count_;
}

size_t RingBufferSink::dropped_count() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dropped_;
}

void RingBufferSink::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    head_ = count_ = dropped_ = 0;
}

std::shared_ptr<RingBufferSink> make_ring_buffer_sink(size_t capacity,
                                                     bool drop_first) {
    return std::make_shared<RingBufferSink>(capacity, drop_first);
}

// ---------------------------------------------------------------------------
// Phase 5a test-sink helpers
// ---------------------------------------------------------------------------

void SubscriberCountingSink::accept(const Message& /*msg*/) {
    count_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t SubscriberCountingSink::count() const {
    return count_.load(std::memory_order_relaxed);
}

void SubscriberCountingSink::reset() {
    count_.store(0, std::memory_order_relaxed);
}

std::shared_ptr<SubscriberCountingSink> make_subscriber_counting_sink() {
    return std::make_shared<SubscriberCountingSink>();
}

// ---

DelayingSink::DelayingSink(std::chrono::milliseconds delay)
    : delay_us_(
          std::chrono::duration_cast<std::chrono::microseconds>(delay)
              .count()) {}

void DelayingSink::accept(const Message& /*msg*/) {
    auto us = delay_us_.load(std::memory_order_relaxed);
    if (us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(us));
    }
    count_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t DelayingSink::count() const {
    return count_.load(std::memory_order_relaxed);
}

void DelayingSink::set_delay(std::chrono::milliseconds d) {
    delay_us_.store(
        std::chrono::duration_cast<std::chrono::microseconds>(d).count(),
        std::memory_order_relaxed);
}

std::shared_ptr<DelayingSink> make_delaying_sink(
    std::chrono::milliseconds delay) {
    return std::make_shared<DelayingSink>(delay);
}

// ---

void BlockingSink::accept(const Message& /*msg*/) {
    std::unique_lock<std::mutex> lk(mu_);
    in_progress_.store(true, std::memory_order_release);
    in_progress_cv_.notify_all();        // wake anyone in wait_until_accept_in_progress()
    cv_.wait(lk, [this] { return released_; });
    in_progress_.store(false, std::memory_order_release);
    in_progress_cv_.notify_all();        // wake waiters observing the exit edge too
    count_.fetch_add(1, std::memory_order_relaxed);
}

void BlockingSink::release() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        released_ = true;
    }
    cv_.notify_all();
}

void BlockingSink::block() {
    std::lock_guard<std::mutex> lk(mu_);
    released_ = false;
}

uint64_t BlockingSink::count() const {
    return count_.load(std::memory_order_relaxed);
}

bool BlockingSink::accept_in_progress() const {
    return in_progress_.load(std::memory_order_acquire);
}

void BlockingSink::wait_until_accept_in_progress() {
    std::unique_lock<std::mutex> lk(mu_);
    in_progress_cv_.wait(lk, [this] {
        return in_progress_.load(std::memory_order_acquire);
    });
}

std::shared_ptr<BlockingSink> make_blocking_sink() {
    return std::make_shared<BlockingSink>();
}

// ---

ExceptionInjectingSink::ExceptionInjectingSink(uint64_t throw_on_call_n,
                                               std::string what)
    : throw_on_call_n_(throw_on_call_n), what_(std::move(what)) {}

void ExceptionInjectingSink::accept(const Message& /*msg*/) {
    uint64_t n = count_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (throw_on_call_n_ != 0 && n == throw_on_call_n_) {
        thrown_count_.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error(what_);
    }
}

uint64_t ExceptionInjectingSink::count() const {
    return count_.load(std::memory_order_relaxed);
}

uint64_t ExceptionInjectingSink::thrown_count() const {
    return thrown_count_.load(std::memory_order_relaxed);
}

std::shared_ptr<ExceptionInjectingSink> make_exception_injecting_sink(
    uint64_t throw_on_call_n, std::string what) {
    return std::make_shared<ExceptionInjectingSink>(throw_on_call_n,
                                                    std::move(what));
}

} // namespace etil::manifold
