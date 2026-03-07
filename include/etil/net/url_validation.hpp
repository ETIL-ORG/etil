#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <cstdint>
#include <string>
#include <vector>

namespace etil::net {

struct HttpClientConfig;

/// Parsed URL components.
struct ParsedUrl {
    std::string scheme;   // "http" or "https"
    std::string host;     // hostname or IP
    int port = 0;         // 0 = default (80/443)
    std::string path;     // path + query + fragment (starts with '/')

    /// Effective port (applies defaults for http/https).
    int effective_port() const {
        if (port != 0) return port;
        return (scheme == "https") ? 443 : 80;
    }
};

/// Parse an HTTP/HTTPS URL into components.
/// Returns false if the URL is malformed or uses an unsupported scheme.
bool parse_url(const std::string& url, ParsedUrl& out);

/// Check whether a resolved IPv4 address is in the SSRF blocklist.
///
/// Blocked ranges (always, regardless of allowlist):
///   - 127.0.0.0/8       (loopback)
///   - 10.0.0.0/8        (RFC1918)
///   - 172.16.0.0/12     (RFC1918)
///   - 192.168.0.0/16    (RFC1918)
///   - 169.254.0.0/16    (link-local, cloud metadata)
///   - 0.0.0.0/8         (current network)
bool is_ipv4_ssrf_blocked(uint32_t ipv4_addr);

/// Check whether a resolved IPv6 address is in the SSRF blocklist.
///
/// Blocked:
///   - ::1               (loopback)
///   - fc00::/7          (unique local)
///   - fe80::/10         (link-local)
///   - ::ffff:0:0/96     (IPv4-mapped — delegates to IPv4 check)
bool is_ipv6_ssrf_blocked(const uint8_t ipv6_addr[16]);

/// Resolve a hostname and check all resolved addresses against the SSRF
/// blocklist.  Returns true if the hostname resolves to at least one
/// non-blocked address.
///
/// If the hostname resolves entirely to blocked addresses, returns false
/// and sets `error` with a description.
bool resolve_and_check_ssrf(const std::string& hostname, std::string& error);

/// Check whether a domain is allowed by the allowlist.
///
/// Rules:
///   - {"*"} matches any domain
///   - "example.com" matches exactly "example.com"
///   - "*.example.com" matches "sub.example.com", "a.b.example.com"
///     but NOT "example.com" itself
bool is_domain_allowed(const std::string& domain,
                       const std::vector<std::string>& allowed_domains);

/// Full URL validation against config.  Checks:
///   1. URL parsing (scheme, host, port, path)
///   2. Scheme policy (https required unless allow_http)
///   3. Domain allowlist
///   4. DNS resolution + SSRF blocklist
///
/// On success, populates `parsed` and returns true.
/// On failure, sets `error` and returns false.
bool validate_url(const std::string& url,
                  const HttpClientConfig& config,
                  ParsedUrl& parsed,
                  std::string& error);

} // namespace etil::net
