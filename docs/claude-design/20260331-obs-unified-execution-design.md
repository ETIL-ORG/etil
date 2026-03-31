# Observable Unified Execution: Converging Sync and Async

**Date:** 2026-03-31
**References:** `20260331-obs-async-incomplete-cases.md`, `20260331-obs-libuv-async-design.md`
**Status:** Design Exploration

---

## Problem: Two Parallel Execution Engines

After the libuv async migration, `observable_primitives.cpp` contains two parallel execution engines:

| Function | Lines | Switch Cases | Purpose |
|---|---|---|---|
| `execute_observable()` | 1066 | 54 | Synchronous, recursive, blocking |
| `build_async_pipeline()` | 647 | 27 | Async, event-loop-driven |

23 of the 27 async cases duplicate observer-wrapping logic from the sync engine. The file is 3774 lines total, with ~1700 lines of execution logic — nearly half is duplication.

The dispatcher `run_observable()` selects between them based on `needs_async()`, which scans the pipeline tree for temporal/concurrent operators. Pure data pipelines use the sync path (zero overhead). Pipelines with timers or merges use the async path.

This fragmentation means:
- Every new operator needs implementation in **both** engines
- The async engine handles only 27 of 54 cases; the rest fall through to sync collection
- 3 operators (RetryDelay, FlatMap, SwitchMap) can't be async because they need dynamic node creation, which the async architecture doesn't support
- Bug fixes to observer wrapping must be applied in two places

---

## Root Cause: The Recursive Sync Model

The sync engine is pull-based and recursive:

```cpp
case K::Take: {
    int64_t remaining = obs->param();   // stack-allocated state
    return execute_observable(obs->source(), ctx, [&](Value v, ExecutionContext& c) -> bool {
        if (remaining <= 0) { value_release(v); return false; }
        --remaining;
        return observer(v, c);          // forward downstream
    });
}
```

Each operator calls `execute_observable()` on its source, which blocks until the source completes. The operator's state (`remaining`) lives on the call stack. The observer lambda captures it by reference — valid because the lambda's lifetime is bounded by the enclosing `execute_observable()` call.

The async engine can't use this pattern because event-loop execution is non-blocking — the function returns before the source completes. State must live on the heap:

```cpp
// Async version — same logic, different lifetime
case K::Take: {
    auto remaining = std::make_shared<int64_t>(obs->param());  // heap-allocated
    Observer wrapped = [remaining, observer](Value v, ExecutionContext& c) -> bool {
        if (*remaining <= 0) { value_release(v); return false; }
        --(*remaining);
        return observer(v, c);
    };
    build_async_pipeline(obs->source(), ctx, wrapped, pipeline);
}
```

The logic is identical. Only the state lifetime differs. This is the duplication.

---

## The Convergence Insight

The observer chain (Map wraps observer, Filter wraps observer, Take wraps observer) is **independent of the execution model**. Whether values arrive synchronously (from Range) or asynchronously (from Timer), the observer chain processes them identically.

The only code that differs between sync and async is at **source nodes** (how values are generated) and **combination nodes** (how multiple sources are coordinated). The ~40 transform/limiting/accumulate operators are mode-independent — they just wrap the observer callback.

If transforms used heap-allocated state unconditionally, a single function could build the observer chain for both sync and async execution. The mode decision would only affect source and combination nodes.

---

## Unified Design: `execute_pipeline()`

One function replaces both `execute_observable()` and `build_async_pipeline()`:

```cpp
static bool execute_pipeline(HeapObservable* obs, ExecutionContext& ctx,
                              const Observer& observer,
                              AsyncPipeline* pipeline = nullptr) {
    using K = HeapObservable::Kind;

    switch (obs->obs_kind()) {

    // --- Sources: behavior differs by mode ---

    case K::Timer: {
        if (pipeline) {
            // Async: register timer handle on event loop
            auto node = std::make_unique<TimerNode>();
            node->delay_ms = ...;
            node->period_ms = ...;
            node->observer = observer;
            pipeline->add_node(std::move(node));
            return true;
        }
        // Sync: blocking emission loop (existing code)
        int64_t counter = 0;
        // ... sleep_until_or_tick loop ...
    }

    case K::Range: {
        if (pipeline) {
            // Async: collect values, register idle handle
            auto node = std::make_unique<IdleNode>();
            for (int64_t i = start; i < end; ++i)
                node->values.push_back(Value(i));
            node->observer = observer;
            pipeline->add_node(std::move(node));
            return true;
        }
        // Sync: direct emission
        for (int64_t i = start; i < end; ++i) {
            if (!observer(Value(i), ctx)) return true;
        }
        return true;
    }

    // --- Transforms: IDENTICAL for sync and async ---

    case K::Take: {
        auto remaining = std::make_shared<int64_t>(obs->param());
        Observer wrapped = [remaining, observer](Value v, ExecutionContext& c) -> bool {
            if (*remaining <= 0) { value_release(v); return false; }
            --(*remaining);
            return observer(v, c);
        };
        return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
    }

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

    // ... all ~40 transforms use the same code for both modes ...

    // --- Combination: behavior differs by mode ---

    case K::Merge: {
        if (pipeline) {
            // Async: register both sources concurrently
            execute_pipeline(obs->source(), ctx, observer, pipeline);
            execute_pipeline(obs->source_b(), ctx, observer, pipeline);
            return true;
        }
        // Sync: sequential
        execute_pipeline(obs->source(), ctx, observer, nullptr);
        return execute_pipeline(obs->source_b(), ctx, observer, nullptr);
    }
    }
}
```

