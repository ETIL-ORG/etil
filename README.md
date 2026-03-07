# Evolutionary Threaded Interpretive Language (ETIL)

A modern reimagining of FORTH with evolutionary programming, JIT compilation, and machine learning-based optimization.

## Overview

ETIL replaces FORTH's traditional linear dictionary with a dynamic DAG structure featuring probability-distributed
weighted word selection. The system enables multiple implementations per word concept with runtime rewriting, chaining,
and inheritance capabilities.

## Features

- **Dynamic DAG Dictionary**: Multiple implementations per word concept
- **Probability-Weighted Selection**: ML-based implementation selection using decision trees and multi-armed bandits (
  planned)
- **JIT Compilation**: LLVM-based native code generation (planned)
- **Genetic Evolution**: Automatic code optimization through mutation and crossover (planned)
- **Word Metadata**: Attach structured metadata (text, markdown, HTML, code, JSON, JSONL) to words for documentation,
  code generation, and AI integration
- **Dynamic Heap Objects**: Reference-counted strings, arrays, and byte buffers with 30 primitive words. `s+` auto-converts integers and floats to strings for easy formatting
- **Self-Hosting Builtins**: Words implemented in ETIL source, loaded on startup — "ETIL written in ETIL"
- **Built-In Help System**: `help <word>` displays description, stack effect, and category for any word — including
  handler words (`:`, `if`, `do`, `."`, etc.); all help text in editable `data/help.til` as single source of truth
- **Debug Introspection**: `dump` deep-inspects TOS without consuming it (recursive, with truncation); `see <word>`
  decompiles word definitions showing bytecode, primitives, or handler status
- **MCP Server**: Model Context Protocol server for programmatic AI interaction — execute TIL code, inspect
  dictionary/stack, set evolutionary weights, monitor per-session CPU/memory usage via structured JSON-RPC 2.0. HTTP
  transport with real-time SSE notification delivery. Dual-mode authentication: JWT with role-based access control
  (preferred) or API key (fallback). Runs sandboxed in Docker with DoS mitigation (instruction budget, execution
  deadline, call depth limits, input/output size caps, Docker CPU/memory/PID limits, nginx connection/request limits).
- **MCP Client TUI**: Python/Textual terminal UI for interactive MCP sessions — triple-window layout with JSON-RPC
  traffic, interpreter I/O, and notifications. Full-screen help browser (F1) with categorized word index, manpages,
  clickable cross-references, and navigation history. OAuth login (`/login`, `/logout`, `/whoami`) with JWT caching.
  Connects via HTTP.
- **LVFS (Little Virtual File System)**: Virtual filesystem with `/home` (writable, per-session) and `/library`
  (read-only, shared) under a virtual root `/`. Shell-like navigation: `cwd`, `cd`, `ls`, `ll`, `lr`, `cat`
- **Interactive REPL**: Line editing, persistent history, tab-completion (via replxx)
- **Container Security**: Docker sandbox with read-only filesystem, no-new-privileges, CPU/memory/PID limits.
  Optional runtime intrusion detection recommended (e.g., Falco eBPF)
- **Outbound HTTP Client**: `http-get` word fetches data from external URLs with SSRF blocklist, domain allowlist,
  per-session fetch budgets, response size limits, and opaque byte return (prevents code injection via `evaluate`).
  HTTPS support via OpenSSL. Design doc: `docs/claude-knowledge/20260227-interpreter-outbound-network-access.md`
- **JWT Authentication**: JWT-based authentication with role-based access control (RBAC) -- per-user permissions for
  HTTP domains, instruction budgets, file I/O, and session limits. Dual-mode auth: JWT preferred, API key fallback
  for backward compatibility
- **OAuth Device Flow**: Server-side OAuth provider integration (GitHub + Google) enabling real device authorization
  grant login. Three endpoints: `/auth/device` (initiate), `/auth/poll` (poll for grant), `/auth/token` (exchange
  provider access token for ETIL JWT). Stateless — no provider tokens stored on server. TUI client supports
  `/login [provider]`, `/logout`, and `/whoami` commands with background device code polling and JWT caching
- **MongoDB Integration**: Optional MongoDB client with multi-connection config (5 TIL primitives:
  `mongo-find`, `mongo-count`, `mongo-insert`, `mongo-update`, `mongo-delete` — all accept String, Json, or Map).
  `mongo-find` returns `HeapJson` directly. Unified `MongoQueryOptions` with skip/limit/sort/projection/hint/collation/upsert. Server-side `count_documents()`.
  User management and audit logging via `etil::aaa` namespace. CMake flag: `ETIL_BUILD_MONGODB=ON` (default OFF)
- **Lock-Free Execution**: Thread-safe, concurrent execution engine
- **Hardware Acceleration**: SIMD vectorization and GPU support (planned)

## Architecture

### Core Components

1. **WordImpl** - Individual word implementation with performance profiling and intrusive reference counting
2. **ExecutionContext** - Thread-local execution environment with vector-backed data stacks, configurable output/error
   streams, execution limits (instruction budget, call depth, deadline, cancellation)
3. **Dictionary** - Thread-safe word lookup with `absl::Mutex` and `absl::flat_hash_map`
4. **Primitives** - 202 built-in words (arithmetic, stack, comparison, logic, I/O, memory, math, PRNG, system, time, string, array,
   byte, map, file I/O, HTTP, MongoDB, execution tokens, conversion, input-reading, dictionary-ops, metadata-ops, help, debug)
5. **ByteCode** - Compiled word bodies with inner interpreter for colon definitions and control flow; per-word data fields with lazy `DataFieldRegistry` registration and bounds-checked DataRef resolution
6. **Interpreter** - Outer interpreter handling all language semantics (parsing, compilation, dispatch) with dual output
   streams (`out_` for normal output, `err_` for errors). Handler logic is extracted into three handler set classes (
   `InterpretHandlerSet`, `CompileHandlerSet`, `ControlFlowHandlerSet`) with a shared abstract base, decoupled from the
   Interpreter via dependency injection
7. **Self-Hosted Builtins** - Words (`variable`, `constant`, `forget`, `forget-all`, `meta!`, `meta@`, `meta-del`, `meta-keys`,
   `impl-meta!`, `impl-meta@`, etc.) implemented in ETIL source (`data/builtins.til`), loaded on startup via
   `load_startup_files()`. Metadata words are simple stack-based aliases for C++ primitives
