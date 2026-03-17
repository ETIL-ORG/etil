# RxJS → ETIL Observable Gap Analysis

## Date: 2026-03-17

## Scoring

**Applicability** (how useful for ETIL's domain — stack-based interpreter, MCP server, file/HTTP I/O):
- **H** = High — would see frequent use, fills a real gap
- **M** = Medium — useful but niche, or partially covered by composition
- **L** = Low — rarely needed, too JS-specific, or not applicable to ETIL's model
- **N/A** = Not applicable (requires async scheduler, DOM, multicasting, or concepts that don't map)

**Difficulty** (implementation effort in ETIL's push-based `execute_observable()` engine):
- **E** = Easy — <30 lines, follows existing patterns (new Kind + simple case)
- **M** = Medium — 30-100 lines, needs new helper or state management
- **H** = Hard — >100 lines, requires architectural changes or external integration

---

## Master Table: All RxJS Operators

### Creation Operators

| RxJS | ETIL `obs-` Word | Applicability | Difficulty | Notes |
|------|-----------------|---------------|------------|-------|
| `from` | `obs-from` | — | — | Implemented |
| `of` | `obs-of` | — | — | Implemented |
| `empty` / `EMPTY` | `obs-empty` | — | — | Implemented |
| `range` | `obs-range` | — | — | Implemented |
| `timer` | `obs-timer` | — | — | Implemented |
| `interval` | `obs-interval` | — | — | Implemented (self-hosted) |
| `defer` | — | M | E | Lazy factory: `( xt -- obs )` where xt returns obs at subscribe time |
| `generate` | — | M | E | Loop-based creation: `( init xt-cond xt-iter -- obs )` |
| `throwError` | — | L | E | ETIL uses return-false for errors, not error channels |
| `create` | — | L | M | Manual observer callbacks — ETIL's model is declarative pipelines |
| `fromEvent` | — | N/A | — | No DOM; `obs-watch` (deferred) covers file events |
| `ajax` | `obs-http-get` | — | — | Implemented (Phase 3) |
| `iif` | — | M | E | Conditional: `( cond obs-true obs-false -- obs )` |
| `repeat` | — | M | E | Re-subscribe on completion: `( obs n -- obs' )` |
| `never` / `NEVER` | — | L | E | Observable that never emits/completes — trivial but rarely useful |

### Transformation Operators

| RxJS | ETIL `obs-` Word | Applicability | Difficulty | Notes |
|------|-----------------|---------------|------------|-------|
| `map` | `obs-map` | — | — | Implemented |
| `scan` | `obs-scan` | — | — | Implemented |
| `buffer` | `obs-buffer` | — | — | Implemented (AVO Phase 1, count-based) |
| `bufferCount` | `obs-buffer` | — | — | Same as obs-buffer |
| `bufferTime` | `obs-buffer-time` | — | — | Implemented |
| `bufferWhen` | `obs-buffer-when` | — | — | Implemented (AVO Phase 1) |
| `bufferToggle` | — | L | M | Open/close signals — complex, niche |
| `window` | `obs-window` | — | — | Implemented (AVO Phase 1, sliding) |
| `windowCount` | `obs-window` | — | — | Same as obs-window |
| `windowTime` | — | L | M | Time-based windowing — `obs-buffer-time` covers most cases |
| `windowToggle` | — | L | H | Signal-based open/close — complex |
| `windowWhen` | — | L | M | Notifier-based windowing |
| `mergeMap` / `flatMap` | `obs-flat-map` | — | — | Implemented (AVO Phase 1, concatMap semantics) |
| `concatMap` | `obs-flat-map` | — | — | Same — ETIL's flat-map is sequential (concatMap) |
| `switchMap` | — | H | M | Cancel previous inner obs on new emission — very useful for search/autocomplete |
| `exhaustMap` | — | M | M | Ignore new emissions while inner is active |
| `groupBy` | — | M | H | Group emissions by key into sub-observables |
| `pairwise` | — | H | E | Emit consecutive pairs: `[prev, curr]` — simple ring buffer |
| `partition` | — | M | M | Split into two observables by predicate |
| `pluck` | — | L | E | `obs-map` with `' key json-get` covers this |
| `mapTo` | — | L | E | `obs-map` with `' drop value` covers this |
| `concatMapTo` | — | L | E | Composition covers this |
| `switchMapTo` | — | L | M | Depends on switchMap |
| `expand` | — | M | M | Recursive flatMap — useful for pagination/tree traversal |
| `mergeScan` | — | L | M | Scan with inner observable merging |
| `toArray` | `obs-to-array` | — | — | Implemented |
| `reduce` | `obs-reduce` | — | — | Implemented |

### Filtering Operators

| RxJS | ETIL `obs-` Word | Applicability | Difficulty | Notes |
|------|-----------------|---------------|------------|-------|
| `filter` | `obs-filter` | — | — | Implemented |
| `take` | `obs-take` | — | — | Implemented |
| `skip` | `obs-skip` | — | — | Implemented |
| `distinct` | `obs-distinct` | — | — | Implemented |
| `debounceTime` | `obs-debounce-time` | — | — | Implemented |
| `throttleTime` | `obs-throttle-time` | — | — | Implemented |
| `auditTime` | `obs-audit-time` | — | — | Implemented |
| `sample` | `obs-sample-time` | — | — | Implemented (time-based) |
| `first` | — | H | E | `1 obs-take` but first also supports predicate |
| `last` | — | H | E | Emit only last value before completion |
| `takeLast` | — | M | E | Emit last n values — ring buffer |
| `skipLast` | — | L | E | Buffer n, emit delayed |
| `takeWhile` | — | H | E | Take while predicate true, then complete |
| `skipWhile` | — | M | E | Skip while predicate true, then pass all |
| `takeUntil` | `obs-take-until-time` | — | — | Implemented (time-based); obs-based version missing |
| `skipUntil` | — | L | M | Requires concurrent observable execution |
| `distinctUntilChanged` | — | H | E | Only suppress consecutive duplicates |
| `distinctUntilKeyChanged` | — | M | E | Consecutive dedup by key — needs key extractor |
| `find` | — | M | E | First matching value, then complete |
| `single` | — | L | E | Assert exactly one value matches |
| `elementAt` | — | M | E | Emit only the nth element |
| `ignoreElements` | — | L | E | Swallow all, pass only completion |
| `debounce` | — | L | M | Observable-based debounce (vs time-based) |
| `throttle` | — | L | M | Observable-based throttle (vs time-based) |
| `audit` | — | L | M | Observable-based audit (vs time-based) |
| `every` | — | M | E | Test all values against predicate, emit bool |

### Combination Operators

| RxJS | ETIL `obs-` Word | Applicability | Difficulty | Notes |
|------|-----------------|---------------|------------|-------|
| `merge` | `obs-merge` | — | — | Implemented |
| `concat` | `obs-concat` | — | — | Implemented |
| `zip` | `obs-zip` | — | — | Implemented |
| `combineLatest` | — | M | H | Requires concurrent subscription — doesn't fit single-threaded push model |
| `forkJoin` | — | M | H | Wait for all to complete — same concurrency issue |
| `race` | — | L | H | First-to-emit wins — needs concurrent subscription |
| `startWith` | — | H | E | Prepend value(s) before source emissions |
| `endWith` | — | M | E | Append value(s) after source completes |
| `withLatestFrom` | — | L | H | Requires concurrent subscription |
| `combineLatestAll` | — | L | H | Higher-order, concurrent |
| `concatAll` | — | M | E | Flatten observable-of-observables sequentially |
| `mergeAll` | — | L | H | Concurrent inner subscription |
| `switchAll` | — | L | H | Switch to latest inner observable |

### Error Handling Operators

| RxJS | ETIL `obs-` Word | Applicability | Difficulty | Notes |
|------|-----------------|---------------|------------|-------|
| `catchError` | — | H | M | Recover from error, substitute alternative observable |
| `retry` | `obs-retry-delay` | — | — | Implemented (with delay) |
| `retryWhen` | — | M | M | Custom retry logic via notifier observable |

### Utility Operators

| RxJS | ETIL `obs-` Word | Applicability | Difficulty | Notes |
|------|-----------------|---------------|------------|-------|
| `tap` / `do` | — | H | E | Side-effect without modifying stream — logging, debugging |
| `delay` | `obs-delay` | — | — | Implemented |
| `delayWhen` | `obs-delay-each` | — | — | Implemented |
| `timeout` | `obs-timeout` | — | — | Implemented |
| `timeInterval` | `obs-time-interval` | — | — | Implemented |
| `timestamp` | `obs-timestamp` | — | — | Implemented |
| `finalize` | — | H | E | Execute cleanup on complete/error — RAII for observable pipelines |
| `repeat` | — | M | E | Re-subscribe n times after completion |
| `timeoutWith` | — | M | M | Switch to fallback observable on timeout |
| `toPromise` | — | N/A | — | No promise concept in ETIL |
| `materialize` | — | L | M | Convert emissions to notification objects |
| `dematerialize` | — | L | M | Reverse of materialize |
| `observeOn` | — | N/A | — | Scheduler concept doesn't apply |
| `subscribeOn` | — | N/A | — | Scheduler concept doesn't apply |

### Mathematical / Aggregate Operators

| RxJS | ETIL `obs-` Word | Applicability | Difficulty | Notes |
|------|-----------------|---------------|------------|-------|
| `count` | `obs-count` | — | — | Implemented |
| `reduce` | `obs-reduce` | — | — | Implemented |
| `max` | — | M | E | Emit maximum value on completion |
| `min` | — | M | E | Emit minimum value on completion |
| `sum` | — | M | E | Self-hostable: `obs-reduce ' + 0` |
| `average` | — | M | E | Scan + count + divide at end |

### Multicasting Operators

| RxJS | ETIL `obs-` Word | Applicability | Difficulty | Notes |
|------|-----------------|---------------|------------|-------|
| `share` | — | N/A | — | Single-threaded push model — no multiple subscribers |
| `shareReplay` | — | N/A | — | Same |
| `publish` | — | N/A | — | Connectable observable — not applicable |
| `multicast` | — | N/A | — | Same |
| `refCount` | — | N/A | — | Same |

---

## Cross-Index: By Applicability (High only, unimplemented)

| RxJS Operator | Category | Difficulty | ETIL Notes |
|---------------|----------|------------|------------|
| `switchMap` | Transform | M | Cancel previous inner on new emission |
| `pairwise` | Transform | E | Emit `[prev, curr]` pairs |
| `first` | Filtering | E | `( obs -- obs' )` or `( obs xt -- obs' )` with predicate |
| `last` | Filtering | E | `( obs -- obs' )` emit only final value |
| `takeWhile` | Filtering | E | `( obs xt -- obs' )` take while predicate true |
| `distinctUntilChanged` | Filtering | E | `( obs -- obs' )` suppress consecutive duplicates |
| `startWith` | Combination | E | `( obs value -- obs' )` prepend value |
| `catchError` | Error | M | `( obs xt -- obs' )` recover from error with fallback |
| `tap` | Utility | E | `( obs xt -- obs' )` side-effect without modifying |
| `finalize` | Utility | E | `( obs xt -- obs' )` cleanup on complete/error |

---

## Cross-Index: By Difficulty (Easy, High applicability)

These are the low-hanging fruit — high value, easy to implement:

| RxJS Operator | Proposed ETIL Word | Stack Effect | Lines Est. |
|---------------|--------------------|-------------|------------|
| `pairwise` | `obs-pairwise` | `( obs -- obs' )` | ~15 |
| `first` | `obs-first` | `( obs -- obs' )` | ~10 |
| `last` | `obs-last` | `( obs -- obs' )` | ~15 |
| `takeWhile` | `obs-take-while` | `( obs xt -- obs' )` | ~15 |
| `distinctUntilChanged` | `obs-distinct-until` | `( obs -- obs' )` | ~20 |
| `startWith` | `obs-start-with` | `( obs value -- obs' )` | ~10 |
| `tap` | `obs-tap` | `( obs xt -- obs' )` | ~10 |
| `finalize` | `obs-finalize` | `( obs xt -- obs' )` | ~15 |

---

## Cross-Index: By Difficulty (Medium, High applicability)

| RxJS Operator | Proposed ETIL Word | Stack Effect | Lines Est. | Notes |
|---------------|--------------------|-------------|------------|-------|
| `switchMap` | `obs-switch-map` | `( obs xt -- obs' )` | ~40 | Needs "cancel previous" semantics in execute_observable |
| `catchError` | `obs-catch` | `( obs xt -- obs' )` | ~30 | xt receives error context, returns fallback obs |

---

## Summary

| | Implemented | High Applicable | Medium Applicable | Low / N/A |
|---|---|---|---|---|
| **Count** | 50 | 10 unimplemented | ~20 unimplemented | ~25 |
| **Easy** | — | 8 | ~10 | — |
| **Medium** | — | 2 | ~8 | — |
| **Hard** | — | 0 | ~5 (all concurrency-related) | — |

**Key insight**: ETIL already covers the most-used RxJS operators. The 10 high-applicability gaps are mostly easy (8/10), with `switchMap` and `catchError` being the only medium-difficulty high-value additions. The hard operators (`combineLatest`, `forkJoin`, `race`, `withLatestFrom`) all require concurrent subscription which doesn't fit ETIL's single-threaded push model.
