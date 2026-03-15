// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_observable.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace etil::core {

// ---------------------------------------------------------------------------
// HeapObservable implementation
// ---------------------------------------------------------------------------

HeapObservable::HeapObservable(Kind k) : HeapObject(HeapObject::Kind::Observable), obs_kind_(k) {}

HeapObservable::~HeapObservable() {
    if (source_) source_->release();
    if (source_b_) source_b_->release();
    if (operator_xt_) operator_xt_->release();
    if (source_array_) source_array_->release();
    state_.release();
}

const char* HeapObservable::kind_name() const {
    switch (obs_kind_) {
    case Kind::FromArray: return "from-array";
    case Kind::Of:        return "of";
    case Kind::Empty:     return "empty";
    case Kind::Range:     return "range";
    case Kind::Map:       return "map";
    case Kind::MapWith:   return "map-with";
    case Kind::Filter:    return "filter";
    case Kind::FilterWith:return "filter-with";
    case Kind::Scan:      return "scan";
    case Kind::Reduce:    return "reduce";
    case Kind::Take:      return "take";
    case Kind::Skip:      return "skip";
    case Kind::Distinct:  return "distinct";
    case Kind::Merge:     return "merge";
    case Kind::Concat:    return "concat";
    case Kind::Zip:       return "zip";
    // Temporal
    case Kind::Timer:         return "timer";
    case Kind::Delay:         return "delay";
    case Kind::Timestamp:     return "timestamp";
    case Kind::TimeInterval:  return "time-interval";
    case Kind::DebounceTime:  return "debounce-time";
    case Kind::ThrottleTime:  return "throttle-time";
    case Kind::SampleTime:    return "sample-time";
    case Kind::Timeout:       return "timeout";
    case Kind::BufferTime:    return "buffer-time";
    case Kind::TakeUntilTime: return "take-until-time";
    case Kind::DelayEach:     return "delay-each";
    case Kind::AuditTime:     return "audit-time";
    case Kind::RetryDelay:    return "retry-delay";
    }
    return "unknown";
}

// --- Factory methods ---

HeapObservable* HeapObservable::from_array(HeapArray* arr) {
    auto* o = new HeapObservable(Kind::FromArray);
    arr->add_ref();
    o->source_array_ = arr;
    return o;
}

HeapObservable* HeapObservable::of(Value val) {
    auto* o = new HeapObservable(Kind::Of);
    value_addref(val);
    o->state_ = val;
    return o;
}

HeapObservable* HeapObservable::empty() {
    return new HeapObservable(Kind::Empty);
}

HeapObservable* HeapObservable::range(int64_t start, int64_t end) {
    auto* o = new HeapObservable(Kind::Range);
    o->state_ = Value(start);
    o->param_ = end;
    return o;
}

HeapObservable* HeapObservable::map(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Map);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::map_with(HeapObservable* source, WordImpl* xt, Value ctx) {
    auto* o = new HeapObservable(Kind::MapWith);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    value_addref(ctx); o->state_ = ctx;
    return o;
}

HeapObservable* HeapObservable::filter(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Filter);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::filter_with(HeapObservable* source, WordImpl* xt, Value ctx) {
    auto* o = new HeapObservable(Kind::FilterWith);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    value_addref(ctx); o->state_ = ctx;
    return o;
}

HeapObservable* HeapObservable::scan(HeapObservable* source, WordImpl* xt, Value init) {
    auto* o = new HeapObservable(Kind::Scan);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    value_addref(init); o->state_ = init;
    return o;
}

HeapObservable* HeapObservable::take(HeapObservable* source, int64_t n) {
    auto* o = new HeapObservable(Kind::Take);
    source->add_ref(); o->source_ = source;
    o->param_ = n;
    return o;
}

HeapObservable* HeapObservable::skip(HeapObservable* source, int64_t n) {
    auto* o = new HeapObservable(Kind::Skip);
    source->add_ref(); o->source_ = source;
    o->param_ = n;
    return o;
}

HeapObservable* HeapObservable::distinct(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::Distinct);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::merge(HeapObservable* a, HeapObservable* b, int64_t max_concurrent) {
    auto* o = new HeapObservable(Kind::Merge);
    a->add_ref(); o->source_ = a;
    b->add_ref(); o->source_b_ = b;
    o->param_ = max_concurrent;
    return o;
}

HeapObservable* HeapObservable::concat(HeapObservable* a, HeapObservable* b) {
    auto* o = new HeapObservable(Kind::Concat);
    a->add_ref(); o->source_ = a;
    b->add_ref(); o->source_b_ = b;
    return o;
}

HeapObservable* HeapObservable::zip(HeapObservable* a, HeapObservable* b) {
    auto* o = new HeapObservable(Kind::Zip);
    a->add_ref(); o->source_ = a;
    b->add_ref(); o->source_b_ = b;
    return o;
}

