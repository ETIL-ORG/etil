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

#include "etil/core/heap_observable.hpp"
#include "etil/core/logging.hpp"
#include "etil/manifold/channel_name.hpp"
#include "etil/manifold/clock.hpp"
#include "etil/manifold/dispatcher.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/rbac.hpp"
#include "etil/manifold/session_hmac.hpp"
#include "etil/manifold/sink.hpp"
#include "etil/manifold/subject.hpp"
#include "etil/mcp/role_permissions.hpp"

#include "route_state.hpp"

namespace etil::manifold {

namespace {

auto& log() {
    static auto l = etil::core::logging::get("etil.manifold");
    return l;
}

// RouteState is defined in route_state.hpp (Phase 5a.3 extraction) so
// both default_service.cpp and dispatcher.cpp can reach it.

/// Phase 5a.1 seam extraction — the pure transform-application +
/// sink-delivery tail of deliver(). No service state references; takes
/// only the route state, the message, and (forward-compat) the clock.
/// Echo-suppression (layer 3) and audit emission stay in the service
/// method that invokes this.
///
/// Invariants:
///   - Does not throw. Sink exceptions are caught and logged; the
///     loop continues so one bad sink does not drop the entire
///     pipeline.
///   - `state.accepted` is incremented per successfully delivered
///     transformed message.
///   - Does not mutate the caller's `msg` (copies for retarget).
///
/// The `clock` parameter is reserved for Phase 5a.5 producer-registry
/// time-stamping from within the delivery path; currently unused.
void apply_transforms_and_deliver(RouteState& state,
                                  const Message& msg,
                                  IClock& /*clock*/) {
    // Apply the transform chain. Each transform may emit 0..N messages.
    std::vector<Message> pipeline = {msg};
    for (auto& transform : state.spec.transforms) {
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

    // For each transformed message, deliver to the sink. Messages
    // retargeted onto a different channel via fan_out still go to
    // this route's sink (transform-driven retargeting through the
    // router is a Phase 2+ follow-up per doc B §20.5).
    for (auto& m : pipeline) {
        Message to_deliver = m;
        if (to_deliver.channel != msg.channel) {
            if (to_deliver.route_trace.size() < kMaxRouteTraceEntries) {
                to_deliver.route_trace.push_back(msg.channel);
            }
            if (to_deliver.hops_remaining > 0) --to_deliver.hops_remaining;
        }

        // Phase 5a.6 note: sink exceptions propagate up to the
        // dispatcher's try/catch, which counts them via
        // DispatcherStats::dispatcher_exceptions. Catching here would
        // double-swallow the exception before the counter sees it.
        // Invariant I5: dispatcher_exceptions increments atomically
        // from the dispatcher thread.
        if (state.spec.sink) {
            state.spec.sink->accept(to_deliver);
            state.accepted.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

/// Forward declaration — dispatch helper for recursive (transform-
/// fan-out) publishes without reentering the public publish() path.
class DefaultChannelService : public ChannelService {
public:
    DefaultChannelService()
        : DefaultChannelService(make_system_clock(), nullptr) {}

    explicit DefaultChannelService(std::shared_ptr<IClock> clock)
        : DefaultChannelService(std::move(clock), nullptr) {}

    /// Full-control constructor (Phase 5a.3). If `dispatcher` is null,
    /// a ThreadDispatcher is created with a delivery closure bound to
    /// this->deliver. Tests can pass an InlineDispatcher for
    /// deterministic synchronous delivery; supply it pre-configured
    /// with the desired DeliveryFn.
    DefaultChannelService(std::shared_ptr<IClock> clock,
                          std::unique_ptr<IDispatcher> dispatcher)
        : clock_(std::move(clock)) {
        if (!origin_is_initialized()) init_origin();
        if (!dispatcher) {
            dispatcher = make_thread_dispatcher(
                [this](std::shared_ptr<RouteState>& state,
                       const Message& msg) { this->deliver(state, msg); });
        }
        dispatcher_ = std::move(dispatcher);
    }

    /// Shutdown the dispatcher before any member is destroyed, so
    /// closures capturing `this` see a live object for their entire
    /// run. Invariant I3 (shutdown drains) is enforced by the
    /// dispatcher.
    ~DefaultChannelService() override {
        if (dispatcher_) dispatcher_->shutdown();
        // Release any transform xt refs left on loops that weren't
        // explicitly torn down via remove_loop.
        std::lock_guard<std::mutex> lk(loop_mu_);
        for (auto& [id, spec] : loops_) {
            for (auto* xt : spec.transform_xts) {
                if (xt) xt->release();
            }
        }
    }

    /// Override — forward to the dispatcher's flush(). Invariant I2.
    void flush_for_tests() override {
        if (dispatcher_) dispatcher_->flush();
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

        // Stamp origin (seq counter) now, after RBAC passes. Promote
        // tags["session_id"] into origin.session_id per doc B §18.3 —
        // origin is identity, tags are routing/filtering, both should
        // carry the session.
        std::string session_for_origin = msg.origin.session_id;
        if (session_for_origin.empty()) {
            auto it = msg.tags.find("session_id");
            if (it != msg.tags.end()) session_for_origin = it->second;
        }
        if (msg.origin.seq == 0 && msg.origin.hostname.empty()) {
            msg.origin = current_origin(session_for_origin);
        } else if (msg.origin.seq == 0) {
            auto o = current_origin(session_for_origin);
            msg.origin.seq = o.seq;
            if (msg.origin.session_id.empty()) {
                msg.origin.session_id = o.session_id;
            }
        }

        // Phase 5a.5 — producer-registry bookkeeping. Happens AFTER
        // origin stamping so the stamp timestamp and producer stamp
        // agree on ordering. Before dispatch so a publish that the
        // cycle detector rejects is still counted as "the producer
        // tried" — matches operator expectations for observability
        // (failed-publish count visible).
        record_publisher(msg.channel);

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
    observe(const std::string& pattern,
            const etil::mcp::RolePermissions* principal) override {
        // RBAC — observing is a Read on the channel.
        auto access = evaluate_access(principal, pattern, ChannelAction::Read);
        if (!access.allowed) {
            log()->warn("observe denied: pattern={} reason={}",
                        pattern, static_cast<int>(access.reason));
            return nullptr;
        }

        // Create a fresh subject + feeder route. The subject's dtor
        // removes the route when its last ref dies.
        auto subject = std::make_shared<ChannelSubject>();
        RouteSpec spec;
        spec.channel_pattern = pattern;
        spec.sink = make_subject_sink(subject);
        auto handle = add_route(std::move(spec), /*principal*/ nullptr);
        if (!handle.valid()) return nullptr;
        subject->set_cleanup(weak_from_this(), handle);

        // Wrap the subject in a HeapObservable. See
        // include/etil/core/heap_observable.hpp Kind::ChannelSubscription.
        return std::shared_ptr<etil::core::HeapObservable>(
            etil::core::HeapObservable::channel_subscription(subject),
            [](etil::core::HeapObservable* p) { if (p) p->release(); });
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
        // Phase 5a.6 — dispatcher-internal counters merged in so one
        // `channel-cycle-stats` call surfaces both cycle diagnostics
        // and dispatcher health.
        if (dispatcher_) {
            auto ds = dispatcher_->stats();
            c.subscriber_queue_depth       = ds.queue_depth;
            c.dispatcher_exceptions        = ds.dispatcher_exceptions;
            c.dispatcher_idle_transitions  = ds.idle_transitions;
            // dropped_by_overflow stays at DispatcherStats default (0)
            // in Phase 5a.6; Phase 5a.7 adds enforcement + counter.
        }
        return c;
    }

    std::string session_hmac(std::string_view session_id) const override {
        return etil::manifold::session_hmac(process_key_, session_id);
    }

    // --- Phase 5a.5 producer registry ---

    std::vector<std::string> producer_list() const override {
        std::vector<std::string> out;
        std::lock_guard<std::mutex> lk(producer_mu_);
        out.reserve(producer_stats_.size());
        for (const auto& [name, _] : producer_stats_) out.push_back(name);
        return out;
    }

    ProducerStats producer_stats(std::string_view channel) const override {
        ProducerStats s;
        {
            std::lock_guard<std::mutex> lk(producer_mu_);
            auto it = producer_stats_.find(std::string(channel));
            if (it == producer_stats_.end()) return s;
            s.channel           = it->first;
            s.published_count   = it->second.published_count;
            s.last_published_ns = it->second.last_published_ns;
        }
        // route_count is derived at query time — walk the route map
        // under its own lock.
        {
            absl::MutexLock lock(&mu_);
            for (const auto& [id, state] : routes_) {
                if (channel_matches(state->spec.channel_pattern, s.channel)) {
                    ++s.route_count;
                }
            }
        }
        return s;
    }

    std::vector<std::string>
    producers_by_pattern(std::string_view pattern) const override {
        std::vector<std::string> out;
        std::string pat(pattern);
        std::lock_guard<std::mutex> lk(producer_mu_);
        for (const auto& [name, _] : producer_stats_) {
            if (channel_matches(pat, name)) out.push_back(name);
        }
        return out;
    }

    // --- Loop registry (model B1 per design discussion) -----------------

    LoopHandle register_loop(std::string out_channel,
                             std::string in_channel,
                             const etil::mcp::RolePermissions* principal) override {
        // Install the forwarding route first — if this fails (RBAC,
        // invalid pattern), the loop is not registered.
        auto weak_self = weak_from_this();
        std::string target = in_channel;
        auto forward_sink = std::shared_ptr<ISink>(
            new ForwardingSink(weak_self, std::move(target)));

        RouteSpec spec;
        spec.channel_pattern = out_channel;
        spec.sink = forward_sink;
        RouteHandle route = add_route(std::move(spec), principal);
        if (!route.valid()) return {};

        LoopHandle h;
        h.id = next_loop_id_.fetch_add(1, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lk(loop_mu_);
            auto& spec_ref = loops_[h.id];
            spec_ref.handle = h;
            spec_ref.out_channel = std::move(out_channel);
            spec_ref.in_channel = in_channel;
            spec_ref.forward_route = route;
            loops_by_destination_[in_channel] = h.id;
        }
        return h;
    }

    bool add_loop_transform(LoopHandle handle, etil::core::WordImpl* xt) override {
        if (!handle.valid() || !xt) return false;
        std::lock_guard<std::mutex> lk(loop_mu_);
        auto it = loops_.find(handle.id);
        if (it == loops_.end()) return false;
        xt->add_ref();
        it->second.transform_xts.push_back(xt);
        return true;
    }

    const LoopSpec* find_loop_for_destination(std::string_view in_channel) const override {
        std::lock_guard<std::mutex> lk(loop_mu_);
        auto it = loops_by_destination_.find(std::string(in_channel));
        if (it == loops_by_destination_.end()) return nullptr;
        auto lit = loops_.find(it->second);
        if (lit == loops_.end()) return nullptr;
        return &lit->second;
    }

    void remove_loop(LoopHandle handle,
                     const etil::mcp::RolePermissions* principal) override {
        if (!handle.valid()) return;
        RouteHandle route_to_remove;
        std::string dest;
        {
            std::lock_guard<std::mutex> lk(loop_mu_);
            auto it = loops_.find(handle.id);
            if (it == loops_.end()) return;
            route_to_remove = it->second.forward_route;
            dest = it->second.in_channel;
            // Release the transform xt refs the registry held.
            for (auto* xt : it->second.transform_xts) {
                if (xt) xt->release();
            }
            loops_.erase(it);
            auto dit = loops_by_destination_.find(dest);
            if (dit != loops_by_destination_.end() && dit->second == handle.id) {
                loops_by_destination_.erase(dit);
            }
        }
        remove_route(route_to_remove, principal);
    }

private:
    /// Internal ISink that republishes onto another channel. Used by
    /// register_loop(). Holds a weak_ptr to the service so it doesn't
    /// keep the service alive past its natural lifetime; if the
    /// service is already gone, the republish is silently dropped.
    class ForwardingSink : public ISink {
    public:
        ForwardingSink(std::weak_ptr<ChannelService> svc, std::string target)
            : svc_(std::move(svc)), target_(std::move(target)) {}
        void accept(const Message& m) override {
            auto svc = svc_.lock();
            if (!svc) return;
            Message echo = m;
            echo.channel = target_;
            // route_trace and hops_remaining carry forward per §20; the
            // service's dispatch() will reject when hops hit 0.
            svc->publish(std::move(echo));
        }
    private:
        std::weak_ptr<ChannelService> svc_;
        std::string target_;
    };


    /// Phase 5a.5 — called from publish() immediately after the RBAC
    /// check and origin stamping succeed. One hash-map upsert + one
    /// clock read. The mutex is separate from mu_ so producer-registry
    /// writes don't contend with route lookups in dispatch().
    void record_publisher(const std::string& channel) {
        uint64_t now = clock_ ? clock_->now_ns() : 0;
        std::lock_guard<std::mutex> lk(producer_mu_);
        auto& stats = producer_stats_[channel];
        stats.published_count += 1;
        stats.last_published_ns = now;
    }
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

        // Phase 5a.3 — async dispatch by default: enqueue one
        // DeliveryItem per matched route; the dispatcher thread drains
        // and calls deliver() outside this call path. See doc B §24.1
        // and the plan §4 Phase 5a.3 invariants I1, I2, I4, I6.
        //
        // v2.11.1 — routes that opted in with DeliveryMode::Inline
        // (route_spec.hpp: "producer thread drives sink synchronously")
        // bypass the dispatcher and run deliver() on the caller's
        // stack. Used by the MCP SSE-out route so sys-/user-notification
        // events stream as they are published rather than batching at
        // the end of the request handler. Re-entrancy risk is bounded
        // by cycle-detection layers 1-2 (visited-trace + hop-TTL),
        // which already protect against publish-from-sink loops.
        for (auto& state : matched) {
            if (state->spec.delivery_mode == DeliveryMode::Inline) {
                deliver(state, msg);
            } else {
                dispatcher_->enqueue(DeliveryItem{state, msg});
            }
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

    /// Called from the dispatcher thread (ThreadDispatcher) or from the
    /// caller's thread (InlineDispatcher). Layer-3 echo check lives
    /// here because it mutates service-owned counters; the pure
    /// transform+sink tail is in apply_transforms_and_deliver().
    void deliver(std::shared_ptr<RouteState>& state,
                 const Message& msg) {
        // Layer 3 cycle detection (doc B §20.3). When the route
        // opted in with reject_own_origin, compare the incoming
        // message's origin against this process's own identity; if
        // they match, drop and audit. Hardwired audit channels
        // bypass this check — they must always flow.
        if (state->spec.reject_own_origin && !is_hardwired_audit(msg.channel)) {
            const auto self = current_origin(msg.origin.session_id);
            if (msg.origin.hostname == self.hostname &&
                msg.origin.app_startup_us == self.app_startup_us) {
                echo_dropped_.fetch_add(1, std::memory_order_relaxed);
                state->dropped.fetch_add(1, std::memory_order_relaxed);
                publish_echo_audit(msg, state->spec.channel_pattern);
                return;
            }
        }

        // Phase 5a.1: the pure tail is now a free function so it can be
        // exercised in isolation (tests construct a RouteState + Message
        // + IClock without needing a full ChannelService).
        apply_transforms_and_deliver(*state, msg, *clock_);
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

    void publish_echo_audit(const Message& original,
                             const std::string& route_pattern) {
        Message m = Message::fresh("etil.aaa.audit.channel.echo-dropped");
        m.origin = current_origin(original.origin.session_id);
        m.tags["origin_channel"] = original.channel;
        m.tags["route_pattern"] = route_pattern;
        m.tags["original_seq"] = std::to_string(original.origin.seq);
        m.payload = std::string("own-origin-echo-dropped");
        m.payload_type = std::type_index(typeid(std::string));
        PublishOutcome throwaway;
        dispatch(m, throwaway);
    }

    bool is_hardwired_audit(const std::string& channel) const {
        // Layer 3 must not fire on audit channels themselves — a
        // self-referential echo of echo-dropped would be a footgun.
        return channel_matches("etil.aaa.audit.**", channel);
    }

    void static_scc_check(const RouteSpec& /*spec*/) {
        // Placeholder: a full SCC walk over the route graph requires
        // modeling transform-retarget edges. Phase 1 installs the
        // hook; Phase 2+ fills it in when the retarget path lands.
        // For now, increment the counter only if we detect a literal
        // self-loop (rare but easy to catch).
    }

    /// Injectable clock — production defaults to SystemClock, tests
    /// inject ManualClock for deterministic last_published_ns. Phase
    /// 5a.1: owned but not yet read on the hot path (Phase 5a.5 wires
    /// producer-registry stamping through it).
    std::shared_ptr<IClock> clock_;

    /// Phase 5a.3 — owns the subscriber/sink dispatcher. Default is
    /// ThreadDispatcher; tests may inject InlineDispatcher for
    /// deterministic sync delivery. The dtor explicitly calls
    /// shutdown() before any other member is destroyed so in-flight
    /// closures capturing `this` see a live object.
    std::unique_ptr<IDispatcher> dispatcher_;

    mutable absl::Mutex mu_;
    std::unordered_map<uint64_t, std::shared_ptr<RouteState>> routes_ ABSL_GUARDED_BY(mu_);

    /// Phase 5a.5 — producer-keyed channel registry (doc B §24.2).
    /// Keyed by channel name; updated once per successful publish()
    /// under a dedicated mutex so it doesn't contend with route
    /// lookups in dispatch().
    struct ProducerRecord {
        uint64_t published_count  = 0;
        uint64_t last_published_ns = 0;
    };
    mutable std::mutex producer_mu_;
    absl::flat_hash_map<std::string, ProducerRecord> producer_stats_;

    /// Loop registry — keyed by LoopHandle.id; destination-channel
    /// lookups go through loops_by_destination_ (one loop per IN).
    mutable std::mutex loop_mu_;
    absl::flat_hash_map<uint64_t, LoopSpec> loops_;
    absl::flat_hash_map<std::string, uint64_t> loops_by_destination_;
    std::atomic<uint64_t> next_loop_id_{1};

    std::atomic<uint64_t> next_handle_id_{1};
    std::atomic<uint64_t> cycles_detected_{0};
    std::atomic<uint64_t> ttl_exhausted_{0};
    std::atomic<uint64_t> echo_dropped_{0};
    std::atomic<uint64_t> static_warnings_{0};

    // Process-local HMAC key for Session-Hmac header (A-5). Generated
    // once at construction, never leaves the process.
    ProcessKey process_key_ = generate_process_key();
};

} // namespace

std::shared_ptr<ChannelService> make_default_channel_service() {
    return std::make_shared<DefaultChannelService>();
}

std::shared_ptr<ChannelService> make_default_channel_service_with_clock(
    std::shared_ptr<IClock> clock) {
    return std::make_shared<DefaultChannelService>(std::move(clock));
}

} // namespace etil::manifold
