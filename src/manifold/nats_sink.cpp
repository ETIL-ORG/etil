// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/nats_sink.hpp"

#include "etil/core/logging.hpp"

#ifdef ETIL_NATS_SINK_ENABLED
#include <nats/nats.h>
#include <mutex>
#endif

namespace etil::manifold {

#ifdef ETIL_NATS_SINK_ENABLED

namespace {

class NatsSink : public BrokerSinkBase {
public:
    NatsSink(BrokerSinkConfig cfg,
             std::weak_ptr<ChannelService> channels,
             natsConnection* conn)
        : BrokerSinkBase(std::move(cfg), std::move(channels)),
          conn_(conn) {}

    ~NatsSink() override {
        if (conn_) {
            natsConnection_Close(conn_);
            natsConnection_Destroy(conn_);
        }
    }

protected:
    bool publish_wire(const std::string& subject,
                      const std::vector<uint8_t>& body,
                      const WireHeaders& headers) override {
        natsMsg* msg = nullptr;
        if (natsMsg_Create(&msg,
                           subject.c_str(),
                           /*reply*/ nullptr,
                           reinterpret_cast<const char*>(body.data()),
                           static_cast<int>(body.size())) != NATS_OK) {
            return false;
        }
        // Header setters — empty strings stay off the wire.
        auto set = [&](const char* key, const std::string& value) {
            if (!value.empty()) {
                natsMsgHeader_Set(msg, key, value.c_str());
            }
        };
        set(broker_headers::kSessionHmac,   headers.session_hmac);
        set(broker_headers::kMsgHost,       headers.host);
        set(broker_headers::kMsgStartup,    headers.startup);
        set(broker_headers::kMsgSeq,        headers.seq);
        set(broker_headers::kMsgOriginType, headers.origin_type);
        set(broker_headers::kMsgCodec,      headers.codec);
        set(broker_headers::kMsgRouteTrace, headers.route_trace);
        set(broker_headers::kMsgHopsLeft,   headers.hops_left);

        natsStatus st;
        {
            std::lock_guard<std::mutex> lk(mu_);
            st = natsConnection_PublishMsg(conn_, msg);
        }
        natsMsg_Destroy(msg);
        return st == NATS_OK;
    }

private:
    natsConnection* conn_ = nullptr;
    std::mutex mu_;
};

} // namespace

std::shared_ptr<BrokerSinkBase> make_nats_sink(
    BrokerSinkConfig cfg,
    std::weak_ptr<ChannelService> channels) {
    auto log = etil::core::logging::get("etil.manifold");

    if (cfg.broker_url.empty()) {
        if (log) log->error("make_nats_sink: empty broker URL");
        return nullptr;
    }

    natsConnection* conn = nullptr;
    natsOptions* opts = nullptr;
    natsStatus st = natsOptions_Create(&opts);
    if (st != NATS_OK) {
        if (log) log->error("make_nats_sink: natsOptions_Create failed: {}",
                            natsStatus_GetText(st));
        return nullptr;
    }
    natsOptions_SetURL(opts, cfg.broker_url.c_str());
    natsOptions_SetAllowReconnect(opts, true);
    natsOptions_SetMaxReconnect(opts, -1);
    if (!cfg.credentials_path.empty()) {
        natsOptions_SetUserCredentialsFromFiles(
            opts, cfg.credentials_path.c_str(), nullptr);
    }

    st = natsConnection_Connect(&conn, opts);
    natsOptions_Destroy(opts);
    if (st != NATS_OK) {
        if (log) log->error("make_nats_sink: connect failed: {} (url={})",
                            natsStatus_GetText(st), cfg.broker_url);
        return nullptr;
    }
    return std::make_shared<NatsSink>(std::move(cfg),
                                      std::move(channels), conn);
}

bool nats_sink_compiled_in() { return true; }

#else  // ETIL_NATS_SINK_ENABLED

std::shared_ptr<BrokerSinkBase> make_nats_sink(
    BrokerSinkConfig /*cfg*/,
    std::weak_ptr<ChannelService> /*channels*/) {
    auto log = etil::core::logging::get("etil.manifold");
    if (log) {
        log->error(
            "nats_sink: binary was built without ETIL_BUILD_NATS_SINK — "
            "channel-tap-nats unavailable");
    }
    return nullptr;
}

bool nats_sink_compiled_in() { return false; }

#endif // ETIL_NATS_SINK_ENABLED

} // namespace etil::manifold
