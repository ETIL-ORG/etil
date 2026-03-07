#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace etil::net {

/// Server-wide HTTP client configuration (immutable after construction).
///
/// Read from environment variables at server startup.  Shared across all
/// sessions via const pointer.
struct HttpClientConfig {
    /// Allowed domains.  Empty = deny all.  {"*"} = allow all (SSRF
    /// blocklist still applies).  Supports wildcards: "*.github.com".
    std::vector<std::string> allowed_domains;

    /// Maximum response body size in bytes.
    size_t max_response_bytes = 1 * 1024 * 1024;  // 1 MB

    /// Per-request timeout in milliseconds.
    int request_timeout_ms = 10'000;  // 10s

    /// Max HTTP fetches per interpret call.
    int per_interpret_budget = 10;

    /// Max HTTP fetches over session lifetime.
    int per_session_budget = 100;

    /// Allow plain HTTP (not just HTTPS)?
    bool allow_http = true;

    /// Max redirect hops (0 = no redirect following).
    int max_redirects = 0;

    /// Construct from environment variables.
    ///
    /// - ETIL_HTTP_ALLOWLIST: comma-separated domain list (e.g. "example.com,*.github.com")
    ///   Empty or unset = deny all.  "*" = allow all.
    /// - ETIL_HTTP_MAX_RESPONSE_BYTES: max response size (default 1048576)
    /// - ETIL_HTTP_TIMEOUT_MS: per-request timeout (default 10000)
    /// - ETIL_HTTP_PER_INTERPRET_BUDGET: fetches per interpret call (default 10)
    /// - ETIL_HTTP_PER_SESSION_BUDGET: fetches per session lifetime (default 100)
    /// - ETIL_HTTP_ALLOW_HTTP: "1" to allow plain HTTP (default "1")
    static HttpClientConfig from_env() {
        HttpClientConfig cfg;

        if (const char* v = std::getenv("ETIL_HTTP_ALLOWLIST")) {
            std::string s(v);
            size_t pos = 0;
            while (pos < s.size()) {
                auto comma = s.find(',', pos);
                if (comma == std::string::npos) comma = s.size();
                auto token = s.substr(pos, comma - pos);
                // Trim whitespace
                while (!token.empty() && token.front() == ' ') token.erase(0, 1);
                while (!token.empty() && token.back() == ' ') token.pop_back();
                if (!token.empty()) cfg.allowed_domains.push_back(std::move(token));
                pos = comma + 1;
            }
        }

        if (const char* v = std::getenv("ETIL_HTTP_MAX_RESPONSE_BYTES"))
            cfg.max_response_bytes = static_cast<size_t>(std::atoll(v));
        if (const char* v = std::getenv("ETIL_HTTP_TIMEOUT_MS"))
            cfg.request_timeout_ms = std::atoi(v);
        if (const char* v = std::getenv("ETIL_HTTP_PER_INTERPRET_BUDGET"))
            cfg.per_interpret_budget = std::atoi(v);
        if (const char* v = std::getenv("ETIL_HTTP_PER_SESSION_BUDGET"))
            cfg.per_session_budget = std::atoi(v);
        if (const char* v = std::getenv("ETIL_HTTP_ALLOW_HTTP"))
            cfg.allow_http = (std::string(v) == "1");

        return cfg;
    }

    /// Returns true if any domains are allowed (i.e. HTTP is enabled).
    bool enabled() const { return !allowed_domains.empty(); }
};

/// Per-session mutable HTTP client state.
///
/// Tracks fetch counts for budget enforcement.  Owned by Session,
/// wired to ExecutionContext as non-owning pointer.
struct HttpClientState {
    const HttpClientConfig* config = nullptr;
    int per_interpret_fetches = 0;
    int lifetime_fetches = 0;

    bool can_fetch() const {
        if (!config || !config->enabled()) return false;
        return per_interpret_fetches < config->per_interpret_budget
            && lifetime_fetches < config->per_session_budget;
    }

    void record_fetch() {
        ++per_interpret_fetches;
        ++lifetime_fetches;
    }

    void reset_per_interpret() {
        per_interpret_fetches = 0;
    }
};

} // namespace etil::net