8. **HeapObject System** - Reference-counted heap objects (strings, arrays, byte buffers) with explicit refcount
   management in primitives
9. **MetadataMap** - Key-value metadata store for attaching text, markdown, HTML, code, JSON, or JSONL to words
10. **REPL** - Interactive shell with replxx-based line editing, persistent command history (
    `~/.etil/repl/history.txt`), tab-completion, color themes, `--quiet` pipe-friendly mode, and dual-stream error
    separation (errors→stderr, output→stdout)
11. **MCP Server** - Model Context Protocol server (`etil_mcp` library) with 10 tools and 4 resources,
    Docker-sandboxed per security rules. HTTP transport uses Streamable HTTP with real-time SSE streaming (chunked
    transfer) for notifications, session management, dual-mode authentication (JWT with RBAC preferred, API key
    fallback), and origin validation. Per-session profiling tracks CPU time, wall time, RSS, and interpreter metrics.

### Implemented Primitives (202 words)

| Category                   | Words                                                                                                                            |
|----------------------------|----------------------------------------------------------------------------------------------------------------------------------|
| Arithmetic                 | `+` `-` `*` `/` `mod` `/mod` `negate` `abs` `max` `min`                                                                          |
| Stack                      | `dup` `drop` `swap` `over` `rot` `pick` `nip` `tuck` `depth` `?dup` `roll`                                                       |
| Comparison                 | `=` `<>` `<` `>` `<=` `>=` `0=` `0<` `0>`                                                                                        |
| Logic                      | `true` `false` `not` `bool` `and` `or` `xor` `invert` `lshift` `rshift` `lroll` `rroll`                                          |
| I/O                        | `.` `.s` `cr` `emit` `space` `spaces` `words`                                                                                    |
| Memory                     | `create` `,` `@` `!` `allot` `immediate`                                                                                         |
| Math                       | `sqrt` `sin` `cos` `tan` `asin` `acos` `atan` `atan2` `log` `log2` `log10` `exp` `pow` `ceil` `floor` `round` `trunc` `fmin` `fmax` `pi` `f~` `random` `random-seed` `random-range` |
| String                     | `type` `s.` `s+` `s=` `s<>` `slength` `substr` `strim` `sfind` `sreplace` `ssplit` `sjoin` `sregex-find` `sregex-replace` `sregex-search` `sregex-match` `staint` `sprintf` |
| Array                      | `array-new` `array-push` `array-pop` `array-get` `array-set` `array-length` `array-shift` `array-unshift` `array-compact` `array-reverse` |
| ByteArray                  | `bytes-new` `bytes-get` `bytes-set` `bytes-length` `bytes-resize` `bytes->string` `string->bytes`                                |
| Map                        | `map-new` `map-set` `map-get` `map-remove` `map-length` `map-keys` `map-values` `map-has?`                                       |
| LVFS                       | `cwd` `cd` `ls` `ll` `lr` `cat`                                                                                                  |
| System                     | `sys-semver` `sys-timestamp` `sys-datafields` `sys-notification` `user-notification` `abort`                                      |
| Time                       | `time-us` `us->iso` `us->iso-us` `us->jd` `jd->us` `us->mjd` `mjd->us` `sleep`                                                  |
| Input Reading              | `word-read` `string-read-delim`                                                                                                  |
| Dictionary Ops             | `dict-forget` `dict-forget-all` `file-load` `include` `library` `evaluate` `marker` `marker-restore`                              |
| Metadata Ops               | `dict-meta-set` `dict-meta-get` `dict-meta-del` `dict-meta-keys` `impl-meta-set` `impl-meta-get`                                 |
| Help                       | `help`                                                                                                                           |
| Execution                  | `'` `execute` `xt?` `>name` `xt-body`                                                                                            |
| Conversion                 | `int->float` `float->int` `number->string` `string->number`                                                                      |
| Debug                      | `dump` `see`                                                                                                                     |
| File I/O (async)           | `exists?` `read-file` `write-file` `append-file` `copy-file` `rename-file` `lstat` `readdir` `mkdir` `mkdir-tmp` `rmdir` `rm` `truncate` |
| File I/O (sync)            | `exists-sync` `read-file-sync` `write-file-sync` `append-file-sync` `copy-file-sync` `rename-sync` `lstat-sync` `readdir-sync` `mkdir-sync` `mkdir-tmp-sync` `rmdir-sync` `rm-sync` `truncate-sync` |
| HTTP Client                | `http-get`                                                                                                                       |
| MongoDB                    | `mongo-find` `mongo-count` `mongo-insert` `mongo-update` `mongo-delete` |
| Parsing (Interpreter)      | `."` `s"` `:` `;`                                                                                                                |
| Self-hosted (builtins.til) | `variable` `constant` `forget` `forget-all` `meta!` `meta@` `meta-del` `meta-keys` `impl-meta!` `impl-meta@` `time-iso` `time-iso-us` `time-jd` `time-mjd` `1+` `1-` `-rot` |
| Control (compile-only)     | `if` `else` `then` `do` `loop` `+loop` `i` `j` `begin` `until` `while` `repeat` `again` `>r` `r>` `r@` `leave` `exit` `recurse` `does>` `[']` |

### Data Structures

- **ValueStack** - Vector-backed stack for single-threaded execution contexts
- **Concurrent HashMap** - Thread-safe dictionary access via Abseil
- **DAG** - Directed acyclic graph for word dependencies (planned)

## Requirements

- **Compiler**: GNU C++ 13 or later (C++20)
- **Build System**: CMake 3.20+, Ninja
- **Dependencies** (fetched automatically by CMake):
    - Abseil C++ (containers, synchronization)
    - Google Test (testing)
    - Google Benchmark (performance testing)
    - spdlog (logging)
    - nlohmann/json (metadata serialization)
    - replxx (REPL line editing, history, tab-completion)
    - cpp-httplib (MCP HTTP transport)
    - jwt-cpp (JWT authentication, optional — requires `ETIL_BUILD_JWT=ON`)

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

