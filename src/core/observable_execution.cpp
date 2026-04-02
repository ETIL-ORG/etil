// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/observable_execution.hpp"
#include "etil/core/observable_async.hpp"

#include "etil/core/heap_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/lvfs/lvfs.hpp"
#include "etil/net/http_client_config.hpp"
#include "etil/net/url_validation.hpp"
#include "etil/mcp/role_permissions.hpp"

#ifdef ETIL_HTTP_CLIENT_ENABLED
#include <httplib.h>
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace etil::core {

// ---------------------------------------------------------------------------
// Execution helpers
// ---------------------------------------------------------------------------

bool sleep_until_or_tick(ExecutionContext& ctx,
                         std::chrono::steady_clock::time_point target) {
    while (std::chrono::steady_clock::now() < target) {
        if (!ctx.tick()) return false;
        auto remaining = target - std::chrono::steady_clock::now();
        if (remaining > std::chrono::milliseconds(1)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}

HeapObservable* execute_xt_pop_observable(WordImpl* xt, ExecutionContext& ctx,
                                          const char* caller) {
    if (!execute_xt(xt, ctx)) return nullptr;
    auto opt = ctx.data_stack().pop();
    if (!opt || opt->type != Value::Type::Observable || !opt->as_ptr) {
        if (opt) opt->release();
        if (caller) ctx.err() << "Error: " << caller << " xt must return an observable\n";
        return nullptr;
    }
    return opt->as_observable();
}

// ---------------------------------------------------------------------------
// Unified pipeline execution
// ---------------------------------------------------------------------------

bool execute_pipeline(HeapObservable* obs, ExecutionContext& ctx,
                      const Observer& observer,
                      AsyncPipeline* pipeline) {
    using K = HeapObservable::Kind;

    switch (obs->obs_kind()) {

    // =====================================================================
    // Unified transforms — shared_ptr state, mode-independent.
    // These recurse via execute_pipeline(), which handles sync/async routing.
    // =====================================================================

    case K::Map: {
        auto* xt = obs->operator_xt();
        Observer wrapped = [xt, observer](Value v, ExecutionContext& c) -> bool {
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) return false;
            auto res = c.data_stack().pop();
            if (!res) return false;
            return observer(*res, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::MapWith: {
        auto* xt = obs->operator_xt();
        Value context = obs->state();
        Observer wrapped = [xt, context, observer](Value v, ExecutionContext& c) -> bool {
            value_addref(context);
            c.data_stack().push(context);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) return false;
            auto res = c.data_stack().pop();
            if (!res) return false;
            return observer(*res, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::Filter: {
        auto* xt = obs->operator_xt();
        Observer wrapped = [xt, observer](Value v, ExecutionContext& c) -> bool {
            value_addref(v);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) { value_release(v); return false; }
            auto pred = c.data_stack().pop();
            if (!pred) { value_release(v); return false; }
            if (pred->type == Value::Type::Boolean && pred->as_bool()) {
                return observer(v, c);
            }
            value_release(v);
            return true;
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::FilterWith: {
        auto* xt = obs->operator_xt();
        Value context = obs->state();
        Observer wrapped = [xt, context, observer](Value v, ExecutionContext& c) -> bool {
            value_addref(v);
            value_addref(context);
            c.data_stack().push(context);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) { value_release(v); return false; }
            auto pred = c.data_stack().pop();
            if (!pred) { value_release(v); return false; }
            if (pred->type == Value::Type::Boolean && pred->as_bool()) {
                return observer(v, c);
            }
            value_release(v);
            return true;
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::Scan: {
        auto* xt = obs->operator_xt();
        struct ScanState {
            Value accum;
            ~ScanState() { value_release(accum); }
        };
        auto state = std::make_shared<ScanState>();
        state->accum = obs->state();
        value_addref(state->accum);
        Observer wrapped = [xt, state, observer](Value v, ExecutionContext& c) -> bool {
            c.data_stack().push(state->accum);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) return false;
            auto res = c.data_stack().pop();
            if (!res) return false;
            value_release(state->accum);
            state->accum = *res;
            value_addref(state->accum);
            return observer(state->accum, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::Reduce:
        return false;  // terminal-only, not a pipeline node

    case K::Take: {
        auto remaining = std::make_shared<int64_t>(obs->param());
        Observer wrapped = [remaining, observer](Value v, ExecutionContext& c) -> bool {
            if (*remaining <= 0) { value_release(v); return false; }
            --(*remaining);
            return observer(v, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::Skip: {
        auto to_skip = std::make_shared<int64_t>(obs->param());
        Observer wrapped = [to_skip, observer](Value v, ExecutionContext& c) -> bool {
            if (*to_skip > 0) { --(*to_skip); value_release(v); return true; }
            return observer(v, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::Distinct: {
        struct DistinctState {
            bool has_prev = false;
            Value prev{};
            ~DistinctState() { if (has_prev) value_release(prev); }
        };
        auto state = std::make_shared<DistinctState>();
        Observer wrapped = [state, observer](Value v, ExecutionContext& c) -> bool {
            bool duplicate = state->has_prev && v.type == state->prev.type
                             && v.as_int == state->prev.as_int;
            if (duplicate) { value_release(v); return true; }
            if (state->has_prev) value_release(state->prev);
            state->prev = v;
            value_addref(v);
            state->has_prev = true;
            return observer(v, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::Tap: {
        auto* xt = obs->operator_xt();
        Observer wrapped = [xt, observer](Value v, ExecutionContext& c) -> bool {
            value_addref(v);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) { value_release(v); return false; }
            auto discard = c.data_stack().pop();
            if (discard) discard->release();
            return observer(v, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::Pairwise: {
        struct PairState {
            bool has_prev = false;
            Value prev{};
            ~PairState() { if (has_prev) value_release(prev); }
        };
        auto state = std::make_shared<PairState>();
        Observer wrapped = [state, observer](Value v, ExecutionContext& c) -> bool {
            if (!state->has_prev) {
                state->prev = v;
                state->has_prev = true;
                return true;
            }
            auto* pair = new HeapArray();
            pair->push_back(state->prev);
            pair->push_back(v);
            state->prev = v;
            value_addref(v);
            return observer(Value::from(pair), c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::First: {
        auto emitted = std::make_shared<bool>(false);
        Observer wrapped = [emitted, observer](Value v, ExecutionContext& c) -> bool {
            if (*emitted) { value_release(v); return false; }
            *emitted = true;
            return observer(v, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::TakeWhile: {
        auto* xt = obs->operator_xt();
        Observer wrapped = [xt, observer](Value v, ExecutionContext& c) -> bool {
            value_addref(v);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) { value_release(v); return false; }
            auto pred = c.data_stack().pop();
            if (!pred) { value_release(v); return false; }
            if (pred->type == Value::Type::Boolean && pred->as_bool()) {
                return observer(v, c);
            }
            value_release(v);
            return false;  // stop on first false
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::DistinctUntil: {
        struct DUState {
            bool has_prev = false;
            Value prev{};
            ~DUState() { if (has_prev) value_release(prev); }
        };
        auto state = std::make_shared<DUState>();
        Observer wrapped = [state, observer](Value v, ExecutionContext& c) -> bool {
            if (state->has_prev && v.type == state->prev.type
                && v.as_int == state->prev.as_int) {
                value_release(v);
                return true;
            }
            if (state->has_prev) value_release(state->prev);
            state->prev = v;
            value_addref(v);
            state->has_prev = true;
            return observer(v, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::StartWith: {
        Value prepend = obs->state();
        value_addref(prepend);
        if (!observer(prepend, ctx)) return true;
        return execute_pipeline(obs->source(), ctx, observer, pipeline);
    }

    case K::Timestamp: {
        Observer wrapped = [observer](Value v, ExecutionContext& c) -> bool {
            auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auto* pair = new HeapArray();
            pair->push_back(Value(int64_t(now_us)));
            pair->push_back(v);
            return observer(Value::from(pair), c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    case K::TimeInterval: {
        auto last_time = std::make_shared<std::chrono::steady_clock::time_point>(
            std::chrono::steady_clock::now());
        Observer wrapped = [last_time, observer](Value v, ExecutionContext& c) -> bool {
            auto now = std::chrono::steady_clock::now();
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now - *last_time).count();
            *last_time = now;
            auto* pair = new HeapArray();
            pair->push_back(Value(int64_t(elapsed_us)));
            pair->push_back(v);
            return observer(Value::from(pair), c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

    // =====================================================================
    // Unified sources — sync/async branching on pipeline pointer.
    // =====================================================================

    case K::FromArray: {
        if (pipeline) {
            auto node = std::make_unique<IdleNode>();
            auto* arr = obs->source_array();
            for (size_t i = 0; i < arr->length(); ++i) {
                Value v;
                if (arr->get(i, v)) node->values.push_back(v);
            }
            node->observer = observer;
            node->ctx = &ctx;
            if (!node->values.empty()) pipeline->add_node(std::move(node));
            return true;
        }
        auto* arr = obs->source_array();
        for (size_t i = 0; i < arr->length(); ++i) {
            if (!ctx.tick()) return false;
            Value v;
            if (!arr->get(i, v)) return false;
            if (!observer(v, ctx)) return true;
        }
        return true;
    }

    case K::Of: {
        if (pipeline) {
            auto node = std::make_unique<IdleNode>();
            Value v = obs->state();
            value_addref(v);
            node->values.push_back(v);
            node->observer = observer;
            node->ctx = &ctx;
            pipeline->add_node(std::move(node));
            return true;
        }
        Value v = obs->state();
        value_addref(v);
        observer(v, ctx);
        return true;
    }

    case K::Empty:
        return true;

    case K::Range: {
        int64_t start = obs->state().as_int;
        int64_t end = obs->param();
        if (pipeline) {
            auto node = std::make_unique<IdleNode>();
            for (int64_t i = start; i < end; ++i)
                node->values.push_back(Value(i));
            node->observer = observer;
            node->ctx = &ctx;
            if (!node->values.empty()) pipeline->add_node(std::move(node));
            return true;
        }
        for (int64_t i = start; i < end; ++i) {
            if (!ctx.tick()) return false;
            if (!observer(Value(i), ctx)) return true;
        }
        return true;
    }

    case K::Timer: {
        int64_t delay_us = obs->state().as_int;
        int64_t period_us = obs->param();
        if (pipeline) {
            auto node = std::make_unique<TimerNode>();
            node->delay_ms = (delay_us > 0) ? static_cast<uint64_t>((delay_us + 999) / 1000) : 0;
            node->period_ms = (period_us > 0) ? static_cast<uint64_t>((period_us + 999) / 1000) : 0;
            node->observer = observer;
            node->ctx = &ctx;
            pipeline->add_node(std::move(node));
            return true;
        }
        if (delay_us > 0) {
            auto target = std::chrono::steady_clock::now()
                        + std::chrono::microseconds(delay_us);
            if (!sleep_until_or_tick(ctx, target)) return false;
        }
        if (!observer(Value(int64_t(0)), ctx)) return true;
        if (period_us <= 0) return true;
        int64_t counter = 1;
        auto next_tick = std::chrono::steady_clock::now()
                       + std::chrono::microseconds(period_us);
        while (true) {
            if (!sleep_until_or_tick(ctx, next_tick)) return false;
            if (!observer(Value(counter++), ctx)) return true;
            next_tick += std::chrono::microseconds(period_us);
        }
    }

    // =====================================================================
    // Unified combination operators
    // =====================================================================

    case K::Concat: {
        // Sequential: run A to completion, then B
        if (pipeline) {
            // Async: register source A now, defer source B until A completes
            size_t a_start = pipeline->node_count();
            execute_pipeline(obs->source(), ctx, observer, pipeline);
            size_t a_end = pipeline->node_count();

            // Build source B's nodes into a temporary pipeline
            AsyncPipeline b_pipeline;
            execute_pipeline(obs->source_b(), ctx, observer, &b_pipeline);

            // Defer B's nodes — activate when all of A's source nodes are done
            auto b_nodes = b_pipeline.extract_nodes();
            if (!b_nodes.empty()) {
                pipeline->add_deferred(std::move(b_nodes),
                    [pipeline, a_start, a_end]() {
                        return pipeline->sources_done_in_range(a_start, a_end);
                    });
            }
            return true;
        }
        bool ok = execute_pipeline(obs->source(), ctx, observer);
        if (!ok) return false;
        return execute_pipeline(obs->source_b(), ctx, observer);
    }

    case K::Merge: {
        if (pipeline) {
            // Async: both sources register concurrently
            execute_pipeline(obs->source(), ctx, observer, pipeline);
            execute_pipeline(obs->source_b(), ctx, observer, pipeline);
            return true;
        }
        // Sync: sequential
        bool ok = execute_pipeline(obs->source(), ctx, observer);
        if (!ok) return false;
        return execute_pipeline(obs->source_b(), ctx, observer);
    }

    case K::Zip: {
        if (pipeline) {
            // Async: concurrent buffered pairing
            struct ZipState {
                std::queue<Value> queue_a, queue_b;
                Observer downstream;
                ExecutionContext* ctx = nullptr;
                bool stopped = false;
                void try_emit() {
                    while (!stopped && !queue_a.empty() && !queue_b.empty()) {
                        auto* pair = new HeapArray();
                        pair->push_back(queue_a.front()); queue_a.pop();
                        pair->push_back(queue_b.front()); queue_b.pop();
                        if (!downstream(Value::from(pair), *ctx)) stopped = true;
                    }
                }
                ~ZipState() {
                    while (!queue_a.empty()) { value_release(queue_a.front()); queue_a.pop(); }
                    while (!queue_b.empty()) { value_release(queue_b.front()); queue_b.pop(); }
                }
            };
            auto state = std::make_shared<ZipState>();
            state->downstream = observer;
            state->ctx = &ctx;
            Observer obs_a = [state](Value v, ExecutionContext&) -> bool {
                if (state->stopped) { value_release(v); return false; }
                state->queue_a.push(v); state->try_emit(); return !state->stopped;
            };
            Observer obs_b = [state](Value v, ExecutionContext&) -> bool {
                if (state->stopped) { value_release(v); return false; }
                state->queue_b.push(v); state->try_emit(); return !state->stopped;
            };
            execute_pipeline(obs->source(), ctx, obs_a, pipeline);
            execute_pipeline(obs->source_b(), ctx, obs_b, pipeline);
            return true;
        }
        // Sync: collect-then-pair
        std::vector<Value> vals_a, vals_b;
        execute_pipeline(obs->source(), ctx, [&](Value v, ExecutionContext&) -> bool {
            vals_a.push_back(v); return true;
        });
        execute_pipeline(obs->source_b(), ctx, [&](Value v, ExecutionContext&) -> bool {
            vals_b.push_back(v); return true;
        });
        size_t n = std::min(vals_a.size(), vals_b.size());
        bool zip_ok = true;
        size_t zi = 0;
        for (; zi < n; ++zi) {
            if (!ctx.tick()) { zip_ok = false; break; }
            auto* pair = new HeapArray();
            pair->push_back(vals_a[zi]); pair->push_back(vals_b[zi]);
            if (!observer(Value::from(pair), ctx)) break;
        }
        for (size_t j = (zip_ok ? zi + 1 : zi); j < vals_a.size(); ++j) value_release(vals_a[j]);
        for (size_t j = (zip_ok ? zi + 1 : zi); j < vals_b.size(); ++j) value_release(vals_b[j]);
        return zip_ok;
    }

    // =====================================================================
    // Unified temporal transforms
    // =====================================================================

    case K::Delay: {
        int64_t delay_us = obs->param();
        if (pipeline) {
            uint64_t delay_ms = static_cast<uint64_t>((delay_us + 999) / 1000);
            Observer wrapped = [delay_ms, observer](Value v, ExecutionContext& c) -> bool {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                return observer(v, c);
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        return execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                auto target = std::chrono::steady_clock::now()
                            + std::chrono::microseconds(delay_us);
                if (!sleep_until_or_tick(c, target)) { value_release(v); return false; }
                return observer(v, c);
            });
    }

    case K::DelayEach: {
        auto* xt = obs->operator_xt();
        if (pipeline) {
            Observer wrapped = [xt, observer](Value v, ExecutionContext& c) -> bool {
                value_addref(v);
                c.data_stack().push(v);
                if (!execute_xt(xt, c)) { value_release(v); return false; }
                auto delay_opt = c.data_stack().pop();
                if (!delay_opt) { value_release(v); return false; }
                int64_t d = delay_opt->as_int;
                if (d > 0) std::this_thread::sleep_for(std::chrono::microseconds(d));
                return observer(v, c);
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        return execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                value_addref(v);
                c.data_stack().push(v);
                if (!execute_xt(xt, c)) { value_release(v); return false; }
                auto delay_opt = c.data_stack().pop();
                if (!delay_opt) { value_release(v); return false; }
                int64_t d = delay_opt->as_int;
                if (d > 0) {
                    auto target = std::chrono::steady_clock::now() + std::chrono::microseconds(d);
                    if (!sleep_until_or_tick(c, target)) { value_release(v); return false; }
                }
                return observer(v, c);
            });
    }

    case K::DebounceTime: {
        if (pipeline) {
            uint64_t quiet_ms = static_cast<uint64_t>((obs->param() + 999) / 1000);
            struct DebounceState {
                TransformTimerNode* timer_node = nullptr;
                Value last_value{}; bool has_value = false;
                Observer downstream; ExecutionContext* ctx = nullptr;
                uint64_t quiet_ms = 0;
                static void on_fire(uv_timer_t* h) {
                    auto* self = static_cast<DebounceState*>(h->data);
                    if (self->has_value) { self->downstream(self->last_value, *self->ctx); self->has_value = false; }
                }
                ~DebounceState() { if (has_value) value_release(last_value); }
            };
            auto state = std::make_shared<DebounceState>();
            state->downstream = observer; state->ctx = &ctx; state->quiet_ms = quiet_ms;
            auto tn = std::make_unique<TransformTimerNode>();
            state->timer_node = tn.get();
            pipeline->add_node(std::move(tn));
            Observer wrapped = [state](Value v, ExecutionContext&) -> bool {
                if (state->has_value) value_release(state->last_value);
                state->last_value = v; value_addref(v); state->has_value = true;
                state->timer_node->handle.data = state.get();
                uv_timer_stop(&state->timer_node->handle);
                uv_timer_start(&state->timer_node->handle, DebounceState::on_fire, state->quiet_ms, 0);
                return true;
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        // Sync
        int64_t quiet_us = obs->param();
        Value last_value = {}; bool has_value = false;
        auto last_time = std::chrono::steady_clock::now();
        bool completed = execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext&) -> bool {
                if (has_value) value_release(last_value);
                last_value = v; value_addref(last_value); has_value = true;
                last_time = std::chrono::steady_clock::now();
                return true;
            });
        if (has_value) { (void)quiet_us; observer(last_value, ctx); }
        return completed;
    }

    case K::ThrottleTime: {
        int64_t window_us = obs->param();
        if (pipeline) {
            auto gate_until = std::make_shared<std::chrono::steady_clock::time_point>(
                std::chrono::steady_clock::time_point::min());
            Observer wrapped = [gate_until, window_us, observer](Value v, ExecutionContext& c) -> bool {
                auto now = std::chrono::steady_clock::now();
                if (now >= *gate_until) {
                    *gate_until = now + std::chrono::microseconds(window_us);
                    return observer(v, c);
                }
                value_release(v); return true;
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        auto gate_until = std::chrono::steady_clock::time_point::min();
        return execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                auto now = std::chrono::steady_clock::now();
                if (now >= gate_until) {
                    gate_until = now + std::chrono::microseconds(window_us);
                    return observer(v, c);
                }
                value_release(v); return true;
            });
    }

    case K::SampleTime: {
        if (pipeline) {
            uint64_t period_ms = static_cast<uint64_t>((obs->param() + 999) / 1000);
            struct SampleState {
                TransformTimerNode* timer_node = nullptr;
                Value latest{}; bool has_latest = false; bool emitted_latest = true;
                Observer downstream; ExecutionContext* ctx = nullptr;
                static void on_sample(uv_timer_t* h) {
                    auto* self = static_cast<SampleState*>(h->data);
                    if (self->has_latest && !self->emitted_latest) {
                        value_addref(self->latest);
                        self->downstream(self->latest, *self->ctx);
                        self->emitted_latest = true;
                    }
                }
                ~SampleState() { if (has_latest) value_release(latest); }
            };
            auto state = std::make_shared<SampleState>();
            state->downstream = observer; state->ctx = &ctx;
            auto tn = std::make_unique<TransformTimerNode>();
            state->timer_node = tn.get(); tn->handle.data = state.get();
            pipeline->add_node(std::move(tn));
            Observer wrapped = [state, period_ms](Value v, ExecutionContext&) -> bool {
                if (state->has_latest) value_release(state->latest);
                state->latest = v; value_addref(v); state->has_latest = true; state->emitted_latest = false;
                if (!uv_is_active(reinterpret_cast<uv_handle_t*>(&state->timer_node->handle)))
                    uv_timer_start(&state->timer_node->handle, SampleState::on_sample, period_ms, period_ms);
                return true;
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        // Sync
        int64_t period_us = obs->param();
        Value latest = {}; bool has_latest = false;
        auto next_sample = std::chrono::steady_clock::now() + std::chrono::microseconds(period_us);
        bool completed = execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                if (has_latest) value_release(latest);
                latest = v; value_addref(latest); has_latest = true;
                auto now = std::chrono::steady_clock::now();
                while (has_latest && now >= next_sample) {
                    if (!observer(latest, c)) { has_latest = false; return false; }
                    has_latest = false; next_sample += std::chrono::microseconds(period_us);
                    now = std::chrono::steady_clock::now();
                }
                return true;
            });
        if (has_latest) value_release(latest);
        return completed;
    }

    case K::AuditTime: {
        if (pipeline) {
            uint64_t window_ms = static_cast<uint64_t>((obs->param() + 999) / 1000);
            struct AuditState {
                TransformTimerNode* timer_node = nullptr;
                Value last_value{}; bool has_value = false; bool in_window = false;
                Observer downstream; ExecutionContext* ctx = nullptr; uint64_t window_ms = 0;
                static void on_fire(uv_timer_t* h) {
                    auto* self = static_cast<AuditState*>(h->data);
                    if (self->has_value) { value_addref(self->last_value); self->downstream(self->last_value, *self->ctx); }
                    self->in_window = false;
                }
                ~AuditState() { if (has_value) value_release(last_value); }
            };
            auto state = std::make_shared<AuditState>();
            state->downstream = observer; state->ctx = &ctx; state->window_ms = window_ms;
            auto tn = std::make_unique<TransformTimerNode>();
            state->timer_node = tn.get();
            pipeline->add_node(std::move(tn));
            Observer wrapped = [state](Value v, ExecutionContext&) -> bool {
                if (state->has_value) value_release(state->last_value);
                state->last_value = v; value_addref(v); state->has_value = true;
                if (!state->in_window) {
                    state->in_window = true; state->timer_node->handle.data = state.get();
                    uv_timer_start(&state->timer_node->handle, AuditState::on_fire, state->window_ms, 0);
                }
                return true;
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        // Sync
        int64_t window_us = obs->param();
        Value last_value = {}; bool has_value = false; bool in_window = false;
        auto window_end = std::chrono::steady_clock::time_point::min();
        bool completed = execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                if (has_value) value_release(last_value);
                last_value = v; value_addref(last_value); has_value = true;
                auto now = std::chrono::steady_clock::now();
                if (!in_window) { in_window = true; window_end = now + std::chrono::microseconds(window_us); }
                if (now >= window_end) {
                    Value emit_v = last_value; value_addref(emit_v); in_window = false;
                    if (!observer(emit_v, c)) return false;
                    in_window = true; window_end = now + std::chrono::microseconds(window_us);
                }
                return true;
            });
        if (has_value && in_window) { observer(last_value, ctx); has_value = false; }
        if (has_value) value_release(last_value);
        return completed;
    }

    case K::BufferTime: {
        if (pipeline) {
            uint64_t period_ms = static_cast<uint64_t>((obs->param() + 999) / 1000);
            struct BufferTimeState {
                TransformTimerNode* timer_node = nullptr;
                HeapArray* buffer = nullptr; Observer downstream; ExecutionContext* ctx = nullptr;
                static void on_flush(uv_timer_t* h) {
                    auto* self = static_cast<BufferTimeState*>(h->data);
                    if (self->buffer && self->buffer->length() > 0) {
                        auto* eb = self->buffer; self->buffer = new HeapArray();
                        self->downstream(Value::from(eb), *self->ctx);
                    }
                }
                ~BufferTimeState() { delete buffer; }
            };
            auto state = std::make_shared<BufferTimeState>();
            state->buffer = new HeapArray(); state->downstream = observer; state->ctx = &ctx;
            auto tn = std::make_unique<TransformTimerNode>();
            state->timer_node = tn.get(); tn->handle.data = state.get();
            pipeline->add_node(std::move(tn));
            Observer wrapped = [state, period_ms](Value v, ExecutionContext&) -> bool {
                state->buffer->push_back(v);
                if (!uv_is_active(reinterpret_cast<uv_handle_t*>(&state->timer_node->handle)))
                    uv_timer_start(&state->timer_node->handle, BufferTimeState::on_flush, period_ms, period_ms);
                return true;
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        // Sync
        int64_t window_us = obs->param();
        auto* buffer = new HeapArray();
        auto window_end = std::chrono::steady_clock::now() + std::chrono::microseconds(window_us);
        bool completed = execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                buffer->push_back(v);
                auto now = std::chrono::steady_clock::now();
                if (now >= window_end) {
                    auto* eb = buffer; buffer = new HeapArray();
                    window_end = now + std::chrono::microseconds(window_us);
                    return observer(Value::from(eb), c);
                }
                return true;
            });
        if (buffer->length() > 0) observer(Value::from(buffer), ctx); else delete buffer;
        return completed;
    }

    case K::TakeUntilTime: {
        if (pipeline) {
            uint64_t duration_ms = static_cast<uint64_t>((obs->param() + 999) / 1000);
            auto expired = std::make_shared<bool>(false);
            auto tn = std::make_unique<TransformTimerNode>();
            auto* raw_timer = tn.get();
            pipeline->add_node(std::move(tn));
            struct DD { std::shared_ptr<bool> expired; };
            auto dd = std::make_shared<DD>(); dd->expired = expired;
            auto started = std::make_shared<bool>(false);
            Observer wrapped = [expired, observer, raw_timer, dd, duration_ms, started](
                    Value v, ExecutionContext& c) -> bool {
                if (*expired) { value_release(v); return false; }
                if (!*started) {
                    *started = true; raw_timer->handle.data = dd.get();
                    uv_timer_start(&raw_timer->handle, [](uv_timer_t* h) {
                        auto* d = static_cast<DD*>(h->data); *d->expired = true;
                    }, duration_ms, 0);
                }
                return observer(v, c);
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        // Sync
        int64_t duration_us = obs->param();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(duration_us);
        return execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                if (std::chrono::steady_clock::now() > deadline) { value_release(v); return false; }
                return observer(v, c);
            });
    }

    case K::Timeout: {
        if (pipeline) {
            uint64_t limit_ms = static_cast<uint64_t>((obs->param() + 999) / 1000);
            auto timed_out = std::make_shared<bool>(false);
            auto tn = std::make_unique<TransformTimerNode>();
            auto* raw_timer = tn.get();
            pipeline->add_node(std::move(tn));
            struct TD { std::shared_ptr<bool> timed_out; };
            auto td = std::make_shared<TD>(); td->timed_out = timed_out;
            Observer wrapped = [timed_out, observer, raw_timer, td, limit_ms](
                    Value v, ExecutionContext& c) -> bool {
                if (*timed_out) { value_release(v); return false; }
                raw_timer->handle.data = td.get();
                uv_timer_stop(&raw_timer->handle);
                uv_timer_start(&raw_timer->handle, [](uv_timer_t* h) {
                    auto* d = static_cast<TD*>(h->data); *d->timed_out = true;
                }, limit_ms, 0);
                return observer(v, c);
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        // Sync
        int64_t limit_us = obs->param();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(limit_us);
        return execute_pipeline(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                auto now = std::chrono::steady_clock::now();
                if (now > deadline) { value_release(v); return false; }
                deadline = now + std::chrono::microseconds(limit_us);
                return observer(v, c);
            });
    }

    case K::RetryDelay: {
        // Sync only (dynamic re-registration deferred to Phase 5)
        int64_t delay_us = obs->state().as_int;
        int64_t max_retries = obs->param();
        for (int64_t attempt = 0; attempt <= max_retries; ++attempt) {
            if (attempt > 0 && delay_us > 0) {
                auto target = std::chrono::steady_clock::now() + std::chrono::microseconds(delay_us);
                if (!sleep_until_or_tick(ctx, target)) return false;
            }
            bool ok = execute_pipeline(obs->source(), ctx, observer);
            if (ok) return true;
            if (!ctx.tick()) return false;
        }
        return false;
    }

    // =====================================================================
    // Dynamic node operators — async via recursive execute_pipeline
    // =====================================================================

    case K::FlatMap: {
        auto* xt = obs->operator_xt();
        if (pipeline) {
            // Async: inner observables register dynamically on the running loop
            Observer wrapped = [xt, observer, pipeline, &ctx](Value v, ExecutionContext& c) -> bool {
                c.data_stack().push(v);
                auto* sub_obs = execute_xt_pop_observable(xt, c);
                if (!sub_obs) return false;
                execute_pipeline(sub_obs, c, observer, pipeline);
                pipeline->register_new_nodes();
                sub_obs->release();
                return true;
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        // Sync: sequential inner execution
        return execute_pipeline(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            c.data_stack().push(v);
            auto* sub_obs = execute_xt_pop_observable(xt, c, "obs-flat-map");
            if (!sub_obs) return false;
            bool ok = execute_pipeline(sub_obs, c, observer);
            sub_obs->release();
            return ok;
        });
    }

    case K::SwitchMap: {
        auto* xt = obs->operator_xt();
        if (pipeline) {
            // Async: cancel previous inner, register new inner dynamically.
            // inner_start tracks where inner nodes begin in the pipeline.
            // Initialized to SIZE_MAX so the first call knows to set it
            // (can't set it before execute_pipeline registers outer source nodes).
            auto inner_start = std::make_shared<size_t>(SIZE_MAX);
            Observer wrapped = [xt, observer, pipeline, inner_start, &ctx](Value v, ExecutionContext& c) -> bool {
                if (*inner_start == SIZE_MAX) {
                    // First call — outer source nodes are now registered.
                    // Inner nodes start after them.
                    *inner_start = pipeline->node_count();
                } else if (*inner_start < pipeline->node_count()) {
                    // Cancel previous inner nodes (not outer source nodes)
                    pipeline->stop_and_close_from(*inner_start);
                }
                *inner_start = pipeline->node_count();
                c.data_stack().push(v);
                auto* sub_obs = execute_xt_pop_observable(xt, c);
                if (!sub_obs) return false;
                execute_pipeline(sub_obs, c, observer, pipeline);
                pipeline->register_new_nodes();
                sub_obs->release();
                return true;
            };
            return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
        }
        // Sync: collect upstream, only forward last inner
        std::vector<Value> upstream;
        bool src_ok = execute_pipeline(obs->source(), ctx, [&](Value v, ExecutionContext&) -> bool {
            upstream.push_back(v); return true;
        });
        if (!src_ok) { for (auto& v : upstream) value_release(v); return false; }
        for (size_t i = 0; i < upstream.size(); ++i) {
            if (!ctx.tick()) {
                for (size_t j = i; j < upstream.size(); ++j) value_release(upstream[j]);
                return false;
            }
            ctx.data_stack().push(upstream[i]);
            auto* sub_obs = execute_xt_pop_observable(xt, ctx, "obs-switch-map");
            if (!sub_obs) return false;
            bool is_last = (i == upstream.size() - 1);
            if (is_last) {
                bool ok = execute_pipeline(sub_obs, ctx, observer);
                sub_obs->release();
                return ok;
            }
            execute_pipeline(sub_obs, ctx, [](Value v, ExecutionContext&) -> bool {
                value_release(v); return true;
            });
            sub_obs->release();
        }
        return true;
    }

    // =====================================================================
    // Not yet migrated — completion-dependent operators remain in
    // execute_observable: Last, Buffer, BufferWhen, Window, Finalize,
    // Catch, File I/O, HTTP.
    // =====================================================================

    default:
        break;
    }

    // Fall through: remaining operators (Last, Buffer, BufferWhen, Window,
    // Finalize, Catch, File I/O, HTTP). These run synchronously via
    // execute_observable. In async context, collect into IdleNode.
    if (pipeline) {
        auto node = std::make_unique<IdleNode>();
        node->observer = observer;
        node->ctx = &ctx;
        execute_observable(obs, ctx, [&](Value v, ExecutionContext&) -> bool {
            node->values.push_back(v);
            return true;
        });
        if (!node->values.empty()) pipeline->add_node(std::move(node));
        return true;
    }
    return execute_observable(obs, ctx, observer);
}

// ---------------------------------------------------------------------------
// Synchronous execution engine
// ---------------------------------------------------------------------------

bool execute_observable(HeapObservable* obs, ExecutionContext& ctx, const Observer& observer) {
    using K = HeapObservable::Kind;
    switch (obs->obs_kind()) {

    // --- AVO Phase 1: Buffer + Composition ---

    case K::Buffer: {
        int64_t count = obs->param();
        auto* batch = new HeapArray();
        bool result = execute_pipeline(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            batch->push_back(v);  // takes ownership of v's ref
            if (static_cast<int64_t>(batch->length()) >= count) {
                // Emit full batch
                bool ok = observer(Value::from(batch), c);
                batch = new HeapArray();  // start new batch
                return ok;
            }
            return true;
        });
        // Emit trailing partial batch (if any)
        if (batch->length() > 0 && result) {
            observer(Value::from(batch), ctx);
        } else {
            delete batch;
        }
        return result;
    }

    case K::BufferWhen: {
        auto* xt = obs->operator_xt();
        auto* batch = new HeapArray();
        bool result = execute_pipeline(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            value_addref(v);  // one ref for batch, one for predicate test
            batch->push_back(v);  // takes one ref
            // Test predicate with the value
            c.data_stack().push(v);  // uses addref'd copy
            if (!execute_xt(xt, c)) return false;
            auto pred = c.data_stack().pop();
            if (!pred) return false;
            if (pred->type == Value::Type::Boolean && pred->as_bool()) {
                // Predicate fired — emit batch
                bool ok = observer(Value::from(batch), c);
                batch = new HeapArray();
                return ok;
            }
            return true;
        });
        // Emit trailing partial batch (if any)
        if (batch->length() > 0 && result) {
            observer(Value::from(batch), ctx);
        } else {
            delete batch;
        }
        return result;
    }

    case K::Window: {
        int64_t size = obs->param();
        std::vector<Value> ring;
        ring.reserve(static_cast<size_t>(size));
        bool result = execute_pipeline(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            value_addref(v);  // ring holds one ref
            ring.push_back(v);
            if (static_cast<int64_t>(ring.size()) > size) {
                value_release(ring.front());
                ring.erase(ring.begin());
            }
            if (static_cast<int64_t>(ring.size()) == size) {
                // Emit a copy of the window as a HeapArray
                auto* win = new HeapArray();
                for (auto& elem : ring) {
                    value_addref(elem);
                    win->push_back(elem);
                }
                if (!observer(Value::from(win), c)) {
                    return false;
                }
            }
            return true;
        });
        // Release remaining ring values
        for (auto& v : ring) value_release(v);
        return result;
    }

    // --- AVO Phase 2: Streaming File I/O ---

    case K::ReadBytes: {
        auto* path_hs = obs->state().as_string();
        int64_t chunk_size = obs->param();
        std::ifstream file(std::string(path_hs->view()), std::ios::binary);
        if (!file) {
            ctx.err() << "Error: obs-read-bytes cannot open '" << path_hs->view() << "'\n";
            return false;
        }
        std::vector<char> buf(static_cast<size_t>(chunk_size));
        while (file) {
            if (!ctx.tick()) return false;
            file.read(buf.data(), chunk_size);
            auto bytes_read = file.gcount();
            if (bytes_read <= 0) break;
            auto* ba = new HeapByteArray(static_cast<size_t>(bytes_read));
            for (std::streamsize i = 0; i < bytes_read; ++i) {
                ba->set(static_cast<size_t>(i), static_cast<uint8_t>(buf[static_cast<size_t>(i)]));
            }
            ba->set_tainted(true);
            if (!observer(Value::from(ba), ctx)) return true;
        }
        return true;
    }

    case K::ReadLines: {
        auto* path_hs = obs->state().as_string();
        std::ifstream file(std::string(path_hs->view()));
        if (!file) {
            ctx.err() << "Error: obs-read-lines cannot open '" << path_hs->view() << "'\n";
            return false;
        }
        std::string line;
        while (std::getline(file, line)) {
            if (!ctx.tick()) return false;
            // Remove trailing \r for \r\n line endings
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto* hs = HeapString::create(line);
            hs->set_tainted(true);
            if (!observer(Value::from(hs), ctx)) return true;
        }
        return true;
    }

    case K::ReadJson: {
        auto* path_hs = obs->state().as_string();
        std::ifstream file(std::string(path_hs->view()));
        if (!file) {
            ctx.err() << "Error: obs-read-json cannot open '" << path_hs->view() << "'\n";
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        try {
            auto j = nlohmann::json::parse(content);
            auto* hj = new HeapJson(std::move(j));
            observer(Value::from(hj), ctx);
        } catch (const nlohmann::json::parse_error& e) {
            ctx.err() << "Error: obs-read-json parse error: " << e.what() << "\n";
            return false;
        }
        return true;
    }

    case K::ReadCsv: {
        auto* path_hs = obs->state().as_string();
        // Get separator from source_array_[0]
        Value sep_val;
        obs->source_array()->get(0, sep_val);
        auto* sep_hs = sep_val.as_string();
        char sep_char = (sep_hs->length() > 0) ? sep_hs->view()[0] : ',';
        sep_hs->release();

        std::ifstream file(std::string(path_hs->view()));
        if (!file) {
            ctx.err() << "Error: obs-read-csv cannot open '" << path_hs->view() << "'\n";
            return false;
        }
        std::string line;
        while (std::getline(file, line)) {
            if (!ctx.tick()) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Split line by separator (handles quoted fields)
            auto* row = new HeapArray();
            std::string field;
            bool in_quotes = false;
            for (size_t i = 0; i < line.size(); ++i) {
                char c = line[i];
                if (c == '"') {
                    if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                        field += '"';
                        ++i;  // skip escaped quote
                    } else {
                        in_quotes = !in_quotes;
                    }
                } else if (c == sep_char && !in_quotes) {
                    auto* fs = HeapString::create(field);
                    fs->set_tainted(true);
                    row->push_back(Value::from(fs));
                    field.clear();
                } else {
                    field += c;
                }
            }
            // Last field
            auto* fs = HeapString::create(field);
            fs->set_tainted(true);
            row->push_back(Value::from(fs));
            if (!observer(Value::from(row), ctx)) return true;
        }
        return true;
    }

    case K::ReadDir: {
        auto* path_hs = obs->state().as_string();
        std::string dir_path(path_hs->view());
        std::error_code ec;
        // Collect and sort entries
        std::vector<std::string> entries;
        for (auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
            if (ec) break;
            entries.push_back(entry.path().filename().string());
        }
        if (ec) {
            ctx.err() << "Error: obs-readdir cannot read '" << dir_path << "': " << ec.message() << "\n";
            return false;
        }
        std::sort(entries.begin(), entries.end());
        for (auto& name : entries) {
            if (!ctx.tick()) return false;
            auto* hs = HeapString::create(name);
            hs->set_tainted(true);
            if (!observer(Value::from(hs), ctx)) return true;
        }
        return true;
    }

    // --- AVO Phase 3: Streaming HTTP ---

#ifdef ETIL_HTTP_CLIENT_ENABLED
    case K::HttpGet:
    case K::HttpSse: {
        // Extract URL data from source_array_: [scheme_host_port, path, hostname, resolved_ip, hdr_k, hdr_v, ...]
        auto* arr = obs->source_array();
        Value v0, v1, v2, v3;
        if (!arr->get(0, v0) || !arr->get(1, v1) || !arr->get(2, v2) || !arr->get(3, v3)) {
            ctx.err() << "Error: obs-http-get: malformed URL data\n";
            return false;
        }
        std::string shp(v0.as_string()->view()); v0.release();
        std::string path(v1.as_string()->view()); v1.release();
        std::string hostname(v2.as_string()->view()); v2.release();
        std::string resolved_ip(v3.as_string()->view()); v3.release();

        httplib::Headers headers;
        for (size_t i = 4; i + 1 < arr->length(); i += 2) {
            Value hk, hv;
            if (arr->get(i, hk) && arr->get(i + 1, hv)) {
                if (hk.type == Value::Type::String && hv.type == Value::Type::String) {
                    headers.emplace(std::string(hk.as_string()->view()),
                                    std::string(hv.as_string()->view()));
                }
                hk.release(); hv.release();
            }
        }

        httplib::Client cli(shp);
        if (!resolved_ip.empty()) {
            cli.set_hostname_addr_map({{hostname, resolved_ip}});
        }
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(30, 0);
        cli.set_follow_location(false);

        if (obs->obs_kind() == K::HttpSse) {
            // SSE: accept text/event-stream
            headers.emplace("Accept", "text/event-stream");
        }

        // Stream response chunks
        bool stopped = false;
        auto result = cli.Get(path, headers,
            [&](const char* data, size_t len) -> bool {
                if (!ctx.tick()) { stopped = true; return false; }

                if (obs->obs_kind() == K::HttpSse) {
                    // SSE: accumulate lines, emit on double-newline
                    // For simplicity, emit each data: payload as a HeapString
                    std::string chunk(data, len);
                    // Split into lines and process
                    std::istringstream iss(chunk);
                    std::string line;
                    while (std::getline(iss, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.rfind("data:", 0) == 0) {
                            std::string payload = line.substr(5);
                            if (!payload.empty() && payload[0] == ' ') payload = payload.substr(1);
                            try {
                                auto j = nlohmann::json::parse(payload);
                                auto* hj = new HeapJson(std::move(j));
                                if (!observer(Value::from(hj), ctx)) { stopped = true; return false; }
                            } catch (...) {
                                // Non-JSON data line: emit as string
                                auto* hs = HeapString::create(payload);
                                hs->set_tainted(true);
                                if (!observer(Value::from(hs), ctx)) { stopped = true; return false; }
                            }
                        }
                    }
                    return true;
                } else {
                    // Regular GET: emit each chunk as HeapByteArray
                    auto* ba = new HeapByteArray(len);
                    std::memcpy(ba->data(), data, len);
                    ba->set_tainted(true);
                    if (!observer(Value::from(ba), ctx)) { stopped = true; return false; }
                    return true;
                }
            });

        if (stopped) return !ctx.tick() ? false : true;  // cancelled vs observer-stopped
        if (!result) {
            ctx.err() << "Error: obs-http-get: " << httplib::to_string(result.error()) << "\n";
            return false;
        }
        return true;
    }

    case K::HttpPost: {
        auto* arr = obs->source_array();
        Value v0, v1, v2, v3;
        if (!arr->get(0, v0) || !arr->get(1, v1) || !arr->get(2, v2) || !arr->get(3, v3)) {
            ctx.err() << "Error: obs-http-post: malformed URL data\n";
            return false;
        }
        std::string shp(v0.as_string()->view()); v0.release();
        std::string path(v1.as_string()->view()); v1.release();
        std::string hostname(v2.as_string()->view()); v2.release();
        std::string resolved_ip(v3.as_string()->view()); v3.release();

        httplib::Headers headers;
        // Last element is content_type; headers are pairs before it
        size_t header_end = arr->length() - 1;  // last is content_type
        for (size_t i = 4; i + 1 < header_end; i += 2) {
            Value hk, hv;
            if (arr->get(i, hk) && arr->get(i + 1, hv)) {
                if (hk.type == Value::Type::String && hv.type == Value::Type::String) {
                    headers.emplace(std::string(hk.as_string()->view()),
                                    std::string(hv.as_string()->view()));
                }
                hk.release(); hv.release();
            }
        }

        // Get content type (last element of source_array)
        Value ct_val;
        arr->get(arr->length() - 1, ct_val);
        std::string content_type(ct_val.as_string()->view());
        ct_val.release();

        // Get body from state_
        auto* body_ba = obs->state().as_byte_array();
        std::string body_str(reinterpret_cast<const char*>(body_ba->data()), body_ba->length());

        httplib::Client cli(shp);
        if (!resolved_ip.empty()) {
            cli.set_hostname_addr_map({{hostname, resolved_ip}});
        }
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(30, 0);
        cli.set_follow_location(false);

        auto result = cli.Post(path, headers, body_str, content_type);
        if (!result) {
            ctx.err() << "Error: obs-http-post: " << httplib::to_string(result.error()) << "\n";
            return false;
        }

        // Emit response body as single HeapByteArray
        auto& rb = result->body;
        auto* ba = new HeapByteArray(rb.size());
        std::memcpy(ba->data(), rb.data(), rb.size());
        ba->set_tainted(true);
        observer(Value::from(ba), ctx);
        return true;
    }
#else
    case K::HttpGet:
    case K::HttpPost:
    case K::HttpSse:
        ctx.err() << "Error: HTTP client not compiled (ETIL_BUILD_HTTP_CLIENT=OFF)\n";
        return false;
#endif

    // --- Gap fill: high-value RxJS operators ---

    case K::Last: {
        bool has_value = false;
        Value last_val;
        bool result = execute_pipeline(obs->source(), ctx, [&](Value v, ExecutionContext&) -> bool {
            if (has_value) value_release(last_val);
            last_val = v;
            has_value = true;
            return true;
        });
        if (has_value && result) observer(last_val, ctx);
        else if (has_value) value_release(last_val);
        return result;
    }

    case K::Finalize: {
        auto* xt = obs->operator_xt();
        bool result = execute_pipeline(obs->source(), ctx, observer);
        execute_xt(xt, ctx);
        auto discard = ctx.data_stack().pop();
        if (discard) discard->release();
        return result;
    }

    case K::Catch: {
        auto* xt = obs->operator_xt();
        bool result = execute_pipeline(obs->source(), ctx, observer);
        if (!result) {
            auto* fallback = execute_xt_pop_observable(xt, ctx, "obs-catch");
            if (!fallback) return false;
            result = execute_pipeline(fallback, ctx, observer);
            fallback->release();
        }
        return result;
    }

    default:
        return false;

    } // switch
}

} // namespace etil::core
