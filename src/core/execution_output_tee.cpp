// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/execution_output_tee.hpp"

#include <cstring>
#include <exception>
#include <typeindex>
#include <utility>

#include "etil/core/logging.hpp"
#include "etil/manifold/message.hpp"
#include "etil/manifold/service.hpp"

namespace etil::core {

ExecutionOutputTeeBuf::ExecutionOutputTeeBuf(
    std::streambuf* downstream,
    etil::manifold::ChannelService* channels,
    std::string session_id,
    std::string channel_name)
    : downstream_(downstream),
      channels_(channels),
      session_id_(std::move(session_id)),
      channel_name_(std::move(channel_name)) {}

ExecutionOutputTeeBuf::~ExecutionOutputTeeBuf() {
    // Flush any partial line on destruction so subscribers see the
    // last bit of output even without a trailing newline.
    std::lock_guard<std::mutex> lk(mu_);
    if (!line_buf_.empty()) flush_line_locked();
}

int ExecutionOutputTeeBuf::overflow(int ch) {
    if (ch == EOF) return 0;
    std::lock_guard<std::mutex> lk(mu_);
    char c = static_cast<char>(ch);
    if (downstream_) {
        if (downstream_->sputc(c) == EOF) return EOF;
    }
    line_buf_.push_back(c);
    if (c == '\n') flush_line_locked();
    return ch;
}

int ExecutionOutputTeeBuf::sync() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!line_buf_.empty()) flush_line_locked();
    if (downstream_) return downstream_->pubsync();
    return 0;
}

void ExecutionOutputTeeBuf::flush_line_locked() {
    if (!channels_ || line_buf_.empty()) {
        line_buf_.clear();
        return;
    }
    std::string payload = std::move(line_buf_);
    line_buf_.clear();

    // Determine output_kind tag from trailing character.
    const char last = payload.empty() ? '\0' : payload.back();
    const char* kind = (last == '\n') ? "cr" : "stdout";

    etil::manifold::Message m;
    m.channel = channel_name_;
    m.payload = payload;
    m.payload_type = std::type_index(typeid(std::string));
    m.tags["output_kind"] = kind;
    if (!session_id_.empty()) m.tags["session_id"] = session_id_;

    // Best-effort publish. We must not throw out of a streambuf
    // operation, and we must not let a publish failure break
    // stdout output — the downstream write already happened.
    // Log via spdlog (not iostream) so diagnostics are observable
    // without re-entering this tee buffer.
    try {
        channels_->publish(std::move(m));
    } catch (const std::exception& e) {
        if (auto log = etil::core::logging::get("etil.manifold"); log) {
            log->warn("tee publish threw on channel {}: {}",
                      channel_name_, e.what());
        }
    } catch (...) {
        if (auto log = etil::core::logging::get("etil.manifold"); log) {
            log->warn("tee publish threw non-std exception on channel {}",
                      channel_name_);
        }
    }
}

ExecutionOutputTee::ExecutionOutputTee(
    std::streambuf* downstream,
    etil::manifold::ChannelService* channels,
    std::string session_id,
    std::string channel_name)
    : buf(std::make_unique<ExecutionOutputTeeBuf>(
          downstream, channels, std::move(session_id), std::move(channel_name))),
      stream(std::make_unique<std::ostream>(buf.get())) {}

} // namespace etil::core
