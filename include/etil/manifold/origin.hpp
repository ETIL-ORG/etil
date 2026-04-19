#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Process-global origin context for Manifold messages.
///
/// Captures (hostname, app_startup_us) once at init and hands out
/// MessageOrigin values with a monotonic per-process sequence number on
/// each publish. See doc B §18.4.

#include <cstdint>
#include <string>

#include "etil/manifold/message.hpp"

namespace etil::manifold {

/// Initialize the process-global origin context. Idempotent — first
/// caller wins. Safe to call from any thread.
///
/// Captures the hostname (via gethostname on native, caller-supplied on
/// WASM/browser) and records current UTC microseconds as the startup
/// timestamp. Subsequent calls to `current_origin()` will stamp this
/// value on every message along with a fresh sequence number.
///
/// origin_type: native builds pass OriginType::Native; browser/WASM
/// builds pass OriginType::Browser with hostname_override supplying the
/// web origin (location.origin) as the hostname.
void init_origin(OriginType origin_type = OriginType::Native,
                 const std::string& hostname_override = "");

/// Tear down the origin context. Safe to call even if init was never run.
void shutdown_origin();

/// True once `init_origin()` has completed.
bool origin_is_initialized();

/// Build a MessageOrigin for a message about to be published.
/// Increments the process-global sequence counter atomically (relaxed
/// ordering is sufficient — see §18.3). session_id may be empty for
/// messages produced outside any session.
MessageOrigin current_origin(const std::string& session_id = "");

/// Read-only access to origin fields, useful for TIL introspection
/// (`channel-origin`, `channel-seq`, `channel-last-published`).
std::string_view origin_hostname();
int64_t          origin_app_startup_us();
int64_t          origin_next_seq_value(); // does not increment
OriginType       origin_type();

} // namespace etil::manifold