- `ETIL_BUILD_TESTS` - Build unit tests (default: ON)
- `ETIL_BUILD_EXAMPLES` - Build example programs (default: ON)
- `ETIL_ENABLE_PROFILING` - Enable profiling support (default: ON)
- `ETIL_BUILD_HTTP_CLIENT` - Build HTTP client primitives (`http-get`) (default: OFF; requires OpenSSL)
- `ETIL_BUILD_JWT` - Build JWT authentication support (default: OFF; fetches jwt-cpp + OpenSSL)

### Docker (MCP Server)

The MCP server must run inside Docker per project security rules:

```bash
# Build the Docker image (runtime-only — binary must be pre-built)
# Stage the release binary first:
#   mkdir -p .docker-stage/bin && cp build/bin/etil_mcp_server .docker-stage/bin/
docker build -t etil-mcp .

# Run the MCP server (API key auth)
docker run -d --rm --read-only \
  -p 127.0.0.1:8080:8080 \
  -e ETIL_MCP_API_KEY=your-secret-key \
  --tmpfs /tmp:size=10M \
  etil-mcp --port 8080

# Run the MCP server (JWT auth with RBAC)
# See data/auth-config/*.json.example for role/permission configuration
docker run -d --rm --read-only \
  -p 127.0.0.1:8080:8080 \
  -e ETIL_AUTH_CONFIG=/etc/etil \
  -v /path/to/auth-config:/etc/etil:ro \
  --tmpfs /tmp:size=10M \
  etil-mcp --port 8080

# Test with curl
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer your-secret-key" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"curl","version":"1.0"}}}'
```

## Running

### Interactive REPL

```bash
./build-debug/bin/etil_repl            # interactive mode
echo '42 . cr' | ./build-debug/bin/etil_repl -q   # pipe-friendly quiet mode
```

**`--quiet` / `-q`**: Pipe-friendly mode — suppresses banner, prompt, stack status, and goodbye message. Infers
`--color=never` unless `--color` is explicitly given. Only primitive I/O (`.`, `cr`, `emit`, `space`) appears on stdout;
errors go to stderr.

```
Evolutionary TIL REPL v0.5.0 (built 2026-02-12 16:48:07 UTC)
Type '/help' for commands, '/quit' to exit

> 42 dup +
(1) 84
> 3 4 + .
7
(0)
> : double dup + ;
> 21 double .
42
(0)
> : absval dup 0< if negate then ;
> -5 absval .
5
(0)
> : sum-to 0 swap 0 do i + loop ;
> 10 sum-to .
45
(0)
> create myvar 0 ,
> 42 myvar !
> myvar @ .
42
(0)
> pi .
3.14159
(0)
> s" hello" s" world" s+ type
helloworld
(0)
> s" hello, world" s" ," ssplit array-length .
2
(0)
> 16 bytes-new bytes-length .
16
(0)
> /quit
Goodbye!
```

### MCP Server

```bash
# Run inside Docker (required by security rules)
docker build -t etil-mcp .
docker run -d --rm --read-only -p 127.0.0.1:8080:8080 \
  -e ETIL_MCP_API_KEY=your-secret-key etil-mcp --port 8080

# Claude Code integration — HTTP (add to project .mcp.json):
# {
#   "mcpServers": {
#     "etil": {
#       "url": "https://your-server.example.com/mcp",
#       "headers": {
#         "Authorization": "Bearer <api-key>"
#       }
#     }
#   }
# }
```

### MCP Client TUI

An interactive terminal UI for the ETIL MCP server, built with Python 3.12 and Textual:

```bash
# Install via .deb package (Ubuntu 24.04)
scripts/build-tui-deb.sh --output /tmp
sudo dpkg -i /tmp/etil-tui_*.deb
etil-tui --connect https://your-server.com/mcp

# Or run from source
cd tools/mcp-client
./setup.sh          # Create venv, install textual
./run.sh            # Launch TUI
```

The TUI provides a triple-window layout:
- **Left panel** — JSON-RPC message log with syntax-highlighted pretty-print
- **Right panel** — Interpreter output with command input and history (Up/Down)
- **Bottom bar** — Color-coded notifications (connection, errors, warnings)

Commands: `/stack`, `/reset`, `/stats`, `/info <word>`, `/load <path>`, `/verbose [on|off]`, `/logfile [path]`, `/logjson [path]`, `/log`, `/help [word]`, `/clear`, `/quit`.
Keybindings: F1 (help browser), F2 (toggle layout), F3 (notification fullscreen), F4 (scroll top/bottom), Tab (cycle panels), Ctrl+Q (quit), Ctrl+D (dismiss), Ctrl+L (clear), Escape (focus input). F2/F3 respond immediately even during long-running server requests.

All panels re-render their content at the correct width when the layout changes (F2) or the terminal is resized.

Server responses are automatically unpacked from JSON: output in green, errors in red (with notification), stack in yellow. Session logging starts automatically on startup (plain-text `.log` and JSONL `.json`), with old logs rotated (max 5 of each type). Use `--nologs` to disable auto-logging or `--norotate` to keep all old logs. `/log` toggles logging on/off during a session.

**Script execution**: `--exec <file|URL>` runs a `.til` file through the TUI pipeline and exits; `--execux <file|URL>` runs the script then stays interactive. Lines are dispatched through the same path as interactive input, and all output is captured in log files.

The **help browser** (F1 or `/help`) provides:
- Categorized word index with clickable links and stack effects
- Word manpages with description, stack effect, live examples (auto-executed in sandbox), implementations, type signatures, and see-also references
- Category pages with filtered word lists
- Breadcrumb navigation (Index > Category > Word) with clickable links
- Back navigation (Backspace), jump-to-index (i), next/prev word in category (n/p)
- Real-time search (/) filtering words by name or description

### Language Features

All language semantics are implemented in the `Interpreter` class (core library), allowing the same interpreter to be
driven by files, sockets, embedded APIs, or the interactive REPL.

