// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/net/url_validation.hpp"
#include "etil/net/http_client_config.hpp"

#include <gtest/gtest.h>

namespace etil::net {

// ── URL Parsing ──────────────────────────────────────────────────────────

TEST(UrlValidation, ParseHttpUrl) {
    ParsedUrl p;
    ASSERT_TRUE(parse_url("http://example.com/data.csv", p));
    EXPECT_EQ(p.scheme, "http");
    EXPECT_EQ(p.host, "example.com");
    EXPECT_EQ(p.port, 0);
    EXPECT_EQ(p.path, "/data.csv");
    EXPECT_EQ(p.effective_port(), 80);
}

TEST(UrlValidation, ParseHttpsUrlWithPort) {
    ParsedUrl p;
    ASSERT_TRUE(parse_url("https://api.example.com:8443/v1/data?fmt=csv", p));
    EXPECT_EQ(p.scheme, "https");
    EXPECT_EQ(p.host, "api.example.com");
    EXPECT_EQ(p.port, 8443);
    EXPECT_EQ(p.path, "/v1/data?fmt=csv");
    EXPECT_EQ(p.effective_port(), 8443);
}

TEST(UrlValidation, ParseHttpsDefaultPort) {
    ParsedUrl p;
    ASSERT_TRUE(parse_url("https://secure.example.com/", p));
    EXPECT_EQ(p.scheme, "https");
    EXPECT_EQ(p.effective_port(), 443);
}

TEST(UrlValidation, ParseNoPath) {
    ParsedUrl p;
    ASSERT_TRUE(parse_url("http://example.com", p));
    EXPECT_EQ(p.path, "/");
}

TEST(UrlValidation, ParseCaseInsensitive) {
    ParsedUrl p;
    ASSERT_TRUE(parse_url("HTTP://EXAMPLE.COM/PATH", p));
    EXPECT_EQ(p.scheme, "http");
    EXPECT_EQ(p.host, "example.com");
    EXPECT_EQ(p.path, "/PATH");
}

TEST(UrlValidation, RejectFtpScheme) {
    ParsedUrl p;
    EXPECT_FALSE(parse_url("ftp://example.com/file", p));
}

TEST(UrlValidation, RejectNoScheme) {
    ParsedUrl p;
    EXPECT_FALSE(parse_url("example.com/file", p));
}

TEST(UrlValidation, RejectEmptyHost) {
    ParsedUrl p;
    EXPECT_FALSE(parse_url("http:///path", p));
}

TEST(UrlValidation, RejectBadPort) {
    ParsedUrl p;
    EXPECT_FALSE(parse_url("http://example.com:99999/path", p));
}

TEST(UrlValidation, ParseIpv6Literal) {
    ParsedUrl p;
    ASSERT_TRUE(parse_url("http://[::1]:8080/path", p));
    EXPECT_EQ(p.host, "::1");
    EXPECT_EQ(p.port, 8080);
}

// ── SSRF Blocklist (IPv4) ────────────────────────────────────────────────

TEST(SsrfBlocklist, BlocksLoopback) {
    // 127.0.0.1 = 0x7F000001
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0x7F000001));
    // 127.255.255.255
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0x7FFFFFFF));
}

TEST(SsrfBlocklist, BlocksRfc1918_10) {
    // 10.0.0.1
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0x0A000001));
    // 10.255.255.255
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0x0AFFFFFF));
}

TEST(SsrfBlocklist, BlocksRfc1918_172) {
    // 172.16.0.1
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0xAC100001));
    // 172.31.255.255
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0xAC1FFFFF));
    // 172.15.255.255 — NOT blocked (outside /12)
    EXPECT_FALSE(is_ipv4_ssrf_blocked(0xAC0FFFFF));
    // 172.32.0.0 — NOT blocked
    EXPECT_FALSE(is_ipv4_ssrf_blocked(0xAC200000));
}

TEST(SsrfBlocklist, BlocksRfc1918_192) {
    // 192.168.0.1
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0xC0A80001));
    // 192.168.255.255
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0xC0A8FFFF));
}

TEST(SsrfBlocklist, BlocksLinkLocal) {
    // 169.254.169.254 (cloud metadata)
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0xA9FEA9FE));
    // 169.254.0.1
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0xA9FE0001));
}

TEST(SsrfBlocklist, BlocksCurrentNetwork) {
    // 0.0.0.0
    EXPECT_TRUE(is_ipv4_ssrf_blocked(0x00000000));
}

TEST(SsrfBlocklist, AllowsPublicIp) {
    // 8.8.8.8 (Google DNS)
    EXPECT_FALSE(is_ipv4_ssrf_blocked(0x08080808));
    // 1.1.1.1 (Cloudflare)
    EXPECT_FALSE(is_ipv4_ssrf_blocked(0x01010101));
    // 104.16.0.1 (Cloudflare CDN)
    EXPECT_FALSE(is_ipv4_ssrf_blocked(0x68100001));
}

// ── SSRF Blocklist (IPv6) ────────────────────────────────────────────────

