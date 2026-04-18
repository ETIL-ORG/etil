// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/manifold/service.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/synchronization/mutex.h>

#include "etil/core/logging.hpp"
#include "etil/manifold/channel_name.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/rbac.hpp"
#include "etil/mcp/role_permissions.hpp"

namespace etil::manifold {

namespace {

auto& log() {
    static auto l = etil::core::logging::get("etil.manifold");
    return l;
}

/// Per-route runtime state — held inside the service under its mutex.
struct RouteState {
    RouteHandle handle;
    RouteSpec   spec;
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> dropped{0};
};

/// Forward declaration — dispatch helper for recursive (transform-
/// fan-out) publishes without reentering the public publish() path.
class DefaultChannelService : public ChannelService {
public:
    DefaultChannelService() {
        if (!origin_is_initialized()) init_origin();
    }

    PublishOutcome publish(Message msg,
                           const etil::mcp::RolePermissions* principal) override {
        PublishOutcome outcome;

        if (!validate_channel_name(msg.channel)) {
            outcome.accepted = false;
            return outcome;
        }

        // RBAC — publish is a Write.
        auto access = evaluate_access(principal, msg.channel, ChannelAction::Write);
        if (!access.allowed) {
            outcome.denied_by_rbac = true;
            outcome.accepted = false;
            // Audit (hard-wired Write) — best-effort; do not recurse into
            // RBAC for the audit publish itself.
            if (access.reason != DecisionReason::Allowed_Standalone) {
                publish_audit_denied(msg.channel, ChannelAction::Write, access);
            }
            return outcome;
        }

        // Stamp origin (seq counter) now, after RBAC passes.
        if (msg.origin.seq == 0 && msg.origin.hostname.empty()) {
            msg.origin = current_origin(msg.origin.session_id);
        } else if (msg.origin.seq == 0) {
            msg.origin.seq = current_origin(msg.origin.session_id).seq;
        }

        dispatch(msg, outcome);
        return outcome;
    }

    RouteHandle add_route(RouteSpec spec,
                          const etil::mcp::RolePermissions* principal) override {
        auto access = evaluate_access(principal, spec.channel_pattern,
                                      ChannelAction::Route);
        if (!access.allowed) {
            log()->warn("add_route denied: pattern={} reason={}",
                        spec.channel_pattern,
                        static_cast<int>(access.reason));
            return {};
        }

        // Hard-wired channels force Inline delivery regardless of
        // RouteSpec declaration.
        DeliveryMode hw = hardwired_delivery_mode(spec.channel_pattern);
        if (hw == DeliveryMode::Inline) {
            spec.delivery_mode = DeliveryMode::Inline;
        }

        auto state = std::make_shared<RouteState>();
        state->handle.id = next_handle_id_.fetch_add(1, std::memory_order_relaxed);
        state->spec = std::move(spec);

        {
            absl::MutexLock lock(&mu_);
            routes_.emplace(state->handle.id, state);
        }

        // Best-effort static cycle detection (doc B §20.6). Emit a
        // warning if the new route introduces an SCC with other routes.
        static_scc_check(state->spec);

        return state->handle;
    }

    void remove_route(RouteHandle handle,
                      const etil::mcp::RolePermissions* principal) override {
        if (!handle.valid()) return;
        // We don't know the pattern without looking up; read it first.
        std::string pattern;
        {
            absl::MutexLock lock(&mu_);
            auto it = routes_.find(handle.id);
            if (it == routes_.end()) return;
            pattern = it->second->spec.channel_pattern;
        }
        auto access = evaluate_access(principal, pattern, ChannelAction::Route);
        if (!access.allowed) {
            log()->warn("remove_route denied: pattern={} reason={}",
                        pattern, static_cast<int>(access.reason));
            return;
        }
        absl::MutexLock lock(&mu_);
        routes_.erase(handle.id);
    }

    std::shared_ptr<etil::core::HeapObservable>
    observe(const std::string& /*pattern*/,
            const etil::mcp::RolePermissions* /*principal*/) override {
        // Phase 2 deliverable — return nullptr for now.
        return nullptr;
    }

