# Plan: Observable Gap Implementation

## Date: 2026-03-17

## Context

The RxJS gap analysis (`20260317-rxjs-observable-gap-analysis.md`) identified 10 high-applicability unimplemented operators. This plan implements them in two stages: 8 easy operators first, then 2 medium operators.

Current state: 50 observable words (v0.9.7). Target: 60.

## Stage 1: Easy High-Value Operators (8 words)

All follow the existing `execute_observable()` pattern — new Kind enum, factory, execution case, primitive, registration, help, tests.

### 1.1 `obs-tap` `( obs xt -- obs' )`

Execute xt as side-effect for each emission, pass value through unchanged. Essential for debugging pipelines.

```cpp
case K::Tap: {
    auto* xt = obs->operator_xt();
    return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
        value_addref(v);
        c.data_stack().push(v);       // copy for xt
        if (!execute_xt(xt, c)) { value_release(v); return false; }
        auto discard = c.data_stack().pop(); // drop xt result
        if (discard) discard->release();
        return observer(v, c);         // forward original
    });
}
```

**Storage**: xt in `operator_xt_`

### 1.2 `obs-pairwise` `( obs -- obs' )`

Emit consecutive `[prev, curr]` pairs as 2-element arrays. First emission is buffered, no output until second.

```cpp
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
        pair->push_back(prev);  // transfers prev ref
        value_addref(v);
        pair->push_back(v);     // array gets one ref
        prev = v;               // keep one ref for next pair
        return observer(Value::from(pair), c);
    });
    if (has_prev) value_release(prev);
    return result;
}
```

**Storage**: no extra fields (stateless node)

### 1.3 `obs-first` `( obs -- obs' )`

Emit only the first value, then complete. Equivalent to `1 obs-take` but clearer intent.

```cpp
case K::First:
    return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
        observer(v, c);
        return false;  // stop after first
    });
```

**Storage**: none

### 1.4 `obs-last` `( obs -- obs' )`

Buffer all emissions, emit only the final value on source completion.

```cpp
case K::Last: {
    bool has_value = false;
    Value last;
    bool result = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext&) -> bool {
        if (has_value) value_release(last);
        last = v;
        has_value = true;
        return true;
    });
    if (has_value && result) observer(last, ctx);
    else if (has_value) value_release(last);
    return result;
}
```

**Storage**: none

### 1.5 `obs-take-while` `( obs xt -- obs' )`

Emit values while predicate xt returns true. Complete on first false.

```cpp
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
```

**Storage**: xt in `operator_xt_`

### 1.6 `obs-distinct-until` `( obs -- obs' )`

Suppress consecutive duplicate values. Unlike `obs-distinct` which tracks all seen values, this only compares with the immediately previous emission.

```cpp
case K::DistinctUntil: {
    bool has_prev = false;
    Value prev;
    bool result = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
        bool duplicate = has_prev && values_equal(prev, v);
        if (duplicate) {
            value_release(v);
            return true;  // skip
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
```

**Note**: Reuses existing `values_equal()` from the Distinct case.

**Storage**: none

### 1.7 `obs-start-with` `( obs value -- obs' )`

Emit the given value before any source emissions.

```cpp
case K::StartWith: {
    Value start_val = obs->state();
    value_addref(start_val);
    if (!observer(start_val, ctx)) return true;
    return execute_observable(obs->source(), ctx, observer);
}
```

**Storage**: prepend value in `state_`

### 1.8 `obs-finalize` `( obs xt -- obs' )`

Execute xt when the pipeline completes (success or error). Cleanup/teardown hook.

```cpp
case K::Finalize: {
    auto* xt = obs->operator_xt();
    bool result = execute_observable(obs->source(), ctx, observer);
    // Always run finalizer
    execute_xt(xt, ctx);
    // Discard any value xt may have pushed
    if (ctx.data_stack().size() > 0) {
        auto discard = ctx.data_stack().pop();
        if (discard) discard->release();
    }
    return result;
}
```

**Storage**: xt in `operator_xt_`

---

## Stage 2: Medium High-Value Operators (2 words)

### 2.1 `obs-switch-map` `( obs xt -- obs' )`

Map each upstream emission to a sub-observable via xt. When a new emission arrives, the previous inner observable is cancelled and replaced. Essential for "latest wins" patterns (search-as-you-type, route changes).

