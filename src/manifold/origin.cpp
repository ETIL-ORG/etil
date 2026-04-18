// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/origin.hpp"

#include <atomic>
#include <chrono>
#include <string>

#include <absl/synchronization/mutex.h>

#ifndef ETIL_WASM_BUILD
#include <unistd.h>
#endif

namespace etil::manifold {

namespace {

struct OriginState {
    absl::Mutex mu;
    bool initialized ABSL_GUARDED_BY(mu) = false;
    std::string hostname_storage ABSL_GUARDED_BY(mu);
    std::string_view hostname_view;  // points into hostname_storage after init
    int64_t app_startup_us = 0;
    OriginType origin_type = OriginType::Native;
    std::atomic<int64_t> seq{0};
};

OriginState& state() {
    static OriginState s;
    return s;
}

} // namespace

void init_origin(OriginType origin_type, const std::string& hostname_override) {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    if (s.initialized) return;

    if (!hostname_override.empty()) {
        s.hostname_storage = hostname_override;
    } else {
#ifdef ETIL_WASM_BUILD
        s.hostname_storage = "wasm";
#else
        char buf[256] = {0};
        if (::gethostname(buf, sizeof(buf) - 1) != 0) {
            buf[0] = '\0';
        }
        s.hostname_storage = buf;
#endif
    }
    s.hostname_view = s.hostname_storage;
    s.app_startup_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    s.origin_type = origin_type;
    s.initialized = true;
}

void shutdown_origin() {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    s.initialized = false;
    s.hostname_storage.clear();
    s.hostname_view = {};
    s.app_startup_us = 0;
    s.seq.store(0, std::memory_order_relaxed);
}

bool origin_is_initialized() {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    return s.initialized;
}

MessageOrigin current_origin(const std::string& session_id) {
    auto& s = state();
    // Safety net: initialize on demand if not yet done, so publish()
    // callers never get an empty-hostname origin.
    bool need_init = false;
    {
        absl::MutexLock lock(&s.mu);
        need_init = !s.initialized;
    }
    if (need_init) init_origin();

    MessageOrigin o;
    {
        absl::MutexLock lock(&s.mu);
        o.hostname        = s.hostname_view;
        o.app_startup_us  = s.app_startup_us;
        o.origin_type     = s.origin_type;
    }
    o.session_id = session_id;
    o.seq = s.seq.fetch_add(1, std::memory_order_relaxed);
    return o;
}

std::string_view origin_hostname() {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    return s.hostname_view;
}

int64_t origin_app_startup_us() {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    return s.app_startup_us;
}

int64_t origin_next_seq_value() {
    return state().seq.load(std::memory_order_relaxed);
}

OriginType origin_type() {
    auto& s = state();
    absl::MutexLock lock(&s.mu);
    return s.origin_type;
}

} // namespace etil::manifold
