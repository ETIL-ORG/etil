// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/net/url_validation.hpp"
#include "etil/net/http_client_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace etil::net {

bool parse_url(const std::string& url, ParsedUrl& out) {
    out = ParsedUrl{};

    // Extract scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return false;

    out.scheme = url.substr(0, scheme_end);
    // Lowercase the scheme
    std::transform(out.scheme.begin(), out.scheme.end(),
                   out.scheme.begin(), ::tolower);

    if (out.scheme != "http" && out.scheme != "https") return false;

    size_t pos = scheme_end + 3;  // skip "://"
    if (pos >= url.size()) return false;

    // Extract host[:port]
    auto path_start = url.find('/', pos);
    std::string authority;
    if (path_start == std::string::npos) {
        authority = url.substr(pos);
        out.path = "/";
    } else {
        authority = url.substr(pos, path_start - pos);
        out.path = url.substr(path_start);
    }

    if (authority.empty()) return false;

    // Handle IPv6 literal: [::1]:port
    if (authority.front() == '[') {
        auto bracket_end = authority.find(']');
        if (bracket_end == std::string::npos) return false;
        out.host = authority.substr(1, bracket_end - 1);
        if (bracket_end + 1 < authority.size() && authority[bracket_end + 1] == ':') {
            auto port_str = authority.substr(bracket_end + 2);
            out.port = std::atoi(port_str.c_str());
            if (out.port <= 0 || out.port > 65535) return false;
        }
    } else {
        // host:port or host
        auto colon = authority.rfind(':');
        if (colon != std::string::npos) {
            // Check if everything after colon is digits (port)
            auto port_str = authority.substr(colon + 1);
            bool all_digits = !port_str.empty() &&
                std::all_of(port_str.begin(), port_str.end(), ::isdigit);
            if (all_digits) {
                out.host = authority.substr(0, colon);
                out.port = std::atoi(port_str.c_str());
                if (out.port <= 0 || out.port > 65535) return false;
            } else {
                out.host = authority;
            }
        } else {
            out.host = authority;
        }
    }

    if (out.host.empty()) return false;

    // Lowercase hostname
    std::transform(out.host.begin(), out.host.end(),
                   out.host.begin(), ::tolower);

    // Ensure path is not empty
    if (out.path.empty()) out.path = "/";

    return true;
}

bool is_ipv4_ssrf_blocked(uint32_t addr) {
    // addr is in host byte order
    uint8_t a = (addr >> 24) & 0xFF;
    uint8_t b = (addr >> 16) & 0xFF;

    // 0.0.0.0/8 — current network
    if (a == 0) return true;

    // 127.0.0.0/8 — loopback
    if (a == 127) return true;

    // 10.0.0.0/8 — RFC1918
    if (a == 10) return true;

    // 172.16.0.0/12 — RFC1918
    if (a == 172 && (b >= 16 && b <= 31)) return true;

    // 192.168.0.0/16 — RFC1918
    if (a == 192 && b == 168) return true;

    // 169.254.0.0/16 — link-local / cloud metadata
    if (a == 169 && b == 254) return true;

    return false;
}

