// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/channel_name.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace etil::manifold {

namespace {

bool segment_char_ok(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) ||
           c == '_' || c == '-';
}

/// Split a dotted name into segments. Empty segments (e.g., leading or
/// trailing dot, consecutive dots) cause an empty vector to be returned
/// via the `ok` out-parameter.
std::vector<std::string_view> split(std::string_view s, bool& ok) {
    std::vector<std::string_view> out;
    ok = true;
    if (s.empty()) { ok = false; return out; }
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            size_t len = i - start;
            if (len == 0) { ok = false; return {}; }
            out.emplace_back(s.data() + start, len);
            start = i + 1;
        }
    }
    return out;
}

bool segment_is_literal(std::string_view seg) {
    for (char c : seg) {
        if (!segment_char_ok(c)) return false;
    }
    return !seg.empty();
}

bool segment_is_wildcard(std::string_view seg) {
    return seg == "*" || seg == "**";
}

} // namespace

bool validate_channel_name(std::string_view name) {
    bool ok = false;
    auto segs = split(name, ok);
    if (!ok) return false;
    for (auto& s : segs) {
        if (!segment_is_literal(s)) return false;
    }
    return true;
}

bool validate_pattern(std::string_view pattern) {
    bool ok = false;
    auto segs = split(pattern, ok);
    if (!ok) return false;
    for (auto& s : segs) {
        if (!segment_is_literal(s) && !segment_is_wildcard(s)) return false;
    }
    return true;
}

/// Recursive glob matcher for dotted channel patterns with `*` and `**`.
/// Classic two-index recursion; `**` can consume any number of segments.
static bool match_segments(const std::vector<std::string_view>& pat,
                           size_t pi,
                           const std::vector<std::string_view>& chan,
                           size_t ci) {
    while (pi < pat.size() && ci < chan.size()) {
        if (pat[pi] == "**") {
            // Greedy: try matching zero, one, ... remaining segments.
            for (size_t k = ci; k <= chan.size(); ++k) {
                if (match_segments(pat, pi + 1, chan, k)) return true;
            }
            return false;
        }
        if (pat[pi] != "*" && pat[pi] != chan[ci]) return false;
        ++pi; ++ci;
    }
    // Trailing ** in pattern matches zero more segments.
    while (pi < pat.size() && pat[pi] == "**") ++pi;
    return pi == pat.size() && ci == chan.size();
}

bool channel_matches(std::string_view pattern, std::string_view channel) {
    bool ok = false;
    auto pat_segs = split(pattern, ok);
    if (!ok) return false;
    auto chan_segs = split(channel, ok);
    if (!ok) return false;
    return match_segments(pat_segs, 0, chan_segs, 0);
}

int pattern_specificity(std::string_view pattern) {
    // Literal segments = 10 points, '*' = 1 point, '**' = 0 points.
    // Higher is more specific; ties broken by lexical pattern length.
    bool ok = false;
    auto segs = split(pattern, ok);
    if (!ok) return -1;
    int score = 0;
    for (auto& s : segs) {
        if (s == "**") score += 0;
        else if (s == "*") score += 1;
        else score += 10;
    }
    return score;
}

} // namespace etil::manifold
