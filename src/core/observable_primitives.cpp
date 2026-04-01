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
#include "etil/core/observable_execution.hpp"

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
    bool ok = execute_pipeline(src, ctx, [&](Value v, ExecutionContext& c) -> bool {
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
    bool ok = execute_pipeline(src, ctx, [&](Value v, ExecutionContext& c) -> bool {
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
    bool ok = execute_pipeline(src, ctx, [&](Value v, ExecutionContext&) -> bool {
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
    bool ok = execute_pipeline(src, ctx, [&](Value v, ExecutionContext&) -> bool {
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

    bool ok = execute_pipeline(obs, ctx, [&](Value v, ExecutionContext&) -> bool {
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

    bool ok = execute_pipeline(obs, ctx, [&](Value v, ExecutionContext&) -> bool {
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
    bool ok = execute_pipeline(obs, ctx, [&](Value v, ExecutionContext&) -> bool {
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

#ifndef ETIL_WASM_BUILD
    // Temporal operators — blocked in WASM (sleep_for freezes the browser main thread)
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
#endif

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