bool is_ipv6_ssrf_blocked(const uint8_t addr[16]) {
    // ::1 — loopback
    static const uint8_t loopback[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    if (std::memcmp(addr, loopback, 16) == 0) return true;

    // :: — unspecified
    static const uint8_t unspecified[16] = {};
    if (std::memcmp(addr, unspecified, 16) == 0) return true;

    // fc00::/7 — unique local address
    if ((addr[0] & 0xFE) == 0xFC) return true;

    // fe80::/10 — link-local
    if (addr[0] == 0xFE && (addr[1] & 0xC0) == 0x80) return true;

    // ::ffff:0:0/96 — IPv4-mapped IPv6
    // Check if first 80 bits are 0 and next 16 bits are 0xFFFF
    static const uint8_t v4mapped_prefix[12] = {0,0,0,0,0,0,0,0,0,0,0xFF,0xFF};
    if (std::memcmp(addr, v4mapped_prefix, 12) == 0) {
        // Extract embedded IPv4 and check it
        uint32_t ipv4 = (static_cast<uint32_t>(addr[12]) << 24) |
                        (static_cast<uint32_t>(addr[13]) << 16) |
                        (static_cast<uint32_t>(addr[14]) << 8) |
                        static_cast<uint32_t>(addr[15]);
        return is_ipv4_ssrf_blocked(ipv4);
    }

    return false;
}

bool resolve_and_check_ssrf(const std::string& hostname,
                            std::string& resolved_ip,
                            std::string& error) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    struct addrinfo* result = nullptr;
    int ret = getaddrinfo(hostname.c_str(), nullptr, &hints, &result);
    if (ret != 0) {
        error = "DNS resolution failed for '" + hostname + "': " +
                gai_strerror(ret);
        return false;
    }

    bool has_safe_addr = false;
    bool all_blocked = true;

    for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            auto* sa = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
            uint32_t addr = ntohl(sa->sin_addr.s_addr);
            if (!is_ipv4_ssrf_blocked(addr)) {
                if (!has_safe_addr) {
                    // Capture the first safe IP as a string
                    char buf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
                    resolved_ip = buf;
                }
                has_safe_addr = true;
                all_blocked = false;
            }
        } else if (rp->ai_family == AF_INET6) {
            auto* sa = reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
            if (!is_ipv6_ssrf_blocked(sa->sin6_addr.s6_addr)) {
                if (!has_safe_addr) {
                    char buf[INET6_ADDRSTRLEN];
                    inet_ntop(AF_INET6, &sa->sin6_addr, buf, sizeof(buf));
                    resolved_ip = buf;
                }
                has_safe_addr = true;
                all_blocked = false;
            }
        }
    }

    freeaddrinfo(result);

    if (!has_safe_addr || all_blocked) {
        error = "All resolved addresses for '" + hostname +
                "' are in the SSRF blocklist (private/loopback/link-local)";
        return false;
    }

    return true;
}

bool is_domain_allowed(const std::string& domain,
                       const std::vector<std::string>& allowed_domains) {
    if (allowed_domains.empty()) return false;

    for (const auto& pattern : allowed_domains) {
        // Wildcard: allow everything
        if (pattern == "*") return true;

        // Wildcard subdomain: *.example.com
        if (pattern.size() > 2 && pattern[0] == '*' && pattern[1] == '.') {
            auto suffix = pattern.substr(1);  // ".example.com"
            // domain must end with the suffix and be longer (has a subdomain)
            if (domain.size() > suffix.size() &&
                domain.compare(domain.size() - suffix.size(),
                              suffix.size(), suffix) == 0) {
                return true;
            }
        }

        // Exact match
        if (domain == pattern) return true;
    }

    return false;
}

bool validate_url(const std::string& url,
                  const HttpClientConfig& config,
                  ParsedUrl& parsed,
                  std::string& error) {
    // 1. Parse URL
    if (!parse_url(url, parsed)) {
        error = "Invalid URL: '" + url + "'";
        return false;
    }

    // 2. Scheme policy
    if (parsed.scheme == "http" && !config.allow_http) {
        error = "Plain HTTP not allowed (HTTPS required)";
        return false;
    }

    // 3. Domain allowlist
    if (!is_domain_allowed(parsed.host, config.allowed_domains)) {
        error = "Domain '" + parsed.host + "' is not in the allowlist";
        return false;
    }

    // 4. DNS resolution + SSRF blocklist
    //    Resolves once and captures the safe IP so callers can connect
    //    by IP (preventing DNS rebinding / TOCTOU attacks).
    if (!resolve_and_check_ssrf(parsed.host, parsed.resolved_ip, error)) {
        return false;
    }

    return true;
}

} // namespace etil::net
