// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/transforms.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <sstream>
#include <typeindex>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "etil/core/logging.hpp"
#include "etil/manifold/channel_name.hpp"

namespace etil::manifold {

namespace {

int level_rank(const std::string& s) {
    auto lvl = etil::core::logging::level_from_string(s);
    return static_cast<int>(lvl);
}

class LevelFilter : public ITransform {
public:
    explicit LevelFilter(std::string min_level)
        : min_rank_(level_rank(min_level)) {}

    std::vector<Message> apply(Message msg) override {
        auto it = msg.tags.find("level");
        if (it == msg.tags.end()) return {std::move(msg)};  // pass-through
        if (level_rank(it->second) < min_rank_) return {};
        return {std::move(msg)};
    }

private:
    int min_rank_;
};

class ChannelFilter : public ITransform {
public:
    explicit ChannelFilter(std::string pattern)
        : pattern_(std::move(pattern)) {}

    std::vector<Message> apply(Message msg) override {
        if (!channel_matches(pattern_, msg.channel)) return {};
        return {std::move(msg)};
    }

private:
    std::string pattern_;
};

class TagFilter : public ITransform {
public:
    TagFilter(std::string key, std::string value)
        : key_(std::move(key)), value_(std::move(value)) {}

    std::vector<Message> apply(Message msg) override {
        auto it = msg.tags.find(key_);
        if (it == msg.tags.end() || it->second != value_) return {};
        return {std::move(msg)};
    }

private:
    std::string key_;
    std::string value_;
};

class TagAnnotator : public ITransform {
public:
    TagAnnotator(std::string key, std::string value)
        : key_(std::move(key)), value_(std::move(value)) {}

    std::vector<Message> apply(Message msg) override {
        msg.tags[key_] = value_;
        return {std::move(msg)};
    }

private:
    std::string key_;
    std::string value_;
};

class Formatter : public ITransform {
public:
    std::vector<Message> apply(Message msg) override {
        if (msg.payload.type() != typeid(std::string)) {
            std::ostringstream os;
            os << "[" << msg.channel << "]";
            for (auto& [k, v] : msg.tags) os << " " << k << "=" << v;
            msg.payload = os.str();
            msg.payload_type = std::type_index(typeid(std::string));
        }
        return {std::move(msg)};
    }
};

class RateLimiter : public ITransform {
public:
    explicit RateLimiter(double max_per_second)
        : rate_(max_per_second),
          tokens_(max_per_second) {}

    std::vector<Message> apply(Message msg) override {
        std::lock_guard<std::mutex> lock(mu_);
        auto now = std::chrono::steady_clock::now();
        if (!initialized_) {
            last_refill_ = now;
            initialized_ = true;
        }
        auto elapsed = std::chrono::duration<double>(now - last_refill_).count();
        last_refill_ = now;
        tokens_ = std::min(tokens_ + elapsed * rate_, rate_);
        if (tokens_ >= 1.0) {
            tokens_ -= 1.0;
            return {std::move(msg)};
        }
        ++dropped_;
        return {};
    }

private:
    double rate_;
    double tokens_;
    std::chrono::steady_clock::time_point last_refill_;
    bool initialized_ = false;
    std::mutex mu_;
    uint64_t dropped_ = 0;
};

class FanOut : public ITransform {
public:
    explicit FanOut(std::vector<std::string> extras)
        : extras_(std::move(extras)) {}

    std::vector<Message> apply(Message msg) override {
        std::vector<Message> out;
        out.reserve(extras_.size() + 1);
        out.push_back(msg);  // original channel preserved
        for (const auto& chan : extras_) {
            Message copy = msg;
            copy.channel = chan;
            out.push_back(std::move(copy));
        }
        return out;
    }

private:
    std::vector<std::string> extras_;
};

class Sampler : public ITransform {
public:
    explicit Sampler(uint64_t one_in_n)
        : one_in_n_(one_in_n == 0 ? 1 : one_in_n) {}