### What Changes

| Operator Category | Count | Code Change |
|---|---|---|
| Sources (Timer, Range, FromArray, Of, Empty) | 5 | `if (pipeline)` branch for async registration |
| Transforms (Map, Filter, Take, Skip, etc.) | ~40 | `shared_ptr` state, single implementation |
| Combination (Merge, Concat, Zip) | 3 | `if (pipeline)` branch for concurrent/deferred |
| Temporal transforms | 10 | `if (pipeline)` branch for timer handles |
| Total | ~58 | |

### What Disappears

- `build_async_pipeline()` — merged into `execute_pipeline()`
- `execute_observable()` — replaced by `execute_pipeline(..., nullptr)`
- `execute_observable_async()` — replaced by `execute_pipeline(..., &pipeline)`
- `run_observable()` — replaced by `execute_pipeline()` with mode auto-detection
- `needs_async()` — the `pipeline` parameter IS the mode selector
- ~650 lines of duplicated async observer wrapping

### What Simplifies

The entry point for all terminal operators becomes:

```cpp
// Before (2 paths):
if (needs_async(obs) && ctx.uv_session()) {
    return execute_observable_async(obs, ctx, observer);
}
return execute_observable(obs, ctx, observer);

// After (1 path):
AsyncPipeline pipeline;
execute_pipeline(obs, ctx, observer, &pipeline);
if (pipeline.has_nodes()) {
    return run_async_pipeline(ctx, pipeline);
}
return true;  // sync completed during execute_pipeline
```

When there are no async nodes, `execute_pipeline` runs synchronously to completion (the pipeline has no nodes to poll). When there are async nodes, the pipeline is polled. No `needs_async()` scan required — the pipeline self-discovers.

---

## How Dynamic Nodes Become Natural

### FlatMap

```cpp
case K::FlatMap: {
    auto* xt = obs->operator_xt();
    Observer wrapped = [xt, observer, pipeline, &ctx](Value v, ExecutionContext& c) -> bool {
        // Execute Xt to get inner observable
        c.data_stack().push(v);
        if (!execute_xt(xt, c)) return false;
        auto inner_obs = c.data_stack().pop();
        auto* inner = inner_obs->as_observable();

        // Build inner observable INTO THE SAME PIPELINE
        execute_pipeline(inner, ctx, observer, pipeline);

        // New nodes are registered on the already-running loop
        pipeline->register_new_nodes();
        inner->release();
        return true;
    };
    return execute_pipeline(obs->source(), ctx, wrapped, pipeline);
}
```

The inner observable is built at runtime — during the observer callback — and its nodes are added to the same pipeline. The event loop picks them up on the next `uv_run()` iteration.

This works because:
1. The observer callback runs on the main thread (during `uv_run`)
2. `execute_pipeline()` can be called recursively
3. If the inner observable is sync (Range, FromArray), it executes immediately inside the callback — no nodes added to the pipeline
4. If the inner observable is async (Timer), its TimerNode is added and starts firing on the next poll iteration

### SwitchMap

Same as FlatMap but first: stop and remove the current inner observable's nodes.

```cpp
// SwitchMap observer callback:
pipeline->stop_group(current_inner_group);
pipeline->remove_group(current_inner_group);
current_inner_group = pipeline->start_new_group();
execute_pipeline(inner, ctx, observer, pipeline);
pipeline->register_new_nodes();
```

This requires `AsyncPipeline` to support **node groups** — sets of nodes that can be independently stopped and removed. Each FlatMap/SwitchMap inner observable creates a group. SwitchMap stops the previous group before creating a new one.

### RetryDelay

```cpp
// RetryDelay observer — on source failure:
// 1. Create a one-shot timer for the retry delay
// 2. Timer callback re-executes the source into the pipeline
auto retry_timer = std::make_unique<TransformTimerNode>();
retry_timer->handle.data = retry_state.get();
uv_timer_start(&retry_timer->handle, [](uv_timer_t* h) {
    auto* state = static_cast<RetryState*>(h->data);
    execute_pipeline(state->source, *state->ctx, state->observer, state->pipeline);
    state->pipeline->register_new_nodes();
}, delay_ms, 0);
pipeline->add_node(std::move(retry_timer));
```

