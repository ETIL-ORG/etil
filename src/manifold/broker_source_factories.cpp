// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/broker_source_factories.hpp"

#include "etil/core/logging.hpp"

#ifdef ETIL_NATS_SINK_ENABLED
#include <nats/nats.h>
#include <atomic>
#include <mutex>
#endif

#ifdef ETIL_AMQP_SINK_ENABLED
#include <proton/connection.hpp>
#include <proton/container.hpp>
#include <proton/message.hpp>
#include <proton/messaging_handler.hpp>
#include <proton/receiver.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#endif

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace etil::manifold {

namespace {

int64_t parse_i64(const std::string& s) {
    if (s.empty()) return 0;
    try { return std::stoll(s); } catch (...) { return 0; }
}

uint8_t parse_u8(const std::string& s) {
    if (s.empty()) return 0;
    try { return static_cast<uint8_t>(std::stoi(s)); } catch (...) { return 0; }
}

} // namespace

// ---------------------------------------------------------------------------
// NATS source
// ---------------------------------------------------------------------------

#ifdef ETIL_NATS_SINK_ENABLED

namespace {

class NatsSource : public BrokerSourceBase {
public:
    NatsSource(BrokerSourceConfig cfg,
               std::weak_ptr<ChannelService> channels)
        : BrokerSourceBase(std::move(cfg), std::move(channels)) {}

    ~NatsSource() override { stop(); }

    bool start() {
        natsStatus st = natsConnection_ConnectTo(&conn_, config().broker_url.c_str());
        if (st != NATS_OK) {
            auto log = etil::core::logging::get("etil.manifold");
            if (log) log->error("nats_source: connect failed: {} (url={})",
                                natsStatus_GetText(st), config().broker_url);
            return false;
        }
        st = natsConnection_Subscribe(
            &sub_, conn_, config().subject.c_str(),
            &NatsSource::on_msg_static, this);
        if (st != NATS_OK) {
            auto log = etil::core::logging::get("etil.manifold");
            if (log) log->error("nats_source: subscribe failed: {}",
                                natsStatus_GetText(st));
            natsConnection_Destroy(conn_);
            conn_ = nullptr;
            return false;
        }
        return true;
    }

    void stop() override {
        if (sub_) { natsSubscription_Unsubscribe(sub_); natsSubscription_Destroy(sub_); sub_ = nullptr; }
        if (conn_) { natsConnection_Close(conn_); natsConnection_Destroy(conn_); conn_ = nullptr; }
    }

private:
    static void on_msg_static(natsConnection* /*nc*/,
                               natsSubscription* /*sub*/,
                               natsMsg* msg,
                               void* closure) {
        auto* self = static_cast<NatsSource*>(closure);
        self->on_msg(msg);
        natsMsg_Destroy(msg);
    }

    void on_msg(natsMsg* msg) {
        const char* subj = natsMsg_GetSubject(msg);
        const char* data = natsMsg_GetData(msg);
        int len = natsMsg_GetDataLength(msg);
        std::vector<uint8_t> body(data, data + len);

        auto hdr = [&](const char* key) -> std::string {
            const char* v = nullptr;
            if (natsMsgHeader_Get(msg, key, &v) == NATS_OK && v) return v;
            return {};
        };
        const std::string codec      = hdr(broker_headers::kMsgCodec);
        const std::string host       = hdr(broker_headers::kMsgHost);
        const std::string startup_s  = hdr(broker_headers::kMsgStartup);
        const std::string seq_s      = hdr(broker_headers::kMsgSeq);
        const std::string origin_t   = hdr(broker_headers::kMsgOriginType);
        const std::string hmac       = hdr(broker_headers::kSessionHmac);
        const std::string trace      = hdr(broker_headers::kMsgRouteTrace);
        const std::string hops_s     = hdr(broker_headers::kMsgHopsLeft);

        dispatch_inbound(subj ? subj : "",
                         codec, body, hmac, host,
                         parse_i64(startup_s), parse_i64(seq_s),
                         origin_t, trace, parse_u8(hops_s));
    }

    natsConnection* conn_ = nullptr;
    natsSubscription* sub_ = nullptr;
};

} // namespace

std::shared_ptr<BrokerSourceBase> make_nats_source(
    BrokerSourceConfig cfg,
    std::weak_ptr<ChannelService> channels) {
    auto src = std::make_shared<NatsSource>(std::move(cfg), std::move(channels));
    if (!src->start()) return nullptr;
    return src;
}

bool nats_source_compiled_in() { return true; }

#else  // ETIL_NATS_SINK_ENABLED

std::shared_ptr<BrokerSourceBase> make_nats_source(
    BrokerSourceConfig /*cfg*/,
    std::weak_ptr<ChannelService> /*channels*/) {
    auto log = etil::core::logging::get("etil.manifold");
    if (log) log->error("nats_source: binary built without ETIL_BUILD_NATS_SINK");
    return nullptr;
}

bool nats_source_compiled_in() { return false; }

#endif

// ---------------------------------------------------------------------------
// AMQP source
// ---------------------------------------------------------------------------

#ifdef ETIL_AMQP_SINK_ENABLED

namespace {

class AmqpSource;

class AmqpSourceHandler : public proton::messaging_handler {
public:
    AmqpSourceHandler(std::string url, std::string address,
                      AmqpSource* owner,
                      std::atomic<bool>& connected,
                      std::atomic<bool>& stop)
        : url_(std::move(url)), address_(std::move(address)),
          owner_(owner), connected_(connected), stop_(stop) {}

    void on_container_start(proton::container& c) override {
        c.connect(url_);
    }
    void on_connection_open(proton::connection& conn) override {
        conn.open_receiver(address_);
    }
    void on_receiver_open(proton::receiver& /*r*/) override {
        connected_.store(true, std::memory_order_release);
    }
    void on_message(proton::delivery& /*d*/,
                    proton::message& m) override;

    void on_transport_error(proton::transport& t) override {
        auto log = etil::core::logging::get("etil.manifold");
        if (log) log->error("amqp_source: transport error: {}",
                            t.error().what());
        connected_.store(false, std::memory_order_release);
    }

private:
    std::string url_;
    std::string address_;
    AmqpSource* owner_;
    std::atomic<bool>& connected_;
    std::atomic<bool>& stop_;
};

class AmqpSource : public BrokerSourceBase {
public:
    AmqpSource(BrokerSourceConfig cfg,
               std::weak_ptr<ChannelService> channels)
        : BrokerSourceBase(std::move(cfg), std::move(channels)) {}

    ~AmqpSource() override { stop(); }

    bool start() {
        handler_ = std::make_unique<AmqpSourceHandler>(
            config().broker_url, config().subject, this,
            connected_, stop_);
        try {
            container_ = std::make_unique<proton::container>(*handler_);
        } catch (const std::exception& e) {
            auto log = etil::core::logging::get("etil.manifold");
            if (log) log->error("amqp_source: container init failed: {}", e.what());
            return false;
        }
        thread_ = std::thread([this] {
            try { container_->run(); }
            catch (const std::exception& e) {
                auto log = etil::core::logging::get("etil.manifold");
                if (log) log->error("amqp_source: loop exited: {}", e.what());
            }
        });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!connected_.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        return connected_.load(std::memory_order_acquire);
    }

    void stop() override {
        stop_.store(true, std::memory_order_release);
        if (container_) { try { container_->stop(); } catch (...) {} }
        if (thread_.joinable()) thread_.join();
        container_.reset();
        handler_.reset();
    }

    void handle_message(const proton::message& m) {
        const std::string subject = m.subject();
        std::string codec;
        std::string hmac, host, startup_s, seq_s, origin_t, trace, hops_s;
        for (auto it = m.properties().begin(); it != m.properties().end(); ++it) {
            const std::string key = proton::coerce<std::string>(it->first);
            const std::string val = proton::coerce<std::string>(it->second);
            if (key == broker_headers::kMsgCodec)       codec = val;
            else if (key == broker_headers::kSessionHmac)   hmac = val;
            else if (key == broker_headers::kMsgHost)       host = val;
            else if (key == broker_headers::kMsgStartup)    startup_s = val;
            else if (key == broker_headers::kMsgSeq)        seq_s = val;
            else if (key == broker_headers::kMsgOriginType) origin_t = val;
            else if (key == broker_headers::kMsgRouteTrace) trace = val;
            else if (key == broker_headers::kMsgHopsLeft)   hops_s = val;
        }
        std::string payload_str;
        try { payload_str = proton::coerce<std::string>(m.body()); }
        catch (...) {}
        std::vector<uint8_t> body(payload_str.begin(), payload_str.end());
        dispatch_inbound(subject, codec, body, hmac, host,
                         parse_i64(startup_s), parse_i64(seq_s),
                         origin_t, trace, parse_u8(hops_s));
    }

private:
    std::unique_ptr<AmqpSourceHandler> handler_;
    std::unique_ptr<proton::container> container_;
    std::thread thread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_{false};
};

void AmqpSourceHandler::on_message(proton::delivery& /*d*/,
                                     proton::message& m) {
    if (owner_) owner_->handle_message(m);
}

} // namespace

std::shared_ptr<BrokerSourceBase> make_amqp_source(
    BrokerSourceConfig cfg,
    std::weak_ptr<ChannelService> channels) {
    auto src = std::make_shared<AmqpSource>(std::move(cfg), std::move(channels));
    if (!src->start()) return nullptr;
    return src;
}

bool amqp_source_compiled_in() { return true; }

#else  // ETIL_AMQP_SINK_ENABLED

std::shared_ptr<BrokerSourceBase> make_amqp_source(
    BrokerSourceConfig /*cfg*/,
    std::weak_ptr<ChannelService> /*channels*/) {
    auto log = etil::core::logging::get("etil.manifold");
    if (log) log->error("amqp_source: binary built without ETIL_BUILD_AMQP_SINK");
    return nullptr;
}

bool amqp_source_compiled_in() { return false; }

#endif

} // namespace etil::manifold
