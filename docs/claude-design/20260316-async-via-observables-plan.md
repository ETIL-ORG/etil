# Plan: AVO — Async Via Observables

## Context

Unify all async and streaming I/O through the Observable system. The full design is in `docs/claude-design/20260316-async-via-observables-design.md`.

Current observable system: 34 words (21 core + 13 temporal), push-based engine with `ctx.tick()` enforcement.

## Implementation: 4 Phases

### Phase 1: Buffer and Composition Operators (4 words)

Pure observable operators — no I/O, no libuv. Prerequisite for all streaming phases.

**Files to modify:**

| File | Changes |
|------|---------|
| `include/etil/core/heap_observable.hpp` | Add Kind enum values: `Buffer`, `BufferWhen`, `Window`, `FlatMap`. Add factory methods. |
| `src/core/observable_primitives.cpp` | 4 execution cases + 4 `prim_obs_*` functions + registration. Update `kind_name()`. |
| `data/help.til` | 4 new help entries |
| `tests/unit/test_observable_primitives.cpp` | ~16 new test cases |
| `tests/til/test_observable.til` | ~12 new integration tests |

**New TIL words:**

| Word | Stack Effect | Kind | Description |
|------|-------------|------|-------------|
| `obs-buffer` | `( obs n -- obs' )` | Buffer | Collect n emissions into arrays |
| `obs-buffer-when` | `( obs xt -- obs' )` | BufferWhen | Buffer until xt returns true |
| `obs-window` | `( obs n -- obs' )` | Window | Sliding window of n elements |
| `obs-flat-map` | `( obs xt -- obs' )` | FlatMap | Map to sub-Observable, flatten |

**Execution logic:**

- **`Buffer`**: Accumulate emissions in a `HeapArray`. When count reaches `param_`, emit the array and start a new one. On source completion, emit any trailing partial batch. `ctx.tick()` per emission.

- **`BufferWhen`**: Accumulate emissions in a `HeapArray`. After each emission, execute `operator_xt_` — if it returns `true`, emit the batch and start a new one. On source completion, emit trailing batch.

- **`Window`**: Maintain a circular buffer of last n values. Once the buffer fills, emit a copy as `HeapArray` on each new emission. Sliding, not tumbling — every emission after the nth produces a window.

- **`FlatMap`**: For each upstream emission, push it to the stack, execute `operator_xt_`, pop the result (must be an Observable), and execute that sub-Observable against the downstream observer. The sub-Observable runs to completion before the next upstream emission is processed (concatMap semantics — no interleaving).

**HeapObservable storage:**
- `obs-buffer`: count in `param_`
- `obs-buffer-when`: predicate xt in `operator_xt_`
- `obs-window`: window size in `param_`
- `obs-flat-map`: mapping xt in `operator_xt_`

---

### Phase 2: Streaming File I/O (8 words)

**Files to modify:**

| File | Changes |
|------|---------|
| `include/etil/core/heap_observable.hpp` | Add Kind enum values: `ReadBytes`, `ReadLines`, `ReadJson`, `ReadCsv`, `ReadDir`. Add factory methods. |
| `src/core/observable_primitives.cpp` | 5 creation cases + 2 terminal cases + 7 `prim_obs_*` functions + registration. |
| `data/help.til` | 8 new help entries |
| `tests/unit/test_observable_primitives.cpp` | ~20 new test cases |
| `tests/til/test_observable.til` | ~15 new integration tests |

**New creation words:**

| Word | Stack Effect | Kind | Description |
|------|-------------|------|-------------|
| `obs-read-bytes` | `( path chunk-size -- obs )` | ReadBytes | Emit `HeapByteArray` chunks |
| `obs-read-lines` | `( path -- obs )` | ReadLines | Emit `HeapString` per line |
| `obs-read-json` | `( path -- obs )` | ReadJson | Emit single `HeapJson` |
| `obs-read-csv` | `( path separator -- obs )` | ReadCsv | Emit `HeapArray` per row |
| `obs-readdir` | `( path -- obs )` | ReadDir | Emit `HeapString` per entry |

