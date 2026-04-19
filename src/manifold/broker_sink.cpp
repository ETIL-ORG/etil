// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/broker_sink.hpp"

#include <atomic>
#include <sstream>
#include <string>

#include "etil/core/logging.hpp"
#include "etil/manifold/message.hpp"
#include "etil/manifold/service.hpp"

namespace etil::manifold {

namespace {

std::string origin_type_str(OriginType t) {
    return t == OriginType::Browser ? "browser" : "native";
}

std::string join_trace(const absl::InlinedVector<std::string, 4>& trace) {
    std::string out;
    for (size_t i = 0; i < trace.size(); ++i) {
        if (i) out.push_back(',');
        out.append(trace[i]);
    }
    return out;
}

std::vector<uint8_t> extract_body(const Message& msg) {
    if (msg.payload_type == std::type_index(typeid(std::string))) {
        try {
            const auto& s = std::any_cast<const std::string&>(msg.payload);
            return std::vector<uint8_t>(s.begin(), s.end());
        } catch (...) {
            return {};
        }
    }
    if (msg.payload_type == std::type_index(typeid(std::vector<uint8_t>))) {
        try {
            return std::any_cast<const std::vector<uint8_t>&>(msg.payload);
        } catch (...) {
            return {};
        }
    }
    return {};
}

} // namespace

BrokerSinkBase::BrokerSinkBase(BrokerSinkConfig cfg,
                                std::weak_ptr<ChannelService> channels)
    : cfg_(std::move(cfg)), channels_(std::move(channels)) {}

BrokerSinkBase::~BrokerSinkBase() = default;

uint64_t BrokerSinkBase::forwarded_count() const {
    return forwarded_.load(std::memory_order_relaxed);
}

uint64_t BrokerSinkBase::dropped_count() const {
    return dropped_.load(std::memory_order_relaxed);
}

std::shared_ptr<ChannelService> BrokerSinkBase::channels() const {
    return channels_.lock();
}

std::string BrokerSinkBase::translate_subject(const std::string& channel) const {
    return channel;
}

void BrokerSinkBase::accept(const Message& msg) {
    auto body = extract_body(msg);
    if (body.empty() &&
        msg.payload_type != std::type_index(typeid(std::string))) {
        // Non-string, non-bytes payload — codec pipeline should have
        // converted it. Drop and log.
        auto log = etil::core::logging::get("etil.manifold");
        if (log) {
            log->warn(
                "broker_sink: non-serialized payload on channel '{}' — "
                "missing codec transform?",
                msg.channel);
        }
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Session HMAC: tag session_id present → hash with ChannelService
    // process key (if available). Raw session_id never crosses the
    // wire.
    std::string session_hmac_b64;
    if (auto it = msg.tags.find("session_id"); it != msg.tags.end()) {
        if (auto svc = channels()) {
            session_hmac_b64 = svc->session_hmac(it->second);
        }
    }

    WireHeaders headers;
    headers.session_hmac = std::move(session_hmac_b64);
    headers.host = std::string(msg.origin.hostname);
    headers.startup = std::to_string(msg.origin.app_startup_us);
    headers.seq = std::to_string(msg.origin.seq);
    headers.origin_type = origin_type_str(msg.origin.origin_type);
    headers.codec = cfg_.codec.empty() ? std::string("json") : cfg_.codec;
    headers.route_trace = join_trace(msg.route_trace);
    headers.hops_left = std::to_string(msg.hops_remaining);

    const std::string subject = translate_subject(msg.channel);

    if (!publish_wire(subject, body, headers)) {
        auto log = etil::core::logging::get("etil.manifold");
        if (log) {
            log->error(
                "broker_sink: publish_wire failed on subject '{}' "
                "(codec={})",
                subject, headers.codec);
        }
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    forwarded_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace etil::manifold
