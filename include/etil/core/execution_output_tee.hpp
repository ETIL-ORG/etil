#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// ExecutionOutputTee — streambuf that forwards interpreter stdout
/// writes both to the original destination *and* onto the Manifold
/// `etil.repl.stdout` channel (Phase 4a). Installed by
/// ExecutionContext when a ChannelService is attached; restored to
/// a plain ostream when channels are detached.
///
/// Legacy observable behavior (REPL printing, MCP string-capture)
/// is preserved — the tee duplicates writes rather than redirecting
/// them. Channel subscribers get an additional view via
/// `channel-subscribe` / `channel-tap-observable`.

#include <memory>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>

namespace etil::manifold {
class ChannelService;
}

namespace etil::core {

/// Streambuf that writes each character to an underlying streambuf
/// AND accumulates into a line buffer. Each newline flushes the
/// accumulated line as a Manifold Message on the configured channel.
/// Thread-safe: a mutex serializes overflow and sync.
class ExecutionOutputTeeBuf : public std::streambuf {
public:
    ExecutionOutputTeeBuf(std::streambuf* downstream,
                          etil::manifold::ChannelService* channels,
                          std::string session_id,
                          std::string channel_name = "etil.repl.stdout");
    ~ExecutionOutputTeeBuf() override;

protected:
    int overflow(int ch) override;
    int sync() override;

private:
    void flush_line_locked();

    std::streambuf* downstream_;
    etil::manifold::ChannelService* channels_;
    std::string session_id_;
    std::string channel_name_;
    std::mutex mu_;
    std::string line_buf_;
};

/// Owning wrapper: holds the tee streambuf + a std::ostream that
/// uses it. ExecutionContext stores one of these when channels are
/// set.
struct ExecutionOutputTee {
    std::unique_ptr<ExecutionOutputTeeBuf> buf;
    std::unique_ptr<std::ostream> stream;

    ExecutionOutputTee(std::streambuf* downstream,
                       etil::manifold::ChannelService* channels,
                       std::string session_id,
                       std::string channel_name = "etil.repl.stdout");
};

} // namespace etil::core
