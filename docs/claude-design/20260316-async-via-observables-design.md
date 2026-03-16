# AVO: Async Via Observables — Design Document

## Date: 2026-03-16

## Context

ETIL's current file I/O and HTTP primitives are synchronous from the caller's perspective. The "async" file words use libuv's thread pool internally but block the interpreter via `await_completion()` until the operation finishes. The sync file words use libuv in synchronous mode or `std::filesystem` directly. Both present the same blocking API to TIL code.

The Observable system (v0.9.5, 34 words) provides a proven push-based execution engine with pipeline composition, temporal operators, backpressure, budget/deadline enforcement, and cancellation. This design extends the Observable pattern to become ETIL's unified model for all async and streaming I/O.

### Current State

| Component | Words | Status | API Model |
|-----------|-------|--------|-----------|
| Async file I/O | 13 (`read-file`, `write-file`, etc.) | Implemented | Cooperative-await (blocking) |
| Sync file I/O | 13 (`read-file-sync`, `write-file-sync`, etc.) | Implemented | Synchronous (blocking) |
| HTTP client | 2 (`http-get`, `http-post`) | Implemented | Synchronous (blocking) |
| Observable core | 21 words | Implemented | Push-based lazy pipeline |
| Observable temporal | 13 words | Implemented | Push-based with wall-clock time |

### Design Goals

1. **Observable-based streaming I/O** — file reads/writes as Observable pipelines
2. **Remove sync file words** — eliminate the `*-sync` duplicates
3. **Keep current async words** — they work correctly for simple cases; rename to reflect their blocking nature
4. **Extend HTTP client** — streaming responses as Observables
5. **Unified async pattern** — one model for all extended-duration operations

---

## Phase 1: Observable Buffer Operators

Before streaming I/O, the Observable system needs non-temporal buffer operators matching RxJS's `buffer` family.