// --- Temporal factory methods ---

HeapObservable* HeapObservable::timer(int64_t delay_us, int64_t period_us) {
    auto* o = new HeapObservable(Kind::Timer);
    o->state_ = Value(delay_us);
    o->param_ = period_us;
    return o;
}

HeapObservable* HeapObservable::delay(HeapObservable* source, int64_t delay_us) {
    auto* o = new HeapObservable(Kind::Delay);
    source->add_ref(); o->source_ = source;
    o->param_ = delay_us;
    return o;
}

HeapObservable* HeapObservable::timestamp(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::Timestamp);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::time_interval(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::TimeInterval);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::delay_each(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::DelayEach);
    source->add_ref(); o->source_ = source;
    xt->add_ref();     o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::debounce_time(HeapObservable* source, int64_t quiet_us) {
    auto* o = new HeapObservable(Kind::DebounceTime);
    source->add_ref(); o->source_ = source;
    o->param_ = quiet_us;
    return o;
}

HeapObservable* HeapObservable::throttle_time(HeapObservable* source, int64_t window_us) {
    auto* o = new HeapObservable(Kind::ThrottleTime);
    source->add_ref(); o->source_ = source;
    o->param_ = window_us;
    return o;
}

HeapObservable* HeapObservable::sample_time(HeapObservable* source, int64_t period_us) {
    auto* o = new HeapObservable(Kind::SampleTime);
    source->add_ref(); o->source_ = source;
    o->param_ = period_us;
    return o;
}

HeapObservable* HeapObservable::timeout(HeapObservable* source, int64_t limit_us) {
    auto* o = new HeapObservable(Kind::Timeout);
    source->add_ref(); o->source_ = source;
    o->param_ = limit_us;
    return o;
}

HeapObservable* HeapObservable::audit_time(HeapObservable* source, int64_t window_us) {
    auto* o = new HeapObservable(Kind::AuditTime);
    source->add_ref(); o->source_ = source;
    o->param_ = window_us;
    return o;
}

HeapObservable* HeapObservable::buffer_time(HeapObservable* source, int64_t window_us) {
    auto* o = new HeapObservable(Kind::BufferTime);
    source->add_ref(); o->source_ = source;
    o->param_ = window_us;
    return o;
}

HeapObservable* HeapObservable::take_until_time(HeapObservable* source, int64_t duration_us) {
    auto* o = new HeapObservable(Kind::TakeUntilTime);
    source->add_ref(); o->source_ = source;
    o->param_ = duration_us;
    return o;
}

HeapObservable* HeapObservable::retry_delay(HeapObservable* source, int64_t delay_us, int64_t max_retries) {
    auto* o = new HeapObservable(Kind::RetryDelay);
    source->add_ref(); o->source_ = source;
    o->state_ = Value(delay_us);
    o->param_ = max_retries;
    return o;
}

// ---------------------------------------------------------------------------
// Execution engine
// ---------------------------------------------------------------------------

/// Observer callback: receives a value, returns true to continue, false to stop.
using Observer = std::function<bool(Value, ExecutionContext&)>;