    std::vector<Message> apply(Message msg) override {
        uint64_t c = counter_.fetch_add(1, std::memory_order_relaxed);
        if ((c % one_in_n_) == 0) return {std::move(msg)};
        return {};
    }

private:
    uint64_t one_in_n_;
    std::atomic<uint64_t> counter_{0};
};

nlohmann::json build_envelope(const Message& msg) {
    nlohmann::json j;
    j["channel"] = msg.channel;
    j["origin"] = {
        {"host", std::string(msg.origin.hostname)},
        {"app_start_us", msg.origin.app_startup_us},
        {"session", msg.origin.session_id},
        {"seq", msg.origin.seq},
        {"origin_type",
         msg.origin.origin_type == OriginType::Browser ? "browser" : "native"},
    };
    j["ts_us"] = std::chrono::duration_cast<std::chrono::microseconds>(
        msg.timestamp.time_since_epoch()).count();
    nlohmann::json tags = nlohmann::json::object();
    for (auto& [k, v] : msg.tags) tags[k] = v;
    j["tags"] = std::move(tags);
    if (msg.payload.type() == typeid(std::string)) {
        try {
            j["payload"] = std::any_cast<std::string>(msg.payload);
        } catch (...) {
            j["payload"] = nullptr;
        }
    } else {
        j["payload"] = nullptr;
    }
    return j;
}

class JsonEncoder : public ITransform {
public:
    std::vector<Message> apply(Message msg) override {
        auto j = build_envelope(msg);
        msg.payload = j.dump();
        msg.payload_type = std::type_index(typeid(std::string));
        return {std::move(msg)};
    }
};

class MsgpackEncoder : public ITransform {
public:
    std::vector<Message> apply(Message msg) override {
        auto j = build_envelope(msg);
        std::vector<uint8_t> bytes = nlohmann::json::to_msgpack(j);
        msg.payload = std::move(bytes);
        msg.payload_type = std::type_index(typeid(std::vector<uint8_t>));
        return {std::move(msg)};
    }
};

class CborEncoder : public ITransform {
public:
    std::vector<Message> apply(Message msg) override {
        auto j = build_envelope(msg);
        std::vector<uint8_t> bytes = nlohmann::json::to_cbor(j);
        msg.payload = std::move(bytes);
        msg.payload_type = std::type_index(typeid(std::vector<uint8_t>));
        return {std::move(msg)};
    }
};

class RawPassthrough : public ITransform {
public:
    std::vector<Message> apply(Message msg) override {
        if (msg.payload_type != std::type_index(typeid(std::vector<uint8_t>))) {
            auto log = etil::core::logging::get("etil.manifold");
            if (log) {
                log->error(
                    "raw_passthrough: expected std::vector<uint8_t> "
                    "payload on channel '{}', got type_index mismatch — dropping",
                    msg.channel);
            }
            return {};
        }
        return {std::move(msg)};
    }
};

} // namespace

std::shared_ptr<ITransform> make_level_filter(std::string min_level) {
    return std::make_shared<LevelFilter>(std::move(min_level));
}

std::shared_ptr<ITransform> make_channel_filter(std::string pattern) {
    return std::make_shared<ChannelFilter>(std::move(pattern));
}

std::shared_ptr<ITransform> make_tag_filter(std::string key, std::string value) {
    return std::make_shared<TagFilter>(std::move(key), std::move(value));
}

std::shared_ptr<ITransform> make_tag_annotator(std::string key, std::string value) {
    return std::make_shared<TagAnnotator>(std::move(key), std::move(value));
}

std::shared_ptr<ITransform> make_formatter() {
    return std::make_shared<Formatter>();
}

std::shared_ptr<ITransform> make_rate_limiter(double max_per_second) {
    return std::make_shared<RateLimiter>(max_per_second);
}

std::shared_ptr<ITransform> make_fan_out(std::vector<std::string> extra_channels) {
    return std::make_shared<FanOut>(std::move(extra_channels));
}

std::shared_ptr<ITransform> make_sampler(uint64_t one_in_n) {
    return std::make_shared<Sampler>(one_in_n);
}

std::shared_ptr<ITransform> make_json_encoder() {
    return std::make_shared<JsonEncoder>();
}

std::shared_ptr<ITransform> make_msgpack_encoder() {
    return std::make_shared<MsgpackEncoder>();
}

std::shared_ptr<ITransform> make_cbor_encoder() {
    return std::make_shared<CborEncoder>();
}

std::shared_ptr<ITransform> make_raw_passthrough() {
    return std::make_shared<RawPassthrough>();
}

} // namespace etil::manifold