- **Number literals**: Integers and floating-point values pushed to stack
- **Colon definitions**: `: name ... ;` compiles user-defined words (multi-line supported)
- **Control flow**: `if`/`else`/`then`, `do`/`loop`/`+loop`/`i`/`j`/`leave`, `begin`/`until`/`again`, `begin`/`while`/`repeat`, `>r`/`r>`/`r@`, `exit`, `recurse`
- **Defining words**: `create`, `does>`, `,`, `@`, `!`, `allot`; self-hosted: `variable`, `constant`
- **Comments**: `#` strips the rest of the line
- **Dot-quote**: `." text"` prints inline text
- **S-quote**: `s" text"` creates a heap-allocated string on the stack
- **Strings**: Immutable, reference-counted UTF-8 strings with concatenation, comparison, substring, trim, find,
  replace, split, join, regex
- **Arrays**: Dynamic, reference-counted arrays holding any value type with push/pop, get/set, shift/unshift, reverse
- **Byte buffers**: Raw byte arrays for I/O with get/set, resize, conversion to/from strings
- **Forget**: `forget name` removes the latest definition (reveals previous); `forget-all name` removes all
  implementations
- **Built-in help**: `help <word>` shows description, stack effect, and category; `/help <word>` in the REPL
- **Debug introspection**: `dump` deep-inspects TOS without consuming it; `see <word>` decompiles word definitions
- **File inclusion**: `include path.til` loads and interprets a file
- **TIL integration tests**: End-to-end tests via `.til` files loaded into the REPL, auto-discovered by CTest
- **Type promotion**: Int op Int = Int, any Float = Float
- **First-class Booleans**: `true`/`false` are opaque Boolean values. Comparisons and predicates return Boolean. Control flow requires Boolean. Arithmetic rejects Boolean.
- **Word metadata**: Attach structured metadata to word concepts and implementations (stack-based conditional returns)

### MCP Server Examples

The following examples were executed remotely via the ETIL MCP server running on the production server. Each example defines a word
using `: name ... ;` (colon definition), then calls it. The MCP `interpret` tool returns captured output, errors, and
stack state as structured JSON.

#### Example 1: Fibonacci Sequence

Generate the first N Fibonacci numbers using a DO loop. The algorithm keeps two running values on the stack (`a` and
`b`), printing `a` each iteration and replacing the pair with `(b, a+b)`.

```
> : fib-n 0 1 rot 0 do over . swap over + loop drop drop ;
                                         Define 'fib-n': start with 0 and 1,
                                         loop N times printing and advancing
> 10 fib-n
0 1 1 2 3 5 8 13 21 34                  First 10 Fibonacci numbers
```

**How it works:**

- `0 1 rot` — push seed values 0 and 1, move N to the top for the loop limit
- `0 do ... loop` — loop N times (i = 0 to N-1)
- `over .` — print `a` (the second-from-top value)
- `swap over +` — compute next: swap to get `(b, a)`, copy `b`, add to get `(b, a+b)`
- `drop drop` — clean up the two remaining values

#### Example 2: Factorial

Compute N! using a DO loop that multiplies an accumulator by each integer from 1 to N. ETIL uses 64-bit integers, so
`20!` (2.4 quintillion) fits without overflow.

```
> : factorial 1 swap 1 + 1 do i * loop ;
                                         Define 'factorial': accumulate product
> 5 factorial .
120                                      5! = 120
> 10 factorial .
3628800                                  10! = 3,628,800
> 20 factorial .
2432902008176640000                      20! = 2.43 × 10^18 (fits in int64)
```

**How it works:**

- `1 swap` — push accumulator 1, move N to top
- `1 + 1 do ... loop` — loop from 1 to N (inclusive; DO uses exclusive upper bound, hence `1 +`)
- `i *` — multiply accumulator by loop index

#### Example 3: Newton's Method Square Root

Iterative square root approximation using Newton's method: x_{n+1} = (x_n + S/x_n) / 2. Converges to machine precision
in ~20 iterations for any positive input.

```
> : my-sqrt dup 0.5 * 20 0 do over over / + 0.5 * loop swap drop ;
                                         Define 'my-sqrt': Newton's method
> 2.0 my-sqrt .
1.41421                                  √2 ≈ 1.41421 (5 sig figs displayed)
> 144.0 my-sqrt .
12                                       √144 = 12 (exact)
> 1000000.0 my-sqrt .
1000                                     √1,000,000 = 1000 (exact)
```

**How it works:**

- `dup 0.5 *` — initial guess = S/2
- `20 0 do ... loop` — 20 Newton iterations (more than enough for `double` precision)
- `over over / + 0.5 *` — compute `(guess + S/guess) / 2`
- `swap drop` — discard the original S, keep the final approximation

#### Example 4: String Reversal

Reverse a string by iterating character-by-character and prepending each to an accumulator. Uses `slength`, `substr`,
and `s+` (string concatenation).

```
> : reverse-str s" " strim swap dup slength 0 do dup i 1 substr rot s+ swap loop drop ;
                                         Define 'reverse-str': character-by-character reversal
> s" Hello, World!" reverse-str s.
!dlroW ,olleH                            Reversed greeting
> s" racecar" reverse-str s.
racecar                                  Palindrome stays the same
> s" ETIL" reverse-str s.
LITE                                     ETIL backwards is LITE
```

**How it works:**

- `s" " strim` — create an empty string (trim a single space) as the accumulator
- `swap dup slength` — bring the input string below and get its length
- `0 do ... loop` — loop over each character index
- `dup i 1 substr` — extract the i-th character (1-char substring)
- `rot s+` — rotate the accumulator to the top; `s+` concatenates `char + accumulator` (prepend)
- `swap` — keep accumulator below the input string for the next iteration
- `drop` — discard the original string, leaving the reversed result

#### Example 5: FizzBuzz

The classic FizzBuzz challenge: for each number 1 to N, print "FizzBuzz" if divisible by 15, "Fizz" if divisible by 3, "
Buzz" if divisible by 5, otherwise the number itself.

```
> : fizzbuzz 1 + 1 do i 15 mod 0= if ." FizzBuzz " else i 3 mod 0= if ." Fizz " else i 5 mod 0= if ." Buzz " else i . then then then loop ;
                                         Define 'fizzbuzz': nested conditionals
> 20 fizzbuzz
1 2 Fizz 4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz 16 17 Fizz 19 Buzz
```

**How it works:**

