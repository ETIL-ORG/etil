// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/sinks.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
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

} // namespace etil::manifold