### New Words

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-buffer` | `( obs n -- obs' )` | Collect n emissions into arrays, emit each batch |
| `obs-buffer-when` | `( obs xt -- obs' )` | Buffer until xt returns true, then emit batch |
| `obs-window` | `( obs n -- obs' )` | Sliding window of n elements as arrays |
| `obs-flat-map` | `( obs xt -- obs' )` | Map each value to an Observable, flatten emissions |

### `obs-buffer` vs `obs-buffer-time`

- `obs-buffer-time` (existing) — emits buffer when a time window elapses
- `obs-buffer` (new) — emits buffer when count reaches n
- `obs-buffer-when` (new) — emits buffer when a predicate fires

These compose naturally:

```forth
source 5 obs-buffer                    # batches of 5
source ' newline? obs-buffer-when      # line-oriented buffering
source 3 obs-window                    # sliding window [a,b,c] [b,c,d] ...
```

### `obs-flat-map`

Essential for streaming I/O — maps each emission to a sub-Observable and flattens:

```forth
# Read multiple files, flatten into one stream of lines
filenames-obs ' obs-read-lines obs-flat-map
```

### Implementation

All four are pure synchronous operators — no libuv, no wall-clock time. Add to `HeapObservable::Kind` enum and `execute_observable()` switch.

---

## Phase 2: Streaming File I/O

### New Observable Creation Words

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-read-bytes` | `( path chunk-size -- obs )` | Emit file as byte array chunks |
| `obs-read-lines` | `( path -- obs )` | Emit file as one string per line |
| `obs-read-json` | `( path -- obs )` | Parse JSON file, emit as HeapJson |
| `obs-read-csv` | `( path separator -- obs )` | Emit one array per CSV row |
| `obs-readdir` | `( path -- obs )` | Emit directory entries as strings |
| `obs-watch` | `( path -- obs )` | Emit events on file/dir changes (libuv `uv_fs_event`) |

### New Observable Terminal Words

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-write-file` | `( obs path -- )` | Write all string/byte emissions to file |
| `obs-append-file` | `( obs path -- )` | Append all emissions to file |

### Execution Model

Each streaming file word creates a `HeapObservable` node. During terminal execution:

1. **`obs-read-bytes`**: Opens file via libuv, reads `chunk-size` bytes at a time, emits each chunk as a `HeapByteArray`. Calls `ctx.tick()` between chunks. On EOF, completes normally. On error, stops with error to `ctx.err()`.

2. **`obs-read-lines`**: Internally uses `obs-read-bytes` with a line-splitting layer. Buffers partial lines across chunk boundaries. Emits complete lines as `HeapString`. Handles `\n`, `\r\n`, and `\r`.

3. **`obs-read-csv`**: Built on `obs-read-lines` with field splitting. Emits each row as a `HeapArray` of `HeapString` fields. Handles quoted fields with embedded delimiters.

4. **`obs-read-json`**: Reads entire file, parses as JSON, emits as single `HeapJson`. (Not streaming JSON — single-document parse.)

5. **`obs-write-file` / `obs-append-file`**: Terminal operators that consume the observable. Each emission is converted to bytes (string → UTF-8, byte array → raw) and written. File is opened before first emission and closed on completion.

### Pipeline Composition Examples

```forth
# Count lines in a file
s" /data/access.log" obs-read-lines obs-count .

# Extract field 3 from CSV, collect unique values
s" /data/users.csv" s" ," obs-read-csv
  ' 2 array-get obs-map
  obs-distinct
  obs-to-array

# Read JSON, transform, write back
s" /data/config.json" obs-read-json
  ' process-config obs-map
  ' json-pretty obs-map
  s" /data/config-new.json" obs-write-file

# Stream large file in 64KB chunks, compute checksum
s" /data/big.bin" 65536 obs-read-bytes
  ' update-checksum obs-subscribe

# Grep: filter lines matching a pattern
s" /data/log.txt" obs-read-lines
  ' s" ERROR" sfind 0 >= obs-filter
  ' type obs-subscribe

# Watch directory for changes
s" /data/inbox/" obs-watch
  ' process-new-file obs-subscribe
```

### Backpressure and Resource Safety

- **`ctx.tick()`** called between every chunk/line emission — budget, deadline, and cancellation enforced
- **File handles** closed on pipeline completion, error, or cancellation (RAII via observer return)
- **Memory bounded** — only one chunk/line in flight at a time; no full-file buffering for streaming reads
- **`obs-take`** composes naturally — `obs-read-lines 100 obs-take` reads only the first 100 lines, then closes the file

---

## Phase 3: Streaming HTTP

### New Words

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-http-get` | `( url headers -- obs )` | Streaming GET — emit response body as byte chunks |
| `obs-http-post` | `( url headers body -- obs )` | POST, emit response as byte chunks |
| `obs-http-sse` | `( url headers -- obs )` | SSE client — emit each event as HeapJson |

### Execution Model

- **`obs-http-get`**: Opens HTTP connection via cpp-httplib. Uses `set_read_timeout` and chunked reading. Each received chunk emitted as `HeapByteArray`. Same SSRF/allowlist/budget enforcement as existing `http-get`.

- **`obs-http-sse`**: Connects to an SSE endpoint. Parses `data:` lines, emits each complete event as `HeapJson`. Handles reconnection via `Last-Event-ID`. Naturally infinite — must be composed with `obs-take`, `obs-take-until-time`, or `obs-timeout` to terminate.

### Examples

```forth
# Streaming download with progress
s" https://example.com/big.tar.gz" map-new obs-http-get
  dup obs-count . s" chunks" type cr      # count chunks
  s" /data/big.tar.gz" obs-write-file      # write to disk

# SSE event stream with timeout
s" https://api.example.com/events" map-new obs-http-sse
  30000000 obs-timeout                      # 30s timeout
  ' process-event obs-subscribe

# Download, parse JSON lines
s" https://api.example.com/export" map-new obs-http-get
  ' bytes->string obs-map
  s" \n" obs-buffer-when                   # or: obs-read-lines equivalent
  ' json-parse obs-map
  ' store-record obs-subscribe
```

---

## Phase 4: Remove Sync File Words

Once streaming file I/O is stable, remove the 13 `*-sync` words:

`exists-sync`, `read-file-sync`, `write-file-sync`, `append-file-sync`, `copy-file-sync`, `rename-sync`, `lstat-sync`, `readdir-sync`, `mkdir-sync`, `mkdir-tmp-sync`, `rmdir-sync`, `rm-sync`, `truncate-sync`

The current async words (`read-file`, `write-file`, etc.) remain for simple one-shot operations. They're synchronous from the user's perspective and should be documented as such — the "async" label is misleading.

### Migration Path

| Old | Replacement |
|-----|------------|
| `read-file-sync` | `read-file` (already equivalent) |
| `write-file-sync` | `write-file` (already equivalent) |
| `readdir-sync` | `readdir` or `obs-readdir` for streaming |
| `lstat-sync` | `lstat` |
| All others | Drop `-sync` suffix, use the existing word |

---

## Implementation Order

```
Phase 1: obs-buffer, obs-buffer-when, obs-window, obs-flat-map
  └─ Pure observable operators, no I/O dependencies
  └─ Prerequisite for composable streaming pipelines

Phase 2: obs-read-bytes, obs-read-lines, obs-read-csv, obs-read-json,
         obs-readdir, obs-write-file, obs-append-file
  └─ Depends on Phase 1 (obs-flat-map for multi-file, obs-buffer-when for line splitting)
  └─ libuv file I/O integration in the observable engine

Phase 3: obs-http-get, obs-http-post, obs-http-sse
  └─ Depends on Phase 1
  └─ cpp-httplib streaming integration

Phase 4: Remove *-sync words
  └─ After Phase 2 is stable and tested
```

---

## Architectural Considerations

### Observable Engine Changes

The current `execute_observable()` function handles all 30 Kinds in a single recursive switch. Streaming I/O Kinds need:

1. **File handle lifecycle** — open before first emission, close on completion/error/cancel. The observer return value (`true` = stop, `false` = continue) already provides the completion signal. Need RAII cleanup for the `false` (cancelled/error) path.

2. **Chunk-based emission loop** — similar to `Kind::Range` but with I/O between emissions:
   ```cpp
   case K::ReadBytes: {
       int fd = open_file(...);
       while (bytes_read = read_chunk(fd, chunk_size)) {
           if (!ctx.tick()) { close(fd); return false; }
           auto* ba = new HeapByteArray(buffer, bytes_read);
           if (!observer(Value::from(ba), ctx)) { close(fd); return true; }
       }
       close(fd);
       return true;
   }
   ```

3. **LVFS integration** — all paths resolved through `ctx.lvfs()` for sandboxing, same as current file I/O words.

### HeapObservable Storage

New Kinds need to store:
- File path → `state_` as `Value::from(HeapString)` (addref'd)
- Chunk size → `param_` (int64)
- Separator string → could use `state_` or add a second string field

The existing `state_` (Value) + `param_` (int64) + `operator_xt_` (WordImpl*) + `source_array_` (HeapArray*) fields should suffice for most Kinds. If a Kind needs both a path string and a separator string, the separator can go in `source_array_` as a single-element array, or we add a `state_b_` field.

### Security

- **File I/O**: same LVFS sandbox, `lvfs_modify` permission, `disk_quota` enforcement as current words
- **HTTP**: same SSRF blocklist, domain allowlist, per-session budget as current `http-get`/`http-post`
- **`obs-watch`**: must be restricted to LVFS paths; inotify/kqueue file descriptors are a finite resource — cap per session

### Testing Strategy

- **Unit tests**: mock file content, verify emission sequences (same pattern as existing observable tests)
- **TIL integration tests**: write temp files, read them back via observable pipelines, verify content
- **Stress tests**: large files (>10MB) chunked reading, verify memory stays bounded
- **PEN tests**: path traversal via observable args, oversized files, infinite `obs-watch` streams

---

## Open Questions

1. **`obs-watch` scope** — should it emit filenames, events (create/modify/delete), or both? RxJS doesn't have a direct analog. libuv `uv_fs_event` provides filename + event type.

2. **Error emissions** — should I/O errors terminate the observable (current behavior for all operators) or emit error values that downstream can handle? RxJS has `catchError` — do we need `obs-catch`?

3. **Write backpressure** — `obs-write-file` consumes as fast as upstream produces. For slow I/O targets, should there be a way to signal backpressure upstream? The current push model doesn't support this — the observer callback is synchronous.

4. **Binary vs text** — `obs-read-bytes` emits `HeapByteArray`, `obs-read-lines` emits `HeapString`. Should there be an `obs-read-text` that emits the entire file as one string (equivalent to current `read-file` but as an observable for pipeline composition)?

5. **Naming convention** — `obs-read-lines` vs `obs-file-lines` vs `file-lines>obs`. The `obs-` prefix is established but gets verbose with the domain qualifier.