/// Sleep until target time or until tick() fails (budget/deadline/cancel).
/// Returns true if the target time was reached, false if interrupted.
static bool sleep_until_or_tick(ExecutionContext& ctx,
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

/// Execute an observable pipeline, delivering each emission to the observer.
/// Returns true if completed normally, false on error/cancel/stop.
static bool execute_observable(HeapObservable* obs, ExecutionContext& ctx, const Observer& observer) {
    using K = HeapObservable::Kind;
    switch (obs->obs_kind()) {

    // --- Creation ---

    case K::FromArray: {
        auto* arr = obs->source_array();
        for (size_t i = 0; i < arr->length(); ++i) {
            if (!ctx.tick()) return false;
            Value v;
            if (!arr->get(i, v)) return false;
            if (!observer(v, ctx)) return true;  // stop = normal completion
        }
        return true;
    }

    case K::Of: {
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
        for (int64_t i = start; i < end; ++i) {
            if (!ctx.tick()) return false;
            if (!observer(Value(i), ctx)) return true;
        }
        return true;
    }

    // --- Transform ---

    case K::Map: {
        auto* xt = obs->operator_xt();
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) return false;
            auto res = c.data_stack().pop();
            if (!res) return false;
            return observer(*res, c);
        });
    }

    case K::MapWith: {
        auto* xt = obs->operator_xt();
        Value context = obs->state();
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            value_addref(context);
            c.data_stack().push(context);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) return false;
            auto res = c.data_stack().pop();
            if (!res) return false;
            return observer(*res, c);
        });
    }

    case K::Filter: {
        auto* xt = obs->operator_xt();
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            value_addref(v);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) { value_release(v); return false; }
            auto pred = c.data_stack().pop();
            if (!pred) { value_release(v); return false; }
            if (pred->type == Value::Type::Boolean && pred->as_bool()) {
                return observer(v, c);
            }
            value_release(v);
            return true;  // continue, but don't forward
        });
    }

    case K::FilterWith: {
        auto* xt = obs->operator_xt();
        Value context = obs->state();
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
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
        });
    }

    // --- Accumulate ---

    case K::Scan: {
        auto* xt = obs->operator_xt();
        Value accum = obs->state();
        value_addref(accum);
        bool ok = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            c.data_stack().push(accum);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) return false;
            auto res = c.data_stack().pop();
            if (!res) return false;
            accum = *res;
            value_addref(accum);
            bool cont = observer(accum, c);
            return cont;
        });
        value_release(accum);
        return ok;
    }

    // Reduce is handled as a terminal — not used as a pipeline node.
    case K::Reduce:
        return false;

    // --- Limiting ---

    case K::Take: {
        int64_t remaining = obs->param();
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            if (remaining <= 0) { value_release(v); return false; }
            --remaining;
            return observer(v, c);
        });
    }

    case K::Skip: {
        int64_t to_skip = obs->param();
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            if (to_skip > 0) { --to_skip; value_release(v); return true; }
            return observer(v, c);
        });
    }

    case K::Distinct: {
        bool has_prev = false;
        Value prev = {};
        bool ok = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            // Simple equality: same type + same as_int/as_float
            bool duplicate = has_prev && v.type == prev.type && v.as_int == prev.as_int;
            if (duplicate) {
                value_release(v);
                return true;
            }
            if (has_prev) value_release(prev);
            prev = v;
            value_addref(v);
            has_prev = true;
            return observer(v, c);
        });
        if (has_prev) value_release(prev);
        return ok;
    }

    // --- Combination ---

    case K::Concat: {
        bool ok = execute_observable(obs->source(), ctx, observer);
        if (!ok) return false;
        return execute_observable(obs->source_b(), ctx, observer);
    }

    case K::Merge: {
        // Phase 1 (synchronous): sequential merge — same as concat.
        // param_ (max_concurrent) stored for future async use.
        bool ok = execute_observable(obs->source(), ctx, observer);
        if (!ok) return false;
        return execute_observable(obs->source_b(), ctx, observer);
    }

    case K::Zip: {
        std::vector<Value> vals_a, vals_b;
        execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext&) -> bool {
            vals_a.push_back(v);
            return true;
        });
        execute_observable(obs->source_b(), ctx, [&](Value v, ExecutionContext&) -> bool {
            vals_b.push_back(v);
            return true;
        });
        size_t n = std::min(vals_a.size(), vals_b.size());
        bool ok = true;
        size_t i = 0;
        for (; i < n; ++i) {
            if (!ctx.tick()) { ok = false; break; }
            auto* pair = new HeapArray();
            pair->push_back(vals_a[i]);
            pair->push_back(vals_b[i]);
            if (!observer(Value::from(pair), ctx)) break;
        }
        // Release any unconsumed values
        for (size_t j = (ok ? i + 1 : i); j < vals_a.size(); ++j) value_release(vals_a[j]);
        for (size_t j = (ok ? i + 1 : i); j < vals_b.size(); ++j) value_release(vals_b[j]);
        return ok;
    }

    // --- Temporal: Creation ---

    case K::Timer: {
        int64_t delay_us = obs->state().as_int;
        int64_t period_us = obs->param();
        // Initial delay
        if (delay_us > 0) {
            auto target = std::chrono::steady_clock::now()
                        + std::chrono::microseconds(delay_us);
            if (!sleep_until_or_tick(ctx, target)) return false;
        }
        // Emit 0
        if (!observer(Value(int64_t(0)), ctx)) return true;
        if (period_us <= 0) return true;  // one-shot
        // Repeating emissions
        int64_t counter = 1;
        auto next_tick = std::chrono::steady_clock::now()
                       + std::chrono::microseconds(period_us);
        while (true) {
            if (!sleep_until_or_tick(ctx, next_tick)) return false;
            if (!observer(Value(counter++), ctx)) return true;
            next_tick += std::chrono::microseconds(period_us);
        }
    }

    // --- Temporal: Transform ---

    case K::Delay: {
        int64_t delay_us = obs->param();
        return execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                auto target = std::chrono::steady_clock::now()
                            + std::chrono::microseconds(delay_us);
                if (!sleep_until_or_tick(c, target)) {
                    value_release(v);
                    return false;
                }
                return observer(v, c);
            });
    }

    case K::Timestamp: {
        return execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                auto* pair = new HeapArray();
                pair->push_back(Value(int64_t(now_us)));
                pair->push_back(v);
                return observer(Value::from(pair), c);
            });
    }

    case K::TimeInterval: {
        auto last_time = std::chrono::steady_clock::now();
        return execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                auto now = std::chrono::steady_clock::now();
                auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    now - last_time).count();
                last_time = now;
                auto* pair = new HeapArray();
                pair->push_back(Value(int64_t(elapsed_us)));
                pair->push_back(v);
                return observer(Value::from(pair), c);
            });
    }

    case K::DelayEach: {
        auto* xt = obs->operator_xt();
        return execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                // Push value, execute xt to get delay-us
                value_addref(v);
                c.data_stack().push(v);
                if (!execute_xt(xt, c)) { value_release(v); return false; }
                auto delay_opt = c.data_stack().pop();
                if (!delay_opt) { value_release(v); return false; }
                int64_t delay_us = delay_opt->as_int;
                if (delay_us > 0) {
                    auto target = std::chrono::steady_clock::now()
                                + std::chrono::microseconds(delay_us);
                    if (!sleep_until_or_tick(c, target)) {
                        value_release(v);
                        return false;
                    }
                }
                return observer(v, c);
            });
    }

    // --- Temporal: Rate-Limiting ---

    case K::DebounceTime: {
        int64_t quiet_us = obs->param();
        Value last_value = {};
        bool has_value = false;
        auto last_emission_time = std::chrono::steady_clock::now();

        bool completed = execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                if (has_value) value_release(last_value);
                last_value = v;
                value_addref(last_value);
                has_value = true;
                last_emission_time = std::chrono::steady_clock::now();
                return true;
            });

        // Source completed; emit pending value if quiet period elapsed
        if (has_value) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                now - last_emission_time).count();
            // For synchronous sources (instantaneous), all emissions arrive at once,
            // so elapsed ≈ 0, which is < quiet_us. Still emit on completion (RxJS behavior).
            (void)elapsed;
            observer(last_value, ctx);
        }
        return completed;
    }

    case K::ThrottleTime: {
        int64_t window_us = obs->param();
        auto gate_until = std::chrono::steady_clock::time_point::min();

        return execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                auto now = std::chrono::steady_clock::now();
                if (now >= gate_until) {
                    gate_until = now + std::chrono::microseconds(window_us);
                    return observer(v, c);
                }
                value_release(v);
                return true;
            });
    }

    case K::SampleTime: {
        int64_t period_us = obs->param();
        Value latest = {};
        bool has_latest = false;
        auto next_sample = std::chrono::steady_clock::now()
                         + std::chrono::microseconds(period_us);

        bool completed = execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                if (has_latest) value_release(latest);
                latest = v;
                value_addref(latest);
                has_latest = true;

                auto now = std::chrono::steady_clock::now();
                while (has_latest && now >= next_sample) {
                    if (!observer(latest, c)) {
                        has_latest = false;
                        return false;
                    }
                    has_latest = false;
                    next_sample += std::chrono::microseconds(period_us);
                    now = std::chrono::steady_clock::now();
                }
                return true;
            });

        if (has_latest) value_release(latest);
        return completed;
    }

    case K::Timeout: {
        int64_t limit_us = obs->param();
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::microseconds(limit_us);

        return execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                auto now = std::chrono::steady_clock::now();
                if (now > deadline) {
                    value_release(v);
                    return false;  // timeout
                }
                // Reset deadline on each emission
                deadline = now + std::chrono::microseconds(limit_us);
                return observer(v, c);
            });
    }

    case K::AuditTime: {
        int64_t window_us = obs->param();
        Value last_value = {};
        bool has_value = false;
        bool in_window = false;
        auto window_end = std::chrono::steady_clock::time_point::min();

        bool completed = execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                if (has_value) value_release(last_value);
                last_value = v;
                value_addref(last_value);
                has_value = true;

                auto now = std::chrono::steady_clock::now();
                if (!in_window) {
                    // First emission starts the window
                    in_window = true;
                    window_end = now + std::chrono::microseconds(window_us);
                }

                if (now >= window_end) {
                    // Window elapsed, emit the last value seen
                    Value emit_v = last_value;
                    value_addref(emit_v);
                    in_window = false;
                    if (!observer(emit_v, c)) return false;
                    // Start new window if this was also a trigger
                    in_window = true;
                    window_end = now + std::chrono::microseconds(window_us);
                }
                return true;
            });

        // Emit trailing value if window active
        if (has_value && in_window) {
            observer(last_value, ctx);
            has_value = false;
        }
        if (has_value) value_release(last_value);
        return completed;
    }

    // --- Temporal: Windowed ---

    case K::BufferTime: {
        int64_t window_us = obs->param();
        auto* buffer = new HeapArray();
        auto window_end = std::chrono::steady_clock::now()
                        + std::chrono::microseconds(window_us);

        bool completed = execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                buffer->push_back(v);

                auto now = std::chrono::steady_clock::now();
                if (now >= window_end) {
                    // Emit the buffer and start a new one
                    auto* emit_buf = buffer;
                    buffer = new HeapArray();
                    window_end = now + std::chrono::microseconds(window_us);
                    return observer(Value::from(emit_buf), c);
                }
                return true;
            });

        // Emit remaining buffer if non-empty
        if (buffer->length() > 0) {
            observer(Value::from(buffer), ctx);
        } else {
            delete buffer;
        }
        return completed;
    }

    // --- Temporal: Limiting ---

    case K::TakeUntilTime: {
        int64_t duration_us = obs->param();
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::microseconds(duration_us);

        return execute_observable(obs->source(), ctx,
            [&](Value v, ExecutionContext& c) -> bool {
                if (std::chrono::steady_clock::now() > deadline) {
                    value_release(v);
                    return false;  // time's up
                }
                return observer(v, c);
            });
    }

    // --- Temporal: Error Recovery ---

    case K::RetryDelay: {
        int64_t delay_us = obs->state().as_int;
        int64_t max_retries = obs->param();

        for (int64_t attempt = 0; attempt <= max_retries; ++attempt) {
            if (attempt > 0 && delay_us > 0) {
                auto target = std::chrono::steady_clock::now()
                            + std::chrono::microseconds(delay_us);
                if (!sleep_until_or_tick(ctx, target)) return false;
            }
            bool ok = execute_observable(obs->source(), ctx, observer);
            if (ok) return true;
            if (!ctx.tick()) return false;  // check cancellation between retries
        }
        return false;  // all retries exhausted
    }

    } // switch
    return false;
}