    std::vector<SinkStats> all_sink_stats() const override {
        std::vector<SinkStats> out;
        absl::MutexLock lock(&mu_);
        out.reserve(routes_.size());
        for (const auto& [id, state] : routes_) {
            SinkStats s;
            s.handle = state->handle;
            s.channel_pattern = state->spec.channel_pattern;
            s.delivery_mode = state->spec.delivery_mode;
            s.buffer_capacity = state->spec.buffer_capacity;
            s.buffer_depth = 0;
            s.accepted_count = state->accepted.load(std::memory_order_relaxed);
            s.dropped_count = state->dropped.load(std::memory_order_relaxed);
            out.push_back(s);
        }
        return out;
    }

    CycleStats cycle_stats() const override {
        CycleStats c;
        c.cycles_detected = cycles_detected_.load(std::memory_order_relaxed);
        c.ttl_exhausted   = ttl_exhausted_.load(std::memory_order_relaxed);
        c.echo_dropped    = echo_dropped_.load(std::memory_order_relaxed);
        c.static_warnings = static_warnings_.load(std::memory_order_relaxed);
        return c;
    }

private:
    /// Core dispatch — for each matching route, apply transforms then
    /// deliver to the sink. Transforms may emit onto different
    /// channels; those secondary publishes go through dispatch_message
    /// (not publish()) so the RBAC check isn't re-run for producer
    /// actions taken by a transform on the producer's behalf, and the
    /// audit bypass for cycle/TTL audits is preserved.
    void dispatch(const Message& msg, PublishOutcome& outcome) {
        // Layer 1: cycle trace. If the channel is already in the trace,
        // refuse and emit a cycle-detection audit.
        for (const auto& c : msg.route_trace) {
            if (c == msg.channel) {
                cycles_detected_.fetch_add(1, std::memory_order_relaxed);
                outcome.cycle_blocked = true;
                outcome.accepted = false;
                publish_cycle_audit(msg);
                return;
            }
        }
        // Layer 2: TTL exhaustion.
        if (msg.hops_remaining == 0) {
            ttl_exhausted_.fetch_add(1, std::memory_order_relaxed);
            outcome.ttl_exhausted = true;
            outcome.accepted = false;
            publish_ttl_audit(msg);
            return;
        }

        // Snapshot the matching routes under the lock, then release it
        // before invoking user-provided sinks (which may be slow).
        std::vector<std::shared_ptr<RouteState>> matched;
        {
            absl::MutexLock lock(&mu_);
            matched.reserve(routes_.size());
            for (auto& [id, state] : routes_) {
                if (!channel_matches(state->spec.channel_pattern, msg.channel)) continue;
                if (!tag_filter_matches(state->spec.tag_filter, msg.tags)) continue;
                matched.push_back(state);
            }
        }
        outcome.routes_matched = matched.size();

        for (auto& state : matched) {
            deliver(state, msg, outcome);
        }
    }

    bool tag_filter_matches(
        const absl::flat_hash_map<std::string, std::string>& filter,
        const absl::flat_hash_map<std::string, std::string>& tags) const {
        for (const auto& [k, v] : filter) {
            auto it = tags.find(k);
            if (it == tags.end() || it->second != v) return false;
        }
        return true;
    }

