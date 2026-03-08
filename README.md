# Evolutionary Threaded Interpretive Language (ETIL)

A modern reimagining of FORTH — 64-bit integers, reference-counted heap objects,
dense linear algebra, MongoDB, and a full MCP server — all from a stack-based language
you can learn in an afternoon.

```
> 3 4 + .
7
> : factorial 1 swap 1 + 1 do i * loop ;
> 20 factorial .
2432902008176640000
> s" Hello" s" , World!" s+ type cr
Hello, World!
> 3 3 mat-eye 2.0 mat-scale mat.
  2.000   0.000   0.000
  0.000   2.000   0.000
  0.000   0.000   2.000
```

## Quick Start

```bash
# Build
mkdir build-debug && cd build-debug
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ..
ninja

# Run the REPL
./bin/etil_repl

# Run tests (1,171 tests, all passing)
ctest --output-on-failure
```

Or connect the [TUI client](https://github.com/krystalmonolith/etil-tui) to the
MCP server for a richer experience — triple-window layout, help browser (F1), OAuth login,
script execution, and session logging.

---

## Features

### Language

- **240 primitive words** across 30 categories — arithmetic, stack, comparison, logic, I/O,
  math, strings, arrays, maps, JSON, file I/O, linear algebra, HTTP, MongoDB, and more
- **Colon definitions** — `: name ... ;` compiles to bytecode, executed by an inner interpreter
- **Full control flow** — `if`/`else`/`then`, `do`/`loop`/`+loop`/`i`/`j`/`leave`,
  `begin`/`until`/`again`, `begin`/`while`/`repeat`, `>r`/`r>`/`r@`, `exit`, `recurse`
- **Defining words** — `create`, `does>`, `variable`, `constant`, `,`, `@`, `!`, `allot`
- **First-class Booleans** — `true`/`false` are distinct from integers; comparisons and
  predicates return Boolean; control flow requires Boolean; arithmetic rejects Boolean
- **Self-hosting builtins** — `variable`, `constant`, `forget`, metadata words, and convenience
  definitions implemented in ETIL source (`data/builtins.til`), loaded on startup
- **`evaluate`** — interpret a string as TIL code at runtime, with call-depth tracking and
  unterminated-definition recovery

### Data Types

Six heap-allocated, reference-counted types — all interoperable on the stack:

| Type | Literal | Example |
|------|---------|---------|
| **String** | `s" hello"` | `s" hello" s" world" s+ type` → `helloworld` |
| **Array** | `array-new` | `array-new 1 array-push 2 array-push array-length .` → `2` |
| **ByteArray** | `bytes-new` | `16 bytes-new 255 0 bytes-set` |
| **Map** | `map-new` | `map-new s" x" 42 map-set s" x" map-get .` → `42` |
| **JSON** | `j\| ... \|` | `j\| {"a":1} \| s" a" json-get .` → `1` |
| **Matrix** | `mat-new` | `3 3 mat-eye mat.` (prints 3×3 identity) |

Strings support regex (`sregex-find`, `sregex-replace`, `sregex-match`), `sprintf` formatting,
and a **taint bit** that tracks data from untrusted sources (HTTP responses, file reads) through
concatenation, substring, split/join, and bytes↔string conversion.

### Linear Algebra (LAPACK/OpenBLAS)

25 words for dense matrix operations — constructors, accessors, arithmetic, solvers, and
decompositions. Column-major `double` storage, passed directly to BLAS/LAPACK with zero
copy overhead.

```
> 2 2 mat-new 1.0 0 0 mat-set 2.0 0 1 mat-set
                3.0 1 0 mat-set 4.0 1 1 mat-set    # A = [1 2; 3 4]
> 2 1 mat-new 5.0 0 0 mat-set 6.0 1 0 mat-set      # b = [5; 6]
> mat-solve drop mat.                                # x = A\b (drop success flag)
 -4.000
  4.500
```

| Category | Words |
|----------|-------|
| Constructors | `mat-new` `mat-eye` `mat-from-array` `mat-diag` `mat-rand` |
| Accessors | `mat-get` `mat-set` `mat-rows` `mat-cols` `mat-row` `mat-col` |
| Arithmetic | `mat*` `mat+` `mat-` `mat-scale` `mat-transpose` |
| Solvers | `mat-solve` (DGESV) `mat-inv` (DGETRF+DGETRI) `mat-det` (LU) |
| Decompositions | `mat-eigen` (DSYEV/DGEEV) `mat-svd` (DGESVD) `mat-lstsq` (DGELS) |
| Utilities | `mat-norm` `mat-trace` `mat.` |

Gated by `ETIL_BUILD_LINALG` (default ON). Requires OpenBLAS or compatible BLAS/LAPACK.

### MongoDB

5 words for document database operations — all accept String, JSON, or Map interchangeably
for filters, documents, and options:

```
> s" users" j| {"active": true} | j| {"limit": 10, "sort": {"name": 1}} | mongo-find
```

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `mongo-find` | `( coll filter opts -- json flag )` | Query with skip/limit/sort/projection |
| `mongo-count` | `( coll filter opts -- n flag )` | Server-side count (O(index), not O(N)) |
| `mongo-insert` | `( coll doc opts -- flag )` | Insert document |
| `mongo-update` | `( coll filter update opts -- flag )` | Update with upsert/hint/collation |
| `mongo-delete` | `( coll filter opts -- flag )` | Delete with hint/collation |

`mongo-find` returns `HeapJson` directly — no parsing step needed. The unified
`MongoQueryOptions` struct supports `skip`, `limit`, `sort`, `projection`, `hint`,
`collation`, `max_time_ms`, `batch_size`, and `upsert`.

x.509 certificate authentication + TLS. Per-role permission enforcement (`mongo_access`
defaults to `false`). CMake flag: `ETIL_BUILD_MONGODB=ON` (default OFF).

### JSON

First-class JSON values with a literal syntax and 12 primitives:

```
> j| [1, 2, 3] | 1 json-get .
2
> j| {"name": "ETIL", "version": 0.8} | json-pretty type
{
    "name": "ETIL",
    "version": 0.8
}
> j| {"x": 1} | json->map s" x" map-get .
1
```

Bidirectional conversion between JSON, Map, and Array (`json->map`, `json->array`,
`map->json`, `array->json`). `json->value` auto-unpacks to the matching ETIL native type.

### Outbound HTTP

`http-get` and `http-post` fetch data from external URLs with defense-in-depth:

```
> s" https://api.example.com/data" map-new http-get
  if ." Status: " . cr bytes->string type cr
  else ." Request failed" cr then
```

- Custom headers via `HeapMap` — set Content-Type, Authorization, etc.
- SSRF blocklist — blocks loopback, RFC 1918, link-local, IPv6 private ranges
- Domain allowlist — configurable via `ETIL_HTTP_ALLOWLIST` env var
- Per-session budgets — 10 fetches per interpret call, 100 per session lifetime
- Opaque byte return — response body is `HeapByteArray`, not a string (prevents
  code injection via `evaluate`)
- HTTPS via OpenSSL

### File I/O

26 words — 13 async (libuv thread pool with cooperative `await`) and 13 sync (libuv
synchronous mode):

```
> s" /home/output.txt" s" Hello from ETIL!" write-file-sync
  if ." Written" cr then
> s" /home/output.txt" read-file-sync
  if bytes->string type cr then
```

Async words (`read-file`, `write-file`, `exists?`, `mkdir`, `readdir`, etc.) are cancellable
via execution limits. Sync words (`read-file-sync`, `write-file-sync`, etc.) use the same
libuv backend without the event loop overhead.

### MCP Server

Model Context Protocol server for programmatic AI interaction — run ETIL from Claude Code,
custom agents, or any MCP client.

- **21 tools** — `interpret`, `list_words`, `get_word_info`, `get_stack`, `set_weight`,
  `reset`, `get_session_stats`, `write_file`, `list_files`, `read_file`,
  `list_sessions`, `kick_session`, `manage_allowlist`, plus 8 admin tools for
  role/user management (`admin_list_roles`, `admin_get_role`, `admin_set_role`,
  `admin_delete_role`, `admin_list_users`, `admin_set_user_role`,
  `admin_delete_user`, `admin_reload_config`)
- **4 resources** — `etil://dictionary`, `etil://word/{name}`, `etil://stack`,
  `etil://session/stats`
- **HTTP Streamable Transport** — real-time SSE streaming for notifications during
  long-running commands (chunked transfer encoding via `set_chunked_content_provider`)
- **Per-session profiling** — CPU time, wall time, RSS tracking, dictionary/stack metrics

### MCP Client TUI

Interactive terminal UI in the separate [`etil-tui`](https://github.com/krystalmonolith/etil-tui)
repository — triple-window layout, full-screen help browser (F1) with live examples,
OAuth login, script execution (`--exec`/`--execux`), and session logging.

### Authentication & Authorization

- **JWT with RBAC** — RS256 JWTs with per-role permissions: HTTP domain allowlists,
  instruction budgets, file I/O gates, MongoDB access, session quotas
- **OAuth Device Flow** — GitHub + Google via RFC 8628. Three endpoints: `/auth/device`,
  `/auth/poll`, `/auth/token`. Stateless — provider tokens used once then discarded
- **API key fallback** — backward-compatible Bearer token auth for simple deployments
- **Admin tools** — 8 MCP tools for role/user management gated by `role_admin` permission;
  create/update/delete roles and user mappings with atomic file persistence and live reload
- **Audit logging** — permission-denied events, session lifecycle, logins, user creation
  (backed by MongoDB)

### Security & Sandboxing

- **Docker sandbox** — read-only filesystem, `no-new-privileges`, CPU/memory/PID limits
- **DoS mitigation** — instruction budget (10M/request), 30s execution deadline, 1K call
  depth limit, 1MB input / 10MB output caps
- **nginx hardening** — connection limits, request timeouts, body size limits
- **Taint tracking** — data from HTTP responses and file reads carries a taint bit through
  string operations; `staint` queries it, `sregex-replace` clears it (sanitization)

### Introspection & Help

- **`help <word>`** — description, stack effect, category, and examples for any word
  (all 240+ words documented in `data/help.til`)
- **`dump`** — deep-inspect TOS without consuming it (recursive, with truncation)
- **`see <word>`** — decompile word definitions showing bytecode, primitives, or handler status
- **Word metadata** — attach text, markdown, HTML, code, JSON, or JSONL to word concepts
  and implementations

### LVFS (Little Virtual File System)

Per-session virtual filesystem with `/home` (writable) and `/library` (read-only, shared):

```
> cd /library
> ls
builtins.til  help.til
> cat builtins.til
```

Shell-like navigation: `cwd`, `cd`, `ls`, `ll` (long format), `lr` (recursive), `cat`.
Directory traversal protection via path normalization.

---

## Examples

### Fibonacci

```
> : fib-n 0 1 rot 0 do over . swap over + loop drop drop ;
> 10 fib-n
0 1 1 2 3 5 8 13 21 34
```

### FizzBuzz

```
> : fizzbuzz 1 + 1 do i 15 mod 0= if ." FizzBuzz " else i 3 mod 0= if ." Fizz "
    else i 5 mod 0= if ." Buzz " else i . then then then loop ;
> 20 fizzbuzz
1 2 Fizz 4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz 16 17 Fizz 19 Buzz
```

### Newton's Method Square Root

```
> : my-sqrt dup 0.5 * 20 0 do over over / + 0.5 * loop swap drop ;
> 2.0 my-sqrt .
1.41421
> 144.0 my-sqrt .
12
```

### String Reversal

```
> : reverse-str s" " strim swap dup slength 0 do dup i 1 substr rot s+ swap loop drop ;
> s" Hello, World!" reverse-str s.
!dlroW ,olleH
> s" ETIL" reverse-str s.
LITE
```

---

## Architecture

### Core Components

1. **WordImpl** — word implementation with performance profiling and intrusive reference counting
2. **ExecutionContext** — thread-local execution environment: vector-backed data/return/float
   stacks, configurable I/O streams, execution limits (instruction budget, call depth,
   deadline, cancellation)
3. **Dictionary** — thread-safe word lookup via `absl::Mutex` with reader/writer locking
   and `absl::flat_hash_map`. Multiple implementations per word concept ("newest wins")
4. **ByteCode** — compiled word bodies with inner interpreter; per-word data fields with
   lazy `DataFieldRegistry` registration and bounds-checked `DataRef` resolution
5. **Interpreter** — outer interpreter handling all language semantics (parsing, compilation,
   dispatch). Handler logic extracted into three handler set classes via dependency injection.
   Dual output streams (`out_`/`err_`) enable MCP output capture
6. **HeapObject** — reference-counted base class for String, Array, ByteArray, Map, JSON,
   and Matrix. Taint bit tracks untrusted data provenance
7. **MCP Server** — `etil_mcp` library with JSON-RPC 2.0, HTTP Streamable Transport,
   per-session profiling, dual-mode auth (JWT/API key)

### Implemented Primitives (240 words)

| Category | Words |
|----------|-------|
| Arithmetic | `+` `-` `*` `/` `mod` `/mod` `negate` `abs` `max` `min` |
| Stack | `dup` `drop` `swap` `over` `rot` `pick` `nip` `tuck` `depth` `?dup` `roll` |
| Comparison | `=` `<>` `<` `>` `<=` `>=` `0=` `0<` `0>` |
| Logic | `true` `false` `not` `bool` `and` `or` `xor` `invert` `lshift` `rshift` `lroll` `rroll` |
| I/O | `.` `.s` `cr` `emit` `space` `spaces` `words` |
| Memory | `create` `,` `@` `!` `allot` `immediate` |
| Math | `sqrt` `sin` `cos` `tan` `asin` `acos` `atan` `atan2` `log` `log2` `log10` `exp` `pow` `ceil` `floor` `round` `trunc` `fmin` `fmax` `pi` `f~` `random` `random-seed` `random-range` |
| String | `type` `s.` `s+` `s=` `s<>` `slength` `substr` `strim` `sfind` `sreplace` `ssplit` `sjoin` `sregex-find` `sregex-replace` `sregex-search` `sregex-match` `staint` `sprintf` |
| Array | `array-new` `array-push` `array-pop` `array-get` `array-set` `array-length` `array-shift` `array-unshift` `array-compact` `array-reverse` |
| ByteArray | `bytes-new` `bytes-get` `bytes-set` `bytes-length` `bytes-resize` `bytes->string` `string->bytes` |
| Map | `map-new` `map-set` `map-get` `map-remove` `map-length` `map-keys` `map-values` `map-has?` |
| JSON | `json-parse` `json-dump` `json-pretty` `json-get` `json-length` `json-type` `json-keys` `json->map` `json->array` `map->json` `array->json` `json->value` |
| Matrix | `mat-new` `mat-eye` `mat-from-array` `mat-diag` `mat-rand` `mat-get` `mat-set` `mat-rows` `mat-cols` `mat-row` `mat-col` `mat*` `mat+` `mat-` `mat-scale` `mat-transpose` `mat-solve` `mat-inv` `mat-det` `mat-eigen` `mat-svd` `mat-lstsq` `mat-norm` `mat-trace` `mat.` |
| LVFS | `cwd` `cd` `ls` `ll` `lr` `cat` |
| System | `sys-semver` `sys-timestamp` `sys-datafields` `sys-notification` `user-notification` `abort` |
| Time | `time-us` `us->iso` `us->iso-us` `us->jd` `jd->us` `us->mjd` `mjd->us` `sleep` |
| Input Reading | `word-read` `string-read-delim` |
| Dictionary Ops | `dict-forget` `dict-forget-all` `file-load` `include` `library` `evaluate` `marker` `marker-restore` |
| Metadata Ops | `dict-meta-set` `dict-meta-get` `dict-meta-del` `dict-meta-keys` `impl-meta-set` `impl-meta-get` |
| Help | `help` |
| Execution | `'` `execute` `xt?` `>name` `xt-body` |
| Conversion | `int->float` `float->int` `number->string` `string->number` |
| Debug | `dump` `see` |
| File I/O (async) | `exists?` `read-file` `write-file` `append-file` `copy-file` `rename-file` `lstat` `readdir` `mkdir` `mkdir-tmp` `rmdir` `rm` `truncate` |
| File I/O (sync) | `exists-sync` `read-file-sync` `write-file-sync` `append-file-sync` `copy-file-sync` `rename-sync` `lstat-sync` `readdir-sync` `mkdir-sync` `mkdir-tmp-sync` `rmdir-sync` `rm-sync` `truncate-sync` |
| HTTP Client | `http-get` `http-post` |
| MongoDB | `mongo-find` `mongo-count` `mongo-insert` `mongo-update` `mongo-delete` |
| Parsing | `."` `s"` `s\|` `.\|` `j\|` `:` `;` |
| Self-hosted | `variable` `constant` `forget` `forget-all` `meta!` `meta@` `meta-del` `meta-keys` `impl-meta!` `impl-meta@` `time-iso` `time-iso-us` `time-jd` `time-mjd` `1+` `1-` `-rot` |
| Control (compile-only) | `if` `else` `then` `do` `loop` `+loop` `i` `j` `begin` `until` `while` `repeat` `again` `>r` `r>` `r@` `leave` `exit` `recurse` `does>` `[']` |

---

## Building

```bash
# Debug build (with ASan + UBSan)
mkdir build-debug && cd build-debug
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ..
ninja

# Release build
mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `ETIL_BUILD_TESTS` | ON | Build unit and integration tests |
| `ETIL_BUILD_EXAMPLES` | ON | Build REPL, MCP server, benchmarks |
| `ETIL_BUILD_LINALG` | ON | Linear algebra (OpenBLAS/LAPACK) — 25 matrix words |
| `ETIL_BUILD_HTTP_CLIENT` | OFF | HTTP client (`http-get`, `http-post`) — requires OpenSSL |
| `ETIL_BUILD_JWT` | OFF | JWT authentication with RBAC |
| `ETIL_BUILD_MONGODB` | OFF | MongoDB integration — requires `ETIL_BUILD_JWT` |

### Dependencies

Fetched automatically by CMake (or resolved from a pre-built prefix via `CMAKE_PREFIX_PATH`):

- [Abseil C++](https://abseil.io/) — containers, synchronization
- [nlohmann/json](https://github.com/nlohmann/json) — JSON handling
- [spdlog](https://github.com/gabime/spdlog) — logging
- [replxx](https://github.com/AmokHuginnworksys/replxx) — REPL line editing
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP transport
- [libuv](https://libuv.org/) — async file I/O
- [Google Test](https://github.com/google/googletest) + [Google Benchmark](https://github.com/google/benchmark) — testing
- [OpenBLAS](https://www.openblas.net/) — BLAS/LAPACK (when `ETIL_BUILD_LINALG=ON`)
- [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) + OpenSSL (when `ETIL_BUILD_JWT=ON`)
- [mongocxx](https://www.mongodb.com/docs/drivers/cxx/) (when `ETIL_BUILD_MONGODB=ON`)

### Docker (MCP Server)

The MCP server runs inside Docker per project security rules:

```bash
# Build and run (API key auth)
docker build -t etil-mcp .
docker run -d --rm --read-only \
  -p 127.0.0.1:8080:8080 \
  -e ETIL_MCP_API_KEY=your-secret-key \
  --tmpfs /tmp:size=10M \
  etil-mcp --port 8080

# Test it
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer your-secret-key" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"curl","version":"1.0"}}}'
```

For JWT auth with RBAC, mount an auth config directory:
```bash
docker run -d --rm --read-only \
  -p 127.0.0.1:8080:8080 \
  -e ETIL_AUTH_CONFIG=/etc/etil \
  -v /path/to/auth-config:/etc/etil:ro \
  --tmpfs /tmp:size=10M \
  etil-mcp --port 8080
```

See `data/auth-config/*.json.example` for role/permission configuration.

---

## Running

### Interactive REPL

```bash
./build-debug/bin/etil_repl            # interactive mode
echo '42 . cr' | ./build-debug/bin/etil_repl -q   # pipe-friendly quiet mode
```

Line editing (arrow keys, Home/End, Ctrl-A/E/K), persistent history (`~/.etil/repl/history.txt`),
tab-completion of all dictionary words and meta commands (via replxx). Color themes
(`--color=auto|always|never`), `--quiet` pipe-friendly mode.

REPL meta commands: `/help [word]`, `/quit`, `/clear`, `/words`, `/history`, `/dark`, `/light`.

### Running Tests

```bash
ctest --test-dir build-debug --output-on-failure    # 1,171 tests (debug)
ctest --test-dir build --output-on-failure           # 1,171 tests (release)
```

---

## Design Principles

### No More Linear Dictionary

FORTH's linked-list dictionary was a memory scarcity artifact. ETIL uses a hash map where
each word concept can have multiple implementations with weighted selection:

```
Word: SORT
├─[weight: 0.7]→ quicksort_avx2    (fast, small data)
├─[weight: 0.2]→ mergesort_parallel (stable, large data)
└─[weight: 0.1]→ radix_sort         (integers only)
```

Currently, `lookup()` returns the newest implementation ("latest wins"). The weighted
selection engine is planned.

### No Single-Cell / Double-Cell Distinction

All integer values are `int64_t` and all floating-point values are `double`. The old FORTH
double-cell words (`UM/MOD`, `D+`, `2DUP`, etc.) have no ETIL equivalents — their
functionality is the default behavior.

### REPL is an I/O Channel, Not an Interpreter

The REPL implements zero language semantics. All parsing, compilation, and word dispatch
live in the `Interpreter` class in the core library. The same interpreter is driven by the
REPL, file inclusion, `evaluate`, and the MCP server.

---

## Planned Features

These features are part of ETIL's roadmap. Infrastructure is in place (e.g., `WordImpl`
tracks execution counts, timing, and weights; LLVM is linked) but the engines themselves
are not yet implemented:

- **Execution Engine** — formal engine with implementation selection and metrics
- **Selection Engine** — decision trees, multi-armed bandits for choosing implementations
- **Evolution Engine** — genetic operators (mutation, crossover), fitness evaluation
- **JIT Compiler** — LLVM-based native code generation
- **Hardware Acceleration** — SIMD vectorization and GPU offload

---

## References

- **FORTH 2012 Standard**: http://www.forth200x.org/documents/forth-2012.pdf
- **Threaded Interpretive Languages**: R.G. Loeliger, 1981
- **Genetic Programming**: Koza, "Genetic Programming", 1992
- **MCP Specification**: https://modelcontextprotocol.io/specification/2025-11-25
- **JSON-RPC Specification**: https://www.jsonrpc.org/specification

## License

BSD-3-Clause

## Author

Mark Deazley — [github.com/krystalmonolith](https://github.com/krystalmonolith)

TIL == **T**hreaded **I**nterpretive **L**anguage

- **1979**: Wrote my first TIL on a
  [Zilog Z-80 Development System](https://vintagecomputer.ca/zilog-z80-development-system/) at Sierra Research.
  Used it to generate wire list cross references.
- **1981**: _Tried_ to write an embedded TIL for the 68000 as an independent study at SUNY Stony Brook.
  The 68000 assembler/emulator would frequently freeze.
  The final straw: the assembler would **crash** the UNIVAC mainframe every time
  I used a `macro` statement, taking out _**all**_ the student and administrative terminals for 15 minutes.
  After I did it twice in a row the system operator stomped out, located me by my terminal ID,
  and emphatically told me to _**STOP**_ running that #$%^ing assembler program.
  And that was the end of the 68000 TIL.
- **1982**: Wrote a TIL for a microprocessors course at SUNY Buffalo — CPM on an
  upconverted [TRS-80 Model III](https://www.trs-80.org/model-3.html)
  with a whopping 4.77 MHz processor and an unheard-of 48K of memory.
- **1983**: Purchased
  [MMS FORTH](https://www.google.com/search?q=MMS+FORTH)
  and wrote a monochrome StarGate clone on the TRS-80. Never published it but spent a lot of time playing it.
- **1991–1993**: Hired by Caltech's OVRO to convert their FORTH radio telescope control code to C.
  Reverse-engineered the FORTH code and implemented functionally enhanced C on the VAXELN RTOS,
  controlling seven 10-meter, 34-ton radio telescopes to 0.6 arc seconds RMS in a 20 mph wind.

## History

The FORTH programming language was invented by Charles H. Moore in the late 1960s, with its
first widely recognized version created in 1970. It was developed to control telescopes and
for other real-time applications.
