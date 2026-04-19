// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/session_hmac.hpp"

#include "etil/core/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <random>

namespace etil::manifold {

namespace {

constexpr size_t kBlockSize = 64;   // SHA-256 block size
constexpr size_t kDigestSize = 32;  // SHA-256 digest size

constexpr const char kBase64UrlAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64url_encode_no_pad(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) |
                     uint32_t(data[i + 2]);
        out.push_back(kBase64UrlAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[v & 0x3F]);
        i += 3;
    }
    if (i + 1 == len) {
        uint32_t v = uint32_t(data[i]) << 16;
        out.push_back(kBase64UrlAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(v >> 12) & 0x3F]);
    } else if (i + 2 == len) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
        out.push_back(kBase64UrlAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kBase64UrlAlphabet[(v >> 6) & 0x3F]);
    }
    return out;
}

std::array<uint8_t, kDigestSize> hmac_sha256(const ProcessKey& key,
                                             std::string_view data) {
    std::array<uint8_t, kBlockSize> k0{};
    std::copy(key.begin(), key.end(), k0.begin());

    std::array<uint8_t, kBlockSize> inner_pad{};
    std::array<uint8_t, kBlockSize> outer_pad{};
    for (size_t i = 0; i < kBlockSize; ++i) {
        inner_pad[i] = uint8_t(k0[i] ^ 0x36);
        outer_pad[i] = uint8_t(k0[i] ^ 0x5C);
    }

    std::vector<uint8_t> inner;
    inner.reserve(kBlockSize + data.size());
    inner.insert(inner.end(), inner_pad.begin(), inner_pad.end());
    inner.insert(inner.end(),
                 reinterpret_cast<const uint8_t*>(data.data()),
                 reinterpret_cast<const uint8_t*>(data.data() + data.size()));
    auto inner_digest = etil::core::sha256(inner.data(), inner.size());

    std::vector<uint8_t> outer;
    outer.reserve(kBlockSize + kDigestSize);
    outer.insert(outer.end(), outer_pad.begin(), outer_pad.end());
    outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
    return etil::core::sha256(outer.data(), outer.size());
}

} // namespace

ProcessKey generate_process_key() {
    std::random_device rd;
    std::mt19937_64 gen(
        (uint64_t(rd()) << 32) | uint64_t(rd()));
    ProcessKey key{};
    for (size_t i = 0; i < key.size(); i += 8) {
        uint64_t v = gen();
        for (size_t b = 0; b < 8 && i + b < key.size(); ++b) {
            key[i + b] = uint8_t((v >> (b * 8)) & 0xFF);
        }
    }
    return key;
}

std::string session_hmac(const ProcessKey& key, std::string_view session_id) {
    if (session_id.empty()) return {};
    auto digest = hmac_sha256(key, session_id);
    return base64url_encode_no_pad(digest.data(), 16);  // 128-bit truncate
}

} // namespace etil::manifold
