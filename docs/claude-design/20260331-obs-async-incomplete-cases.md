# Observable Async: Incomplete Cases

**Date:** 2026-03-31
**References:** `20260331-obs-libuv-async-design.md`, `20260331-obs-libuv-async-plan.md`
**Status:** Review — seed for next design phase

---

## Summary

After completing all 4 phases of the libuv async migration, 3 of 52 observable operators fall through to synchronous collection when used inside an async pipeline. All three work correctly in isolation — the limitation is that they cannot interleave with other concurrent sources (e.g., inside a merge of timed streams).

---

## 1. RetryDelay

**Current behavior:** Falls through to sync collection. The sync implementation loops: execute source → if failed, `sleep_until_or_tick(delay)` → retry up to N times.

**Why not async:** Async retry requires re-registering the source's libuv handles on the event loop after each failure. The current `build_async_pipeline()` walks the tree once at subscription time. Re-registration would need either:

- A mechanism to rebuild and re-register a sub-pipeline dynamically during the poll loop, or
- A persistent source handle that can be reset and restarted without rebuilding

The blocking sleep between retries would also need to become a one-shot `uv_timer_t` that triggers re-registration on fire.

**Impact:** RetryDelay on a timed source inside a merge will block the entire pipeline during the retry delay. Standalone RetryDelay works correctly.

---

## 2. FlatMap

**Current behavior:** Falls through to sync collection. The sync implementation: for each upstream value, execute Xt to produce an inner observable, then recursively execute that inner observable, forwarding all its emissions downstream.

**Why not async:** FlatMap creates inner observables dynamically — one per upstream value. Each inner observable may itself be timed (e.g., `obs-interval`). True async FlatMap requires:

- Building and registering new async nodes on the event loop at runtime (during the poll loop), not just at subscription time
- Tracking multiple active inner subscriptions with independent completion
- Respecting `max_concurrent` (limit how many inner observables run simultaneously)
- Cleaning up inner observable handles when they complete or when the pipeline stops

The current `AsyncPipeline` architecture builds all nodes upfront via `build_async_pipeline()`. FlatMap needs a "dynamic node factory" — the ability to call `build_async_pipeline()` for an inner observable during execution and register the resulting nodes on the already-running loop. This is architecturally possible (libuv supports adding handles to a running loop) but requires extending `AsyncPipeline` to accept new nodes mid-execution.

**Impact:** FlatMap where the inner observable is timed will execute each inner synchronously (blocking), not concurrently. FlatMap with sync inner observables works correctly.

---

## 3. SwitchMap

**Current behavior:** Falls through to sync collection. The sync implementation: for each upstream value, execute Xt to produce an inner observable, cancel the previous inner, run the new one.

**Why not async:** SwitchMap has the same dynamic node creation requirement as FlatMap, plus cancellation:

- When a new upstream value arrives, all handles belonging to the current inner observable must be stopped and closed
- The new inner observable's handles must be built and registered on the loop
- This is a "hot swap" of sub-pipelines during execution

Cancellation adds complexity: the inner observable's handles must be tracked separately from the main pipeline's handles so they can be selectively stopped without affecting the rest of the pipeline.

**Impact:** SwitchMap where the inner observable is timed will run each inner synchronously. SwitchMap with sync inner observables works correctly.

---

## Common Requirement: Dynamic Node Registration

All three cases share the same architectural gap: the need to create and register async nodes **during** the poll loop, not just before it starts. The current flow is:

```
build_async_pipeline()  →  register_handles()  →  run poll loop
       (build time)          (one-time)           (execution time)
```

The required flow for these operators:

```
build_async_pipeline()  →  register_handles()  →  run poll loop
       (build time)          (one-time)              ↕
                                                 build + register
                                                 new nodes mid-loop
```

This is the seed for the next design phase: extending `AsyncPipeline` to support dynamic node lifecycle during execution.
