#pragma once

// Standalone SHA-256 implementation — no external dependencies.
// Based on FIPS 180-4. Public domain.

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace etil::core {

/// Compute SHA-256 hash of arbitrary data. Returns 32-byte digest.
std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len);

/// Convenience: hash a string.
inline std::array<uint8_t, 32> sha256(const std::string& s) {
    return sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

/// Convenience: hash a byte vector.
inline std::array<uint8_t, 32> sha256(const std::vector<uint8_t>& v) {
    return sha256(v.data(), v.size());
}

} // namespace etil::core