In ETIL's synchronous push model, `obs-flat-map` already runs each inner to completion before processing the next upstream value. `obs-switch-map` differs by stopping the current inner when a new upstream value is available. Since we can't truly interleave, the semantics are: execute inner observable, but check for "new upstream ready" between inner emissions via a flag.

However, in ETIL's single-threaded model where the source runs to completion before the inner starts, switchMap and concatMap produce the same behavior for synchronous sources. The difference only matters for async/temporal sources. For temporal inner observables, we can implement cancellation by tracking a generation counter.

```cpp
case K::SwitchMap: {
    auto* xt = obs->operator_xt();
    // For synchronous sources, behaves like flat-map but only forwards
    // emissions from the most recent inner observable
    Value latest;
    bool has_latest = false;

    // Collect all upstream values first
    std::vector<Value> upstream;
    bool src_ok = execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext&) -> bool {
        upstream.push_back(v);
        return true;
    });
    if (!src_ok) {
        for (auto& v : upstream) value_release(v);
        return false;
    }

    // Only execute inner observable for the LAST upstream value (switch semantics)
    // For intermediate values, still execute but discard results
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
            // Forward emissions from the last inner
            bool ok = execute_observable(sub_obs, ctx, observer);
            sub_obs->release();
            return ok;
        } else {
            // Discard emissions from non-last inners
            execute_observable(sub_obs, ctx, [](Value v, ExecutionContext&) -> bool {
                value_release(v);
                return true;
            });
            sub_obs->release();
        }
    }
    return true;
}
```

**Storage**: xt in `operator_xt_`

### 2.2 `obs-catch` `( obs xt -- obs' )`

On pipeline error (execute_observable returns false), call xt which should push a fallback observable. Execute the fallback instead.

```cpp
case K::Catch: {
    auto* xt = obs->operator_xt();
    bool result = execute_observable(obs->source(), ctx, observer);
    if (!result) {
        // Error occurred — call recovery xt
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
```

**Storage**: xt in `operator_xt_`

---

## Files to Modify

| File | Changes |
|------|---------|
| `include/etil/core/heap_observable.hpp` | Add 10 Kind enum values + 10 factory methods |
| `src/core/observable_primitives.cpp` | 10 factories + 10 kind_names + 10 execution cases + 10 primitives + registration |
| `data/help.til` | 10 new help entries |
| `tests/unit/test_primitives.cpp` | Update concept_count (+10) |
| `tests/til/test_observable.til` | ~30 new integration tests |

---

## Verification

```bash
# Build
scripts/build.sh debug

# Run all tests
scripts/test.sh debug

# Run observable tests
scripts/test.sh debug --filter observable

# Manual REPL verification
./build-debug/bin/etil_repl

# obs-tap: side-effect logging
> 1 4 obs-range ' . obs-tap obs-count .
=> 1 2 3 3

# obs-pairwise: consecutive pairs
> 1 5 obs-range obs-pairwise obs-to-array dump drop
=> [[1, 2], [2, 3], [3, 4]]

# obs-first / obs-last
> 1 10 obs-range obs-first obs-to-array dump drop
=> [1]
> 1 10 obs-range obs-last obs-to-array dump drop
=> [9]

# obs-take-while
> 1 10 obs-range ' 5 < obs-take-while obs-to-array dump drop
=> [1, 2, 3, 4]

# obs-distinct-until
> array-new 1 array-push 1 array-push 2 array-push 2 array-push 1 array-push obs-from obs-distinct-until obs-to-array dump drop
=> [1, 2, 1]

# obs-start-with
> 1 4 obs-range 0 obs-start-with obs-to-array dump drop
=> [0, 1, 2, 3]

# obs-catch
> : fail-obs obs-empty ;  obs-empty ' fail-obs obs-catch obs-count .
=> 0
```

## Implementation Order

```
Stage 1 (all independent, implement together):
  obs-tap, obs-pairwise, obs-first, obs-last,
  obs-take-while, obs-distinct-until, obs-start-with, obs-finalize
  >>> Build + test <<<

Stage 2 (independent of each other, depend on Stage 1 patterns):
  obs-switch-map, obs-catch
  >>> Build + test <<<

Super-push as single feature branch v0.9.8
```
