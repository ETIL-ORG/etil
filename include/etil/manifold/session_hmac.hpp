#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// HMAC-SHA256 helper for session_id hashing (A-5 resolution).
///
/// The MCP `session_id` functions as a bearer credential and MUST
/// NOT cross the broker boundary verbatim. Producers HMAC it with a
/// per-process key; the resulting 128-bit truncated base64url token
/// travels in the broker `Session-Hmac` header. Local subscribers
/// that already know the plaintext session_id can compute the same
/// token and filter by it.
///
/// The process key is generated at ChannelService construction via
/// CSPRNG and never leaves the process.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace etil::manifold {

/// 32-byte (256-bit) HMAC key. Owned by the ChannelService.
using ProcessKey = std::array<uint8_t, 32>;

/// Generate a fresh process key from a CSPRNG. Called once at
/// ChannelService init.
ProcessKey generate_process_key();

/// Compute HMAC-SHA256(key, session_id), truncate to 128 bits,
/// return base64url without padding (22 characters).
///
/// Empty session_id returns an empty string.
std::string session_hmac(const ProcessKey& key, std::string_view session_id);

} // namespace etil::manifold
