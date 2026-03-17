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

#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_map.hpp"
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
#include <filesystem>
#include <fstream>
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
    case Kind::Buffer:        return "buffer";
    case Kind::BufferWhen:    return "buffer-when";
    case Kind::Window:        return "window";
    case Kind::FlatMap:       return "flat-map";
    case Kind::ReadBytes:     return "read-bytes";
    case Kind::ReadLines:     return "read-lines";
    case Kind::ReadJson:      return "read-json";
    case Kind::ReadCsv:       return "read-csv";
    case Kind::ReadDir:       return "read-dir";
    case Kind::HttpGet:       return "http-get";
    case Kind::HttpPost:      return "http-post";
    case Kind::HttpSse:       return "http-sse";
    case Kind::Tap:           return "tap";
    case Kind::Pairwise:      return "pairwise";
    case Kind::First:         return "first";
    case Kind::Last:          return "last";
    case Kind::TakeWhile:     return "take-while";
    case Kind::DistinctUntil: return "distinct-until";
    case Kind::StartWith:     return "start-with";
    case Kind::Finalize:      return "finalize";
    case Kind::SwitchMap:     return "switch-map";
    case Kind::Catch:         return "catch";
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

// AVO Phase 1 factories

HeapObservable* HeapObservable::buffer(HeapObservable* source, int64_t count) {
    auto* o = new HeapObservable(Kind::Buffer);
    source->add_ref(); o->source_ = source;
    o->param_ = count;
    return o;
}

HeapObservable* HeapObservable::buffer_when(HeapObservable* source, WordImpl* predicate_xt) {
    auto* o = new HeapObservable(Kind::BufferWhen);
    source->add_ref(); o->source_ = source;
    predicate_xt->add_ref(); o->operator_xt_ = predicate_xt;
    return o;
}

HeapObservable* HeapObservable::window(HeapObservable* source, int64_t size) {
    auto* o = new HeapObservable(Kind::Window);
    source->add_ref(); o->source_ = source;
    o->param_ = size;
    return o;
}

HeapObservable* HeapObservable::flat_map(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::FlatMap);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

// AVO Phase 2 factories

HeapObservable* HeapObservable::read_bytes(HeapString* fs_path, int64_t chunk_size) {
    auto* o = new HeapObservable(Kind::ReadBytes);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    o->param_ = chunk_size;
    return o;
}

HeapObservable* HeapObservable::read_lines(HeapString* fs_path) {
    auto* o = new HeapObservable(Kind::ReadLines);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    return o;
}

HeapObservable* HeapObservable::read_json(HeapString* fs_path) {
    auto* o = new HeapObservable(Kind::ReadJson);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    return o;
}

HeapObservable* HeapObservable::read_csv(HeapString* fs_path, HeapString* separator) {
    auto* o = new HeapObservable(Kind::ReadCsv);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    // Store separator in a single-element source_array
    auto* arr = new HeapArray();
    separator->add_ref();
    arr->push_back(Value::from(separator));
    o->source_array_ = arr;
    return o;
}

HeapObservable* HeapObservable::read_dir(HeapString* fs_path) {
    auto* o = new HeapObservable(Kind::ReadDir);
    fs_path->add_ref();
    o->state_ = Value::from(fs_path);
    return o;
}

// AVO Phase 3 factories

HeapObservable* HeapObservable::http_get(HeapArray* url_data) {
    auto* o = new HeapObservable(Kind::HttpGet);
    url_data->add_ref();
    o->source_array_ = url_data;
    return o;
}

HeapObservable* HeapObservable::http_post(HeapArray* url_data, HeapByteArray* body, HeapString* content_type) {
    auto* o = new HeapObservable(Kind::HttpPost);
    url_data->add_ref();
    o->source_array_ = url_data;
    // Store body as Value in state_
    body->add_ref();
    o->state_ = Value::from(body);
    // Store content_type: use param_ as flag, add content_type to url_data
    content_type->add_ref();
    url_data->push_back(Value::from(content_type));  // last element
    return o;
}

HeapObservable* HeapObservable::http_sse(HeapArray* url_data) {
    auto* o = new HeapObservable(Kind::HttpSse);
    url_data->add_ref();
    o->source_array_ = url_data;
    return o;
}