---

## Performance Consideration: `shared_ptr` Overhead

The unified design uses `std::make_shared` for operator state instead of stack allocation. For a pipeline with N operators:

| Allocation | Current Sync | Unified |
|---|---|---|
| Per-operator state | 0 (stack) | 1 `make_shared` (16-32 bytes + control block) |
| Observer chain | N closures (stack) | N closures (heap via `std::function`) |

The `std::function` in the Observer typedef already heap-allocates the closure. Adding `make_shared` for an int64_t counter adds ~48 bytes per operator. For a 10-operator pipeline, that's 480 bytes — negligible.

For pure data pipelines processing millions of values, the per-subscription allocation cost is constant (not per-value). The observer chain runs at the same speed — the lambda bodies are identical.

**Benchmark prediction:** <1% performance difference for pure data pipelines. Zero difference for temporal/async pipelines (already heap-allocating in the async path).

---

## AsyncPipeline Extensions Required

### Node Groups (for FlatMap/SwitchMap)

```cpp
using GroupId = uint32_t;

GroupId start_new_group();
void stop_group(GroupId id);
void remove_group(GroupId id);
```

Each node tracks its group ID. `stop_group` stops all nodes in the group. `remove_group` closes and removes them. New groups can be created during the poll loop.

### Runtime Node Registration

```cpp
void register_new_nodes();
```

Called after `execute_pipeline` adds nodes during a callback. Initializes any unregistered nodes on the loop.

### Completion Tracking

`complete()` needs to understand that:
- Source nodes that are `done` contribute to completion
- Groups created by FlatMap are tracked separately
- The pipeline is complete when all sources AND all active FlatMap groups are done

---

## Migration Path

The unified design can be implemented incrementally, operator by operator, without breaking existing functionality.

### Phase 1: Infrastructure

- Add `execute_pipeline()` as a new function alongside the existing ones
- Route all terminal operators through `execute_pipeline()`
- `execute_pipeline()` delegates to `execute_observable()` (sync) or `build_async_pipeline()` (async) initially — no behavior change

### Phase 2: Migrate Transforms (Batch)

For each of the ~40 transform/limiting operators:
- Move the observer wrapping into `execute_pipeline()` with `shared_ptr` state
- Remove the case from both `execute_observable()` and `build_async_pipeline()`
- Each migration is a mechanical change — same logic, different lifetime

This is the bulk of the work but each operator is a 5-10 line change.

### Phase 3: Migrate Sources

Move source execution (Timer, Range, FromArray, Of, Empty) into `execute_pipeline()` with `if (pipeline)` branches. Remove from both old functions.

### Phase 4: Migrate Combination + Temporal

Move Merge, Concat, Zip, and all temporal operators. After this phase, `execute_observable()` and `build_async_pipeline()` are empty and can be deleted.

### Phase 5: Dynamic Nodes

Implement FlatMap, SwitchMap, RetryDelay using `execute_pipeline()` recursive calls with node groups. These operators work for the first time in async mode.

### Phase 6: Cleanup

- Delete `execute_observable()`, `build_async_pipeline()`, `execute_observable_async()`
- Delete `needs_async()` (no longer needed)
- Delete `run_observable()` (replaced by `execute_pipeline()`)

---

## Impact Summary

| Metric | Current | Unified |
|---|---|---|
| Execution functions | 5 (`execute_observable`, `build_async_pipeline`, `execute_observable_async`, `run_observable`, `run_async_pipeline`) | 2 (`execute_pipeline`, `run_async_pipeline`) |
| Switch cases total | 54 + 27 = 81 | ~58 (one per operator) |
| Duplicated observer wrapping | 23 cases (~500 lines) | 0 |
| Operators fully async-capable | 49/52 | 52/52 |
| Lines in execution logic | ~1700 | ~1200 (estimated) |
| Dynamic node support | No | Yes (via recursive `execute_pipeline` calls) |
| `needs_async()` tree scan | Required per subscription | Eliminated |

---

## Conclusion

The sync and async execution paths diverge only at source nodes (how values are generated) and combination nodes (how sources are coordinated). The ~40 transform operators are identical in both modes — the current duplication is an accident of the implementation history, not a design requirement.

The unified `execute_pipeline()` function converges both paths by:
1. Using `shared_ptr` state for all operators (identical observer wrapping)
2. Using the `pipeline` parameter as the mode selector (`nullptr` = sync)
3. Supporting recursive calls during execution (enabling FlatMap/SwitchMap/RetryDelay)

This eliminates ~500 lines of duplication, removes the `needs_async()` scan, and naturally supports dynamic node creation — the architectural gap identified in the incomplete cases review.