- `1 + 1 do ... loop` — loop from 1 to N (inclusive)
- `i 15 mod 0=` — check divisibility by 15 first (covers both 3 and 5)
- Nested `if`/`else`/`then` — FORTH-style conditionals; the most specific test (15) comes first
- `." FizzBuzz "` — dot-quote prints inline text
- `i .` — fallback: print the number itself

#### Session Profiling

After running examples, the MCP `get_session_stats` tool shows cumulative resource usage for the session:

```json
{
  "interpretCallCount": 7,
  "dictionaryConceptCount": 119,
  "dataStackDepth": 0,
  "currentRssMb": 6.47,
  "peakRssMb": 6.47,
  "sessionUptimeMs": 106663
}
```

- **interpretCallCount** — total `interpret` calls since last reset
- **dictionaryConceptCount** — 114 builtins + 5 user-defined words = 119
- **currentRssMb** — process resident set size (the entire Docker container)
- **sessionUptimeMs** — milliseconds since session start or last reset

### Word Metadata

ETIL supports attaching metadata to words at two levels: **concept-level** (shared across all implementations of a word)
and **implementation-level** (specific to one implementation). Metadata can be in any of six formats: `text`,
`markdown`, `html`, `code`, `json`, `jsonl`.

All metadata words are **stack-based** — they consume all arguments from the data stack and use
**conditional returns**:

- **Success**: TOS = `-1` (true), with result values below when applicable
- **Failure**: TOS = `0` (false), no additional values

#### Setting and Retrieving Metadata

```
> : double dup + ;
> s" double" s" description" s" text" s" Doubles the top-of-stack value" meta!
> .
-1
> s" double" s" description" meta@
> . type
-1 Doubles the top-of-stack value
> s" double" s" usage" s" markdown" s| ## Usage\n`n -- 2n` | meta!
> drop   # discard success flag
> s" double" meta-keys
> . array-length .
-1 2
```

#### Typical Workflow: Define Word with Metadata

```
> : square dup * ;
> s" square" s" description" s" text" s" Squares a number" meta!
> s" square" s" stack-effect" s" text" s" ( n -- n*n )" meta!
> s" square" s" example" s" code" s" 5 square .  \ prints 25" meta!
> drop drop drop   # discard three success flags

> s" square" s" stack-effect" meta@
> if type cr then
( n -- n*n )

> s" square" meta-keys
> if array-length . drop else ." no keys" then
3
```

#### Implementation-Level Metadata

Metadata can also be attached to a specific implementation (the first/default one):

```
> : fast-sort dup ;   # placeholder
> s" fast-sort" s" algorithm" s" text" s" quicksort with AVX2" impl-meta!
> drop
> s" fast-sort" s" algorithm" impl-meta@
> if type cr then
quicksort with AVX2
```

#### Removing Metadata

```
> s" square" s" example" meta-del
> .
-1
> s" square" s" example" meta@
> .
0
```

#### Error Handling

```
> s" nonexistent" s" desc" s" text" s" hello" meta!
> .
0
> s" nonexistent" s" desc" meta@
> .
0
> s" square" s" desc" s" badformat" s" hello" meta!
> .
0
```

#### Metadata Word Reference

| Word         | Stack Effect                                       | Success                      | Failure    |
|--------------|----------------------------------------------------|------------------------------|------------|
| `meta!`      | `( word-str key-str fmt-str content-str -- bool )`  | `true`                       | `false`    |
| `meta@`      | `( word-str key-str -- content-str bool )`          | `content-str true`           | `false`    |
| `meta-del`   | `( word-str key-str -- bool )`                      | `true`                       | `false`    |
| `meta-keys`  | `( word-str -- array bool )`                        | `array-of-strings true`      | `false`    |
| `impl-meta!` | `( word-str key-str fmt-str content-str -- bool )`  | `true`                       | `false`    |
| `impl-meta@` | `( word-str key-str -- content-str bool )`          | `content-str true`           | `false`    |

Supported formats: `text`, `markdown`, `html`, `code`, `json`, `jsonl`

### REPL Meta Commands

The REPL features line editing, persistent command history, and tab-completion (via replxx). Available meta commands:

- `/help [word]` — Show help for a word, or list available commands
- `/quit` / `/exit` — Exit the REPL
- `/clear` — Clear the data stack
- `/words` — List all dictionary words
- `/history` — Show command history
- `/dark` — Switch to dark color theme
- `/light` — Switch to light color theme

### Running Tests

```bash
ctest --test-dir build-debug --output-on-failure
```

### Running Benchmarks

```bash
./build-debug/bin/etil_benchmark
```

## Project Structure

