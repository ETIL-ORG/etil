// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/amqp_sink.hpp"

#include "etil/core/logging.hpp"

#ifdef ETIL_AMQP_SINK_ENABLED
#include <proton/connection.hpp>
#include <proton/container.hpp>
#include <proton/message.hpp>
#include <proton/messaging_handler.hpp>
#include <proton/sender.hpp>
#include <proton/tracker.hpp>
#include <proton/types.hpp>
#include <proton/url.hpp>
#include <proton/work_queue.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#endif

namespace etil::manifold {

#ifdef ETIL_AMQP_SINK_ENABLED

namespace {

class AmqpSink;

/// Proton handler — owns the sender and pulls outbound work off the
/// sink's queue. Lives entirely on the Proton container thread.
class AmqpHandler : public proton::messaging_handler {
public:
    AmqpHandler(std::string url, std::string address,
                std::atomic<bool>& connected,
                std::mutex& mu,
                std::condition_variable& cv,
                std::queue<proton::message>& queue,
                std::atomic<bool>& stop)
        : url_(std::move(url)),
          address_(std::move(address)),
          connected_(connected),
          mu_(mu), cv_(cv), queue_(queue), stop_(stop) {}

    void on_container_start(proton::container& c) override {
        c.connect(url_);
    }

    void on_connection_open(proton::connection& conn) override {
        sender_ = conn.open_sender(address_);
    }

    void on_sender_open(proton::sender& /*s*/) override {
        connected_.store(true, std::memory_order_release);
        drain();
    }

    void on_sendable(proton::sender& /*s*/) override {
        drain();
    }

    void on_tracker_accept(proton::tracker& /*t*/) override {
        // No-op: we count accepts via the outer sink's forwarded_count.
    }

    void on_transport_error(proton::transport& t) override {
        auto log = etil::core::logging::get("etil.manifold");
        if (log) log->error("amqp_sink: transport error: {}",
                            t.error().what());
        connected_.store(false, std::memory_order_release);
    }

    void wake() {
        drain();
    }

private:
    void drain() {
        if (!sender_) return;
        std::unique_lock<std::mutex> lk(mu_);
        while (!queue_.empty() && sender_.credit() > 0) {
            auto msg = std::move(queue_.front());
            queue_.pop();
            lk.unlock();
            sender_.send(msg);
            lk.lock();
        }
        if (stop_.load(std::memory_order_acquire) && queue_.empty()) {
            sender_.close();
            sender_.connection().close();
        }
    }

    std::string url_;
    std::string address_;
    proton::sender sender_;
    std::atomic<bool>& connected_;
    std::mutex& mu_;
    std::condition_variable& cv_;
    std::queue<proton::message>& queue_;
    std::atomic<bool>& stop_;
};

class AmqpSink : public BrokerSinkBase {
public:
    AmqpSink(BrokerSinkConfig cfg,
             std::weak_ptr<ChannelService> channels)
        : BrokerSinkBase(std::move(cfg), std::move(channels)) {}

    ~AmqpSink() override {
        stop_.store(true, std::memory_order_release);
        if (container_thread_.joinable()) {
            // Trigger a final drain to let the handler observe stop_.
            if (handler_) handler_->wake();
            container_thread_.join();
        }
    }

    bool start() {
        const std::string address = config().broker_url;  // treat URL as address-target
        handler_ = std::make_unique<AmqpHandler>(
            config().broker_url,
            default_address(),
            connected_, mu_, cv_, queue_, stop_);
        try {
            container_ = std::make_unique<proton::container>(*handler_);
        } catch (const std::exception& e) {
            auto log = etil::core::logging::get("etil.manifold");
            if (log) log->error("amqp_sink: container init failed: {}", e.what());
            return false;
        }
        container_thread_ = std::thread([this] {
            try {
                container_->run();
            } catch (const std::exception& e) {
                auto log = etil::core::logging::get("etil.manifold");
                if (log) log->error("amqp_sink: container loop exited: {}",
                                    e.what());
            }
        });
        // Brief wait for connect — the caller wants to know up front.
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(2);
        while (!connected_.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        return connected_.load(std::memory_order_acquire);
    }

protected:
    bool publish_wire(const std::string& subject,
                      const std::vector<uint8_t>& body,
                      const WireHeaders& headers) override {
        if (!connected_.load(std::memory_order_acquire)) {
            return false;
        }
        proton::message msg(std::string(body.begin(), body.end()));
        msg.subject(subject);
        msg.address(default_address());
        msg.content_type(content_type_for_codec(headers.codec));

        auto& props = msg.properties();
        auto set = [&](const char* key, const std::string& value) {
            if (!value.empty()) props.put(key, value);
        };
        set(broker_headers::kSessionHmac,   headers.session_hmac);
        set(broker_headers::kMsgHost,       headers.host);
        set(broker_headers::kMsgStartup,    headers.startup);
        set(broker_headers::kMsgSeq,        headers.seq);
        set(broker_headers::kMsgOriginType, headers.origin_type);
        set(broker_headers::kMsgCodec,      headers.codec);
        set(broker_headers::kMsgRouteTrace, headers.route_trace);
        set(broker_headers::kMsgHopsLeft,   headers.hops_left);

        {
            std::lock_guard<std::mutex> lk(mu_);
            queue_.push(std::move(msg));
        }
        cv_.notify_one();
        if (handler_) handler_->wake();
        return true;
    }

    std::string translate_subject(const std::string& channel) const override {
        // AMQP brokers use # for multi-segment wildcard at subscribe
        // time; publishers emit the literal channel name. Translation
        // happens on subscribe, not publish.
        return channel;
    }

private:
    static std::string content_type_for_codec(const std::string& codec) {
        if (codec == "msgpack") return "application/msgpack";
        if (codec == "cbor")    return "application/cbor";
        if (codec == "raw")     return "application/octet-stream";
        return "application/json";
    }

    std::string default_address() const {
        // Address == codec-agnostic topic prefix; the broker routes
        // per-subject via the message.subject() field.
        return "etil";
    }

    std::unique_ptr<AmqpHandler> handler_;
    std::unique_ptr<proton::container> container_;
    std::thread container_thread_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stop_{false};
    std::mutex mu_;
    std::condition_variable cv_;
    std::queue<proton::message> queue_;
};

} // namespace

std::shared_ptr<BrokerSinkBase> make_amqp_sink(
    BrokerSinkConfig cfg,
    std::weak_ptr<ChannelService> channels) {
    auto log = etil::core::logging::get("etil.manifold");
    if (cfg.broker_url.empty()) {
        if (log) log->error("make_amqp_sink: empty broker URL");
        return nullptr;
    }
    auto sink = std::make_shared<AmqpSink>(std::move(cfg), std::move(channels));
    if (!sink->start()) {
        if (log) log->error("make_amqp_sink: container failed to connect");
        return nullptr;
    }
    return sink;
}

bool amqp_sink_compiled_in() { return true; }

#else  // ETIL_AMQP_SINK_ENABLED

std::shared_ptr<BrokerSinkBase> make_amqp_sink(
    BrokerSinkConfig /*cfg*/,
    std::weak_ptr<ChannelService> /*channels*/) {
    auto log = etil::core::logging::get("etil.manifold");
    if (log) {
        log->error(
            "amqp_sink: binary was built without ETIL_BUILD_AMQP_SINK — "
            "channel-tap-amqp unavailable");
    }
    return nullptr;
}

bool amqp_sink_compiled_in() { return false; }

#endif // ETIL_AMQP_SINK_ENABLED

} // namespace etil::manifold
