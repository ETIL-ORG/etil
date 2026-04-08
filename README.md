<img src="images/ETIL-Logo-2.png" alt="ETIL Logo" width="800px">

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
---

## Quick Start

See the [BUILD_INSTRUCTIONS.md](./BUILD_INSTRUCTIONS.md) for compilation instructions.

## ETIL TUI: _Text User Interface_ Network Client

### See the TUI [README.md](https://github.com/ETIL-ORG/etil-tui/blob/master/README.md) in the ETIL TUI Project for more detail.

Connect the [TUI Client](https://github.com/ETIL-ORG/etil-tui) to the
ETIL MCP server directly for a richer experience:
- Triple Window Layout, 
- Help Browser (F1)
- OAuth Login
- Script Execution
- Session Logging and Screen Shots.

### Interactive REPL: Simple Read-Evaluate-Process-Loop for the console.

- The REPL only works with a subset of the words, and is primarily a comparative testing tool. 

```bash
# interactive mode
$ build/bin/etil_repl --data-dir evolutionary-til/data
Evolutionary TIL REPL v1.6.0
Type '/help' for commands, '/quit' to exit

> /help dup
dup
  Description:  Duplicate top of stack
  Stack effect: ( x -- x x )
  Category:     stack
> : x 41 + . ;
> 1 x
42

# pipe-friendly quiet mode
./build/bin/etil_repl -q
```

- Line editing (arrow keys, Home/End, Ctrl-A/E/K)
- Persistent history (`~/.etil/repl/history.txt`),
- Tab-completion of all dictionary words and meta commands (via replxx).
- Color themes (`--color=auto|always|never`), 
- `--quiet` pipe-friendly mode.
- REPL meta commands: 
  - `/help [word]`, 
  - `/quit`, 
  - `/clear`, 
  - `/words`, 
  - `/history`, 
  - `/dark`, 
  - `/light`

## ETIL Features

### Language

- **250+ words** across 32 categories — arithmetic, stack, comparison, logic, I/O,
  math, strings, arrays, maps, JSON, file I/O, linear algebra, HTTP, MongoDB, observables, and more
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

Seven heap-allocated, reference-counted types — all interoperable on the stack:

| Type | Literal | Example |
|------|---------|---------|
| **String** | `s" hello"` | `s" hello" s" world" s+ type` → `helloworld` |
| **Array** | `array-new` | `array-new 1 array-push 2 array-push array-length .` → `2` |
| **ByteArray** | `bytes-new` | `16 bytes-new 0 255 bytes-set` |
| **Map** | `map-new` | `map-new s" x" 42 map-set s" x" map-get .` → `42` |
| **JSON** | `j\| ... \|` | `j\| {"a":1} \| s" a" json-get .` → `1` |
| **Matrix** | `mat-new` | `3 3 mat-eye mat.` (prints 3×3 identity) |
| **Observable** | `obs-range` | `1 6 obs-range ' double obs-map obs-to-array` |

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
| Constructors | `mat-new` `mat-eye` `array->mat` `mat-diag` `mat-rand` `mat-randn` |
| Accessors | `mat-get` `mat-set` `mat-rows` `mat-cols` `mat-row` `mat-col` |
| Arithmetic | `mat*` `mat+` `mat-` `mat-scale` `mat-transpose` `mat-hadamard` `mat-add-col` `mat-clip` |
| Activations | `mat-relu` `mat-sigmoid` `mat-tanh` |
| Derivatives | `mat-relu'` `mat-sigmoid'` `mat-tanh'` |
| Reductions | `mat-sum` `mat-col-sum` `mat-mean` |
| Classification | `mat-softmax` `mat-cross-entropy` |
| Extensibility | `mat-apply` (execute xt per element) |
| Solvers | `mat-solve` (DGESV) `mat-inv` (DGETRF+DGETRI) `mat-det` (LU) |
| Decompositions | `mat-eigen` (DSYEV/DGEEV) `mat-svd` (DGESVD) `mat-lstsq` (DGELS) |
| Utilities | `mat-norm` `mat-trace` `mat.` |
| Conversion | `mat->json` `json->mat` `mat->array` |
| TIL-level | `mat-xavier` `mat-he` `mat-mse` |

Requires OpenBLAS or compatible BLAS/LAPACK (always compiled in).

### Observables

RxJS-style reactive pipelines — lazy, push-based, composable data processing:

```
> 1 6 obs-range ' double obs-map ' even? obs-filter ' add 0 obs-reduce .
30
> 0 1000000 obs-range 5 obs-take obs-count .
5
```

21 words across 7 categories:

| Category | Words |
|----------|-------|
| Creation | `obs-from` `obs-of` `obs-empty` `obs-range` |
| Transform | `obs-map` `obs-map-with` `obs-filter` `obs-filter-with` |
| Accumulate | `obs-scan` `obs-reduce` |
| Limiting | `obs-take` `obs-skip` `obs-distinct` |
| Combination | `obs-merge` `obs-concat` `obs-zip` |
| Terminal | `obs-subscribe` `obs-to-array` `obs-count` |
| Introspection | `obs?` `obs-kind` |

Pipelines are lazy — nodes build a linked list; terminal operators trigger recursive execution.
The `-with` variants (`obs-map-with`, `obs-filter-with`) carry a context value per node,
enabling closure-like data binding without language-level closures.

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
| `mongo-insert` | `( coll doc -- id flag )` | Insert document, return `_id` |
| `mongo-update` | `( coll filter update opts -- n flag )` | Update with upsert/hint/collation |
| `mongo-delete` | `( coll filter opts -- n flag )` | Delete with hint/collation |

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
- SSRF blocklist — blocks loopback, RFC 1918, link-local, IPv6 private ranges,
  and internal/local/localhost domains
- Domain allowlist — configurable via `ETIL_HTTP_ALLOWLIST` env var
- Per-session budgets — 10 fetches per interpret call, 100 per session lifetime
- Opaque byte return — response body is `HeapByteArray`, not a string (prevents
  code injection via `evaluate`)
- HTTPS via OpenSSL

### File I/O

13 file I/O words using libuv's thread pool with cooperative await, plus 7 observable
streaming words (`obs-read-lines`, `obs-read-csv`, `obs-write-file`, etc.):

```
> s" Hello from ETIL!" s" /home/output.txt" write-file
  if ." Written" cr then
> s" /home/output.txt" obs-read-lines obs-count .
  # Count lines in a file via observable pipeline
```

All file I/O is cancellable via execution limits and sandboxed through the LVFS.

### MCP Server

Model Context Protocol server for programmatic AI interaction — run ETIL from Claude Code,
custom agents, or any MCP client.

- **22 tools** — `interpret`, `list_words`, `get_word_info`, `get_stack`, `set_weight`,
  `reset`, `get_session_stats`, `write_file`, `list_files`, `read_file`,
  `list_sessions`, `kick_session`, `manage_allowlist`, plus 9 admin tools for
  role/user management (`admin_list_roles`, `admin_get_role`, `admin_set_role`,
  `admin_delete_role`, `admin_list_users`, `admin_set_user_role`,
  `admin_delete_user`, `admin_set_default_role`, `admin_reload_config`)
- **4 resources** — `etil://dictionary`, `etil://word/{name}`, `etil://stack`,
  `etil://session/stats`
- **HTTP Streamable Transport** — real-time SSE streaming for notifications during
  long-running commands (chunked transfer encoding via `set_chunked_content_provider`)
- **Per-session profiling** — CPU time, wall time, RSS tracking, dictionary/stack metrics

### MCP Client TUI

Interactive terminal UI in the separate [`etil-tui`](https://github.com/ETIL-ORG/etil-tui)
repository — triple-window layout, full-screen help browser (F1) with live examples,
OAuth login, script execution (`--exec`/`--execux`), and session logging.

### Authentication & Authorization

- **JWT with RBAC** — RS256 JWTs with clock-skew tolerance, issued-at validation,
  type-safe claim extraction, and per-role permissions: HTTP domain allowlists,
  instruction budgets, per-call and cumulative execution time limits, file I/O gates,
  MongoDB access, session quotas
- **OAuth Device Flow** — GitHub + Google via RFC 8628. Three endpoints: `/auth/device`,
  `/auth/poll`, `/auth/token`. Stateless — provider tokens used once then discarded
- **API key fallback** — backward-compatible Bearer token auth for simple deployments
- **Admin tools** — 9 MCP tools for role/user management gated by `role_admin` permission;
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
  (all words documented in `data/help.til`)
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
   Matrix, and Observable. Taint bit tracks untrusted data provenance
7. **MCP Server** — `etil_mcp` library with JSON-RPC 2.0, HTTP Streamable Transport,
   per-session profiling, dual-mode auth (JWT/API key)

### Implemented Primitives

| Category | Words |
|----------|-------|
| Arithmetic | `+` `-` `*` `/` `mod` `/mod` `negate` `abs` `max` `min` |
| Stack | `dup` `drop` `swap` `over` `rot` `pick` `nip` `tuck` `depth` `?dup` `roll` |
| Comparison | `=` `<>` `<` `>` `<=` `>=` `0=` `0<` `0>` |
| Logic | `true` `false` `not` `bool` `and` `or` `xor` `invert` `lshift` `rshift` `lroll` `rroll` |
| I/O | `.` `.s` `cr` `emit` `space` `spaces` `words` |
| Memory | `create` `,` `@` `!` `allot` `cell-get` `cell-set` `immediate` |
| Math | `sqrt` `sin` `cos` `tan` `tanh` `asin` `acos` `atan` `atan2` `log` `log2` `log10` `exp` `pow` `ceil` `floor` `round` `trunc` `fmin` `fmax` `pi` `f~` `random` `random-seed` `random-range` |
| String | `type` `s.` `s+` `s=` `s<>` `slength` `substr` `strim` `sfind` `sreplace` `ssplit` `sjoin` `sregex-find` `sregex-replace` `sregex-search` `sregex-match` `staint` `sprintf` |
| Array | `array-new` `array-push` `array-pop` `array-get` `array-set` `array-length` `array-shift` `array-unshift` `array-compact` `array-reverse` |
| ByteArray | `bytes-new` `bytes-get` `bytes-set` `bytes-length` `bytes-resize` `bytes->string` `string->bytes` |
| Map | `map-new` `map-set` `map-get` `map-remove` `map-length` `map-keys` `map-values` `map-has?` |
| JSON | `json-parse` `json-dump` `json-pretty` `json-get` `json-length` `json-type` `json-keys` `json->map` `json->array` `map->json` `array->json` `json->value` |
| Matrix | `mat-new` `mat-eye` `array->mat` `mat-diag` `mat-rand` `mat-randn` `mat-get` `mat-set` `mat-rows` `mat-cols` `mat-row` `mat-col` `mat*` `mat+` `mat-` `mat-scale` `mat-transpose` `mat-hadamard` `mat-add-col` `mat-clip` `mat-relu` `mat-sigmoid` `mat-tanh` `mat-relu'` `mat-sigmoid'` `mat-tanh'` `mat-sum` `mat-col-sum` `mat-mean` `mat-softmax` `mat-cross-entropy` `mat-apply` `mat-solve` `mat-inv` `mat-det` `mat-eigen` `mat-svd` `mat-lstsq` `mat-norm` `mat-trace` `mat.` `mat->json` `json->mat` `mat->array` |
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
| File I/O | `exists?` `read-file` `write-file` `append-file` `copy-file` `rename-file` `lstat` `readdir` `mkdir` `mkdir-tmp` `rmdir` `rm` `truncate` |
| HTTP Client | `http-get` `http-post` |
| Observable | `obs-from` `obs-of` `obs-empty` `obs-range` `obs-map` `obs-map-with` `obs-filter` `obs-filter-with` `obs-scan` `obs-reduce` `obs-take` `obs-skip` `obs-distinct` `obs-merge` `obs-concat` `obs-zip` `obs-subscribe` `obs-to-array` `obs-count` `obs?` `obs-kind` |
| Array Iteration | `array-each` `array-map` `array-filter` `array-reduce` |
| MongoDB | `mongo-find` `mongo-count` `mongo-insert` `mongo-update` `mongo-delete` |
| Parsing | `."` `s"` `s\|` `.\|` `j\|` `:` `;` |
| Self-hosted | `variable` `constant` `forget` `forget-all` `meta!` `meta@` `meta-del` `meta-keys` `impl-meta!` `impl-meta@` `time-iso` `time-iso-us` `time-jd` `time-mjd` `1+` `1-` `-rot` |
| Control (compile-only) | `if` `else` `then` `do` `loop` `+loop` `i` `j` `begin` `until` `while` `repeat` `again` `>r` `r>` `r@` `leave` `exit` `recurse` `does>` `[']` |

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

## Appendix Index

| Appendix | Topic | Words |
|----------|-------|------:|
| [A](#appendix-a-integers-and-floats) | Integers and Floats | 61 |
| [B](#appendix-b-strings) | Strings | 20 |
| [C](#appendix-c-arrays) | Arrays | 14 |
| [D](#appendix-d-maps) | Maps | 8 |
| [E](#appendix-e-json) | JSON | 13 |
| [F](#appendix-f-matrices) | Matrices | 45 |
| [G](#appendix-g-stack-manipulation) | Stack Manipulation | 12 |
| [H](#appendix-h-io-and-printing) | I/O and Printing | 8 |
| [I](#appendix-i-variables-constants-and-defining-words) | Variables, Constants, and Defining Words | 11 |
| [J](#appendix-j-control-flow) | Control Flow | 20 |
| [K](#appendix-k-execution-tokens-and-evaluation) | Execution Tokens and Evaluation | 6 |
| [L](#appendix-l-dictionary-operations) | Dictionary Operations | 12 |
| [M](#appendix-m-metadata) | Metadata | 12 |
| [N](#appendix-n-byte-arrays) | Byte Arrays | 7 |
| [O](#appendix-o-file-io) | File I/O | 13 |
| [P](#appendix-p-lvfs-virtual-filesystem) | LVFS (Virtual Filesystem) | 6 |
| [Q](#appendix-q-http-client-and-mongodb) | HTTP Client and MongoDB | 7 |
| [R](#appendix-r-system-time-and-debug) | System, Time, and Debug | 21 |
| [S](#appendix-s-observables) | Observables | 60 |
| [T](#appendix-t-evolution-and-selection) | Evolution and Selection | 7 |

---

## Appendix A: Integers and Floats

ETIL has two numeric types: 64-bit signed integers (`int64_t`) and 64-bit IEEE 754
doubles (`double`). There is no single-cell/double-cell distinction — all integers are
64 bits wide. Arithmetic operators auto-promote: if either operand is a float, the
result is a float; two integers produce an integer. Division of two integers truncates
toward zero (use `int->float` first for real-valued division).

Booleans are a separate type (`true`/`false`). Arithmetic operators reject Boolean
operands. Use `bool` to convert integers to Boolean, and comparisons to convert in the
other direction.

**Words:** `*` `+` `-` `/` `/mod` `0<` `0=` `0>` `1+` `1-` `<` `<=` `<>` `=` `>` `>=`
`abs` `acos` `and` `asin` `atan` `atan2` `bool` `ceil` `cos` `emit` `exp` `f~` `false`
`floor` `fmax` `fmin` `float->int` `int->float` `invert` `log` `log10` `log2` `lroll`
`lshift` `max` `min` `mod` `negate` `not` `number->string` `or` `pi` `pow` `random`
`random-range` `random-seed` `round` `rroll` `rshift` `sin` `sqrt` `string->number`
`tan` `tanh` `true` `trunc` `xor`

### Arithmetic

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `+` | `( a b -- a+b )` | Add | `3 4 +` → `7` |
| `-` | `( a b -- a-b )` | Subtract | `10 3 -` → `7` |
| `*` | `( a b -- a*b )` | Multiply | `6 7 *` → `42` |
| `/` | `( a b -- a/b )` | Divide (truncating for ints) | `7 2 /` → `3` |
| `mod` | `( a b -- a%b )` | Remainder | `7 3 mod` → `1` |
| `/mod` | `( a b -- rem quot )` | Divide with remainder | `7 3 /mod` → `1 2` |
| `negate` | `( n -- -n )` | Negate | `-5 negate` → `5` |
| `abs` | `( n -- \|n\| )` | Absolute value | `-42 abs` → `42` |
| `max` | `( a b -- max )` | Greater of two | `3 7 max` → `7` |
| `min` | `( a b -- min )` | Lesser of two | `3 7 min` → `3` |
| `1+` | `( n -- n+1 )` | Increment | `41 1+` → `42` |
| `1-` | `( n -- n-1 )` | Decrement | `43 1-` → `42` |

### Comparison

All comparisons consume both operands and push a Boolean result.

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `=` | `( a b -- bool )` | Equal | `3 3 =` → `true` |
| `<>` | `( a b -- bool )` | Not equal | `3 4 <>` → `true` |
| `<` | `( a b -- bool )` | Less than | `3 4 <` → `true` |
| `>` | `( a b -- bool )` | Greater than | `4 3 >` → `true` |
| `<=` | `( a b -- bool )` | Less or equal | `3 3 <=` → `true` |
| `>=` | `( a b -- bool )` | Greater or equal | `4 3 >=` → `true` |
| `0=` | `( n -- bool )` | Is zero? | `0 0=` → `true` |
| `0<` | `( n -- bool )` | Is negative? | `-1 0<` → `true` |
| `0>` | `( n -- bool )` | Is positive? | `1 0>` → `true` |

### Logic and Bitwise

`and`/`or`/`xor` are overloaded: two Booleans give logical result (Boolean), two integers
give bitwise result (integer). Mixed types are an error.

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `true` | `( -- bool )` | Push true | `true` → `true` |
| `false` | `( -- bool )` | Push false | `false` → `false` |
| `not` | `( x -- bool )` | Logical negation | `false not` → `true` |
| `bool` | `( x -- bool )` | Convert to Boolean | `42 bool` → `true` |
| `and` | `( a b -- c )` | Logical/bitwise AND | `true false and` → `false` |
| `or` | `( a b -- c )` | Logical/bitwise OR | `true false or` → `true` |
| `xor` | `( a b -- c )` | Logical/bitwise XOR | `0xFF 0x0F xor` → `240` |
| `invert` | `( n -- ~n )` | Bitwise/logical NOT | `true invert` → `false` |
| `lshift` | `( x u -- x<<u )` | Left shift | `1 8 lshift` → `256` |
| `rshift` | `( x u -- x>>u )` | Logical right shift | `256 4 rshift` → `16` |
| `lroll` | `( x u -- x' )` | Rotate left | `1 63 lroll` → `-9223372036854775808` |
| `rroll` | `( x u -- x' )` | Rotate right | `1 1 rroll` → `-9223372036854775808` |

### Math Functions

All math functions promote integers to float and return float.

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `pi` | `( -- 3.14159... )` | Push pi | `pi .` → `3.14159` |
| `sqrt` | `( x -- sqrt(x) )` | Square root | `144.0 sqrt` → `12.0` |
| `sin` | `( x -- sin(x) )` | Sine (radians) | `pi 2.0 / sin` → `1.0` |
| `cos` | `( x -- cos(x) )` | Cosine | `0.0 cos` → `1.0` |
| `tan` | `( x -- tan(x) )` | Tangent | `pi 4.0 / tan` → `1.0` |
| `tanh` | `( x -- tanh(x) )` | Hyperbolic tangent | `0.0 tanh` → `0.0` |
| `asin` | `( x -- asin(x) )` | Arc sine | `1.0 asin` → `1.5708` |
| `acos` | `( x -- acos(x) )` | Arc cosine | `1.0 acos` → `0.0` |
| `atan` | `( x -- atan(x) )` | Arc tangent | `1.0 atan` → `0.785398` |
| `atan2` | `( y x -- atan2(y,x) )` | Two-argument arc tangent | `1.0 1.0 atan2` → `0.785398` |
| `log` | `( x -- ln(x) )` | Natural logarithm | `1.0 exp log` → `1.0` |
| `log2` | `( x -- log2(x) )` | Base-2 logarithm | `1024.0 log2` → `10.0` |
| `log10` | `( x -- log10(x) )` | Base-10 logarithm | `1000.0 log10` → `3.0` |
| `exp` | `( x -- e^x )` | Exponential | `1.0 exp` → `2.71828` |
| `pow` | `( b e -- b^e )` | Power | `2.0 10.0 pow` → `1024.0` |
| `ceil` | `( x -- ceil(x) )` | Ceiling | `3.2 ceil` → `4.0` |
| `floor` | `( x -- floor(x) )` | Floor | `3.8 floor` → `3.0` |
| `round` | `( x -- round(x) )` | Round to nearest | `3.5 round` → `4.0` |
| `trunc` | `( x -- trunc(x) )` | Truncate toward zero | `-3.7 trunc` → `-3.0` |
| `fmin` | `( a b -- min )` | Float-safe minimum | `3.14 2.71 fmin` → `2.71` |
| `fmax` | `( a b -- max )` | Float-safe maximum | `3.14 2.71 fmax` → `3.14` |
| `f~` | `( r1 r2 tol -- bool )` | Approximate equality | `3.14 pi 0.01 f~` → `true` |

### Random Numbers

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `random` | `( -- f )` | Random float in [0, 1) | `random .` → `0.839431` |
| `random-seed` | `( n -- )` | Seed the PRNG | `42 random-seed` |
| `random-range` | `( lo hi -- n )` | Random integer in [lo, hi) | `1 7 random-range` → `4` |

### Conversion

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `int->float` | `( n -- f )` | Integer to float | `42 int->float` → `42.0` |
| `float->int` | `( f -- n )` | Float to integer (truncates) | `3.9 float->int` → `3` |
| `number->string` | `( n -- str )` | Number to string | `42 number->string` → `"42"` |
| `string->number` | `( str -- val flag )` | Parse string as number | `s" 42" string->number` → `42 true` |

---

## Appendix B: Strings

Strings in ETIL are immutable, heap-allocated, reference-counted UTF-8 byte sequences.
The literal syntax is `s" hello"` (space after `s"` is required — it's a parsing word
that reads to the closing `"`). The alternative `s|` syntax uses `|` as delimiter,
useful when the string contains double-quote characters.

String operations that produce new strings (concatenation, substring, replace) allocate
new heap objects — the originals are never modified. Strings from untrusted sources (HTTP
responses, file reads) carry a **taint bit** that propagates through concatenation,
substring, split/join, and bytes conversion. Use `staint` to query it and `sregex-replace`
to clear it (regex validation acts as sanitization).

**Words:** `s"` `s|` `s+` `s.` `s<>` `s=` `sfind` `sjoin` `slength` `sprintf`
`sreplace` `sregex-find` `sregex-match` `sregex-replace` `sregex-search` `ssplit`
`staint` `strim` `substr` `type`

### Creating and Printing

| Word      | Stack Effect             | Description                                          | Example                                    |
|-----------|--------------------------|------------------------------------------------------|--------------------------------------------|
| `s"`      | `( -- str )`             | String literal (parse to `"`), no escaping           | `s" hello"` → `"hello"`                    |
| `s\|`     | `( -- str )`             | String literal (parse to `\|`), supports \ escaping. | `s\| \\|hello\\|\n\| → "\|hello\|\n"`    |` → `"hello"`          |
| `type`    | `( str -- )`             | Print string (consume)                               | `s" hello" type` prints `hello`            |
| `s.`      | `( str -- )`             | Print string with trailing space                     | `s" hi" s.` prints `hi `                   |
| `sprintf` | `( args... fmt -- str )` | C-style formatting                                   | `42 s" Value: %d" sprintf` → `"Value: 42"` |

### Comparison and Properties

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `s=` | `( str1 str2 -- bool )` | String equality | `s" abc" s" abc" s=` → `true` |
| `s<>` | `( str1 str2 -- bool )` | String inequality | `s" abc" s" xyz" s<>` → `true` |
| `slength` | `( str -- n )` | Length in bytes | `s" hello" slength` → `5` |
| `staint` | `( str -- bool )` | Is string tainted? | `s" safe" staint` → `false` |

### Manipulation

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `s+` | `( str1 str2 -- str3 )` | Concatenate | `s" hello" s" world" s+` → `"helloworld"` |
| `substr` | `( str start len -- sub )` | Extract substring | `s" hello" 1 3 substr` → `"ell"` |
| `strim` | `( str -- trimmed )` | Trim whitespace | `s"  hi  " strim` → `"hi"` |
| `sfind` | `( str needle -- index )` | Find substring (-1 if not found) | `s" hello" s" ll" sfind` → `2` |
| `sreplace` | `( str old new -- result )` | Replace all occurrences | `s" aabaa" s" a" s" x" sreplace` → `"xxbxx"` |

### Split and Join

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `ssplit` | `( str delim -- array )` | Split by delimiter | `s" a,b,c" s" ," ssplit` → array `["a","b","c"]` |
| `sjoin` | `( array delim -- str )` | Join with delimiter | `array s" -" sjoin` → `"a-b-c"` |

### Regular Expressions

Regex uses the C++ `<regex>` library (ECMAScript syntax by default).

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `sregex-find` | `( str pattern -- index )` | First match position (-1 if not found) | `s" abc123" s" [0-9]+" sregex-find` → `3` |
| `sregex-replace` | `( str pat repl -- result )` | Replace matches (clears taint) | `s" foo123bar" s" [0-9]+" s" NUM" sregex-replace` → `"fooNUMbar"` |
| `sregex-search` | `( str pattern -- matches flag )` | Search with captures | `s" 2026-03-12" s" ([0-9]+)-([0-9]+)-([0-9]+)" sregex-search` → array `true` |
| `sregex-match` | `( str pattern -- matches flag )` | Full string match | `s" hello" s" h.*o" sregex-match` → array `true` |

---

## Appendix C: Arrays

Arrays are heap-allocated, reference-counted, dynamically-sized sequences of arbitrary
values. Any ETIL value can be stored in an array — integers, floats, strings, other
arrays (nesting), maps, JSON, matrices, or observables.

Arrays are mutable: push, pop, set, shift, and unshift modify the array in place and
return it for chaining. Index access is zero-based and bounds-checked.

The four iteration words (`array-each`, `array-map`, `array-filter`, `array-reduce`)
accept execution tokens (`xt`) for the callback, enabling functional-style data
processing without observables.

**Words:** `array-compact` `array-each` `array-filter` `array-get` `array-length`
`array-map` `array-new` `array-pop` `array-push` `array-reduce` `array-reverse`
`array-set` `array-shift` `array-unshift`

### Creation and Access

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `array-new` | `( -- array )` | Create empty array | `array-new` → `[]` |
| `array-push` | `( array val -- array )` | Append to end | `array-new 1 array-push 2 array-push` → `[1, 2]` |
| `array-pop` | `( array -- array val )` | Remove from end | `[1,2,3] array-pop` → `[1,2] 3` |
| `array-get` | `( array idx -- val )` | Get by index | `[10,20,30] 1 array-get` → `20` |
| `array-set` | `( array idx val -- array )` | Set by index | `[10,20,30] 1 99 array-set` → `[10,99,30]` |
| `array-length` | `( array -- n )` | Element count (consuming — use `dup array-length` to keep array) | `[1,2,3] array-length` → `3` |

### Queue and Deque Operations

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `array-shift` | `( array -- array val )` | Remove from front | `[1,2,3] array-shift` → `[2,3] 1` |
| `array-unshift` | `( array val -- array )` | Insert at front | `[2,3] 1 array-unshift` → `[1,2,3]` |

### Transformation

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `array-reverse` | `( array -- array )` | Reverse in place | `[1,2,3] array-reverse` → `[3,2,1]` |
| `array-compact` | `( array -- array )` | Remove zero/null entries | `[1,0,2,0,3] array-compact` → `[1,2,3]` |

### Iteration (Functional)

These words accept an execution token (xt) as the callback. Define the callback with
`: name ... ;` and pass it with `'` (tick).

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `array-each` | `( array xt -- )` | Call xt for each element | `: show . space ; [1,2,3] ' show array-each` prints `1 2 3` |
| `array-map` | `( array xt -- array )` | Transform each element | `: double dup + ; [1,2,3] ' double array-map` → `[2,4,6]` |
| `array-filter` | `( array xt -- array )` | Keep elements where xt returns true | `: even? 2 mod 0= ; [1,2,3,4] ' even? array-filter` → `[2,4]` |
| `array-reduce` | `( array xt init -- val )` | Fold to single value | `: add + ; [1,2,3,4] ' add 0 array-reduce` → `10` |

---

## Appendix D: Maps

Maps are heap-allocated, reference-counted hash tables with string keys and arbitrary
values. They provide O(1) average-case lookup, insertion, and deletion.

Maps are mutable — `map-set` and `map-remove` modify the map in place and return it for
chaining. Keys must be strings. Values can be any ETIL type.

Maps interoperate with JSON (`map->json`, `json->map`) and can be passed directly to
MongoDB words and HTTP client words as header collections.

**Words:** `map-get` `map-has?` `map-keys` `map-length` `map-new` `map-remove` `map-set`
`map-values`

### Reference

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `map-new` | `( -- map )` | Create empty map | `map-new` → `{}` |
| `map-set` | `( map key val -- map )` | Set key/value (returns map) | `map-new s" x" 42 map-set` → `{"x":42}` |
| `map-get` | `( map key -- val )` | Get value by key (fails if missing) | `m s" x" map-get` → `42` |
| `map-remove` | `( map key -- map )` | Remove key (fails if missing) | `m s" x" map-remove` → `{}` |
| `map-length` | `( map -- n )` | Number of entries | `m map-length` → `2` |
| `map-keys` | `( map -- array )` | All keys as array of strings | `m map-keys` → `["x","y"]` |
| `map-values` | `( map -- array )` | All values as array | `m map-values` → `[1,2]` |
| `map-has?` | `( map key -- bool )` | Key exists? | `m s" x" map-has?` → `true` |

### Building a Map

Maps are built by chaining `map-set` calls. Each call returns the map, so they compose
naturally:

```
> map-new
    s" name" s" Alice" map-set
    s" age" 30 map-set
    s" active" true map-set
```

### Using Maps as HTTP Headers

```
> s" https://api.example.com/data"
  map-new s" Accept" s" application/json" map-set
  http-get
```

---

## Appendix E: JSON

ETIL has first-class JSON values backed by `nlohmann::json`. JSON values live on the
stack like any other heap type and can represent objects, arrays, strings, numbers,
booleans, and null.

The literal syntax `j| ... |` parses everything between `j|` and the closing `|` as JSON.
This works in both interpret and compile mode.

JSON interoperates with Maps and Arrays via bidirectional conversion words. MongoDB words
accept JSON directly for filters, documents, and options.

**Words:** `array->json` `j|` `json->array` `json->map` `json->value` `json-dump`
`json-get` `json-keys` `json-length` `json-parse` `json-pretty` `json-type` `map->json`

### Creating JSON

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `j\|` | `( -- json )` | JSON literal | `j\| {"a": 1, "b": [2,3]} \|` |
| `json-parse` | `( str -- json )` | Parse JSON string | `s" [1,2,3]" json-parse` |

### Inspecting JSON

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `json-get` | `( json key\|idx -- val )` | Object key or array index | `j\| {"x":42} \| s" x" json-get` → `42` |
| `json-length` | `( json -- n )` | Array length or object size | `j\| [1,2,3] \| json-length` → `3` |
| `json-type` | `( json -- str )` | Type name | `j\| "hello" \| json-type` → `"string"` |
| `json-keys` | `( json -- array )` | Object keys | `j\| {"a":1,"b":2} \| json-keys` → `["a","b"]` |

### Serializing JSON

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `json-dump` | `( json -- str )` | Compact string | `j\| {"a":1} \| json-dump` → `'{"a":1}'` |
| `json-pretty` | `( json -- str )` | Indented string | `j\| {"a":1} \| json-pretty type` |

### Converting Between Types

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `json->map` | `( json -- map )` | JSON object to Map (recursive) | `j\| {"x":1} \| json->map` |
| `json->array` | `( json -- array )` | JSON array to Array (recursive) | `j\| [1,2,3] \| json->array` |
| `map->json` | `( map -- json )` | Map to JSON object (recursive) | `m map->json` |
| `array->json` | `( array -- json )` | Array to JSON array (recursive) | `a array->json` |
| `json->value` | `( json -- val )` | Auto-unpack to native type | `j\| 42 \| json->value` → `42` |

### Array Index Access

`json-get` accepts both string keys (for objects) and integer indices (for arrays):

```
> j| [10, 20, 30] | 1 json-get .
20
> j| [{"name":"Alice"}, {"name":"Bob"}] | 1 json-get s" name" json-get type
Bob
```

---

## Appendix F: Matrices

ETIL provides 43 words for dense linear algebra backed by LAPACK and OpenBLAS. Matrices
are heap-allocated, reference-counted, column-major `double` arrays passed directly to
BLAS/LAPACK routines with zero copy overhead.

Matrices are used for numerical computation, machine learning (forward/backward pass
primitives for building multilayer perceptrons), and data analysis. Solver and
decomposition words return a Boolean success flag.

Three TIL-level convenience words (`mat-xavier`, `mat-he`, `mat-mse`) are defined in
`data/library/mlp.til` for neural network weight initialization and loss computation.
They become available after `include /library/mlp.til` (or `include data/library/mlp.til`
for native builds).

**Words:** `json->mat` `mat+` `mat-` `mat*` `mat-add-col` `mat-apply` `mat-clip`
`mat-col` `mat-col-sum` `mat-cols` `mat-cross-entropy` `mat-det` `mat-diag` `mat-eigen`
`mat-eye` `array->mat` `mat-hadamard` `mat-he` `mat-inv` `mat-lstsq` `mat-mean`
`mat-mse` `mat-new` `mat-norm` `mat-rand` `mat-randn` `mat-relu` `mat-relu'` `mat-row`
`mat-rows` `mat-scale` `mat-sigmoid` `mat-sigmoid'` `mat-softmax` `mat-solve` `mat-sum`
`mat-svd` `mat-tanh` `mat-tanh'` `mat-trace` `mat-transpose` `mat-xavier` `mat.`
`mat->array` `mat->json`

### Constructors

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `mat-new` | `( rows cols -- mat )` | Zero-filled matrix | `3 3 mat-new` |
| `mat-eye` | `( n -- mat )` | Identity matrix | `3 mat-eye mat.` |
| `array->mat` | `( nested-array -- mat )` | From nested 2D array | `array-new array-new 1.0 array-push 2.0 array-push array-push array-new 3.0 array-push 4.0 array-push array-push array->mat` |
| `mat-diag` | `( array -- mat )` | Diagonal matrix | `array-new 1.0 array-push 2.0 array-push 3.0 array-push mat-diag` |
| `mat-rand` | `( rows cols -- mat )` | Uniform random [0,1) | `3 3 mat-rand` |
| `mat-randn` | `( rows cols -- mat )` | Standard normal random | `100 1 mat-randn` |

### Accessors

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `mat-get` | `( mat row col -- val )` | Get element | `m 0 0 mat-get` → `1.0` |
| `mat-set` | `( mat row col val -- mat )` | Set element (returns mat) | `m 0 0 9.0 mat-set` |
| `mat-rows` | `( mat -- n )` | Row count | `m mat-rows` → `3` |
| `mat-cols` | `( mat -- n )` | Column count | `m mat-cols` → `3` |
| `mat-row` | `( mat i -- array )` | Extract row | `m 0 mat-row` → `[1.0, 0.0, 0.0]` |
| `mat-col` | `( mat j -- array )` | Extract column | `m 0 mat-col` → `[1.0, 0.0, 0.0]` |

### Arithmetic

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `mat*` | `( A B -- C )` | Matrix multiply (DGEMM) | `A B mat*` |
| `mat+` | `( A B -- C )` | Element-wise add | `A B mat+` |
| `mat-` | `( A B -- C )` | Element-wise subtract | `A B mat-` |
| `mat-scale` | `( mat s -- mat )` | Scalar multiply | `m 2.0 mat-scale` |
| `mat-transpose` | `( mat -- mat )` | Transpose | `m mat-transpose` |
| `mat-hadamard` | `( A B -- C )` | Element-wise multiply | `A B mat-hadamard` |
| `mat-add-col` | `( mat col -- mat )` | Broadcast-add column vector | `m bias mat-add-col` |
| `mat-clip` | `( mat lo hi -- mat )` | Clamp elements to [lo, hi] | `m -1.0 1.0 mat-clip` |

### Solvers and Decompositions

All return a Boolean flag: `true` = success, `false` = singular or failed.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `mat-solve` | `( A b -- x flag )` | Solve Ax=b via LU (DGESV) |
| `mat-inv` | `( mat -- inv flag )` | Matrix inverse via LU (DGETRF+DGETRI) |
| `mat-det` | `( mat -- det flag )` | Determinant via LU |
| `mat-eigen` | `( mat -- evals evecs flag )` | Eigendecomposition (DSYEV/DGEEV) |
| `mat-svd` | `( mat -- U S Vt flag )` | Singular value decomposition (DGESVD) |
| `mat-lstsq` | `( A b -- x flag )` | Least squares solve (DGELS) |

```
> # Solve [1 2; 3 4] x = [5; 6]
> 2 2 mat-new 1.0 0 0 mat-set 2.0 0 1 mat-set
                3.0 1 0 mat-set 4.0 1 1 mat-set
> 2 1 mat-new 5.0 0 0 mat-set 6.0 1 0 mat-set
> mat-solve drop mat.
 -4.000
  4.500
```

### Reductions

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `mat-sum` | `( mat -- scalar )` | Sum all elements | `3 mat-eye mat-sum` → `3.0` |
| `mat-col-sum` | `( mat -- col )` | Sum across columns → column vector | `m mat-col-sum` |
| `mat-mean` | `( mat -- scalar )` | Mean of all elements | `3 mat-eye mat-mean` → `0.333` |
| `mat-norm` | `( mat -- val )` | Frobenius norm | `3 mat-eye mat-norm` → `1.732` |
| `mat-trace` | `( mat -- val )` | Sum of diagonal | `3 mat-eye mat-trace` → `3.0` |

### Neural Network Activations and Derivatives

These words operate element-wise. The derivative words (`'` suffix) compute from
the **pre-activation** input, not the activated output.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `mat-relu` | `( mat -- mat )` | max(0, x) |
| `mat-sigmoid` | `( mat -- mat )` | 1/(1+exp(-x)) |
| `mat-tanh` | `( mat -- mat )` | tanh(x) |
| `mat-relu'` | `( mat -- mat )` | 1 if x>0, else 0 |
| `mat-sigmoid'` | `( mat -- mat )` | sigmoid(x) * (1-sigmoid(x)) |
| `mat-tanh'` | `( mat -- mat )` | 1 - tanh(x)^2 |
| `mat-softmax` | `( mat -- mat )` | Column-wise softmax (numerically stable) |
| `mat-cross-entropy` | `( pred actual -- scalar )` | Cross-entropy loss |
| `mat-apply` | `( mat xt -- mat )` | Apply function per element |

### Conversion and Display

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `mat.` | `( mat -- )` | Pretty-print (consumes) |
| `mat->array` | `( mat -- array )` | To nested 2D array (array of row arrays) |
| `mat->json` | `( mat -- json )` | To JSON `{rows, cols, data}` |
| `json->mat` | `( json -- mat )` | From JSON `{rows, cols, data}` |

### TIL-Level Convenience (from `data/library/mlp.til`)

Requires `include /library/mlp.til` before use.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `mat-xavier` | `( rows cols -- mat )` | Xavier/Glorot initialization |
| `mat-he` | `( rows cols -- mat )` | He initialization (for ReLU networks) |
| `mat-mse` | `( predicted actual -- scalar )` | Mean squared error loss |

---

## Appendix G: Stack Manipulation

ETIL's data stack is the primary mechanism for passing values between words. These
12 words rearrange stack values without modifying them. All work in both interpret and
compile mode.

**Words:** `-rot` `?dup` `depth` `drop` `dup` `nip` `over` `pick` `roll` `rot` `swap`
`tuck`

### Basic Operations

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `dup` | `( x -- x x )` | Duplicate TOS | `5 dup .s` → `5 5` |
| `drop` | `( x -- )` | Remove TOS | `1 2 drop .` → `1` |
| `swap` | `( a b -- b a )` | Swap top two | `1 2 swap .s` → `2 1` |
| `over` | `( a b -- a b a )` | Copy second to top | `1 2 over .s` → `1 2 1` |

### Rearranging

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `rot` | `( a b c -- b c a )` | Rotate third to top | `1 2 3 rot .s` → `2 3 1` |
| `-rot` | `( a b c -- c a b )` | Reverse rotate | `1 2 3 -rot .s` → `3 1 2` |
| `nip` | `( a b -- b )` | Drop second item | `1 2 nip .` → `2` |
| `tuck` | `( a b -- b a b )` | Copy TOS under second | `1 2 tuck .s` → `2 1 2` |

### Indexed and Conditional

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `pick` | `( xn..x0 n -- xn..x0 xn )` | Copy Nth item (0-based) | `10 20 30 2 pick .` → `10` |
| `roll` | `( xn..x0 n -- xn-1..x0 xn )` | Move Nth item to top | `10 20 30 2 roll .` → `10` |
| `depth` | `( -- n )` | Current stack depth | `1 2 3 depth .` → `3` |
| `?dup` | `( x -- x x \| 0 )` | Dup if non-zero | `5 ?dup .s` → `5 5`; `0 ?dup .s` → `0` |

---

## Appendix H: I/O and Printing

Words for outputting text and characters. `."` and `.|` are parsing words that read a
string literal from the input stream and print it immediately. Both work in interpret
mode (at the command line) and compile mode (inside colon definitions).

**Words:** `.` `."` `.\|` `cr` `emit` `space` `spaces` `words`

### Reference

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `.` | `( x -- )` | Print and consume TOS | `42 .` prints `42` |
| `cr` | `( -- )` | Print newline | `42 . cr` prints `42` + newline |
| `emit` | `( n -- )` | Print character by ASCII code | `65 emit` prints `A` |
| `space` | `( -- )` | Print one space | `1 . space 2 .` prints `1 2` |
| `spaces` | `( n -- )` | Print n spaces | `3 spaces` prints `   ` |
| `."` | `( -- )` | Print string literal (parse to `"`) | `." hello" cr` prints `hello` |
| `.\|` | `( -- )` | Print string literal with escapes | `.\| line1\nline2\|` prints two lines |
| `words` | `( -- )` | List all dictionary words | `words` |

### Using `."` in Compiled Words

`."` and `.|` compile seamlessly into colon definitions — the string is embedded in
the bytecode and printed each time the word executes:

```
> : greet ." Hello, World!" cr ;
> greet
Hello, World!
> : show-pair  ( a b -- )  ." (" swap . ." , " . ." )" cr ;
> 3 7 show-pair
(3, 7)
```

### Building Characters with `emit`

```
> : star 42 emit ;            # ASCII 42 = '*'
> : stars  ( n -- )  0 do star loop cr ;
> 5 stars
*****
> : box  ( w h -- )
    0 do dup stars loop drop ;
> 4 3 box
****
****
****
```

---

## Appendix I: Variables, Constants, and Defining Words

ETIL supports user-defined data storage and word creation. `:` and `;` create compiled
word definitions (colon definitions). `create` and `does>` build custom defining words
that manufacture new words with shared behavior and per-word data.

`variable` and `constant` are self-hosted — defined in `data/builtins.til` using
`create`, `does>`, and `allot`.

**Words:** `!` `,` `:` `;` `@` `allot` `constant` `create` `does>` `immediate`
`variable`

### Colon Definitions

| Word | Mode | Description |
|------|------|-------------|
| `:` | interpret-only | Begin colon definition. Reads the word name from input |
| `;` | **compile-only** | End colon definition. Registers the new word in the dictionary |

```
> : square  ( n -- n*n )  dup * ;
> 7 square .
49
> : cube  ( n -- n*n*n )  dup dup * * ;
> 3 cube .
27
> : hypotenuse  ( a b -- c )  swap square swap square + int->float sqrt ;
> 3 4 hypotenuse .
5
```

### Variables and Constants

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `variable` | `( -- )` | Define a variable (init to 0) | `variable counter` |
| `constant` | `( x -- )` | Define a named constant | `42 constant answer` |
| `@` | `( dataref -- x )` | Fetch value from variable | `counter @` → `0` |
| `!` | `( x dataref -- )` | Store value in variable | `5 counter !` |

```
> variable score
> 100 score !
> score @ .
100
> score @ 10 + score !    # increment by 10
> score @ .
110
> 3.14159 constant pi-approx
> pi-approx .
3.14159
```

### Low-Level Defining Words

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `create` | `( -- )` | Define a word with a data field (reads name from input) |
| `,` | `( x -- )` | Append value to last created word's data field |
| `allot` | `( n -- )` | Reserve n cells in last created word |
| `cell-get` | `( dataref n -- value )` | Fetch value at cell index n from a data field |
| `cell-set` | `( value dataref n -- )` | Store value at cell index n in a data field |
| `does>` | `( -- )` | Set runtime behavior for `create`d words (**compile-only**) |
| `immediate` | `( -- )` | Mark last defined word as immediate (runs during compilation) |

`create` and `does>` together build word-factories. Everything before `does>` runs
once at definition time; everything after `does>` runs each time the created word
executes, with the data field reference pushed onto the stack:

```
> # A constant is just create + comma + does> + fetch:
> : my-constant  create , does> @ ;
> 99 my-constant bottles
> bottles .
99

> # A variable is create + allot + does> (push the dataref):
> : my-variable  create 0 , does> ;
> my-variable x
> 42 x !
> x @ .
42
```

---

## Appendix J: Control Flow

All control flow words are **compile-only** — they can only be used inside colon
definitions (`: name ... ;`). Attempting to use them at the top level produces an error.

ETIL requires **Boolean** values for conditional tests (`if`, `while`, `until`).
Use comparison words (`=`, `<`, `0=`, etc.) or `bool` to produce Booleans from
integers.

**Words:** `[']` `again` `begin` `do` `else` `exit` `i` `if` `j` `leave` `loop`
`+loop` `r>` `r@` `recurse` `repeat` `then` `until` `while` `>r`

### Word Reference

All words below are **compile-only** unless noted.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `if` | `( bool -- )` | Conditional branch — requires Boolean on TOS |
| `else` | `( -- )` | Alternative branch |
| `then` | `( -- )` | End conditional |
| `do` | `( limit start -- )` | Begin counted loop |
| `loop` | `( -- )` | Increment index by 1 and loop |
| `+loop` | `( n -- )` | Add n to index and loop |
| `i` | `( -- n )` | Push current loop index |
| `j` | `( -- n )` | Push outer loop index |
| `begin` | `( -- )` | Begin indefinite loop |
| `until` | `( bool -- )` | Loop back to `begin` unless true |
| `while` | `( bool -- )` | Continue loop if true, else jump past `repeat` |
| `repeat` | `( -- )` | Loop back to `begin` (end of `begin`/`while` loop) |
| `again` | `( -- )` | Unconditional loop back to `begin` |
| `>r` | `( x -- ) R:( -- x )` | Move value to return stack |
| `r>` | `( -- x ) R:( x -- )` | Move value from return stack |
| `r@` | `( -- x ) R:( x -- x )` | Copy top of return stack |
| `leave` | `( -- )` | Exit `do` loop immediately |
| `exit` | `( -- )` | Return from current word immediately |
| `recurse` | `( -- )` | Call current word recursively |
| `[']` | `( -- xt )` | Compile-time tick — push xt of next word at runtime |

### Conditionals: `if` / `else` / `then`

`if` pops a Boolean from the stack. If `true`, the code between `if` and `else` (or
`then`) executes. If `false`, the `else` branch executes (or nothing if no `else`).

```
> # Simple if/then (no else):
> : check-positive  ( n -- )
    0> if ." positive" cr then ;
> 5 check-positive
positive
> -3 check-positive
>
```

```
> # if/else/then:
> : abs-value  ( n -- |n| )
    dup 0< if negate then ;
> -42 abs-value .
42
> 7 abs-value .
7
```

```
> # Nested conditionals:
> : classify  ( n -- )
    dup 0> if
      drop ." positive" cr
    else
      dup 0= if
        drop ." zero" cr
      else
        drop ." negative" cr
      then
    then ;
> 5 classify
positive
> 0 classify
zero
> -3 classify
negative
```

```
> # Fizzbuzz:
> : fizzbuzz  ( n -- )
    dup 15 mod 0= if drop ." FizzBuzz" else
    dup  3 mod 0= if drop ." Fizz" else
    dup  5 mod 0= if drop ." Buzz" else
    .
    then then then ;
> : run-fizzbuzz  16 1 do i fizzbuzz space loop cr ;
> run-fizzbuzz
1 2 Fizz 4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz
```

### Counted Loops: `do` / `loop` / `+loop` / `i` / `j`

`do` pops a limit and start value: `( limit start -- )`. The loop body executes
with the index going from `start` up to (but not including) `limit`. Inside the loop,
`i` pushes the current index and `j` pushes the outer loop's index.

```
> # Count 0 to 9:
> : count-up  10 0 do i . space loop cr ;
> count-up
0 1 2 3 4 5 6 7 8 9

> # Sum 1 to 100:
> : gauss  0  101 1 do i + loop ;
> gauss .
5050
```

`+loop` adds a custom increment instead of 1:

```
> # Count by twos:
> : evens  10 0 do i . space 2 +loop cr ;
> evens
0 2 4 6 8

> # Count backwards:
> : countdown  0 10 do i . space -1 +loop cr ;
> countdown
10 9 8 7 6 5 4 3 2 1 0
```

Nested loops use `j` for the outer index:

```
> : times-table  ( n -- )
    dup 1+ 1 do
      dup 1+ 1 do
        i j * 4 spaces swap .
      loop cr
    loop drop ;
> 4 times-table
   1   2   3   4
   2   4   6   8
   3   6   9  12
   4   8  12  16
```

### Indefinite Loops: `begin` / `until`

`begin ... until` loops back to `begin` when `until` pops `false`. When it pops
`true`, the loop exits.

```
> # Collatz sequence:
> : collatz  ( n -- )
    begin
      dup . space
      dup 1 = not
    while
      dup 2 mod 0= if 2 / else 3 * 1+ then
    repeat drop cr ;
> 6 collatz
6 3 10 5 16 8 4 2 1
```

```
> # Count down with begin/until:
> : count-down  ( n -- )
    begin dup . space 1- dup 0= until drop cr ;
> 5 count-down
5 4 3 2 1
```

### Indefinite Loops: `begin` / `while` / `repeat`

`begin ... while ... repeat` tests the condition at the top of the loop.
`while` pops a Boolean — if `true`, the body between `while` and `repeat` executes
and loops back to `begin`. If `false`, the loop exits past `repeat`.

```
> # Process while positive:
> : halve-until-one  ( n -- )
    begin dup 1 > while
      dup . space
      2 /
    repeat drop cr ;
> 64 halve-until-one
64 32 16 8 4 2
```

```
> # GCD (Euclid's algorithm):
> : gcd  ( a b -- gcd )
    begin dup 0 <> while
      swap over mod
    repeat drop ;
> 48 18 gcd .
6
> 100 75 gcd .
25
```

### Infinite Loops: `begin` / `again` with `exit`

`begin ... again` loops unconditionally. Use `exit` (return from word) or `leave`
(exit enclosing `do` loop) to break out.

```
> # Find first power of 2 exceeding n:
> : next-pow2  ( n -- 2^k )
    1 begin
      dup rot dup rot > if nip exit then
      swap dup +
    again ;
> 100 next-pow2 .
128
> 1000 next-pow2 .
1024
```

### Return Stack: `>r` / `r>` / `r@`

The return stack temporarily saves values across computations. `>r` moves TOS to the
return stack; `r>` moves it back; `r@` copies without removing. Values pushed with
`>r` **must** be popped with `r>` before the word returns.

```
> # Save a value across a computation (avoid >r/r> across do/loop —
> # do uses the return stack for its loop counter).
> : hypot  ( a b -- sqrt(a*a + b*b) )
    dup * >r                      # save b*b on return stack
    dup *                         # compute a*a
    r> +                          # retrieve b*b, add
    sqrt ;
> 3.0 4.0 hypot .
5
```

```
> # Use r@ to read without consuming:
> : repeat-char  ( char n -- )
    >r begin r@ 0> while
      over emit r> 1- >r
    repeat r> drop drop cr ;
> 42 5 repeat-char
*****
```

### `leave` — Exit a `do` Loop Early

`leave` immediately terminates the innermost `do` loop, jumping past `loop`/`+loop`:

```
> # Find first even number in a range:
> : first-even  ( start end -- n )
    swap do
      i 2 mod 0= if i leave then
    loop ;
> 1 20 first-even .
2
> 7 20 first-even .
8
```

### `exit` — Return from a Word Early

`exit` returns from the current word immediately, like `return` in C:

```
> : classify-sign  ( n -- str )
    dup 0> if drop s" positive" exit then
    dup 0< if drop s" negative" exit then
    drop s" zero" ;
> 5 classify-sign type cr
positive
> -3 classify-sign type cr
negative
> 0 classify-sign type cr
zero
```

### `recurse` — Self-Referencing Definitions

`recurse` calls the word currently being defined:

```
> : factorial  ( n -- n! )
    dup 1 <= if drop 1 exit then
    dup 1- recurse * ;
> 5 factorial .
120
> 10 factorial .
3628800
```

```
> : fib  ( n -- fib(n) )
    dup 2 < if exit then
    dup 1- recurse swap 2 - recurse + ;
> 10 fib .
55
```

### `[']` — Compile-Time Tick

`[']` is the compile-mode counterpart of `'` (tick). It reads a word name at compile
time and embeds the execution token in the bytecode so it is pushed at runtime:

```
> : double dup + ;
> : apply-double  ['] double execute ;
> 21 apply-double .
42
```

This is equivalent to writing `' double` at runtime, but `[']` resolves the word at
compile time, avoiding the dictionary lookup at runtime.

---

## Appendix K: Execution Tokens and Evaluation

Execution tokens (XTs) are first-class references to word implementations. They enable
higher-order programming: passing words as arguments to other words, storing them in
data structures, and executing them dynamically. The `evaluate` word interprets a
string as TIL code at runtime.

**Words:** `'` `>name` `evaluate` `execute` `xt-body` `xt?`

### Getting and Using Execution Tokens

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `'` | `( -- xt )` | Parse next word, push its execution token |
| `execute` | `( xt -- )` | Pop xt and execute the referenced word |
| `xt?` | `( val -- bool )` | Test if value is an execution token |
| `>name` | `( xt -- str )` | Extract the word name as a string |
| `xt-body` | `( xt -- dataref )` | Get data field reference from a `create`d word's xt |

### Finding and Executing XTs

`'` (tick) reads the next word from the input and pushes its execution token onto the
stack. `execute` pops the xt and runs the word:

```
> : double dup + ;
> ' double          # push xt for "double"
> 21 swap execute . # execute it
42
> ' double xt? .
true
> 42 xt? .
false
> ' double >name type cr
double
```

### XTs as Function Arguments

XTs are the mechanism behind all of ETIL's higher-order words — `array-map`,
`array-filter`, `array-reduce`, `array-each`, `obs-map`, `obs-filter`, `obs-reduce`,
`obs-subscribe`, and `mat-apply`:

```
> : square dup * ;
> : even? 2 mod 0= ;
> : add + ;

> # Pass square to array-map:
> array-new 1 array-push 2 array-push 3 array-push
  ' square array-map
  # Result: [1, 4, 9]

> # Pass even? to array-filter:
> array-new 1 array-push 2 array-push 3 array-push 4 array-push
  ' even? array-filter
  # Result: [2, 4]

> # Pass add to obs-reduce:
> 1 11 obs-range ' add 0 obs-reduce .
55
```

### Building a Dispatch Table

```
> : do-add + ;
> : do-sub - ;
> : do-mul * ;
> : dispatch  ( a b op-xt -- result )  execute ;
> 10 3 ' do-add dispatch .
13
> 10 3 ' do-sub dispatch .
7
> 10 3 ' do-mul dispatch .
30
```

### Dynamic Introspection with `xt-body`

For words created with `create`, `xt-body` retrieves the data field reference,
enabling dynamic access to a word's stored data:

```
> create magic 42 ,
> ' magic xt-body @ .
42
```

### `evaluate` — Runtime Code Interpretation

`evaluate` pops a string from the stack and interprets it as TIL code. It can define
new words, execute existing words, and interact with the full interpreter. Call depth
tracking prevents infinite recursion.

```
> s" 2 3 + ." evaluate
5
```

```
> # Define and immediately use a word:
> s| : triple 3 * ; 7 triple .| evaluate
21
```

```
> # Build code dynamically:
> : make-constant  ( val name-str -- )
    s" : " swap s+ s"  " s+ swap number->string s+ s"  ; " s+ evaluate ;
> 42 s" answer" make-constant
> answer .
42
```

```
> # Evaluate preserves the data stack:
> 10 20
> s" + ." evaluate
30
```

---

## Appendix L: Dictionary Operations

Words for managing the dictionary — loading files, removing words, creating checkpoints,
and low-level input parsing. `forget` and `forget-all` are self-hosted (defined in
`data/builtins.til`); the rest are C++ primitives.

**Words:** `dict-forget` `dict-forget-all` `file-load` `forget` `forget-all` `help`
`include` `library` `marker` `marker-restore` `string-read-delim` `word-read`

### Loading Files

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `file-load` | `( path-str -- flag )` | Load and interpret a TIL file |
| `include` | `( -- )` | Load a TIL file (reads path from input) |
| `library` | `( -- )` | Load from library directory (reads path from input) |

```
> include data/builtins.til       # load a file by path
> library examples/evolve.til     # load from /library
> s" my-script.til" file-load     # programmatic load (returns bool)
```

### Removing Words

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `forget` | `( -- )` | Remove latest implementation (reads name from input) |
| `forget-all` | `( -- )` | Remove all implementations (reads name from input) |
| `dict-forget` | `( name-str -- flag )` | Stack-based: remove latest impl |
| `dict-forget-all` | `( name-str -- flag )` | Stack-based: remove all impls |

```
> : test 42 . cr ;
> test
42
> forget test              # remove the definition
> : test 99 . cr ;         # redefine
> test
99
```

### Dictionary Checkpoints

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `marker` | `( -- )` | Create a checkpoint (reads name from input) |
| `marker-restore` | `( name-str -- )` | Restore dictionary to checkpoint |

```
> marker clean-state
> : temp1 1 ;
> : temp2 2 ;
> clean-state                # executing the marker restores the dictionary
> temp1                      # error — temp1 no longer exists
```

### Input Parsing and Help

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `word-read` | `( -- str flag )` | Read next whitespace-delimited token from input |
| `string-read-delim` | `( char -- str flag )` | Read string between delimiters |
| `help` | `( -- )` | Show help for a word (reads name from input) |

`word-read` and `string-read-delim` are low-level parsing primitives used by
self-hosted defining words (e.g., `forget` uses `word-read` to read the word name).

---

## Appendix M: Metadata

ETIL's metadata system attaches key-value pairs to words at two levels:
**concept-level** (shared across all implementations of a word) and
**implementation-level** (per-implementation). The help system (`data/help.til`) uses
metadata extensively to store descriptions, stack effects, categories, and examples.

The `meta!`/`meta@` family are self-hosted aliases (defined in `builtins.til`) for the
underlying `dict-meta-*`/`impl-meta-*` C++ primitives.

**Words:** `dict-meta-del` `dict-meta-get` `dict-meta-keys` `dict-meta-set`
`impl-meta!` `impl-meta-get` `impl-meta-set` `impl-meta@` `meta!` `meta-del`
`meta-keys` `meta@`

### Concept-Level Metadata

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `meta!` | `( word key fmt content -- flag )` | Set metadata |
| `meta@` | `( word key -- content flag )` | Get metadata |
| `meta-del` | `( word key -- flag )` | Delete metadata |
| `meta-keys` | `( word -- array flag )` | List metadata keys |

```
> # Attach a description to a word:
> s" square" s" description" s" text" s" Multiply a number by itself" meta! drop

> # Read it back:
> s" square" s" description" meta@
  # Stack: "Multiply a number by itself" true

> # List all keys for a word:
> s" square" meta-keys
  # Stack: ["description"] true
```

### Implementation-Level Metadata

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `impl-meta!` | `( word key fmt content -- flag )` | Set impl metadata |
| `impl-meta@` | `( word key -- content flag )` | Get impl metadata |

Implementation-level metadata is per-implementation, useful when a word has multiple
implementations and each needs its own source location or notes.

### C++ Primitives (Underlying API)

| Word | Alias for |
|------|-----------|
| `dict-meta-set` | `meta!` |
| `dict-meta-get` | `meta@` |
| `dict-meta-del` | `meta-del` |
| `dict-meta-keys` | `meta-keys` |
| `impl-meta-set` | `impl-meta!` |
| `impl-meta-get` | `impl-meta@` |

---

## Appendix N: Byte Arrays

Byte arrays are heap-allocated, reference-counted, mutable buffers of raw bytes.
They are used for binary data, HTTP request/response bodies, and conversions
to/from strings. Index access is bounds-checked.

**Words:** `bytes->string` `bytes-get` `bytes-length` `bytes-new` `bytes-resize`
`bytes-set` `string->bytes`

### Reference

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `bytes-new` | `( n -- bytes )` | Create zero-filled byte array | `4 bytes-new` |
| `bytes-get` | `( bytes idx -- val )` | Get byte at index (0–255) | `buf 0 bytes-get` → `65` |
| `bytes-set` | `( bytes idx val -- bytes )` | Set byte at index (returns buf) | `buf 0 72 bytes-set` |
| `bytes-length` | `( bytes -- n )` | Get byte array length | `buf bytes-length` → `4` |
| `bytes-resize` | `( bytes n -- bytes )` | Resize (zero-fills new bytes) | `buf 8 bytes-resize` |
| `bytes->string` | `( bytes -- str )` | Convert to UTF-8 string | `buf bytes->string` |
| `string->bytes` | `( str -- bytes )` | Convert string to bytes | `s" hello" string->bytes` |

### Round-Trip Example

```
> s" Hello" string->bytes dup bytes-length .
5
> dup 0 bytes-get .       # 'H' = 72
72
> 0 104 bytes-set         # 'h' = 104
> bytes->string type cr
hello
```

### Use with HTTP

HTTP response bodies are returned as byte arrays. Convert to string for text
processing:

```
> s" https://api.example.com/data"
  map-new s" Accept" s" application/json" map-set
  http-get           # ( bytes status-code flag )
  drop drop          # keep just the bytes
  bytes->string      # convert to string
  json-parse         # parse as JSON
```

---

## Appendix O: File I/O

ETIL provides 13 file I/O words using libuv's thread pool with cooperative await.
All paths are virtual — `/home` is writable (per-session), `/library` is read-only (shared).
For streaming file I/O, see [Appendix S: Observables](#appendix-s-observables).

**Words:** `append-file` `copy-file` `exists?` `lstat` `mkdir` `mkdir-tmp`
`read-file` `readdir` `rename-file` `rm` `rmdir` `truncate` `write-file`

### File Operations

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `exists?` | `( path -- flag )` | Check if path exists |
| `read-file` | `( path -- string? flag )` | Read file contents |
| `write-file` | `( string path -- flag )` | Write string to file (truncate) |
| `append-file` | `( string path -- flag )` | Append string to file |
| `copy-file` | `( src dest -- flag )` | Copy file |
| `rename-file` | `( old new -- flag )` | Rename/move file |
| `lstat` | `( path -- array? flag )` | File metadata |
| `readdir` | `( path -- array? flag )` | List directory |
| `mkdir` | `( path -- flag )` | Create directory (recursive) |
| `mkdir-tmp` | `( prefix -- string? flag )` | Create temp directory |
| `rmdir` | `( path -- flag )` | Remove empty directory |
| `rm` | `( path -- flag )` | Remove file/tree |
| `truncate` | `( path -- flag )` | Truncate to zero length |

### Read and Write Example

```
> s" Hello, file!" s" /home/test.txt" write-file .
true
> s" /home/test.txt" read-file .
true
> type cr
Hello, file!
```

### File Status

`lstat` returns a 4-element array: `[size, mtime_us, is_dir, is_read_only]`:

```
> s" /home/test.txt" lstat drop
> dup 0 array-get .          # size in bytes
12
> 1 array-get .              # mtime as microseconds since epoch
```

### Async vs Sync

Async words use libuv's thread pool and poll with `ctx.tick()`, so they respect
instruction budgets and execution deadlines. Sync words block the interpreter thread.
Prefer async words in the MCP server; sync words are fine for startup scripts and
REPL use.

---

## Appendix P: LVFS (Virtual Filesystem)

The Little Virtual File System provides shell-like navigation within the sandboxed
`/home` (writable, per-session) and `/library` (read-only, shared) directories.
These words are parsing words — they read an optional argument from the input stream
(except `cat`, which requires one).

**Words:** `cat` `cd` `cwd` `ll` `lr` `ls`

### Reference

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `cwd` | `( -- )` | Print current working directory |
| `cd` | `( -- )` | Change directory (no arg → `/home`). Accepts absolute or relative paths |
| `ls` | `( -- )` | List directory contents (no arg → CWD). Directories shown with trailing `/` |
| `ll` | `( -- )` | Long listing sorted by modification time (newest first) |
| `lr` | `( -- )` | Recursive long listing with relative paths |
| `cat` | `( -- )` | Print file contents (requires a path argument) |

```
> cwd
/home
> ls
scripts/  notes.txt
> cd scripts
> cwd
/home/scripts
> ll
  1234  2026-03-12T10:30:00Z  run.til
> cat run.til
42 . cr
> cd /library
> ls
examples/  help.til  builtins.til
> cd
> cwd
/home
```

---

## Appendix Q: HTTP Client and MongoDB

### HTTP Client

Outbound HTTP/HTTPS requests with SSRF protection, domain allowlisting, and per-session
fetch budgets. Requires `ETIL_BUILD_HTTP_CLIENT=ON` at build time. Domains must be in
`ETIL_HTTP_ALLOWLIST`. SSRF protection blocks loopback, RFC 1918, link-local, and cloud
metadata IPs. DNS is resolved once and connections are made by IP to prevent DNS
rebinding attacks.

**Words:** `http-get` `http-post`

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `http-get` | `( url headers -- bytes code flag )` | GET request. Headers is a HeapMap |
| `http-post` | `( url headers body -- bytes code flag )` | POST request. Body is a HeapByteArray |

```
> # GET request (map-new for no extra headers):
> s" https://api.example.com/data" map-new http-get
  # Stack: bytes 200 true
  drop drop bytes->string json-parse

> # GET with custom headers:
> s" https://api.example.com/data"
  map-new s" Accept" s" application/json" map-set
  http-get drop drop bytes->string type cr

> # POST with JSON body:
> s" https://api.example.com/submit"
  map-new s" Content-Type" s" application/json" map-set
  s| {"key": "value"} | string->bytes
  http-post drop drop bytes->string type cr
```

### MongoDB

All MongoDB words require `mongo_access` permission (per-role RBAC). Filter, document,
and options parameters accept String, Json, or Map types interchangeably. Requires
`ETIL_BUILD_MONGODB=ON` at build time.

**Words:** `mongo-count` `mongo-delete` `mongo-find` `mongo-insert` `mongo-update`

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `mongo-find` | `( coll filter opts -- json flag )` | Query documents. Returns results as Json |
| `mongo-count` | `( coll filter opts -- n flag )` | Count matching documents |
| `mongo-insert` | `( coll doc -- id flag )` | Insert document, return `_id` |
| `mongo-update` | `( coll filter update opts -- n flag )` | Update documents, return modified count |
| `mongo-delete` | `( coll filter opts -- n flag )` | Delete documents, return deleted count |

Options support: `skip`, `limit`, `sort`, `projection`, `hint`, `collation`,
`max_time_ms`, `batch_size`, `upsert`.

```
> # Insert a document:
> s" users" j| {"name": "Alice", "age": 30} | mongo-insert
  # Stack: "65f..." true

> # Find with options:
> s" users" j| {"age": {"$gte": 18}} | j| {"limit": 10, "sort": {"name": 1}} |
  mongo-find drop json-pretty type cr

> # Count:
> s" users" j| {} | j| {} | mongo-count drop .
1
```

---

## Appendix R: System, Time, and Debug

### System Words

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `sys-semver` | `( -- )` | Print project version | `sys-semver` → `v0.9.1` |
| `sys-timestamp` | `( -- )` | Print build timestamp | `sys-timestamp` → `2026-03-12 ...` |
| `sys-datafields` | `( -- )` | Print DataFieldRegistry diagnostics | `sys-datafields` |
| `sys-notification` | `( x -- )` | Queue an MCP notification | `s" hello" sys-notification` |
| `user-notification` | `( msg user -- flag )` | Send notification to a user's sessions | `s" alert" s" github:123" user-notification` |
| `abort` | `( flag -- )` | Terminate interpretation | `true abort` (success) |

`abort` has two modes: with `true`, it terminates cleanly (success abort). With
`false`, it first pops an error message string, then terminates as an error:

```
> s" something went wrong" false abort
Error: something went wrong
```

### Time Words

ETIL represents time as microseconds since the Unix epoch (UTC, `int64_t`) or as
Julian Date / Modified Julian Date (`double`). All formatting produces UTC timestamps.

**Words:** `jd->us` `mjd->us` `sleep` `time-iso` `time-iso-us` `time-jd` `time-mjd`
`time-us` `us->iso` `us->iso-us` `us->jd` `us->mjd`

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `time-us` | `( -- n )` | Current UTC as microseconds since epoch | `time-us .` |
| `time-iso` | `( -- str )` | Current UTC as compact ISO 8601 | `time-iso type` → `20260312T153000Z` |
| `time-iso-us` | `( -- str )` | Current UTC with microseconds | `time-iso-us type` |
| `us->iso` | `( n -- str )` | Format microseconds as ISO 8601 | `time-us us->iso type` |
| `us->iso-us` | `( n -- str )` | Format microseconds with high-res | `time-us us->iso-us type` |
| `us->jd` | `( n -- f )` | Microseconds to Julian Date | `time-us us->jd .` |
| `jd->us` | `( f -- n )` | Julian Date to microseconds | `2460000.0 jd->us .` |
| `us->mjd` | `( n -- f )` | Microseconds to Modified Julian Date | `time-us us->mjd .` |
| `mjd->us` | `( f -- n )` | Modified Julian Date to microseconds | `60000.0 mjd->us .` |
| `time-jd` | `( -- f )` | Current UTC as Julian Date | `time-jd .` |
| `time-mjd` | `( -- f )` | Current UTC as Modified Julian Date | `time-mjd .` |
| `sleep` | `( n -- )` | Sleep for n microseconds | `1000000 sleep` (1 second) |

```
> # Elapsed time measurement:
> time-us
> 1000000 sleep           # sleep 1 second
> time-us swap - .        # elapsed microseconds
1000123                   # approximately 1,000,000 μs

> # Julian Date round-trip:
> time-us dup us->jd jd->us - .
0                         # lossless round-trip
```

### Debug Words

| Word | Stack Effect | Description | Example |
|------|-------------|-------------|---------|
| `dump` | `( x -- x )` | Deep-inspect TOS (non-destructive) | `42 dump` |
| `see` | `( -- )` | Decompile a word (reads name from input) | `see square` |
| `.s` | `( -- )` | Display entire stack without modifying it | `1 2 3 .s` → `1 2 3` |

`dump` shows the value's type, contents, and internal details (refcount for heap
objects, data field for `create`d words). Unlike `.`, it does not consume the value:

```
> 42 dump
<Integer: 42>
> s" hello" dump
<String: "hello" (len=5, tainted=false, rc=1)>
> drop drop
```

`see` decompiles a compiled word, showing the bytecode instructions:

```
> : double dup + ;
> see double
: double
  0: Call dup
  1: Call +
;
```

---

## Appendix S: Observables

Observables are ETIL's reactive programming system — lazy, push-based, composable
data pipelines inspired by RxJS. An observable represents a sequence of values that
are produced on demand when a **terminal operator** is invoked.

### How Observables Work

An observable is a linked list of **pipeline nodes**. Each node represents one processing
step: a source, a transformation, a filter, an accumulator, or a combinator. Building a
pipeline is cheap — it just allocates nodes and links them. No values flow until a
terminal operator (`obs-subscribe`, `obs-to-array`, `obs-reduce`, `obs-count`) triggers
execution.

When a terminal fires, ETIL walks the pipeline recursively from terminal back to source,
then pushes values forward through each node. This is **push-based** execution: the
source emits values one at a time, each value flows through the entire chain of
transforms and filters, and the terminal collects or processes the result.

```
Source  ──emit──>  Transform  ──emit──>  Filter  ──emit──>  Terminal
(range)            (map)                 (filter)           (to-array)
```

Each emission checks the interpreter's execution budget (`ctx.tick()`), so runaway
pipelines are safely interrupted by instruction limits and timeouts.

### Source Operators — Creating Observables

Source operators create the head of a pipeline. Every pipeline begins with one.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-from` | `( array -- obs )` | Emit each array element in order |
| `obs-of` | `( value -- obs )` | Emit a single value |
| `obs-empty` | `( -- obs )` | Emit nothing (zero elements) |
| `obs-range` | `( start end -- obs )` | Emit integers from start (inclusive) to end (exclusive) |

```
> # obs-from: emit each element of an array
> array-new 10 array-push 20 array-push 30 array-push
  obs-from obs-to-array
  # Result: array [10, 20, 30]

> # obs-of: single-element observable
> 42 obs-of obs-to-array
  # Result: array [42]

> # obs-empty: produces nothing
> obs-empty obs-count .
0

> # obs-range: integer sequence [start, end)
> 1 6 obs-range obs-to-array
  # Result: array [1, 2, 3, 4, 5]
```

### Transform Operators — Modifying Values

Transform operators sit in the middle of a pipeline. They receive each value from
upstream, process it, and pass the result downstream.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-map` | `( obs xt -- obs' )` | Transform each value by applying xt |
| `obs-map-with` | `( obs xt ctx -- obs' )` | Transform with a bound context value |
| `obs-filter` | `( obs xt -- obs' )` | Keep only values where xt returns true |
| `obs-filter-with` | `( obs xt ctx -- obs' )` | Filter with a bound context value |

**`obs-map`** applies a function to every emitted value:

```
> : double dup + ;
> 1 6 obs-range ' double obs-map obs-to-array
  # Result: array [2, 4, 6, 8, 10]
```

**`obs-filter`** keeps only values for which the predicate returns `true`:

```
> : even? 2 mod 0= ;
> 1 11 obs-range ' even? obs-filter obs-to-array
  # Result: array [2, 4, 6, 8, 10]
```

**`obs-map-with` and `obs-filter-with`** carry a context value that is pushed onto the
stack before the xt executes, enabling closure-like data binding:

```
> : multiply * ;
> 1 6 obs-range ' multiply 10 obs-map-with obs-to-array
  # Result: array [10, 20, 30, 40, 50]

> : greater-than > ;
> 1 11 obs-range ' greater-than 5 obs-filter-with obs-to-array
  # Result: array [6, 7, 8, 9, 10]
```

### Accumulation Operators — Running State

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-scan` | `( obs xt init -- obs' )` | Emit each intermediate accumulation |
| `obs-reduce` | `( obs xt init -- result )` | Reduce to a single value (terminal) |

```
> : add + ;
> 1 6 obs-range ' add 0 obs-scan obs-to-array
  # Result: array [1, 3, 6, 10, 15]
  # Running sum: 0+1=1, 1+2=3, 3+3=6, 6+4=10, 10+5=15

> 1 11 obs-range ' add 0 obs-reduce .
55
  # Sum of 1..10 = 55 (obs-reduce is a terminal operator)
```

### Limiting Operators — Controlling Flow

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-take` | `( obs n -- obs' )` | Emit only the first n values |
| `obs-skip` | `( obs n -- obs' )` | Skip the first n values |
| `obs-distinct` | `( obs -- obs' )` | Suppress duplicate values |
| `obs-first` | `( obs -- obs' )` | Emit only the first value |
| `obs-last` | `( obs -- obs' )` | Emit only the final value |
| `obs-take-while` | `( obs xt -- obs' )` | Emit while predicate returns true |
| `obs-distinct-until` | `( obs -- obs' )` | Suppress consecutive duplicates only |

```
> # Take 3 from a range of a million — only 3 values ever produced
> 0 1000000 obs-range 3 obs-take obs-to-array
  # Result: array [0, 1, 2]

> # Skip and take for pagination
> 0 100 obs-range 20 obs-skip 10 obs-take obs-to-array
  # Result: array [20, 21, 22, 23, 24, 25, 26, 27, 28, 29]

> # First and last
> 1 10 obs-range obs-first obs-to-array    # Result: [1]
> 1 10 obs-range obs-last obs-to-array     # Result: [9]

> # Take while predicate holds
> : less-than-5 5 < ;
> 1 10 obs-range ' less-than-5 obs-take-while obs-to-array
  # Result: array [1, 2, 3, 4]

> # Distinct-until vs distinct
> array-new 1 array-push 1 array-push 2 array-push 2 array-push 1 array-push obs-from
  obs-distinct-until obs-to-array
  # Result: array [1, 2, 1] — only consecutive duplicates suppressed
```

### Combination Operators — Merging Pipelines

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-merge` | `( obs-a obs-b max -- obs )` | Interleave two observables |
| `obs-concat` | `( obs-a obs-b -- obs )` | Emit all of A, then all of B |
| `obs-zip` | `( obs-a obs-b -- obs )` | Pair up elements into 2-element arrays |
| `obs-start-with` | `( obs value -- obs' )` | Prepend a value before source emissions |

```
> 1 4 obs-range 10 13 obs-range obs-concat obs-to-array
  # Result: array [1, 2, 3, 10, 11, 12]

> 1 4 obs-range 10 13 obs-range 2 obs-merge obs-to-array
  # Result: array [1, 10, 2, 11, 3, 12]

> 1 4 obs-range 0 obs-start-with obs-to-array
  # Result: array [0, 1, 2, 3]
```

### Buffer and Composition Operators

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-buffer` | `( obs n -- obs' )` | Collect n emissions into arrays |
| `obs-buffer-when` | `( obs xt -- obs' )` | Buffer until predicate fires |
| `obs-window` | `( obs n -- obs' )` | Sliding window of n elements |
| `obs-flat-map` | `( obs xt -- obs' )` | Map to sub-observable, flatten (concatMap) |
| `obs-switch-map` | `( obs xt -- obs' )` | Map to sub-observable, last wins |
| `obs-pairwise` | `( obs -- obs' )` | Emit consecutive `[prev, curr]` pairs |

**`obs-buffer`** collects n emissions into arrays, emitting each batch. Trailing partial
batches are emitted on completion:

```
> 1 8 obs-range 3 obs-buffer obs-to-array
  # Result: array [[1, 2, 3], [4, 5, 6], [7]]
```

**`obs-flat-map`** maps each upstream value to a sub-observable and flattens the results
into a single stream (concatMap semantics — each inner runs to completion):

```
> : expand dup * obs-of ;
> 1 4 obs-range ' expand obs-flat-map obs-to-array
  # Result: array [1, 4, 9]
  # Each value mapped to obs-of its square, flattened
```

**`obs-switch-map`** is like `obs-flat-map` but only forwards emissions from the last
inner observable (for synchronous sources, only the last upstream value's inner runs):

```
> : to-obs obs-of ;
> 1 4 obs-range ' to-obs obs-switch-map obs-to-array
  # Result: array [3] — only the last value's inner is forwarded
```

**`obs-pairwise`** emits consecutive pairs as 2-element arrays:

```
> 1 5 obs-range obs-pairwise obs-to-array
  # Result: array [[1, 2], [2, 3], [3, 4]]
```

**`obs-window`** maintains a sliding window:

```
> 1 6 obs-range 3 obs-window obs-to-array
  # Result: array [[1, 2, 3], [2, 3, 4], [3, 4, 5]]
```

### Terminal Operators — Triggering Execution

Terminal operators consume the pipeline and produce a concrete result. Until one of these
is called, no values are emitted.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-subscribe` | `( obs xt -- )` | Call xt for each value (side effects) |
| `obs-to-array` | `( obs -- array )` | Collect all values into an array |
| `obs-count` | `( obs -- n )` | Count emitted values |
| `obs-reduce` | `( obs xt init -- val )` | Fold to single value |
| `obs-to-string` | `( obs -- string )` | Concatenate all string emissions |

```
> : show . space ;
> 1 6 obs-range ' show obs-subscribe
1 2 3 4 5

> 1 1000001 obs-range ' even? obs-filter obs-count .
500000

> 1 101 obs-range ' add 0 obs-reduce .
5050
```

### Utility Operators

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-tap` | `( obs xt -- obs' )` | Side-effect without modifying stream |
| `obs-finalize` | `( obs xt -- obs' )` | Execute cleanup on completion/error |
| `obs-catch` | `( obs xt -- obs' )` | Recover from errors with fallback observable |

**`obs-tap`** executes an xt for each emission as a side-effect (logging, counting) but
passes the original value through unchanged:

```
> 1 4 obs-range ' . obs-tap obs-count .
1 2 3 3
  # Side-effect: printed 1, 2, 3. Then obs-count printed 3.
```

**`obs-finalize`** executes a cleanup xt when the pipeline completes (success or error):

```
> variable cleaned
> : mark-clean true cleaned ! ;
> false cleaned !
> 1 4 obs-range ' mark-clean obs-finalize obs-count .
3
> cleaned @ .
true
```

**`obs-catch`** recovers from pipeline errors by calling a recovery xt that returns a
fallback observable:

```
> : fallback 42 obs-of ;
> 1 4 obs-range ' fallback obs-catch obs-count .
3   # No error occurred, catch passed through normally
```

### Introspection

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs?` | `( val -- bool )` | Is this value an observable? |
| `obs-kind` | `( obs -- str )` | Node kind name (e.g. "range", "map") |

```
> 1 10 obs-range obs? .
true
> 42 obs? .
false
```

### Temporal Operators — Wall-Clock Time

Temporal operators introduce real wall-clock time into observable pipelines: timers,
delays, rate-limiting, and time-stamping. All use microseconds as their time unit.

#### Creation

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-timer` | `( delay-us period-us -- obs )` | Emit after delay, then every period (0=one-shot) |
| `obs-interval` | `( period-us -- obs )` | Repeating timer with no initial delay (self-hosted) |

```
> # One-shot timer after 100ms
> 100000 0 obs-timer obs-to-array
  # Result: array [0] — emits 0 after 100ms

> # Repeating timer: emit 0, 1, 2 every 500ms, take 3
> 0 500000 obs-timer 3 obs-take obs-to-array
  # Result: array [0, 1, 2]
```

#### Transform

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-delay` | `( obs delay-us -- obs' )` | Delay each emission |
| `obs-timestamp` | `( obs -- obs' )` | Wrap each value in `[time-us, value]` |
| `obs-time-interval` | `( obs -- obs' )` | Wrap each value in `[elapsed-us, value]` |
| `obs-delay-each` | `( obs xt -- obs' )` | Per-item delay: xt returns delay in us |

```
> # Timestamp: attach wall-clock time to each emission
> 1 4 obs-range obs-timestamp obs-to-array
  # Result: array [[1710000000123, 1], [1710000000124, 2], [1710000000125, 3]]
```

#### Rate-Limiting

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-debounce-time` | `( obs quiet-us -- obs' )` | Emit after quiet period |
| `obs-throttle-time` | `( obs window-us -- obs' )` | One emission per window |
| `obs-sample-time` | `( obs period-us -- obs' )` | Emit latest at regular intervals |
| `obs-timeout` | `( obs limit-us -- obs' )` | Error if gap exceeds limit |
| `obs-audit-time` | `( obs window-us -- obs' )` | Emit latest after silence |

```
> # Throttle: only the first value in each 1-second window passes
> 0 100000 obs-timer 20 obs-take 1000000 obs-throttle-time obs-count .
  # Result: ~2 (only ~2 values pass through 2 one-second windows)
```

#### Windowed and Limiting

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-buffer-time` | `( obs window-us -- obs' )` | Collect into time-based batches |
| `obs-take-until-time` | `( obs duration-us -- obs' )` | Complete after duration |
| `obs-retry-delay` | `( obs delay-us max -- obs' )` | Retry on error with delay |

### Streaming File I/O

Observable-based file I/O reads and writes files as streams of chunks, lines, or records.
All paths are resolved through the LVFS sandbox.

#### Reading

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-read-bytes` | `( path chunk-size -- obs )` | Stream file as byte array chunks |
| `obs-read-lines` | `( path -- obs )` | Stream file as one string per line |
| `obs-read-json` | `( path -- obs )` | Parse JSON file, emit as HeapJson |
| `obs-read-csv` | `( path separator -- obs )` | Stream CSV file as arrays of fields |
| `obs-readdir` | `( path -- obs )` | Stream directory entries (sorted) |

```
> # Count lines in a file
> s" /home/data.txt" obs-read-lines obs-count .

> # Read CSV, take first 5 rows
> s" /home/users.csv" s" ," obs-read-csv 5 obs-take obs-to-array

> # Stream large file in 64KB chunks
> s" /home/big.bin" 65536 obs-read-bytes obs-count .
```

#### Writing

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-write-file` | `( obs path -- )` | Write all emissions to file (truncate) |
| `obs-append-file` | `( obs path -- )` | Append all emissions to file |

```
> # Generate numbers, write as lines
> 1 11 obs-range ' number->string obs-map s" /home/numbers.txt" obs-write-file
```

### Streaming HTTP

Observable-based HTTP performs streaming requests with the same SSRF protection, domain
allowlist, and fetch budget enforcement as the standard `http-get`/`http-post` words.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `obs-http-get` | `( url headers -- obs )` | Stream GET response as byte chunks |
| `obs-http-post` | `( url headers body -- obs )` | POST, stream response as byte chunks |
| `obs-http-sse` | `( url headers -- obs )` | SSE client: emit events as HeapJson/string |

```
> # Streaming download
> s" https://example.com/data.csv" map-new obs-http-get
    obs-to-array   # collect all chunks

> # SSE event stream with timeout
> s" https://api.example.com/events" map-new obs-http-sse
    30000000 obs-timeout   # 30-second timeout
    5 obs-take             # take first 5 events
    obs-to-array
```

### Composing Pipelines

The real power of observables comes from chaining operators into complex data processing
pipelines:

```
> # Sum of squares of even numbers from 1 to 100
> : square dup * ;
> : even? 2 mod 0= ;
> : add + ;
> 1 101 obs-range
    ' even? obs-filter
    ' square obs-map
    ' add 0 obs-reduce .
171700

> # Pipeline with skip/take for windowed processing
> 0 1000000 obs-range          # million integers
  ' even? obs-filter           # only evens
  100 obs-skip                 # skip first 100 evens
  5 obs-take                   # take next 5
  obs-to-array
  # Result: array [200, 202, 204, 206, 208]
  # Only ~208 values were ever produced

> # Read CSV, filter, count
> s" /home/data.csv" s" ," obs-read-csv
    ' 0 array-get s" ERROR" sfind 0 >= obs-filter
    obs-count .
  # Count rows where first field contains "ERROR"
```

### Key Concepts

- **Lazy evaluation**: No work happens until a terminal operator is called
- **Short-circuit**: `obs-take` stops the source early
- **Budget-aware**: Every emission calls `ctx.tick()` for instruction/timeout enforcement
- **No closures needed**: `-with` variants carry a context value per node
- **Memory efficient**: Values flow through one at a time — no full materialization
- **LVFS sandboxed**: All file I/O paths resolved through the virtual filesystem
- **SSRF protected**: HTTP words enforce domain allowlists and fetch budgets

### Observable Word Index

#### By Category

| Category | Words |
|----------|-------|
| [Source](#source-operators--creating-observables) | `obs-from` `obs-of` `obs-empty` `obs-range` `obs-timer` `obs-interval` |
| [Transform](#transform-operators--modifying-values) | `obs-map` `obs-map-with` `obs-filter` `obs-filter-with` `obs-tap` |
| [Accumulation](#accumulation-operators--running-state) | `obs-scan` `obs-reduce` |
| [Limiting](#limiting-operators--controlling-flow) | `obs-take` `obs-skip` `obs-distinct` `obs-first` `obs-last` `obs-take-while` `obs-distinct-until` |
| [Combination](#combination-operators--merging-pipelines) | `obs-merge` `obs-concat` `obs-zip` `obs-start-with` |
| [Buffer/Composition](#buffer-and-composition-operators) | `obs-buffer` `obs-buffer-when` `obs-window` `obs-flat-map` `obs-switch-map` `obs-pairwise` |
| [Terminal](#terminal-operators--triggering-execution) | `obs-subscribe` `obs-to-array` `obs-count` `obs-reduce` `obs-to-string` |
| [Utility](#utility-operators) | `obs-tap` `obs-finalize` `obs-catch` |
| [Introspection](#introspection) | `obs?` `obs-kind` |
| [Temporal](#temporal-operators--wall-clock-time) | `obs-timer` `obs-interval` `obs-delay` `obs-timestamp` `obs-time-interval` `obs-delay-each` `obs-debounce-time` `obs-throttle-time` `obs-sample-time` `obs-timeout` `obs-audit-time` `obs-buffer-time` `obs-take-until-time` `obs-retry-delay` |
| [File I/O](#streaming-file-io) | `obs-read-bytes` `obs-read-lines` `obs-read-json` `obs-read-csv` `obs-readdir` `obs-write-file` `obs-append-file` |
| [HTTP](#streaming-http) | `obs-http-get` `obs-http-post` `obs-http-sse` |

#### Alphabetical

| Word | Stack Effect | Category |
|------|-------------|----------|
| `obs-append-file` | `( obs path -- )` | File I/O |
| `obs-audit-time` | `( obs window-us -- obs' )` | Temporal |
| `obs-buffer` | `( obs n -- obs' )` | Buffer |
| `obs-buffer-time` | `( obs window-us -- obs' )` | Temporal |
| `obs-buffer-when` | `( obs xt -- obs' )` | Buffer |
| `obs-catch` | `( obs xt -- obs' )` | Utility |
| `obs-concat` | `( obs-a obs-b -- obs )` | Combination |
| `obs-count` | `( obs -- n )` | Terminal |
| `obs-debounce-time` | `( obs quiet-us -- obs' )` | Temporal |
| `obs-delay` | `( obs delay-us -- obs' )` | Temporal |
| `obs-delay-each` | `( obs xt -- obs' )` | Temporal |
| `obs-distinct` | `( obs -- obs' )` | Limiting |
| `obs-distinct-until` | `( obs -- obs' )` | Limiting |
| `obs-empty` | `( -- obs )` | Source |
| `obs-filter` | `( obs xt -- obs' )` | Transform |
| `obs-filter-with` | `( obs xt ctx -- obs' )` | Transform |
| `obs-finalize` | `( obs xt -- obs' )` | Utility |
| `obs-first` | `( obs -- obs' )` | Limiting |
| `obs-flat-map` | `( obs xt -- obs' )` | Buffer |
| `obs-from` | `( array -- obs )` | Source |
| `obs-http-get` | `( url headers -- obs )` | HTTP |
| `obs-http-post` | `( url headers body -- obs )` | HTTP |
| `obs-http-sse` | `( url headers -- obs )` | HTTP |
| `obs-interval` | `( period-us -- obs )` | Temporal |
| `obs-kind` | `( obs -- str )` | Introspection |
| `obs-last` | `( obs -- obs' )` | Limiting |
| `obs-map` | `( obs xt -- obs' )` | Transform |
| `obs-map-with` | `( obs xt ctx -- obs' )` | Transform |
| `obs-merge` | `( obs-a obs-b max -- obs )` | Combination |
| `obs-of` | `( value -- obs )` | Source |
| `obs-pairwise` | `( obs -- obs' )` | Buffer |
| `obs-range` | `( start end -- obs )` | Source |
| `obs-read-bytes` | `( path chunk-size -- obs )` | File I/O |
| `obs-read-csv` | `( path separator -- obs )` | File I/O |
| `obs-read-json` | `( path -- obs )` | File I/O |
| `obs-read-lines` | `( path -- obs )` | File I/O |
| `obs-readdir` | `( path -- obs )` | File I/O |
| `obs-reduce` | `( obs xt init -- val )` | Terminal |
| `obs-retry-delay` | `( obs delay-us max -- obs' )` | Temporal |
| `obs-sample-time` | `( obs period-us -- obs' )` | Temporal |
| `obs-scan` | `( obs xt init -- obs' )` | Accumulation |
| `obs-skip` | `( obs n -- obs' )` | Limiting |
| `obs-start-with` | `( obs value -- obs' )` | Combination |
| `obs-subscribe` | `( obs xt -- )` | Terminal |
| `obs-switch-map` | `( obs xt -- obs' )` | Buffer |
| `obs-take` | `( obs n -- obs' )` | Limiting |
| `obs-take-until-time` | `( obs duration-us -- obs' )` | Temporal |
| `obs-take-while` | `( obs xt -- obs' )` | Limiting |
| `obs-tap` | `( obs xt -- obs' )` | Utility |
| `obs-throttle-time` | `( obs window-us -- obs' )` | Temporal |
| `obs-time-interval` | `( obs -- obs' )` | Temporal |
| `obs-timeout` | `( obs limit-us -- obs' )` | Temporal |
| `obs-timestamp` | `( obs -- obs' )` | Temporal |
| `obs-to-array` | `( obs -- array )` | Terminal |
| `obs-to-string` | `( obs -- string )` | Terminal |
| `obs-window` | `( obs n -- obs' )` | Buffer |
| `obs-write-file` | `( obs path -- )` | File I/O |
| `obs-zip` | `( obs-a obs-b -- obs )` | Combination |
| `obs?` | `( val -- bool )` | Introspection |

---

## Appendix T: Evolution and Selection

ETIL's core innovation: multiple implementations per word with runtime selection and evolutionary optimization. The selection engine chooses which implementation to execute; the evolution engine creates new implementations via AST-level genetic operators.

### Selection Words

These words control the runtime selection strategy. Available in both the REPL and MCP server sessions.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `select-strategy` | `( n -- )` | Set strategy: 0=latest, 1=weighted-random, 2=epsilon-greedy, 3=UCB1 |
| `select-epsilon` | `( f -- )` | Set epsilon for epsilon-greedy (0.0=always exploit, 1.0=always explore) |
| `select-off` | `( -- )` | Revert to deterministic latest-wins selection |

**Strategy 0 — Latest (default):** Always picks the most recently registered implementation. Identical to standard FORTH behavior. Zero overhead.

**Strategy 1 — Weighted Random:** Probability proportional to each implementation's `weight_` field. After evolution updates weights based on fitness, higher-fitness implementations are selected more often.

**Strategy 2 — Epsilon-Greedy:** With probability (1-epsilon), pick the highest-weight implementation. With probability epsilon, pick uniformly at random. Classic explore/exploit tradeoff.

**Strategy 3 — UCB1:** Upper Confidence Bound. Balances exploitation (high success rate) with exploration (under-tested implementations). Automatically explores new implementations.

### Evolution Words

These words drive the evolutionary pipeline. Available in both the REPL and MCP server sessions.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `evolve-register` | `( word-str tests-array -- flag )` | Register test cases for fitness evaluation |
| `evolve-register-pool` | `( word-str tests-array pool-array -- flag )` | Register test cases with a restricted word pool |
| `evolve-word` | `( word-str -- n )` | Run one generation of evolution, return children created |
| `evolve-all` | `( -- )` | Evolve all words with registered test cases |
| `evolve-status` | `( word-str -- n )` | Return number of generations evolved |
| `evolve-sub` | `( sub-str chain-str -- n )` | MCE: mutate sub-concept, evaluate chain for fitness |
| `evolve-chain` | `( chain-str subs-array gens -- )` | MCE: round-robin evolution of sub-concepts |
| `evolve-seed!` | `( n -- )` | Seed all evolution RNGs for reproducible runs |
| `evolve-dag-register` | `( root-str tests-array -- flag )` | Build ConceptDAG from call graph, register tests |
| `evolve-dag` | `( root-str generations -- )` | Run DAG-aware contribution-weighted evolution |
| `evolve-dag-show` | `( root-str -- )` | Print ConceptDAG structure with weights and stats |
| `evolve-contribution` | `( concept-str -- x )` | Query a concept's contribution weight |

### Semantic Tags and Bridges

Tags enable tiered substitution — mutations prefer words with matching tags before falling back to signature-compatible alternatives.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `evolve-tag` | `( word-str tag-str -- )` | Add a semantic tag to a word (e.g., `"+" "arithmetic"`) |
| `evolve-untag` | `( word-str -- )` | Remove all semantic tags from a word |
| `evolve-bridge` | `( from-type to-type bridge-word -- )` | Register a type conversion bridge word |

### Fitness Configuration

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `evolve-fitness-mode` | `( n -- )` | 0=binary pass/fail (default), 1=distance-based scoring |
| `evolve-fitness-alpha` | `( x -- )` | Distance scaling factor alpha (default 1.0) |
| `evolve-instruction-budget` | `( n -- )` | Max instructions per fitness evaluation (default 100000) |

Distance mode computes `1/(1 + alpha * |actual - expected|)` per test case, providing a smooth gradient instead of a binary cliff.

### Mutation Weights

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `evolve-mutation-weights` | `( sub per mov ctl grw shr -- )` | Set weights for the 6 mutation operators |

The six operators and their default weights:

| Operator | Default | Description |
|----------|---------|-------------|
| Substitute | 0.30 | Replace a word call with a semantically compatible alternative |
| Perturb | 0.15 | Add noise to a numeric literal (±1..3 for small integers, Gaussian for floats) |
| Move | 0.10 | Relocate a word call to a different position |
| Control flow | 0.10 | Wrap/unwrap a word in `if/then` |
| Grow | 0.20 | Insert a new word or literal into the AST |
| Shrink | 0.15 | Remove a node from the AST |

### The Evolution Pipeline

When `evolve-word` is called, the engine:

1. **Selects parents** from existing implementations (weighted by fitness)
2. **Decompiles** the parent's bytecode to an AST (Abstract Syntax Tree)
3. **Mutates** the AST using one of 6 weighted operators (substitute, perturb, move, control flow, grow, shrink)
4. **Repairs** type mismatches by inserting stack shuffling (`swap`, `rot`, `roll`) or bridge words from the BridgeMap
5. **Compiles** the mutated AST back to bytecode with structure markers
6. **Evaluates** the child against registered test cases (fitness = correctness + speed)
7. **Updates weights** on all implementations based on fitness scores
8. **Prunes** the weakest implementations if the population exceeds the limit

### Type-Directed Bridges

The **substitute** and **grow** mutation operators are type-aware. Before selecting a candidate word, the engine simulates the type stack at the mutation point and filters candidates using a promotion-aware compatibility matrix:

- **Integer on stack, word wants Float**: compatible (widening promotion)
- **Float on stack, word wants Integer**: incompatible (narrowing)
- **Boolean on stack, word wants Integer**: incompatible (undefined)
- **Unknown on either side**: always compatible (permissive)

Bridge words registered via `evolve-bridge` (e.g., `int->float`, `array-length`) appear naturally as type-legal candidates. Adjacent inverse bridges (e.g., `int->float` followed by `float->int`) are detected and rejected to prevent no-op cycles.

Type repair (step 4) can insert single-hop or multi-hop bridge words when a type mismatch can't be resolved by stack shuffling alone. For example, if the stack has `Array` but the next word needs `Float`, repair inserts `array-length` (Array→Integer) + `int->float` (Integer→Float).

Fitness evaluation errors from mutated code are routed to the evolution log file, not stderr.

### Type Bridge Back Propagation (TBBP)

Each `BridgeEdge` carries a learned weight. When type repair needs to select a bridge, it uses **weighted-random sampling** among available paths proportional to path weight (product of edge weights). After each mutation, weights update via exponential moving average:

```
weight = (1 - α) × weight + α × reward
reward = 1.0 if child_fitness > parent_fitness, else 0.0
```

Weights have a floor (default 0.05) to preserve exploration. The default learning rate `α = 0.1` and floor are configurable via `EvolutionConfig::tbbp_alpha` and `tbbp_min_weight`.

**Runtime toggle:** `evolve-tbbp-enabled?` ( flag -- ) turns TBBP on/off at runtime for ablation studies. When disabled, bridges use deterministic BFS selection (first path found). Default: on.

**Log output** (at `EvolveLogCategory::Bridge`):
```
[bridge] select: Matrix->Float chose 'mat-norm' (w=0.845, 4 candidates)
[bridge] update: 'mat-norm' 0.845 -> 0.861 (reward=1)
[bridge] summary: top 3 of 5 used bridges:
  int->float (w=0.923, 45 sel, 42 succ, 93% rate)
  array-length (w=0.872, 12 sel, 10 succ, 83% rate)
  mat-norm (w=0.845, 8 sel, 6 succ, 75% rate)
```

Weights reset per evolution run (no persistence). Useful for discovering which bridges are productive for a given problem domain.

### Modular Co-Evolution (MCE)

MCE decomposes a target word into sub-concepts and evolves each independently while evaluating chain-level fitness. Instead of evolving one monolithic word, users decompose the problem:

```til
: square-term  dup * ;                                     # sub-concept
: linear-term  3 * ;                                       # sub-concept
: offset       5 ;                                         # sub-concept
: target-fn    dup square-term swap linear-term + offset + ;  # chain

# Register tests on the chain word
s" target-fn" tests evolve-register drop

# Round-robin: each generation evolves one sub-concept via chain fitness
s" target-fn"
  array-new s" square-term" array-push s" linear-term" array-push s" offset" array-push
  100 evolve-chain
```

`evolve-chain` iterates N generations, calling `evolve-sub` on each sub-concept in round-robin order. `evolve-sub` mutates the sub-concept's impls but evaluates the chain word's test cases for fitness — so children are scored on their contribution to the full chain, not in isolation.

`evolve-seed!` seeds all evolution RNGs (engine, AST genetic ops, bytecode genetic ops, bridge map) for deterministic, reproducible benchmark runs.

## Appendix U: Evolution Logging

The evolution engine writes detailed diagnostic logs to timestamped files (`YYYYMMDDThhmmss-evolve.log`).

### Logging Control Words

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `evolve-log-dir` | `( dir-str -- )` | Set output directory for log files |
| `evolve-log-start` | `( level mask -- )` | Start logging at level (1=logical, 2=granular) with category bitmask |
| `evolve-log-stop` | `( -- )` | Stop logging and close the log file |
| `evolve-log-show-failed` | `( flag -- )` | Show rejected mutations in diff view (default: off) |

### Log Levels

| Level | Name | Description |
|-------|------|-------------|
| 1 | Logical | Per-generation summaries, child fitness, operator selection |
| 2 | Granular | All of logical plus baseline fitness, operator fallback chains, detail tags |

### Log Category Bitmask

Categories are independently enabled via the bitmask parameter to `evolve-log-start`. Use `-1` (all bits set) to enable everything.

| Bit | Value | Category | Description |
|-----|-------|----------|-------------|
| 0 | 0x0001 | Engine | Generation start/end, operator selection, impl boundaries |
| 1 | 0x0002 | Fitness | Child fitness scores |
| 2 | 0x0004 | Selection | Parent selection, pruning decisions |
| 3 | 0x0008 | Crossover | Crossover attempts and results |
| 4 | 0x0010 | Substitute | Word substitution with tier info (L1/L2/L3) |
| 5 | 0x0020 | Perturb | Constant perturbation (old → new) |
| 6 | 0x0040 | Move | Block move operations |
| 7 | 0x0080 | Control | Control flow wrap/unwrap |
| 8 | 0x0100 | Grow | Node insertion |
| 9 | 0x0200 | Shrink | Node removal |
| 10 | 0x0400 | Repair | Type repair success/failure |
| 11 | 0x0800 | Tag | Tag inference at `;` time |
| 12 | 0x1000 | Pool | Pool-restricted candidate selection |
| 13 | 0x2000 | Bridge | Bridge word insertion, cycle detection, substitute/grow bridge events |
| 14 | 0x4000 | Diff | Side-by-side before/after mutation diff |
| 15 | 0x8000 | ASTDump | Tree-format AST dumps |

### Diff View Format

When the Diff category is enabled, each mutation is displayed as a four-column table:

```
+-  MUTATION: substitute  ----------------------------------------
| BEFORE                      | AFTER                       | R | ANNOTATION
| dup                         | dup                         |   |
| +                           | *                           |   | <- changed
+- RESULT: success ------------------------------------------------------
```

The `R` column shows `*` for lines inserted or modified by type repair. The `ANNOTATION` column marks `<- changed`, `<- inserted`, or `<- removed` lines, with `(repair)` suffix when type repair was responsible.

### Impl Boundary Markers

When Diff or ASTDump is enabled, each mutation attempt is bracketed by:

```
===== MUTATE impl#351 (gen 0, 'target-fn') BEGIN =====
... mutation details ...
===== MUTATE impl#351 → impl#353 END (success) =====
```

### MLP Library Words (data/library/mlp.til)

The MLP library provides feedforward neural network training in pure TIL. Load with `include data/library/mlp.til`.

| Word | Stack Effect | Description |
|------|-------------|-------------|
| `make-layer` | `( fan_in fan_out act-xt act'-xt -- layer )` | Create a layer with Xavier-initialized weights |
| `make-network` | `( layer1 ... layerN n -- network )` | Assemble layers into a network |
| `forward` | `( X network -- Y )` | Forward pass |
| `train` | `( X Y network lr epochs -- network )` | Train for N epochs with SGD |
| `predict` | `( X network -- Y )` | Inference (alias for `forward`) |

### Examples

**Multiple implementations with weighted selection:**

```forth
: activate mat-relu ;     \ Implementation 1
: activate mat-sigmoid ;  \ Implementation 2

\ Set weights (via MCP set_weight tool or programmatically)
\ Then switch to weighted random selection:
1 select-strategy

\ Now "activate" probabilistically picks between ReLU and sigmoid
\ based on their weights. Higher fitness = higher selection probability.
```

**XOR neural network training:**

```forth
include data/library/mlp.til

7 random-seed
2 4 ' mat-relu ' mat-relu' make-layer
4 1 ' mat-sigmoid ' mat-sigmoid' make-layer
2 make-network
variable net
net !

\ Training data: X = 2x4 (inputs), Y = 1x4 (XOR outputs)
array-new
  array-new 0.0 array-push 0.0 array-push 1.0 array-push 1.0 array-push array-push
  array-new 0.0 array-push 1.0 array-push 0.0 array-push 1.0 array-push array-push
array->mat
variable X
X !

array-new
  array-new 0.0 array-push 1.0 array-push 1.0 array-push 0.0 array-push array-push
array->mat
variable Y
Y !

X @ Y @ net @ 1.0 5000 train net !
\ Output: Epoch 0: loss = 0.2648 ... Epoch 4500: loss = 0.00006
```

**Registering test cases for evolution:**

```forth
: double dup + ;

\ Build test cases as array of maps with "in" and "out" keys
array-new
  map-new s" in" array-new 3 array-push map-set
         s" out" array-new 6 array-push map-set array-push
  map-new s" in" array-new 5 array-push map-set
         s" out" array-new 10 array-push map-set array-push

s" double" swap evolve-register drop

\ Run 10 generations of evolution
10 0 do s" double" evolve-word drop loop

\ Check how many generations ran
s" double" evolve-status .   \ => 10
```

> **Note:** Evolution outcomes are **non-deterministic** — different random seeds produce different mutations, fitness scores, and surviving implementations. The error messages during `evolve-word` (e.g., "Error in 'mat-apply'") are expected — they come from mutated code that calls random words and fails fitness evaluation. The evolution engine discards these failed mutants automatically.

---

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

## References

- **FORTH 2012 Standard**: http://www.forth200x.org/documents/forth-2012.pdf
- **Threaded Interpretive Languages**: R.G. Loeliger, 1981
- **Genetic Programming**: Koza, "Genetic Programming", 1992
- **MCP Specification**: https://modelcontextprotocol.io/specification/2025-11-25
- **JSON-RPC Specification**: https://www.jsonrpc.org/specification

## License

BSD-3-Clause