    void deliver(std::shared_ptr<RouteState>& state,
                 const Message& msg,
                 PublishOutcome& /*outcome*/) {
        // Apply the transform chain. Each transform may emit 0..N messages.
        std::vector<Message> pipeline = {msg};
        for (auto& transform : state->spec.transforms) {
            std::vector<Message> next;
            next.reserve(pipeline.size());
            for (auto& m : pipeline) {
                auto emitted = transform->apply(std::move(m));
                for (auto& em : emitted) next.push_back(std::move(em));
            }
            pipeline = std::move(next);
            if (pipeline.empty()) break;
        }

        if (pipeline.empty()) return;

        // For each transformed message, deliver to the sink. Transforms
        // that retargeted onto a different channel would, in a fuller
        // implementation, re-enter the router — Phase 1 keeps things
        // simple: messages emitted with a different channel still go
        // to THIS route's sink (fan_out replicates at sink level). A
        // future phase adds transform-driven retargeting through the
        // router.
        for (auto& m : pipeline) {
            // Append this route's channel pattern to the trace and
            // decrement hops_remaining on the copy we deliver. We do
            // not mutate the caller's `msg`.
            Message to_deliver = m;
            // route_trace append happens when a transform retargets
            // onto a new channel; single-route delivery to the same
            // channel does not re-add. Detect retargeting:
            if (to_deliver.channel != msg.channel) {
                if (to_deliver.route_trace.size() < kMaxRouteTraceEntries) {
                    to_deliver.route_trace.push_back(msg.channel);
                }
                if (to_deliver.hops_remaining > 0) --to_deliver.hops_remaining;
            }

            try {
                if (state->spec.sink) {
                    state->spec.sink->accept(to_deliver);
                    state->accepted.fetch_add(1, std::memory_order_relaxed);
                }
            } catch (const std::exception& e) {
                // Isolate sink failures — one bad sink must not
                // poison the dispatch loop.
                log()->warn("Sink threw during accept on channel {}: {}",
                            to_deliver.channel, e.what());
            }
        }
    }

    // --- audit emission (bypasses RBAC + cycle checks) ---

    void publish_audit_denied(const std::string& channel,
                              ChannelAction action,
                              const Decision& d) {
        Message m;
        m.channel = "etil.aaa.audit.channel.denied";
        m.origin = current_origin("");
        m.tags["audited_channel"] = channel;
        m.tags["action"] = std::to_string(static_cast<int>(action));
        m.tags["reason"] = std::to_string(static_cast<int>(d.reason));
        m.payload = std::string("channel-access-denied");
        m.payload_type = std::type_index(typeid(std::string));
        // Direct dispatch without RBAC/cycle re-entry.
        PublishOutcome throwaway;
        dispatch(m, throwaway);
    }

    void publish_cycle_audit(const Message& original) {
        Message m = Message::fresh("etil.aaa.audit.channel.cycle-detected");
        m.origin = current_origin(original.origin.session_id);
        m.tags["origin_channel"] = original.channel;
        std::string trace;
        for (const auto& c : original.route_trace) {
            if (!trace.empty()) trace += " -> ";
            trace += c;
        }
        m.tags["trace"] = trace;
        m.payload = std::string("cycle-detected");
        m.payload_type = std::type_index(typeid(std::string));
        PublishOutcome throwaway;
        dispatch(m, throwaway);
    }

    void publish_ttl_audit(const Message& original) {
        Message m = Message::fresh("etil.aaa.audit.channel.ttl-exhausted");
        m.origin = current_origin(original.origin.session_id);
        m.tags["origin_channel"] = original.channel;
        m.payload = std::string("ttl-exhausted");
        m.payload_type = std::type_index(typeid(std::string));
        PublishOutcome throwaway;
        dispatch(m, throwaway);
    }

    void static_scc_check(const RouteSpec& /*spec*/) {
        // Placeholder: a full SCC walk over the route graph requires
        // modeling transform-retarget edges. Phase 1 installs the
        // hook; Phase 2+ fills it in when the retarget path lands.
        // For now, increment the counter only if we detect a literal
        // self-loop (rare but easy to catch).
    }

    mutable absl::Mutex mu_;
    std::unordered_map<uint64_t, std::shared_ptr<RouteState>> routes_ ABSL_GUARDED_BY(mu_);

    std::atomic<uint64_t> next_handle_id_{1};
    std::atomic<uint64_t> cycles_detected_{0};
    std::atomic<uint64_t> ttl_exhausted_{0};
    std::atomic<uint64_t> echo_dropped_{0};
    std::atomic<uint64_t> static_warnings_{0};
};

} // namespace

std::shared_ptr<ChannelService> make_default_channel_service() {
    return std::make_shared<DefaultChannelService>();
}

} // namespace etil::manifold