```
evolutionary-til/
├── CMakeLists.txt              # Root build configuration
├── cmake/
│   └── Dependencies.cmake      # External dependency management
├── include/etil/
│   ├── core/
│   │   ├── word_impl.hpp       # Word implementation with profiling
│   │   ├── execution_context.hpp # Execution environment + dictionary + last_created + out_/err_ streams + DataFieldRegistry + execution limits
│   │   ├── dictionary.hpp      # Thread-safe dictionary + forget_word/forget_all
│   │   ├── primitives.hpp      # 125 primitive word declarations
│   │   ├── version.hpp.in     # Build-time version header template
│   │   ├── compiled_body.hpp   # Instruction, ByteCode, execute_compiled
│   │   ├── interpreter.hpp    # Outer interpreter (all language semantics)
│   │   ├── handler_set.hpp   # HandlerSetBase + 3 concrete handler set classes
│   │   ├── metadata.hpp       # MetadataFormat, MetadataEntry, MetadataMap
│   │   ├── metadata_json.hpp  # JSON serialization for metadata
│   │   ├── heap_object.hpp    # HeapObject base + value_addref/value_release
│   │   ├── heap_string.hpp    # Immutable HeapString (flexible array member)
│   │   ├── heap_array.hpp     # Dynamic HeapArray (bounds-checked)
│   │   ├── heap_byte_array.hpp # Raw byte buffer HeapByteArray
│   │   ├── heap_map.hpp       # Hash map HeapMap (string keys → Value)
│   │   ├── heap_json.hpp      # JSON container HeapJson (wraps nlohmann::json)
│   │   ├── json_primitives.hpp # JSON primitive registration
│   │   ├── heap_primitives.hpp # String/array/byte primitive registration
│   │   └── value_stack.hpp    # Vector-backed stack for execution contexts
│   └── mcp/
│       ├── json_rpc.hpp       # JSON-RPC 2.0 types and parsing
│       ├── http_transport.hpp # HTTP transport (Streamable HTTP with SSE)
│       ├── mcp_server.hpp     # MCP server with tool/resource dispatch
│       ├── auth_config.hpp    # AuthConfig: role/permission/user mappings (gated by ETIL_BUILD_JWT)
│       ├── jwt_auth.hpp       # JwtAuth: RS256 JWT minting and validation (gated by ETIL_BUILD_JWT)
│       ├── oauth_provider.hpp # OAuthProvider ABC + data structs (gated by ETIL_BUILD_JWT)
│       ├── oauth_github.hpp   # GitHubProvider: GitHub device flow (gated by ETIL_BUILD_JWT)
│       └── oauth_google.hpp   # GoogleProvider: Google device flow (gated by ETIL_BUILD_JWT)
├── src/core/                   # Implementation files
│   ├── word_impl.cpp
│   ├── execution_context.cpp
│   ├── dictionary.cpp
│   ├── primitives.cpp          # 125 primitives (core + heap + map + input/dict/meta + help + debug + sys-datafields + sys-notification + time + Julian Date + PRNG + evaluate)
│   ├── compiled_body.cpp       # Inner interpreter + PushString opcode
│   ├── interpreter.cpp         # Outer interpreter + s" parsing
│   ├── interpret_handlers.cpp  # InterpretHandlerSet (3 handlers: words, .", s")
│   ├── compile_handlers.cpp    # CompileHandlerSet (4 handlers)
│   ├── control_flow_handlers.cpp # ControlFlowHandlerSet (11 handlers)
│   ├── metadata.cpp            # MetadataMap implementation
│   ├── metadata_json.cpp       # JSON serialization (nlohmann/json)
│   ├── string_primitives.cpp   # 15 string primitives (incl. staint)
│   ├── array_primitives.cpp    # 10 array primitives + ssplit/sjoin
│   └── byte_primitives.cpp     # 7 byte array primitives
├── src/lvfs/                  # LVFS (Little Virtual File System)
│   ├── lvfs.cpp               # Lvfs class implementation
│   └── lvfs_primitives.cpp    # 6 LVFS primitives (cwd, cd, ls, ll, lr, cat)
├── src/mcp/                   # MCP server implementation
│   ├── json_rpc.cpp
│   ├── http_transport.cpp     # HTTP transport (Streamable HTTP with SSE)
│   ├── mcp_server.cpp
│   ├── tool_handlers.cpp      # 10 MCP tools + SessionStats
│   ├── resource_handlers.cpp  # 4 MCP resources
│   ├── auth_config.cpp        # AuthConfig JSON parser, role/permission resolution (gated by ETIL_BUILD_JWT)
│   ├── jwt_auth.cpp           # JWT minting/validation using jwt-cpp (gated by ETIL_BUILD_JWT)
│   ├── oauth_github.cpp       # GitHub device flow provider (gated by ETIL_BUILD_JWT)
│   └── oauth_google.cpp       # Google device flow provider (gated by ETIL_BUILD_JWT)
├── tests/unit/                 # Unit tests (338 core + 82 MCP + 12 HTTP + 6 new DataRef tests)
│   ├── test_word_impl.cpp
│   ├── test_dictionary.cpp
│   ├── test_primitives.cpp
│   ├── test_compiled_body.cpp  # Colon defs, control flow, create/does>
│   ├── test_interpreter.cpp   # Interpreter integration tests
│   ├── test_metadata.cpp      # Metadata, JSON serialization, interpreter words
│   ├── test_heap_objects.cpp  # HeapObject lifecycle, refcounting
│   ├── test_string_primitives.cpp # String word tests
│   ├── test_array_primitives.cpp  # Array word tests
│   ├── test_byte_primitives.cpp   # Byte word tests
│   ├── test_handler_sets.cpp  # Handler set isolation tests
│   ├── test_jwt_auth.cpp     # JWT auth tests (AuthConfig, role resolution, JWT round-trip) — gated by ETIL_BUILD_JWT
│   └── test_oauth_provider.cpp # OAuth provider tests (config parsing, mock provider, JWT minting) — gated by ETIL_BUILD_JWT
├── data/
│   ├── builtins.til            # Self-hosted words (loaded on startup)
│   ├── help.til                # Help metadata for all words (loaded on startup)
│   └── auth-config/            # Example auth config (3-file split)
│       ├── roles.json.example  # Roles + default_role
│       ├── keys.json.example   # JWT keys, TTL, OAuth providers
│       └── users.json.example  # User→role mappings
├── tests/til/                  # TIL integration tests (auto-discovered)
│   ├── harness.til             # Shared test harness (expect-eq, pass, fail)
│   ├── test_create.til         # CREATE/DOES> end-to-end tests
│   ├── test_create.sh          # Bash launcher for CREATE tests
│   ├── test_variable_constant.til # Variable/constant defining word tests (15 tests)
│   ├── test_variable_constant.sh  # Bash launcher for variable/constant tests
│   ├── test_metadata.til       # Metadata words end-to-end tests (27 assertions)
│   ├── test_metadata.sh        # Bash launcher for metadata tests
│   ├── test_builtins.til       # Self-hosted builtins integration tests
│   ├── test_builtins.sh        # Bash launcher for builtins tests
│   ├── test_help.til           # Help system tests
│   ├── test_help.sh            # Bash launcher for help tests
│   ├── test_dump_see.til       # Dump/see debug primitive tests
│   ├── test_dump_see.sh        # Bash launcher for dump/see tests
│   ├── test_pen_bytes.*        # PEN: byte array boundary conditions (negative/oversize indices, value overflow)
│   ├── test_pen_arrays.*       # PEN: array boundary conditions (empty ops, negative/oversize indices)
│   ├── test_pen_strings.*      # PEN: string boundary conditions (substr clamping, empty strings, split/join)
│   ├── test_pen_complex.*      # PEN: complex arrangements (nested arrays, mixed types, stress loops)
│   └── string/                 # String primitive tests (71 tests across 5 suites)
│       ├── test_string_basic.*       # s", slength, s+, s=, s<>
│       ├── test_string_substr_trim.* # substr, strim
│       ├── test_string_search.*      # sfind, sreplace
│       ├── test_string_split_join.*  # ssplit, sjoin
│       └── test_string_regex.*       # sregex-find, sregex-replace
├── tests/docker/              # Docker integration tests
│   ├── test_mcp_http.sh      # E2E MCP HTTP test (18 curl-based tests)
│   ├── test_mcp_file_io.sh   # E2E file I/O with Docker volumes (23 tests)
│   ├── test_file_io_stress.sh    # E2E file I/O stress tests (56 tests, bash)
│   ├── test_file_io_stress.py    # E2E file I/O stress tests (56 tests, Python)
│   └── test_file_io_stress_py.sh # Bash wrapper for Python stress tests
├── examples/
│   ├── simple_repl.cpp         # REPL with replxx line editing, history, tab-completion
│   ├── mcp_server.cpp         # MCP server executable (--host, --port)
│   └── benchmark.cpp           # Performance benchmarks
├── tools/mcp-client/          # Python/Textual TUI for interactive MCP sessions
│   ├── setup.sh               # Create venv, install deps
│   ├── run.sh                 # Launch TUI
│   └── etil_mcp_client/       # Python package (transport, protocol, widgets)
├── scripts/
│   ├── deploy.sh              # Automated deploy (build, transfer, restart, smoke test)
│   └── backup-workspace.sh    # Timestamped source backup
├── deploy/production/          # Production deployment config
│   ├── nginx-production.conf      # nginx: TLS, /mcp → ETIL, /auth/ → OAuth
│   ├── docker-compose.prod.yml # Production docker-compose
│   └── .env.example           # API key template
├── docs/                       # Documentation
├── Dockerfile                 # Multi-stage Docker build (HTTP transport enabled)
├── .dockerignore
└── docker-compose.yml         # Security-hardened services
```