// Gap fill factories

HeapObservable* HeapObservable::tap(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Tap);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::pairwise(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::Pairwise);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::first(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::First);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::last(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::Last);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::take_while(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::TakeWhile);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::distinct_until(HeapObservable* source) {
    auto* o = new HeapObservable(Kind::DistinctUntil);
    source->add_ref(); o->source_ = source;
    return o;
}

HeapObservable* HeapObservable::start_with(HeapObservable* source, Value val) {
    auto* o = new HeapObservable(Kind::StartWith);
    source->add_ref(); o->source_ = source;
    value_addref(val);
    o->state_ = val;
    return o;
}

HeapObservable* HeapObservable::finalize(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Finalize);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::switch_map(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::SwitchMap);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
    return o;
}

HeapObservable* HeapObservable::catch_error(HeapObservable* source, WordImpl* xt) {
    auto* o = new HeapObservable(Kind::Catch);
    source->add_ref(); o->source_ = source;
    xt->add_ref(); o->operator_xt_ = xt;
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

    // --- AVO Phase 1: Buffer + Composition ---

    case K::Buffer: {
        int64_t count = obs->param();
        auto* batch = new HeapArray();
        bool result = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
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
        bool result = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
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
        bool result = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
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

    case K::FlatMap: {
        auto* xt = obs->operator_xt();
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            // Push value, execute xt — should return an Observable
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) return false;
            auto opt = c.data_stack().pop();
            if (!opt || opt->type != Value::Type::Observable || !opt->as_ptr) {
                if (opt) opt->release();
                c.err() << "Error: obs-flat-map xt must return an observable\n";
                return false;
            }
            auto* sub_obs = opt->as_observable();
            // Execute sub-observable, forwarding emissions to downstream observer
            bool ok = execute_observable(sub_obs, c, observer);
            sub_obs->release();
            return ok;
        });
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

    case K::Tap: {
        auto* xt = obs->operator_xt();
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            value_addref(v);
            c.data_stack().push(v);
            if (!execute_xt(xt, c)) { value_release(v); return false; }
            auto discard = c.data_stack().pop();
            if (discard) discard->release();
            return observer(v, c);
        });
    }

    case K::Pairwise: {
        bool has_prev = false;
        Value prev;
        bool result = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            if (!has_prev) {
                prev = v;
                has_prev = true;
                return true;
            }
            auto* pair = new HeapArray();
            pair->push_back(prev);
            value_addref(v);
            pair->push_back(v);
            prev = v;
            return observer(Value::from(pair), c);
        });
        if (has_prev) value_release(prev);
        return result;
    }

    case K::First:
        return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            observer(v, c);
            return false;  // stop after first
        });

    case K::Last: {
        bool has_value = false;
        Value last_val;
        bool result = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext&) -> bool {
            if (has_value) value_release(last_val);
            last_val = v;
            has_value = true;
            return true;
        });
        if (has_value && result) observer(last_val, ctx);
        else if (has_value) value_release(last_val);
        return result;
    }

    case K::TakeWhile: {
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
            return false;  // stop
        });
    }

    case K::DistinctUntil: {
        bool has_prev = false;
        Value prev;
        bool result = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
            bool duplicate = has_prev && v.type == prev.type && v.as_int == prev.as_int;
            if (duplicate) {
                value_release(v);
                return true;
            }
            if (has_prev) value_release(prev);
            value_addref(v);
            prev = v;
            has_prev = true;
            return observer(v, c);
        });
        if (has_prev) value_release(prev);
        return result;
    }

    case K::StartWith: {
        Value start_val = obs->state();
        value_addref(start_val);
        if (!observer(start_val, ctx)) return true;
        return execute_observable(obs->source(), ctx, observer);
    }

    case K::Finalize: {
        auto* xt = obs->operator_xt();
        bool result = execute_observable(obs->source(), ctx, observer);
        execute_xt(xt, ctx);
        auto discard = ctx.data_stack().pop();
        if (discard) discard->release();
        return result;
    }

    case K::SwitchMap: {
        auto* xt = obs->operator_xt();
        // Collect all upstream values
        std::vector<Value> upstream;
        bool src_ok = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext&) -> bool {
            upstream.push_back(v);
            return true;
        });
        if (!src_ok) {
            for (auto& v : upstream) value_release(v);
            return false;
        }
        // Only forward emissions from the last inner observable
        for (size_t i = 0; i < upstream.size(); ++i) {
            if (!ctx.tick()) {
                for (size_t j = i; j < upstream.size(); ++j) value_release(upstream[j]);
                return false;
            }
            ctx.data_stack().push(upstream[i]);
            if (!execute_xt(xt, ctx)) return false;
            auto opt = ctx.data_stack().pop();
            if (!opt || opt->type != Value::Type::Observable || !opt->as_ptr) {
                if (opt) opt->release();
                ctx.err() << "Error: obs-switch-map xt must return an observable\n";
                return false;
            }
            auto* sub_obs = opt->as_observable();
            bool is_last = (i == upstream.size() - 1);
            if (is_last) {
                bool ok = execute_observable(sub_obs, ctx, observer);
                sub_obs->release();
                return ok;
            } else {
                execute_observable(sub_obs, ctx, [](Value v, ExecutionContext&) -> bool {
                    value_release(v);
                    return true;
                });
                sub_obs->release();
            }
        }
        return true;
    }

    case K::Catch: {
        auto* xt = obs->operator_xt();
        bool result = execute_observable(obs->source(), ctx, observer);
        if (!result) {
            if (!execute_xt(xt, ctx)) return false;
            auto opt = ctx.data_stack().pop();
            if (!opt || opt->type != Value::Type::Observable || !opt->as_ptr) {
                if (opt) opt->release();
                return false;
            }
            auto* fallback = opt->as_observable();
            result = execute_observable(fallback, ctx, observer);
            fallback->release();
        }
        return result;
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

// --- AVO Phase 1: Buffer + Composition ---

// obs-buffer ( obs n -- obs' )
bool prim_obs_buffer(ExecutionContext& ctx) {
    auto opt_n = ctx.data_stack().pop();
    if (!opt_n) return false;
    if (opt_n->type != Value::Type::Integer || opt_n->as_int <= 0) {
        ctx.data_stack().push(*opt_n);
        ctx.err() << "Error: obs-buffer requires a positive integer\n";
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_n); return false; }
    auto* obs = HeapObservable::buffer(src, opt_n->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-buffer-when ( obs xt -- obs' )
bool prim_obs_buffer_when(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt || !xt_val->as_ptr) {
        ctx.data_stack().push(*xt_val);
        ctx.err() << "Error: obs-buffer-when requires an xt\n";
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::buffer_when(src, xt_val->as_xt_impl());
    src->release();
    xt_val->as_xt_impl()->release();  // factory addref'd
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-window ( obs n -- obs' )
bool prim_obs_window(ExecutionContext& ctx) {
    auto opt_n = ctx.data_stack().pop();
    if (!opt_n) return false;
    if (opt_n->type != Value::Type::Integer || opt_n->as_int <= 0) {
        ctx.data_stack().push(*opt_n);
        ctx.err() << "Error: obs-window requires a positive integer\n";
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_n); return false; }
    auto* obs = HeapObservable::window(src, opt_n->as_int);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-flat-map ( obs xt -- obs' )
bool prim_obs_flat_map(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt || !xt_val->as_ptr) {
        ctx.data_stack().push(*xt_val);
        ctx.err() << "Error: obs-flat-map requires an xt\n";
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::flat_map(src, xt_val->as_xt_impl());
    src->release();
    xt_val->as_xt_impl()->release();  // factory addref'd
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// --- AVO Phase 2: Streaming File I/O primitives ---

// Helper: pop a path string, resolve via LVFS, return resolved HeapString*.
// Returns nullptr on failure (error printed, stack handled).
static HeapString* pop_and_resolve_obs_path(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return nullptr;
    std::string vpath(hs->view());
    hs->release();

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.err() << "Error: no LVFS available\n";
        return nullptr;
    }
    std::string fs_path = lvfs->resolve(vpath);
    if (fs_path.empty()) {
        ctx.err() << "Error: cannot resolve path '" << vpath << "'\n";
        return nullptr;
    }
    return HeapString::create(fs_path);
}

// obs-read-bytes ( path chunk-size -- obs )
bool prim_obs_read_bytes(ExecutionContext& ctx) {
    auto opt_size = ctx.data_stack().pop();
    if (!opt_size) return false;
    if (opt_size->type != Value::Type::Integer || opt_size->as_int <= 0) {
        ctx.data_stack().push(*opt_size);
        ctx.err() << "Error: obs-read-bytes requires a positive chunk size\n";
        return false;
    }
    auto* path = pop_and_resolve_obs_path(ctx);
    if (!path) { ctx.data_stack().push(*opt_size); return false; }
    auto* obs = HeapObservable::read_bytes(path, opt_size->as_int);
    path->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-read-lines ( path -- obs )
bool prim_obs_read_lines(ExecutionContext& ctx) {
    auto* path = pop_and_resolve_obs_path(ctx);
    if (!path) return false;
    auto* obs = HeapObservable::read_lines(path);
    path->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-read-json ( path -- obs )
bool prim_obs_read_json(ExecutionContext& ctx) {
    auto* path = pop_and_resolve_obs_path(ctx);
    if (!path) return false;
    auto* obs = HeapObservable::read_json(path);
    path->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-read-csv ( path separator -- obs )
bool prim_obs_read_csv(ExecutionContext& ctx) {
    auto* sep = pop_string(ctx);
    if (!sep) return false;
    auto* path = pop_and_resolve_obs_path(ctx);
    if (!path) { sep->release(); return false; }
    auto* obs = HeapObservable::read_csv(path, sep);
    path->release();
    sep->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-readdir ( path -- obs )
bool prim_obs_readdir(ExecutionContext& ctx) {
    auto* path = pop_and_resolve_obs_path(ctx);
    if (!path) return false;
    auto* obs = HeapObservable::read_dir(path);
    path->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-write-file ( obs path -- ) — terminal: write all emissions to file
bool prim_obs_write_file(ExecutionContext& ctx) {
    auto* path_hs = pop_string(ctx);
    if (!path_hs) return false;
    std::string vpath(path_hs->view());
    path_hs->release();

    auto* obs = pop_observable(ctx);
    if (!obs) return false;

    // Check write permission
    auto* perms = ctx.permissions();
    if (perms && !perms->lvfs_modify) {
        ctx.err() << "Error: file modification not permitted\n";
        obs->release();
        return false;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) { ctx.err() << "Error: no LVFS\n"; obs->release(); return false; }
    std::string fs_path = lvfs->resolve(vpath);
    if (fs_path.empty() || lvfs->is_read_only(vpath)) {
        ctx.err() << "Error: cannot write to '" << vpath << "'\n";
        obs->release();
        return false;
    }

    std::ofstream file(fs_path, std::ios::binary | std::ios::trunc);
    if (!file) {
        ctx.err() << "Error: obs-write-file cannot open '" << fs_path << "'\n";
        obs->release();
        return false;
    }

    bool ok = execute_observable(obs, ctx, [&](Value v, ExecutionContext&) -> bool {
        if (v.type == Value::Type::String && v.as_ptr) {
            auto sv = v.as_string()->view();
            file.write(sv.data(), static_cast<std::streamsize>(sv.size()));
        } else if (v.type == Value::Type::ByteArray && v.as_ptr) {
            auto* ba = v.as_byte_array();
            for (size_t i = 0; i < ba->length(); ++i) {
                uint8_t byte;
                ba->get(i, byte);
                file.put(static_cast<char>(byte));
            }
        }
        value_release(v);
        return true;
    });
    obs->release();
    file.close();
    return ok;
}

// obs-append-file ( obs path -- ) — terminal: append all emissions to file
bool prim_obs_append_file(ExecutionContext& ctx) {
    auto* path_hs = pop_string(ctx);
    if (!path_hs) return false;
    std::string vpath(path_hs->view());
    path_hs->release();

    auto* obs = pop_observable(ctx);
    if (!obs) return false;

    auto* perms = ctx.permissions();
    if (perms && !perms->lvfs_modify) {
        ctx.err() << "Error: file modification not permitted\n";
        obs->release();
        return false;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) { ctx.err() << "Error: no LVFS\n"; obs->release(); return false; }
    std::string fs_path = lvfs->resolve(vpath);
    if (fs_path.empty() || lvfs->is_read_only(vpath)) {
        ctx.err() << "Error: cannot write to '" << vpath << "'\n";
        obs->release();
        return false;
    }

    std::ofstream file(fs_path, std::ios::binary | std::ios::app);
    if (!file) {
        ctx.err() << "Error: obs-append-file cannot open '" << fs_path << "'\n";
        obs->release();
        return false;
    }

    bool ok = execute_observable(obs, ctx, [&](Value v, ExecutionContext&) -> bool {
        if (v.type == Value::Type::String && v.as_ptr) {
            auto sv = v.as_string()->view();
            file.write(sv.data(), static_cast<std::streamsize>(sv.size()));
        } else if (v.type == Value::Type::ByteArray && v.as_ptr) {
            auto* ba = v.as_byte_array();
            for (size_t i = 0; i < ba->length(); ++i) {
                uint8_t byte;
                ba->get(i, byte);
                file.put(static_cast<char>(byte));
            }
        }
        value_release(v);
        return true;
    });
    obs->release();
    file.close();
    return ok;
}

// --- AVO Phase 3: Streaming HTTP primitives ---

#ifdef ETIL_HTTP_CLIENT_ENABLED
// Helper: pop URL + headers-map, validate, build url_data array.
// Returns nullptr on failure (error printed, stack handled).
static HeapArray* pop_and_validate_http(ExecutionContext& ctx) {
    // Pop headers map (TOS)
    auto hdr_opt = ctx.data_stack().pop();
    if (!hdr_opt) return nullptr;
    if (hdr_opt->type != Value::Type::Map || !hdr_opt->as_ptr) {
        ctx.err() << "Error: expected Map for headers\n";
        value_release(*hdr_opt);
        return nullptr;
    }
    auto* hdr_map = hdr_opt->as_map();

    // Pop URL string
    auto* hs = pop_string(ctx);
    if (!hs) { hdr_map->release(); return nullptr; }
    std::string url(hs->view());
    hs->release();

    // Check permissions
    auto* perms = ctx.permissions();
    if (perms && !perms->net_client_allowed) {
        ctx.err() << "Error: HTTP not permitted\n";
        hdr_map->release();
        return nullptr;
    }

    auto* http_state = ctx.http_client_state();
    if (!http_state || !http_state->config || !http_state->config->enabled()) {
        ctx.err() << "Error: HTTP client not configured\n";
        hdr_map->release();
        return nullptr;
    }
    if (!http_state->can_fetch()) {
        ctx.err() << "Error: fetch budget exceeded\n";
        hdr_map->release();
        return nullptr;
    }

    etil::net::ParsedUrl parsed;
    std::string error;
    if (!etil::net::validate_url(url, *http_state->config, parsed, error)) {
        ctx.err() << "Error: " << error << "\n";
        hdr_map->release();
        return nullptr;
    }

    http_state->record_fetch();

    // Build url_data array: [scheme_host_port, path, hostname, resolved_ip, hdr_k, hdr_v, ...]
    std::string shp = parsed.scheme + "://" + parsed.host;
    if (parsed.port != 0) shp += ":" + std::to_string(parsed.port);

    auto* arr = new HeapArray();
    arr->push_back(Value::from(HeapString::create(shp)));
    arr->push_back(Value::from(HeapString::create(parsed.path)));
    arr->push_back(Value::from(HeapString::create(parsed.host)));
    arr->push_back(Value::from(HeapString::create(parsed.resolved_ip)));

    for (const auto& [key, val] : hdr_map->entries()) {
        if (val.type == Value::Type::String && val.as_ptr) {
            arr->push_back(Value::from(HeapString::create(key)));
            value_addref(val);
            arr->push_back(val);
        }
    }
    hdr_map->release();
    return arr;
}

// obs-http-get ( url headers -- obs )
bool prim_obs_http_get(ExecutionContext& ctx) {
    auto* url_data = pop_and_validate_http(ctx);
    if (!url_data) return false;
    auto* obs = HeapObservable::http_get(url_data);
    url_data->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-http-post ( url headers body -- obs )
bool prim_obs_http_post(ExecutionContext& ctx) {
    // Pop body (TOS)
    auto* body = pop_byte_array(ctx);
    if (!body) return false;

    auto* url_data = pop_and_validate_http(ctx);
    if (!url_data) { body->release(); return false; }

    // Extract Content-Type from headers if present, default to application/octet-stream
    auto* ct = HeapString::create("application/octet-stream");
    // Check if headers had Content-Type (it's in url_data pairs starting at index 4)
    for (size_t i = 4; i + 1 < url_data->length(); i += 2) {
        Value hk;
        if (url_data->get(i, hk) && hk.type == Value::Type::String) {
            std::string key(hk.as_string()->view());
            hk.release();
            // Case-insensitive Content-Type check
            std::string lower_key = key;
            for (auto& c : lower_key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower_key == "content-type") {
                Value hv;
                if (url_data->get(i + 1, hv) && hv.type == Value::Type::String) {
                    ct->release();
                    ct = HeapString::create(std::string(hv.as_string()->view()));
                    hv.release();
                }
                break;
            }
        }
    }

    auto* obs = HeapObservable::http_post(url_data, body, ct);
    url_data->release();
    body->release();
    ct->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-http-sse ( url headers -- obs )
bool prim_obs_http_sse(ExecutionContext& ctx) {
    auto* url_data = pop_and_validate_http(ctx);
    if (!url_data) return false;
    auto* obs = HeapObservable::http_sse(url_data);
    url_data->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}
#endif  // ETIL_HTTP_CLIENT_ENABLED

// --- Gap fill: high-value RxJS operators ---

// obs-tap ( obs xt -- obs' )
bool prim_obs_tap(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt || !xt_val->as_ptr) {
        ctx.data_stack().push(*xt_val);
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::tap(src, xt_val->as_xt_impl());
    src->release();
    xt_val->as_xt_impl()->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-pairwise ( obs -- obs' )
bool prim_obs_pairwise(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    auto* obs = HeapObservable::pairwise(src);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-first ( obs -- obs' )
bool prim_obs_first(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    auto* obs = HeapObservable::first(src);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-last ( obs -- obs' )
bool prim_obs_last(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    auto* obs = HeapObservable::last(src);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-take-while ( obs xt -- obs' )
bool prim_obs_take_while(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt || !xt_val->as_ptr) {
        ctx.data_stack().push(*xt_val);
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::take_while(src, xt_val->as_xt_impl());
    src->release();
    xt_val->as_xt_impl()->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-distinct-until ( obs -- obs' )
bool prim_obs_distinct_until(ExecutionContext& ctx) {
    auto* src = pop_observable(ctx);
    if (!src) return false;
    auto* obs = HeapObservable::distinct_until(src);
    src->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-start-with ( obs value -- obs' )
bool prim_obs_start_with(ExecutionContext& ctx) {
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) return false;
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*opt_val); return false; }
    auto* obs = HeapObservable::start_with(src, *opt_val);
    src->release();
    // start_with factory addref'd val; we consumed the stack's ref
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-finalize ( obs xt -- obs' )
bool prim_obs_finalize(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt || !xt_val->as_ptr) {
        ctx.data_stack().push(*xt_val);
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::finalize(src, xt_val->as_xt_impl());
    src->release();
    xt_val->as_xt_impl()->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-switch-map ( obs xt -- obs' )
bool prim_obs_switch_map(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt || !xt_val->as_ptr) {
        ctx.data_stack().push(*xt_val);
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::switch_map(src, xt_val->as_xt_impl());
    src->release();
    xt_val->as_xt_impl()->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-catch ( obs xt -- obs' )
bool prim_obs_catch(ExecutionContext& ctx) {
    auto xt_val = ctx.data_stack().pop();
    if (!xt_val) return false;
    if (xt_val->type != Value::Type::Xt || !xt_val->as_ptr) {
        ctx.data_stack().push(*xt_val);
        return false;
    }
    auto* src = pop_observable(ctx);
    if (!src) { ctx.data_stack().push(*xt_val); return false; }
    auto* obs = HeapObservable::catch_error(src, xt_val->as_xt_impl());
    src->release();
    xt_val->as_xt_impl()->release();
    ctx.data_stack().push(Value::from(obs));
    return true;
}

// obs-to-string ( obs -- string ) — concatenate all string emissions
bool prim_obs_to_string(ExecutionContext& ctx) {
    auto* obs = pop_observable(ctx);
    if (!obs) return false;
    std::string result;
    bool ok = execute_observable(obs, ctx, [&](Value v, ExecutionContext&) -> bool {
        if (v.type == Value::Type::String && v.as_ptr) {
            result += v.as_string()->view();
        }
        value_release(v);
        return true;
    });
    obs->release();
    if (!ok) return false;
    auto* hs = HeapString::create(result);
    ctx.data_stack().push(Value::from(hs));
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

    // AVO Phase 1: Buffer + Composition
    dict.register_word("obs-buffer", mk("prim_obs_buffer", prim_obs_buffer,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-buffer-when", mk("prim_obs_buffer_when", prim_obs_buffer_when,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-window", mk("prim_obs_window", prim_obs_window,
        {T::Unknown, T::Integer}, {T::Unknown}));
    dict.register_word("obs-flat-map", mk("prim_obs_flat_map", prim_obs_flat_map,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-to-string", mk("prim_obs_to_string", prim_obs_to_string,
        {T::Unknown}, {T::String}));

    // AVO Phase 2: Streaming File I/O
    dict.register_word("obs-read-bytes", mk("prim_obs_read_bytes", prim_obs_read_bytes,
        {T::String, T::Integer}, {T::Unknown}));
    dict.register_word("obs-read-lines", mk("prim_obs_read_lines", prim_obs_read_lines,
        {T::String}, {T::Unknown}));
    dict.register_word("obs-read-json", mk("prim_obs_read_json", prim_obs_read_json,
        {T::String}, {T::Unknown}));
    dict.register_word("obs-read-csv", mk("prim_obs_read_csv", prim_obs_read_csv,
        {T::String, T::String}, {T::Unknown}));
    dict.register_word("obs-readdir", mk("prim_obs_readdir", prim_obs_readdir,
        {T::String}, {T::Unknown}));
    dict.register_word("obs-write-file", mk("prim_obs_write_file", prim_obs_write_file,
        {T::Unknown, T::String}, {}));
    dict.register_word("obs-append-file", mk("prim_obs_append_file", prim_obs_append_file,
        {T::Unknown, T::String}, {}));

#ifdef ETIL_HTTP_CLIENT_ENABLED
    // AVO Phase 3: Streaming HTTP
    dict.register_word("obs-http-get", mk("prim_obs_http_get", prim_obs_http_get,
        {T::String, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-http-post", mk("prim_obs_http_post", prim_obs_http_post,
        {T::String, T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-http-sse", mk("prim_obs_http_sse", prim_obs_http_sse,
        {T::String, T::Unknown}, {T::Unknown}));
#endif

    // Gap fill: high-value RxJS operators
    dict.register_word("obs-tap", mk("prim_obs_tap", prim_obs_tap,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-pairwise", mk("prim_obs_pairwise", prim_obs_pairwise,
        {T::Unknown}, {T::Unknown}));
    dict.register_word("obs-first", mk("prim_obs_first", prim_obs_first,
        {T::Unknown}, {T::Unknown}));
    dict.register_word("obs-last", mk("prim_obs_last", prim_obs_last,
        {T::Unknown}, {T::Unknown}));
    dict.register_word("obs-take-while", mk("prim_obs_take_while", prim_obs_take_while,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-distinct-until", mk("prim_obs_distinct_until", prim_obs_distinct_until,
        {T::Unknown}, {T::Unknown}));
    dict.register_word("obs-start-with", mk("prim_obs_start_with", prim_obs_start_with,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-finalize", mk("prim_obs_finalize", prim_obs_finalize,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-switch-map", mk("prim_obs_switch_map", prim_obs_switch_map,
        {T::Unknown, T::Unknown}, {T::Unknown}));
    dict.register_word("obs-catch", mk("prim_obs_catch", prim_obs_catch,
        {T::Unknown, T::Unknown}, {T::Unknown}));
}

} // namespace etil::core