TEST(SsrfBlocklistV6, BlocksLoopback) {
    uint8_t addr[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    EXPECT_TRUE(is_ipv6_ssrf_blocked(addr));
}

TEST(SsrfBlocklistV6, BlocksUnspecified) {
    uint8_t addr[16] = {};
    EXPECT_TRUE(is_ipv6_ssrf_blocked(addr));
}

TEST(SsrfBlocklistV6, BlocksUniqueLocal) {
    // fc00::1
    uint8_t addr[16] = {0xFC,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    EXPECT_TRUE(is_ipv6_ssrf_blocked(addr));
    // fd12::1
    uint8_t addr2[16] = {0xFD,0x12,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    EXPECT_TRUE(is_ipv6_ssrf_blocked(addr2));
}

TEST(SsrfBlocklistV6, BlocksLinkLocal) {
    // fe80::1
    uint8_t addr[16] = {0xFE,0x80,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
    EXPECT_TRUE(is_ipv6_ssrf_blocked(addr));
}

TEST(SsrfBlocklistV6, BlocksIpv4Mapped127) {
    // ::ffff:127.0.0.1
    uint8_t addr[16] = {0,0,0,0,0,0,0,0,0,0,0xFF,0xFF,127,0,0,1};
    EXPECT_TRUE(is_ipv6_ssrf_blocked(addr));
}

TEST(SsrfBlocklistV6, AllowsIpv4MappedPublic) {
    // ::ffff:8.8.8.8
    uint8_t addr[16] = {0,0,0,0,0,0,0,0,0,0,0xFF,0xFF,8,8,8,8};
    EXPECT_FALSE(is_ipv6_ssrf_blocked(addr));
}

TEST(SsrfBlocklistV6, AllowsPublicIpv6) {
    // 2607:f8b0:4004:800::200e (Google)
    uint8_t addr[16] = {0x26,0x07,0xF8,0xB0,0x40,0x04,0x08,0x00,
                         0,0,0,0,0,0,0x20,0x0E};
    EXPECT_FALSE(is_ipv6_ssrf_blocked(addr));
}

// ── Domain Allowlist ─────────────────────────────────────────────────────

TEST(DomainAllowlist, EmptyDeniesAll) {
    EXPECT_FALSE(is_domain_allowed("example.com", {}));
}

TEST(DomainAllowlist, WildcardAllowsAll) {
    EXPECT_TRUE(is_domain_allowed("anything.example.com", {"*"}));
    EXPECT_TRUE(is_domain_allowed("evil.com", {"*"}));
}

TEST(DomainAllowlist, ExactMatch) {
    EXPECT_TRUE(is_domain_allowed("example.com", {"example.com"}));
    EXPECT_FALSE(is_domain_allowed("other.com", {"example.com"}));
}

TEST(DomainAllowlist, WildcardSubdomain) {
    std::vector<std::string> allowed = {"*.example.com"};
    EXPECT_TRUE(is_domain_allowed("sub.example.com", allowed));
    EXPECT_TRUE(is_domain_allowed("a.b.example.com", allowed));
    // *.example.com does NOT match example.com itself
    EXPECT_FALSE(is_domain_allowed("example.com", allowed));
    EXPECT_FALSE(is_domain_allowed("other.com", allowed));
}

TEST(DomainAllowlist, MultiplePatterns) {
    std::vector<std::string> allowed = {"api.weather.gov", "*.github.com"};
    EXPECT_TRUE(is_domain_allowed("api.weather.gov", allowed));
    EXPECT_TRUE(is_domain_allowed("raw.github.com", allowed));
    EXPECT_FALSE(is_domain_allowed("evil.com", allowed));
}

// ── DNS Resolution + SSRF ────────────────────────────────────────────────

TEST(ResolveSsrf, BlocksLocalhostResolution) {
    std::string resolved_ip, error;
    EXPECT_FALSE(resolve_and_check_ssrf("localhost", resolved_ip, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(resolved_ip.empty());
}

TEST(ResolveSsrf, AllowsPublicDomain) {
    // This test requires network, so skip in offline environments
    std::string resolved_ip, error;
    bool ok = resolve_and_check_ssrf("dns.google", resolved_ip, error);
    if (!ok) {
        // DNS may not be available in test environment
        GTEST_SKIP() << "DNS resolution not available: " << error;
    }
    EXPECT_TRUE(ok);
    // resolved_ip should be populated with the first safe IP
    EXPECT_FALSE(resolved_ip.empty());
}

TEST(ResolveSsrf, ResolvedIpPopulatedForPublicHost) {
    // Verify that validate_url populates parsed.resolved_ip
    HttpClientConfig cfg;
    cfg.allowed_domains = {"*"};
    cfg.allow_http = true;
    ParsedUrl parsed;
    std::string error;
    bool ok = validate_url("http://dns.google/", cfg, parsed, error);
    if (!ok) {
        GTEST_SKIP() << "DNS resolution not available: " << error;
    }
    EXPECT_FALSE(parsed.resolved_ip.empty());
}

// ── Full validate_url ────────────────────────────────────────────────────

TEST(ValidateUrl, RejectsEmptyAllowlist) {
    HttpClientConfig cfg;
    // No allowlist = deny all
    ParsedUrl parsed;
    std::string error;
    EXPECT_FALSE(validate_url("https://example.com/", cfg, parsed, error));
    EXPECT_NE(error.find("not in the allowlist"), std::string::npos);
}

TEST(ValidateUrl, RejectsHttpWhenDisabled) {
    HttpClientConfig cfg;
    cfg.allowed_domains = {"example.com"};
    cfg.allow_http = false;
    ParsedUrl parsed;
    std::string error;
    EXPECT_FALSE(validate_url("http://example.com/", cfg, parsed, error));
    EXPECT_NE(error.find("HTTPS required"), std::string::npos);
}

TEST(ValidateUrl, RejectsSsrfLoopback) {
    HttpClientConfig cfg;
    cfg.allowed_domains = {"*"};  // Allow all domains
    ParsedUrl parsed;
    std::string error;
    EXPECT_FALSE(validate_url("http://localhost/", cfg, parsed, error));
    EXPECT_FALSE(error.empty());
}

} // namespace etil::net