**New terminal words:**

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-write-file` | `( obs path -- )` | Write all emissions to file |
| `obs-append-file` | `( obs path -- )` | Append all emissions to file |
| `obs-to-string` | `( obs -- string )` | Concatenate all string emissions |

**Execution logic:**

- **`ReadBytes`**: Open file (libuv sync or `fopen`), read `param_` bytes into a `HeapByteArray`, emit it. Loop until EOF. `ctx.tick()` between reads. Close file in all exit paths (RAII guard).

- **`ReadLines`**: Open file, read in chunks (8KB default), split on `\n`/`\r\n`. Buffer partial trailing line across chunks. Emit each complete line as `HeapString`. Emit trailing partial line on EOF if non-empty.

- **`ReadCsv`**: Layer on top of ReadLines internally. Split each line by separator (stored as `HeapString` in `state_`). Handle quoted fields: track in-quote state, skip embedded separators. Emit each row as `HeapArray` of `HeapString` fields.

- **`ReadJson`**: Read entire file into string, parse with `nlohmann::json`, emit as single `HeapJson`. Essentially a creation word that emits once and completes.

- **`ReadDir`**: List directory via libuv or `std::filesystem`, emit each entry name as `HeapString`. Alphabetically sorted.

- **`obs-write-file` / `obs-append-file`**: Terminal operators. Open file (create/truncate or append mode). Subscribe to the observable — each emission is converted to bytes and written. Close file on completion. These are implemented as primitives that call `execute_observable()` with a writing observer, not as Kind enum entries.

- **`obs-to-string`**: Terminal operator. Collect all string emissions into a single concatenated `HeapString`. Useful for `obs-read-lines` → filter → reassemble.

**HeapObservable storage:**
- `ReadBytes`: path in `state_` (HeapString Value), chunk size in `param_`
- `ReadLines`: path in `state_` (HeapString Value)
- `ReadCsv`: path in `state_` (HeapString Value), separator in `source_array_` (single-element HeapArray wrapping a HeapString)
- `ReadJson`: path in `state_` (HeapString Value)
- `ReadDir`: path in `state_` (HeapString Value)

**Security:**
- All paths resolved through `ctx.lvfs()` — LVFS sandbox enforced
- `lvfs_modify` permission required for `obs-write-file` / `obs-append-file`
- `disk_quota` enforcement on writes
- Read permissions checked at pipeline execution time (when the terminal operator runs), not at node construction time

---

### Phase 3: Streaming HTTP (3 words)

**Files to modify:**

| File | Changes |
|------|---------|
| `include/etil/core/heap_observable.hpp` | Add Kind enum values: `HttpGet`, `HttpPost`, `HttpSse`. Add factory methods. |
| `src/core/observable_primitives.cpp` | 3 execution cases + 3 `prim_obs_*` functions + registration. |
| `src/net/http_primitives.cpp` | May share URL validation and SSRF checks with existing words. |
| `data/help.til` | 3 new help entries |
| `tests/unit/test_observable_primitives.cpp` | ~10 new test cases |

**New words:**

| Word | Stack Effect | Kind | Description |
|------|-------------|------|-------------|
| `obs-http-get` | `( url headers -- obs )` | HttpGet | Emit response as byte chunks |
| `obs-http-post` | `( url headers body -- obs )` | HttpPost | POST, emit response as byte chunks |
| `obs-http-sse` | `( url headers -- obs )` | HttpSse | Emit SSE events as HeapJson |

**Execution logic:**

- **`HttpGet`**: Validate URL (SSRF blocklist, allowlist, budget). Use cpp-httplib's content receiver callback to emit each chunk as `HeapByteArray`. `ctx.tick()` between chunks. Also emit HTTP status code as first emission (integer) so downstream can check for errors.

- **`HttpPost`**: Same as HttpGet but with POST method and request body. Body stored in HeapObservable as `HeapByteArray` in `source_array_` (wrapped).

- **`HttpSse`**: Connect to SSE endpoint. Parse incoming data line by line. Accumulate `data:` fields. On empty line (event boundary), emit the event as `HeapJson` with `{event, data, id}` fields. Handle `retry:` directive. Naturally infinite — must compose with `obs-take`, `obs-timeout`, or `obs-take-until-time`.

**Security:**
- Same SSRF blocklist, domain allowlist, per-session fetch budget as `http-get`/`http-post`
- `net_client_allowed` permission required
- Fetch budget decremented per request (not per chunk)
- `obs-http-sse` counts as one fetch for budget purposes, but has a separate max-events-per-request limit

**HeapObservable storage:**
- `HttpGet`: URL in `state_` (HeapString Value), headers in `source_array_` (HeapArray of key-value pairs or HeapMap in state)
- `HttpPost`: URL in `state_`, headers + body need additional storage — may need `state_b_` field
- `HttpSse`: same as HttpGet

---

### Phase 4: Remove Sync File Words (cleanup)

**Files to modify:**

| File | Changes |
|------|---------|
| `src/fileio/file_io_primitives.cpp` | Remove 13 `prim_*_sync` functions and their registration |
| `data/help.til` | Remove 13 help entries for `*-sync` words |
| `tests/unit/test_file_io_primitives.cpp` | Remove sync-specific tests |
| `tests/til/test_observable.til` | Add migration tests verifying non-sync equivalents |

**Words to remove (13):**

`exists-sync`, `read-file-sync`, `write-file-sync`, `append-file-sync`, `copy-file-sync`, `rename-sync`, `lstat-sync`, `readdir-sync`, `mkdir-sync`, `mkdir-tmp-sync`, `rmdir-sync`, `rm-sync`, `truncate-sync`

**Not removed:** The 13 current "async" words (`read-file`, `write-file`, etc.) remain as simple one-shot operations for cases where a full Observable pipeline is unnecessary.

---

## Key Patterns to Reuse

- **`execute_observable()` recursive engine** — all new Kind cases added to the existing switch
- **`HeapObservable` factory pattern** — each Kind gets a static factory storing params in `state_`/`param_`/`operator_xt_`/`source_array_`
- **`make_primitive()` registration** — used by `register_observable_primitives()`
- **`pop_observable()` helper** — stack pop with type check for pipeline operators
- **`ctx.tick()` per emission** — budget/deadline/cancellation enforcement
- **LVFS path resolution** — `ctx.lvfs()->map_to_filesystem()` for sandboxed file access
- **SSRF validation** — `validate_url()` + `HttpClientConfig` for HTTP words
- **File I/O helpers** — `file_io_helpers.hpp` for common path/permission checks

## Dependencies Between Phases

```
Phase 1: Buffer/Composition operators
  ├── Phase 2: Streaming File I/O (uses obs-flat-map, obs-buffer-when)
  ├── Phase 3: Streaming HTTP (uses obs-flat-map for response processing)
  └── Phase 4: Remove sync words (after Phase 2 stable)