## Design Principles

### 1. No More Linear Dictionary

FORTH's traditional linked-list dictionary was a memory scarcity artifact. ETIL uses a DAG where each word concept can
have multiple implementations:

```
Word: SORT
├─[weight: 0.7]→ quicksort_avx2    (fast, small data)
├─[weight: 0.2]→ mergesort_parallel (stable, large data)
└─[weight: 0.1]→ radix_sort         (integers only)
```

### 2. No Single-Cell / Double-Cell Distinction

All integer values are `int64_t` and all floating-point values are `double`. The old FORTH double-cell words have no
ETIL equivalents — their functionality is the default behavior.

### 3. REPL is an I/O Channel, Not an Interpreter

The REPL implements zero language semantics. All parsing, compilation, and word dispatch live in the `Interpreter` class
in the core library. This means the same interpreter can be driven by files, network sockets, embedded APIs, or any
other I/O channel.

### 4. Runtime Evolution (Planned)

The system will learn optimal implementations through:

- Performance profiling
- A/B testing with multi-armed bandits
- Genetic operators (mutation, crossover)
- Automatic specialization

### 5. Free-Threaded Multiprocessor

Modern hardware capabilities:

- Vector-backed stacks (single-threaded per ExecutionContext)
- Thread-local execution contexts
- Atomic operations with explicit memory ordering
- SIMD vectorization within words (planned)

## Development Status

### Completed

- Core data structures (WordImpl, TypeSignature, PerfProfile)
- Vector-backed ValueStack for execution context stacks
- Thread-safe dictionary with multiple implementations per word, `forget_word()` (removes latest impl), `forget_all()` (
  removes entire concept), `lookup()` returns latest ("newest wins")
- 125 primitive words with full test coverage (arithmetic, stack, comparison, logic, I/O, memory, math, PRNG, system, time,
  map, input-reading, dictionary-ops, metadata-ops, help, debug, evaluate)
- Compiled word bodies (`ByteCode`) with inner interpreter (`execute_compiled`)
- Colon definitions (`: name ... ;`) compiling to bytecode
- Control flow: `if`/`else`/`then`, `do`/`loop`/`+loop`/`i`, `begin`/`until`, `begin`/`while`/`repeat`
- Defining words: `create`, `does>`, `,`, `@`, `!`, `allot`; self-hosted: `variable`, `constant`
- `forget` (removes latest definition, reveals previous), `forget-all` (removes all implementations)
- `Interpreter` class in core library (all language semantics decoupled from I/O, dual output streams for normal output
  and errors)
- REPL with replxx line editing, persistent history, tab-completion, color themes, `--quiet` pipe-friendly mode, and
  dual-stream error separation (errors→stderr, output→stdout)
- Word metadata system with concept-level and implementation-level metadata
- JSON serialization for metadata, word implementations, and word concepts
- Six stack-based metadata words with conditional returns: `meta!`, `meta@`, `meta-del`, `meta-keys`, `impl-meta!`,
  `impl-meta@` (simple aliases for C++ primitives, defined in `data/builtins.til`)
- Self-hosting builtins: Words reimplemented in ETIL source, loaded on startup via `load_startup_files()`.
  `InterpretHandlerSet` reduced from 12 to 3 C++ handlers.
- HeapObject system with intrusive reference counting (HeapString, HeapArray, HeapByteArray, HeapMap, HeapJson)
- 42 string/array/byte/map/json primitive words with explicit refcount management
- First-class JSON type (`j|` literal syntax, 12 JSON primitives, pack/unpack to Map/Array)
- `s"` parsing word creates heap strings (interpret + compile mode with `PushString` opcode)
- System primitives (`sys-semver`, `sys-timestamp`) with build-time generated version header
- CMake build system with automatic dependency fetching
- TIL integration test framework with shared harness and auto-discovery by CTest
- Built-in help system: `help <word>` primitive with metadata-driven help text (`data/help.til`) as single source of
  truth for all words including handler words, `/help [word]` in REPL
- Debug primitives: `dump` (deep-inspect TOS non-destructively with recursive display and truncation) and `see` (
  decompile word definitions — bytecode, primitives, or handler words)
- MCP Server: Model Context Protocol server with JSON-RPC 2.0, HTTP transport, 10 tools (interpret, list_words,
  get_word_info, get_stack, set_weight, reset, get_session_stats, write_file, list_files, read_file), 4 resources
  (dictionary, word, stack, session/stats), Docker sandbox with security hardening
