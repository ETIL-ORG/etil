// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/broker_source.hpp"

#include <cstdlib>
#include <sstream>
#include <typeindex>
#include <utility>

#include <nlohmann/json.hpp>

#include "etil/core/logging.hpp"
#include "etil/manifold/message.hpp"
#include "etil/manifold/service.hpp"

namespace etil::manifold {

namespace {

std::string decode_envelope_payload(const std::string& codec,
                                     const std::vector<uint8_t>& body) {
    try {
        nlohmann::json j;
        if (codec == "msgpack") {
            j = nlohmann::json::from_msgpack(body);
        } else if (codec == "cbor") {
            j = nlohmann::json::from_cbor(body);
        } else if (codec == "raw") {
            // Raw is opaque — forward as hex-ish placeholder.
            return std::string(body.begin(), body.end());
        } else {
            j = nlohmann::json::parse(std::string(body.begin(), body.end()));
        }
        if (j.contains("payload") && j["payload"].is_string()) {
            return j["payload"].get<std::string>();
        }
        return j.dump();
    } catch (const std::exception&) {
        return {};
    }
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ',') { if (!cur.empty()) out.push_back(std::move(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

} // namespace

void BrokerSourceBase::dispatch_inbound(
    const std::string& subject,
    const std::string& codec_override,
    const std::vector<uint8_t>& body,
    const std::string& session_hmac,
    const std::string& origin_host,
    int64_t origin_startup,
    int64_t origin_seq,
    const std::string& origin_type_str,
    const std::string& route_trace_csv,
    uint8_t hops_left) {
    auto svc = channels();
    if (!svc) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const std::string codec = codec_override.empty() ? cfg_.codec
                                                     : codec_override;
    std::string payload_text = decode_envelope_payload(codec, body);
    if (payload_text.empty() && !body.empty()) {
        auto log = etil::core::logging::get("etil.manifold");
        if (log) {
            log->warn("broker_source: decode failed codec={} subject={} — dropping",
                       codec, subject);
        }
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    Message m;
    m.channel = cfg_.channel_pattern;   // local republish channel
    m.payload = std::move(payload_text);
    m.payload_type = std::type_index(typeid(std::string));
    if (!session_hmac.empty()) m.tags["session_hmac"] = session_hmac;
    m.tags["broker_subject"] = subject;
    m.tags["from_broker"] = "true";

    // Reconstruct origin from wire headers when present.
    if (!origin_host.empty()) {
        // hostname view lifetime: we stash into tags and let the
        // service's own origin override where possible. For now use
        // the header host directly; it stays valid because the
        // service's origin-lookup path owns an interned string pool.
        m.origin.session_id = session_hmac;  // placeholder correlation
        m.origin.app_startup_us = origin_startup;
        m.origin.seq = origin_seq;
        m.origin.origin_type =
            (origin_type_str == "browser") ? OriginType::Browser
                                            : OriginType::Native;
    }
    if (hops_left > 0) m.hops_remaining = hops_left;
    for (auto& hop : split_csv(route_trace_csv)) {
        m.route_trace.push_back(std::move(hop));
    }

    auto outcome = svc->publish(std::move(m));
    if (outcome.accepted) {
        received_.fetch_add(1, std::memory_order_relaxed);
    } else {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace etil::manifold
