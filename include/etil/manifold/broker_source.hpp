#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Broker-source abstract base — counterpart to BrokerSinkBase.
/// A source subscribes to a broker subject/address, decodes inbound
/// messages via the configured codec, reconstructs MessageOrigin
/// from the wire headers, and re-publishes into the local
/// ChannelService on the specified channel pattern.
///
/// Sources are installed via TIL words `channel-source-nats` and
/// `channel-source-amqp` (doc B §16.5 broker-source bullet + plan
/// doc 20260419A §Phase 3d). When the producer IS this same
/// process (same hostname + app_startup_us), the destination route
/// should opt in to `reject_own_origin` so the echo gets dropped
/// at cycle-detection layer 3.

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace etil::manifold {

class ChannelService;

struct BrokerSourceConfig {
    std::string broker_url;        // e.g. "nats://host:4222" or "amqp://host:5672"
    std::string codec;             // "json" | "msgpack" | "cbor" | "raw"
    std::string subject;           // broker-side subscription subject/address
    std::string channel_pattern;   // local channel name to republish under
    std::string credentials_path;  // optional
    std::string tls_cert_path;     // optional
};

class BrokerSourceBase {
public:
    explicit BrokerSourceBase(BrokerSourceConfig cfg,
                               std::weak_ptr<ChannelService> channels)
        : cfg_(std::move(cfg)), channels_(std::move(channels)) {}

    virtual ~BrokerSourceBase() = default;

    BrokerSourceBase(const BrokerSourceBase&) = delete;
    BrokerSourceBase& operator=(const BrokerSourceBase&) = delete;

    const BrokerSourceConfig& config() const { return cfg_; }

    /// Number of messages successfully decoded and published locally.
    uint64_t received_count() const {
        return received_.load(std::memory_order_relaxed);
    }

    /// Number of inbound messages dropped because of decode failure
    /// or local publish rejection.
    uint64_t dropped_count() const {
        return dropped_.load(std::memory_order_relaxed);
    }

    /// Stop the subscription and release broker resources.
    virtual void stop() = 0;

protected:
    /// Decode a single inbound wire-message onto the local
    /// ChannelService. Called by subclasses from their broker
    /// callback. Preserves MessageOrigin when headers are present;
    /// stamps a fresh one otherwise.
    void dispatch_inbound(const std::string& subject,
                          const std::string& codec_override,
                          const std::vector<uint8_t>& body,
                          const std::string& session_hmac,
                          const std::string& origin_host,
                          int64_t origin_startup,
                          int64_t origin_seq,
                          const std::string& origin_type_str,
                          const std::string& route_trace_csv,
                          uint8_t hops_left);

    std::atomic<uint64_t> received_{0};
    std::atomic<uint64_t> dropped_{0};

private:
    BrokerSourceConfig cfg_;
    std::weak_ptr<ChannelService> channels_;

    std::shared_ptr<ChannelService> channels() const { return channels_.lock(); }
};

} // namespace etil::manifold