- MCP HTTP Transport: Streamable HTTP with real-time SSE streaming (chunked transfer) for notification delivery,
  session management, dual-mode authentication (JWT with RBAC preferred, API key fallback), origin validation, nginx
  deployment for the production server
- MCP output capture: All I/O primitives route through `ExecutionContext` streams, enabling the MCP `interpret` tool to
  capture output instead of losing it to `std::cout`
- Per-session profiling: CPU time, wall time, RSS memory tracking, dictionary/stack metrics via `get_session_stats` tool
  and `etil://session/stats` resource
- DoS mitigation: instruction budget (10M per MCP request), 30s execution deadline, 1K call depth limit, 1MB input /
  10MB output caps; Docker CPU/memory/PID limits; nginx connection limits and request timeouts
- MCP server error handling: SIGPIPE protection, catch-all exception handling in `handle_message()`, `dispatch()`
  method in HTTP transport — server survives client disconnects and reports errors to stderr instead of dying silently
- TUI resilience: debounced resize with cooldown (prevents scrollbar oscillation), 16MB StreamReader limit (fixes
  64KB default that killed large responses), 60s request timeout, CancelledError handling
- LVFS (Little Virtual File System): Virtual filesystem with `/home` and `/library` under virtual root `/`, 6
  shell-like primitives (`cwd`, `cd`, `ls`, `ll`, `lr`, `cat`), per-session CWD, directory traversal protection
- Outbound HTTP Client: `http-get` word with SSRF blocklist, domain allowlist, per-session fetch budgets, response
  size limits, HTTPS via OpenSSL
- JWT Authentication (Phase 1): RS256 JWT minting/validation via jwt-cpp, `AuthConfig` for role/permission/user
  mappings from JSON config (`ETIL_AUTH_CONFIG` env var), dual-mode auth (JWT preferred, API key fallback),
  per-role permission enforcement (HTTP domains, instruction budgets, file I/O, session limits). See
  `data/auth-config/` for role configuration (3-file split: roles.json, keys.json, users.json)
- OAuth Device Flow (Phase 2): Server-side GitHub + Google OAuth provider integration via RFC 8628 device
  authorization grant. `OAuthProvider` interface with `GitHubProvider` and `GoogleProvider` implementations.
  Three HTTP endpoints: `/auth/device`, `/auth/poll`, `/auth/token`. Stateless — provider tokens used once for
  user info then discarded. Provider config in `keys.json` `providers` section
- TUI OAuth Login: `/login [provider]` starts device flow with background polling, `/logout` reverts to API key,
  `/whoami` shows auth state. JWT cached in `~/.etil/connections.json` with automatic expiry detection
- 977 tests, ASan + UBSan enabled in debug builds; E2E Docker tests: 18 HTTP + 23 file-I/O + 56 file-I/O stress (bash + Python)

### Planned

- Execution engine with implementation selection
- Selection engine (decision trees, multi-armed bandits)
- Evolution engine (genetic operators, fitness evaluation)
- JIT compiler (LLVM integration)

## References

- **FORTH 2012 Standard**: http://www.forth200x.org/documents/forth-2012.pdf
- **Threaded Interpretive Languages**: R.G. Loeliger, 1981
- **Genetic Programming**: Koza, "Genetic Programming", 1992
- **MCP Specification**: https://modelcontextprotocol.io/specification/2025-11-25
- **JSON-RPC Specification**: https://www.jsonrpc.org/specification

## License

MIT License (to be finalized)

## Author

Initial development by Mark Deazley, based on FORTH experience dating back to 1979.

- TIL == **T**hreaded **I**nterpretive **L**anguage

- 1979: Wrote my first TIL on a
  [Zilog Z-80 Development System](https://vintagecomputer.ca/zilog-z80-development-system/) at Sierra Research.
  Used it to generate some wire list cross references.
- 1981: _Tried_ to write an embedded TIL for the 68000 for a independent study at SUNY Stony Brook...
  The 68000 assembler/emulator would frequently stop working and freeze.
  The final straw was the assembler would **crash** the UNIVAC mainframe every time
  I tried to use a `macro` statement, taking out _**all**_ the student and administrative terminals for 15 minutes.
  After I did it twice in a row the UNIVAC system operator stomped out and located me by my terminal ID.
  He emphatically told me to _**STOP**_ running that #$%^ing assembler program and stomped back into his cave...
  And that was the end of the 68000 TIL and any chance of getting a good mark on the project.
- 1982: Wrote a TIL for a microprocessors course at SUNY Buffalo. I was for the CPM operating system on an
  upconverted [Radio Shack TRS-80 Model III](https://www.trs-80.org/model-3.html)
  with a whopping 4.77 MHz processor and and unheard amount to memory (48K!).
- 1983: Purchased
  [MMS FORTH](https://www.google.com/search?q=MMS+FORTH&rlz=1C1ONGR_enUS1150US1150&oq=MMS+FORTH&gs_lcrp=EgZjaHJvbWUyBggAEEUYOTIHCAEQIRigATIHCAIQIRigATIHCAMQIRigATIHCAQQIRigATIHCAUQIRiPAjIHCAYQIRiPAtIBCTQzMTJqMGoxNagCCLACAfEF1XboTva4yJ4&sourceid=chrome&ie=UTF-8)
  and wrote a monochrome StarGate clone on the TRS-80 Model III. Never published it but spent alot of time playing it!
- 1991-1993: Hired by Caltech's OVRO to convert their existing FORTH radio telescope control code to the C language.
  Their existing radio telescope control computers were older over-clocked over-heating Digital computers,
  and they had shiny new rack mount MicroVAX computers they wanted to use. I reversed engineered the FORTH code and
  implemented new functionally enhanced C code that ran on top of the VAXELN RTOS, controlling seven 10 meter, 34 ton
  radio telescopes and their receivers to 0.6 arc seconds RMS in a 20 mph wind.

## History

The FORTH programming language was invented by Charles H. Moore in the late 1960s, with its first, fully functional, and
widely recognized version created in 1970. It was developed to control telescopes and for other real-time applications,
initially emerging between 1968 and 1970. 