```

Phase 1 is a hard prerequisite. Phases 2 and 3 are independent of each other. Phase 4 depends on Phase 2.

## Verification

```bash
# Build
ninja -C build-debug

# Run all tests
ctest --test-dir build-debug --output-on-failure

# Run just observable tests
./build-debug/bin/etil_tests --gtest_filter=Observable*

# Run TIL integration tests
ctest --test-dir build-debug --output-on-failure -R observable

# Manual REPL verification — Phase 1
./build-debug/bin/etil_repl
# > 1 5 obs-range 3 obs-buffer obs-to-array dump drop
# > 1 10 obs-range ' 3 mod 0= obs-buffer-when obs-to-array dump drop
# > 1 6 obs-range 3 obs-window obs-to-array dump drop

# Manual REPL verification — Phase 2 (requires files in LVFS)
# > s" /home/test.txt" obs-read-lines obs-count .
# > s" /home/data.csv" s" ," obs-read-csv 5 obs-take obs-to-array dump drop
# > 1 10 obs-range ' number->string obs-map s" /home/out.txt" obs-write-file
```

## Open Questions (from design doc)

1. **`obs-watch` deferred** — libuv `uv_fs_event` integration is complex (event loop, persistent handles). Defer to a future phase after the synchronous streaming words prove the pattern.
2. **Error handling** — start with terminate-on-error (current behavior). Add `obs-catch` in a future phase if needed.
3. **Backpressure** — not needed for Phase 2 (file I/O is fast relative to CPU). Revisit for Phase 3 HTTP if needed.
4. **`obs-read-text`** — not included; `obs-read-lines obs-to-string` covers this via composition.
5. **Naming** — keep `obs-` prefix for consistency with existing 34 observable words.
