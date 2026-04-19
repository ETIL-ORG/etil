#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Broker-sink abstract base for Manifold network transports
/// (doc B §16.5). Concrete subclasses: nats_sink (Phase 3b),
/// amqp_sink (Phase 3c). Each subclass owns a broker-client
/// connection, maps channel names to broker subjects/addresses,
/// and serializes payloads through the codec pipeline configured
/// on the RouteSpec.
///
/// The base handles:
///   - Allowlist enforcement (host:port must match configured list).
///   - Codec pipeline hook (Msg-Codec header population).
///   - Standard header population (Msg-Host / Msg-Startup / Msg-Seq /
///     Msg-OriginType / Msg-RouteTrace / Msg-HopsLeft / Session-Hmac).
///   - Audit emission on connection failure onto etil.logging.error.
///
/// Subclasses implement connect() / publish_wire() / disconnect()
/// and the broker-specific header translation.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "etil/manifold/sink.hpp"

namespace etil::manifold {

class ChannelService;

/// Configuration for a broker-backed sink. One instance per route.
struct BrokerSinkConfig {
    std::string broker_url;        // e.g. "nats://host:4222" or "amqp://host:5672"
    std::string codec;             // "json" | "msgpack" | "cbor" | "raw"
    std::string credentials_path;  // optional; path to NKey/SASL file
    std::string tls_cert_path;     // optional
    std::unordered_set<std::string> allowlist;  // permitted broker host:port pairs
};

/// Standard broker header names. Kept as constants so nats_sink and
/// amqp_sink agree on wire format.
namespace broker_headers {
    inline constexpr const char* kSessionHmac   = "Session-Hmac";
    inline constexpr const char* kMsgHost       = "Msg-Host";
    inline constexpr const char* kMsgStartup    = "Msg-Startup";
    inline constexpr const char* kMsgSeq        = "Msg-Seq";
    inline constexpr const char* kMsgOriginType = "Msg-OriginType";
    inline constexpr const char* kMsgCodec      = "Msg-Codec";
    inline constexpr const char* kMsgRouteTrace = "Msg-RouteTrace";
    inline constexpr const char* kMsgHopsLeft   = "Msg-HopsLeft";
}

class BrokerSinkBase : public ISink {
public:
    BrokerSinkBase(BrokerSinkConfig cfg,
                   std::weak_ptr<ChannelService> channels);
    ~BrokerSinkBase() override;

    BrokerSinkBase(const BrokerSinkBase&) = delete;
    BrokerSinkBase& operator=(const BrokerSinkBase&) = delete;

    /// Accept is implemented in terms of publish_wire(): the base
    /// serializes the payload via the configured codec, populates
    /// standard headers, then hands the opaque byte sequence plus
    /// header map to the subclass.
    void accept(const Message& msg) override;
    void flush() override {}

    const BrokerSinkConfig& config() const { return cfg_; }

    /// Number of messages successfully forwarded to publish_wire().
    uint64_t forwarded_count() const;

    /// Number of messages dropped because serialization or the
    /// allowlist failed.
    uint64_t dropped_count() const;

protected:
    struct WireHeaders {
        std::string session_hmac;
        std::string host;
        std::string startup;
        std::string seq;
        std::string origin_type;
        std::string codec;
        std::string route_trace;
        std::string hops_left;
    };

    /// Subclass hook: push the serialized payload + headers onto the
    /// broker. Must not throw; return false to report a broker-side
    /// failure (base emits an etil.logging.error audit).
    virtual bool publish_wire(const std::string& subject,
                              const std::vector<uint8_t>& body,
                              const WireHeaders& headers) = 0;

    /// Subclass hook: translate a Manifold channel name to a
    /// broker subject/address. Default: pass-through (valid for NATS;
    /// AMQP subclass overrides to translate `**` → `#`).
    virtual std::string translate_subject(const std::string& channel) const;

    /// Access the ChannelService this sink is attached to (for
    /// emitting audit messages and computing the session HMAC).
    /// Returns nullptr if the service has been destroyed.
    std::shared_ptr<ChannelService> channels() const;

private:
    BrokerSinkConfig cfg_;
    std::weak_ptr<ChannelService> channels_;
    mutable std::atomic<uint64_t> forwarded_{0};
    mutable std::atomic<uint64_t> dropped_{0};
};

} // namespace etil::manifold