// ---------------------------------------------------------------------------
// TIL Primitives
// ---------------------------------------------------------------------------

// --- Creation ---

// obs-from ( array -- obs )
bool prim_obs_from(ExecutionContext& ctx) {
    auto* arr = pop_array(ctx);
    if (!arr) return false;
    auto* obs = HeapObservable::from_array(arr);
    arr->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-of ( value -- obs )
bool prim_obs_of(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    auto* obs = HeapObservable::of(*opt);
    opt->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-empty ( -- obs )
bool prim_obs_empty(ExecutionContext& ctx) {
    ctx.data_stack().push(Value::from(HeapObservable::empty()));
    return true;
}

// obs-range ( start end -- obs )
bool prim_obs_range(ExecutionContext& ctx) {
    auto opt_end = ctx.data_stack().pop();
    if (!opt_end) return false;
    auto opt_start = ctx.data_stack().pop();
    if (!opt_start) { ctx.data_stack().push(*opt_end); return false; }
    auto* obs = HeapObservable::range(opt_start->as_int, opt_end->as_int);
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- Transform ---

// obs-map ( obs xt -- obs' )
bool prim_obs_map(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt) { ctx.data_stack().push(*xt_val); return false; }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::map(src, xt_val->as_xt_impl());
    src->release();
    xt_val->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-map-with ( obs xt ctx -- obs' )
bool prim_obs_map_with(ExecutionContext& ctx) {
    auto opt_ctx = ctx.data_stack().pop();
    if (!opt_ctx) return false;
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) { ctx.data_stack().push(*opt_ctx); return false; }
    if (xt_val->type != Value::Type::Xt) {
        ctx.data_stack().push(*xt_val); ctx.data_stack().push(*opt_ctx); return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) {
        ctx.data_stack().push(*xt_val); ctx.data_stack().push(*opt_ctx); return false;
    }
    auto* obs = HeapObservable::map_with(src, xt_val->as_xt_impl(), *opt_ctx);
    src->release();
    xt_val->release();
    opt_ctx->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-filter ( obs xt -- obs' )
bool prim_obs_filter(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt) { ctx.data_stack().push(*xt_val); return false; }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::filter(src, xt_val->as_xt_impl());
    src->release();
    xt_val->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-filter-with ( obs xt ctx -- obs' )
bool prim_obs_filter_with(ExecutionContext& ctx) {
    auto opt_ctx = ctx.data_stack().pop();
    if (!opt_ctx) return false;
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) { ctx.data_stack().push(*opt_ctx); return false; }
    if (xt_val->type != Value::Type::Xt) {
        ctx.data_stack().push(*xt_val); ctx.data_stack().push(*opt_ctx); return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) {
        ctx.data_stack().push(*xt_val); ctx.data_stack().push(*opt_ctx); return false;
    }
    auto* obs = HeapObservable::filter_with(src, xt_val->as_xt_impl(), *opt_ctx);
    src->release();
    xt_val->release();
    opt_ctx->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- Accumulate ---

// obs-scan ( obs xt init -- obs' )
bool prim_obs_scan(ExecutionContext& ctx) {
    auto opt_init = ctx.data_stack().pop();
    if (!opt_init) return false;
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) { ctx.data_stack().push(*opt_init); return false; }
    if (xt_val->type != Value::Type::Xt) {
        ctx.data_stack().push(*xt_val); ctx.data_stack().push(*opt_init); return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) {
        ctx.data_stack().push(*xt_val); ctx.data_stack().push(*opt_init); return false;
    }
    auto* obs = HeapObservable::scan(src, xt_val->as_xt_impl(), *opt_init);
    src->release();
    xt_val->release();
    opt_init->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-reduce ( obs xt init -- result ) — terminal
bool prim_obs_reduce(ExecutionContext& ctx) {
    auto opt_init = ctx.data_stack().pop();
    if (!opt_init) return false;
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) { ctx.data_stack().push(*opt_init); return false; }
    if (xt_val->type != Value::Type::Xt) {
        ctx.data_stack().push(*xt_val); ctx.data_stack().push(*opt_init); return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) {
        ctx.data_stack().push(*xt_val); ctx.data_stack().push(*opt_init); return false;
    }
    auto* xt = xt_val->as_xt_impl();
    Value accum = *opt_init;
    bool ok = execute_observable(src, ctx, [&](Value v, ExecutionContext& c) -> bool {
        c.data_stack().push(accum);
        c.data_stack().push(v);
        if (!execute_xt(xt, c)) return false;
        auto res = c.data_stack().pop();
        if (!res) return false;
        accum = *res;
        return true;
    });
    src->release();
    xt->release();
    if (!ok) { value_release(accum); return false; }
    ctx.data_stack().push(accum);
    return true;
}

// --- Limiting ---

// obs-take ( obs n -- obs' )
bool prim_obs_take(ExecutionContext& ctx) {
    auto opt_n = ctx.data_stack().pop();
    if (!opt_n) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_n); return false; }
    auto* obs = HeapObservable::take(src, opt_n->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-skip ( obs n -- obs' )
bool prim_obs_skip(ExecutionContext& ctx) {
    auto opt_n = ctx.data_stack().pop();
    if (!opt_n) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_n); return false; }
    auto* obs = HeapObservable::skip(src, opt_n->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-distinct ( obs -- obs' )
bool prim_obs_distinct(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    auto* obs = HeapObservable::distinct(src);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- Combination ---

// obs-merge ( obs-a obs-b max-concurrent -- obs )
bool prim_obs_merge(ExecutionContext& ctx) {
    auto opt_mc = ctx.data_stack().pop();
    if (!opt_mc) return false;
    auto* b = pop_observable(ctx);
    if (!b) { ctx.data_stack().push(*opt_mc); return false; }
    auto* a = pop_observable(ctx);
    if (!a) { ctx.data_stack().push(Value::from(b)); ctx.data_stack().push(*opt_mc); return false; }
    auto* obs = HeapObservable::merge(a, b, opt_mc->as_int);
    a->release();
    b->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-concat ( obs-a obs-b -- obs )
bool prim_obs_concat(ExecutionContext& ctx) {
    auto* b = pop_observable(ctx);
    if (!b) return false;
    auto* a = pop_observable(ctx);
    if (!a) { ctx.data_stack().push(Value::from(b)); return false; }
    auto* obs = HeapObservable::concat(a, b);
    a->release();
    b->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-zip ( obs-a obs-b -- obs )
bool prim_obs_zip(ExecutionContext& ctx) {
    auto* b = pop_observable(ctx);
    if (!b) return false;
    auto* a = pop_observable(ctx);
    if (!a) { ctx.data_stack().push(Value::from(b)); return false; }
    auto* obs = HeapObservable::zip(a, b);
    a->release();
    b->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- Terminal ---

// obs-subscribe ( obs xt -- )
bool prim_obs_subscribe(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt) { ctx.data_stack().push(*xt_val); return false; }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* xt = xt_val->as_xt_impl();
    bool ok = execute_observable(src, ctx, [&](Value v, ExecutionContext& c) -> bool {
        c.data_stack().push(v);
        return execute_xt(xt, c);
    });
    src->release();
    xt->release();
    return ok;
}

// obs-to-array ( obs -- array )
bool prim_obs_to_array(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    auto* result = new HeapArray();
    bool ok = execute_observable(src, ctx, [&](Value v, ExecutionContext&) -> bool {
        result->push_back(v);
        return true;
    });
    src->release();
    if (!ok) { delete result; return false; }
    ctx.data_stack().push(Value::from(result));
    return true;
}

// obs-count ( obs -- n )
bool prim_obs_count(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    int64_t count = 0;
    bool ok = execute_observable(src, ctx, [&](Value v, ExecutionContext&) -> bool {
        value_release(v);
        ++count;
        return true;
    });
    src->release();
    if (!ok) return false;
    ctx.data_stack().push(Value(count));
    return true;
}

// --- Introspection ---

// obs? ( value -- bool )
bool prim_obs_check(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    bool is_obs = (opt->type == Value::Type::Observable && opt->as_ptr != nullptr);
    opt->release();
    ctx.data_stack().push(Value(is_obs));
    return true;
}

// obs-kind ( obs -- string )
bool prim_obs_kind(ExecutionContext& ctx) {
    auto* obs = pop_observable(ctx);
    if (!obs) return false;
    auto* name = HeapString::create(obs->kind_name());
    obs->release();
    ctx.data_stack().push(Value::from(name));
    return true;
}

// --- Temporal: Creation ---

// obs-timer ( delay-us period-us -- obs )
bool prim_obs_timer(ExecutionContext& ctx) {
    auto opt_period = ctx.data_stack().pop();
    if (!opt_period) return false;
    auto opt_delay = ctx.data_stack().pop();
    if (!opt_delay) { ctx.data_stack().push(*opt_period); return false; }
    auto* obs = HeapObservable::timer(opt_delay->as_int, opt_period->as_int);
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- Temporal: Transform ---

// obs-delay ( obs delay-us -- obs' )
bool prim_obs_delay(ExecutionContext& ctx) {
    auto opt_delay = ctx.data_stack().pop();
    if (!opt_delay) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_delay); return false; }
    auto* obs = HeapObservable::delay(src, opt_delay->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-timestamp ( obs -- obs' )
bool prim_obs_timestamp(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    auto* obs = HeapObservable::timestamp(src);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-time-interval ( obs -- obs' )
bool prim_obs_time_interval(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    auto* obs = HeapObservable::time_interval(src);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-delay-each ( obs xt -- obs' )
bool prim_obs_delay_each(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt) { ctx.data_stack().push(*xt_val); return false; }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::delay_each(src, xt_val->as_xt_impl());
    src->release();
    xt_val->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- Temporal: Rate-Limiting ---

// obs-debounce-time ( obs quiet-us -- obs' )
bool prim_obs_debounce_time(ExecutionContext& ctx) {
    auto opt_quiet = ctx.data_stack().pop();
    if (!opt_quiet) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_quiet); return false; }
    auto* obs = HeapObservable::debounce_time(src, opt_quiet->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-throttle-time ( obs window-us -- obs' )
bool prim_obs_throttle_time(ExecutionContext& ctx) {
    auto opt_window = ctx.data_stack().pop();
    if (!opt_window) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_window); return false; }
    auto* obs = HeapObservable::throttle_time(src, opt_window->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-sample-time ( obs period-us -- obs' )
bool prim_obs_sample_time(ExecutionContext& ctx) {
    auto opt_period = ctx.data_stack().pop();
    if (!opt_period) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_period); return false; }
    auto* obs = HeapObservable::sample_time(src, opt_period->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-timeout ( obs limit-us -- obs' )
bool prim_obs_timeout(ExecutionContext& ctx) {
    auto opt_limit = ctx.data_stack().pop();
    if (!opt_limit) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_limit); return false; }
    auto* obs = HeapObservable::timeout(src, opt_limit->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-audit-time ( obs window-us -- obs' )
bool prim_obs_audit_time(ExecutionContext& ctx) {
    auto opt_window = ctx.data_stack().pop();
    if (!opt_window) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_window); return false; }
    auto* obs = HeapObservable::audit_time(src, opt_window->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- Temporal: Windowed + Limiting ---

// obs-buffer-time ( obs window-us -- obs' )
bool prim_obs_buffer_time(ExecutionContext& ctx) {
    auto opt_window = ctx.data_stack().pop();
    if (!opt_window) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_window); return false; }
    auto* obs = HeapObservable::buffer_time(src, opt_window->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-take-until-time ( obs duration-us -- obs' )
bool prim_obs_take_until_time(ExecutionContext& ctx) {
    auto opt_dur = ctx.data_stack().pop();
    if (!opt_dur) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_dur); return false; }
    auto* obs = HeapObservable::take_until_time(src, opt_dur->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- Temporal: Error Recovery ---

// obs-retry-delay ( obs delay-us max-retries -- obs' )
bool prim_obs_retry_delay(ExecutionContext& ctx) {
    auto opt_retries = ctx.data_stack().pop();
    if (!opt_retries) return false;
    auto opt_delay = ctx.data_stack().pop();
    if (!opt_delay) { ctx.data_stack().push(*opt_retries); return false; }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_delay); ctx.data_stack().push(*opt_retries); return false; }
    auto* obs = HeapObservable::retry_delay(src, opt_delay->as_int, opt_retries->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void register_observable_primitives(Dictionary& dict) {
    using TS = TypeSignature;
    using T = TS::Type;

    auto mk = [](const char* name, WordImpl::FunctionPtr fn,
                  std::vector<T> inputs, std::vector<T> outputs) {
        return make_primitive(name, fn, std::move(inputs), std::move(outputs));
    };

    // Creation
    dict.register_word("obs-from",    mk("prim_obs_from", prim_obs_from,
        {T::Array}, {T::Unknown}));
    dict.register_word("obs-of",      mk("prim_obs_of", prim_obs_of,
        {T::Unknown}, {T::Unknown}));
    dict.register_word("obs-empty",   mk("prim_obs_empty", prim_obs_empty,
        {}, {T::Unknown}));
    dict.register_word("obs-range",   mk("prim_obs_range", prim_obs_range,
        {T::Integer, T::Integer}, {T::Unknown}));

    // Transform
    dict.register_word("obs-map",     mk("prim_obs_map", prim_obs_map,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-map-with", mk("prim_obs_map_with", prim_obs_map_with,
        {T::Unknown, T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-filter",  mk("prim_obs_filter", prim_obs_filter,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-filter-with", mk("prim_obs_filter_with", prim_obs_filter_with,
        {T::Unknown, T::Unknown, T::Unknown}, {T::Unknown}));

    // Accumulate
    dict.register_word("obs-scan",    mk("prim_obs_scan", prim_obs_scan,
        {T::Unknown, T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-reduce",  mk("prim_obs_reduce", prim_obs_reduce,
        {T::Unknown, T::Unknown, T::Unknown}, {T::Unknown}));

    // Limiting
    dict.register_word("obs-take",    mk("prim_obs_take", prim_obs_take,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-skip",    mk("prim_obs_skip", prim_obs_skip,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-distinct", mk("prim_obs_distinct", prim_obs_distinct,
        {T::Unknown}, {T::Unknown}));

    // Combination
    dict.register_word("obs-merge",   mk("prim_obs_merge", prim_obs_merge,
        {T::Unknown, T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-concat",  mk("prim_obs_concat", prim_obs_concat,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-zip",     mk("prim_obs_zip", prim_obs_zip,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    // Terminal
    dict.register_word("obs-subscribe", mk("prim_obs_subscribe", prim_obs_subscribe,
        {T::Unknown, T::Unknown}, {}));
    dict.register_word("obs-to-array", mk("prim_obs_to_array", prim_obs_to_array,
        {T::Unknown}, {T::Array}));
    dict.register_word("obs-count",   mk("prim_obs_count", prim_obs_count,
        {T::Unknown}, {T::Integer}));

    // Introspection
    dict.register_word("obs?",        mk("prim_obs_check", prim_obs_check,
        {T::Unknown}, {T::Unknown}));
    dict.register_word("obs-kind",    mk("prim_obs_kind", prim_obs_kind,
        {T::Unknown}, {T::Unknown}));

    // Temporal: Creation
    dict.register_word("obs-timer",   mk("prim_obs_timer", prim_obs_timer,
        {T::Integer, T::Integer}, {T::Unknown}));

    // Temporal: Transform
    dict.register_word("obs-delay",   mk("prim_obs_delay", prim_obs_delay,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-timestamp", mk("prim_obs_timestamp", prim_obs_timestamp,
        {T::Unknown}, {T::Unknown}));
    dict.register_word("obs-time-interval", mk("prim_obs_time_interval", prim_obs_time_interval,
        {T::Unknown}, {T::Unknown}));
    dict.register_word("obs-delay-each", mk("prim_obs_delay_each", prim_obs_delay_each,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    // Temporal: Rate-Limiting
    dict.register_word("obs-debounce-time", mk("prim_obs_debounce_time", prim_obs_debounce_time,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-throttle-time", mk("prim_obs_throttle_time", prim_obs_throttle_time,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-sample-time", mk("prim_obs_sample_time", prim_obs_sample_time,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-timeout", mk("prim_obs_timeout", prim_obs_timeout,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-audit-time", mk("prim_obs_audit_time", prim_obs_audit_time,
        {T::Unknown, T::Integer}, {T::Unknown}));

    // Temporal: Windowed + Limiting
    dict.register_word("obs-buffer-time", mk("prim_obs_buffer_time", prim_obs_buffer_time,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-take-until-time", mk("prim_obs_take_until_time", prim_obs_take_until_time,
        {T::Unknown, T::Integer}, {T::Unknown}));

    // Temporal: Error Recovery
    dict.register_word("obs-retry-delay", mk("prim_obs_retry_delay", prim_obs_retry_delay,
        {T::Unknown, T::Integer, T::Integer}, {T::Unknown}));
}

} // namespace etil::core
