# Changelog

All notable changes to this project are documented here.

## v0.9.1 — 2026-03-12

- Security hardening Phase 4: JWT validation, input caps, SSRF domain blocklist
- JWT: 60s clock-skew leeway, empty-subject rejection, iat future-token check, type-safe
  claim extraction
- Input: resource URI 1KB cap, `mkdir-tmp` prefix rejects `/` and `..` (path traversal fix)
- Buffers: 1000 notification cap
- SSRF domain blocklist (`*.internal`, `*.local`, `*.localhost`) checked before DNS resolution
- 11 new tests (5 JWT + 6 SSRF domain)

## v0.9.0 — 2026-03-12

- **Observable type system** — RxJS-style reactive pipelines: lazy, push-based, composable
- 21 new primitives: `obs-from`, `obs-of`, `obs-empty`, `obs-range`, `obs-map`, `obs-filter`,
  `obs-scan`, `obs-reduce`, `obs-take`, `obs-skip`, `obs-distinct`, `obs-merge`, `obs-concat`,
  `obs-zip`, `obs-subscribe`, `obs-to-array`, `obs-count`, `obs?`, `obs-kind`, plus `-with`
  variants for closure-like data binding
- 4 array iteration primitives: `array-each`, `array-map`, `array-filter`, `array-reduce`
- Codebase refactoring and security hardening (Phases 1–3):
  - Macro-generated math primitives, template `pop_heap_value<>()`, `pop_two_matrices()` helper
  - Table-driven `register_primitives()` replacing 414-line function
  - Merged 6 copy-paste pairs, consolidated sync/async file I/O duplication
  - Extracted `init_auth()` / `init_database()` from McpServer constructor
  - DNS rebinding fix (SSRF bypass): resolve once, connect by IP, set Host header
  - Bytecode cache invalidation after `forget` via dictionary generation counter
  - Size-limit constants for DoS prevention (matrix, byte array, JSON array)

## v0.8.24 — 2026-03-11

- Fix `pop_*` value leak on type mismatch — values were silently consumed instead of
  pushed back, corrupting the stack
- `.` now prints string content; `.s` shows string content truncated to 64 chars

## v0.8.22 — 2026-03-10

- 3 new conversion words: `mat->json`, `json->mat`, `mat->array`
- Interpreter resilience: `load_file()` cleans up orphaned stack values on error lines

## v0.8.21 — 2026-03-10

- **MLP primitives** — 17 new matrix words for neural network forward/backward pass
  and classification (`mat-relu`, `mat-sigmoid`, `mat-tanh`, derivatives, reductions,
  `mat-softmax`, `mat-cross-entropy`, `mat-apply`, `mat-randn`, etc.)
- `tanh` scalar primitive
- TIL-level: `mat-xavier`, `mat-he`, `mat-mse`

## v0.8.15 — 2026-03-08

- **Admin MCP tools** — 9 tools for role/user management gated by `role_admin` permission
- Thread-safe auth config reload via `shared_ptr<const>` with atomic swap

## v0.8.14 — 2026-03-08

- LAPACK solver/decomposition words return Boolean flags (not integer info codes)

## v0.8.13 — 2026-03-08

- **`http-post` primitive** — outbound HTTP POST with same security model as `http-get`
- HeapMatrix always compiled (ETIL_BUILD_LINALG gate removed)
- Deploy smoke test retry (3 attempts)

## v0.8.9 — 2026-03-08

- Pre-built CI dependencies — ~4-6 min savings per pipeline run
- `http-get` now takes a headers map parameter

## v0.8.5 — 2026-03-07

- Add BSD-3-Clause license, ATTRIBUTION.md, copyright headers on all source files
- TUI: start with Server I/O panel full width
- TUI: show friendly login message instead of raw user ID

## v0.8.4 — 2026-03-06

- Split auth config into 3 files (`roles.json`, `keys.json`, `users.json`)
- TUI: fix help category ordering

## v0.8.3 — 2026-03-05

- Session idle timeout (30 min) with automatic reaping
- TUI heartbeat keepalive (5 min interval)
- TUI dpkg packaging (`scripts/build-tui-deb.sh`)
- Google OAuth device flow deployed

## v0.8.2

- `json-get` supports integer index for array access

## v0.8.1

- `mongo-find` returns HeapJson directly (no `json-parse` needed)
- Remove redundant `mongo-map-*` words

## v0.8.0

- **HeapJson type** — first-class JSON values on the stack
- `j|` handler word for JSON literals
- 12 JSON primitives (`json-parse`, `json-dump`, `json-get`, etc.)
- MongoDB words accept String, Json, or Map parameters

## v0.7.5

- **First-class Boolean type** — `Value::Type::Boolean`
- All comparisons and predicates return Boolean (not FORTH flags)
- Control flow (`if`/`while`/`until`) requires Boolean on TOS
- `and`/`or`/`xor` overloaded: Boolean→logical, Integer→bitwise

## v0.7.4

- Redesign MongoDB primitives — unified API accepting String, Json, or Map
- `MongoQueryOptions` struct with skip, limit, sort, projection, upsert, etc.

## v0.7.0

- **MongoDB integration** — `mongo-find`, `mongo-insert`, `mongo-update`,
  `mongo-delete`, `mongo-count` with per-role permission enforcement

## v0.6.14

- **Taint tracking** — taint bit on HeapString/HeapByteArray for external data
- `staint` primitive, taint propagation through string operations
- `sregex-replace` acts as sanitizer (untaints)

## v0.6.9

- **OAuth device flow** — GitHub and Google authentication
- `/auth/device`, `/auth/poll`, `/auth/token` endpoints

## v0.6.8

- **JWT authentication** — RS256 signing/validation, role-based access control
- `AuthConfig` with roles, permissions, user mappings
- Dual-mode auth (JWT preferred, API key fallback)

## v0.6.7

- **`http-get` primitive** — outbound HTTP/HTTPS with SSRF protection
- URL validation, domain allowlisting, per-session budgets

## v0.6.1

- **LVFS file upload/download** — `read_file`/`write_file` MCP tools
- TUI `/upload` and `/download` commands

## v0.5.58

- **LVFS** (Little Virtual File System) — `/home` (writable) + `/library` (read-only)
- 6 primitives: `cwd`, `cd`, `ls`, `ll`, `lr`, `cat`

## v0.5.55

- **`evaluate` word** — interpret string as TIL code at runtime
- Security-hardened: call depth tracking, unterminated definition recovery

## v0.5.46

- Real-time SSE streaming for MCP notifications

## v0.5.42

- Replace lock-free stack with vector-backed `ValueStack`

## v0.5.28

- **HeapMap** — hash map data structure with 8 primitives

## v0.5.12

- **DoS mitigation** — instruction budget, execution deadline, call depth limits
- Docker resource caps, nginx rate limiting

## v0.5.9

- **TUI MCP Client** — Python/Textual terminal interface

## v0.5.0

- **MCP Server** — Model Context Protocol with HTTP transport, 10 tools, 4 resources
- Docker sandbox, nginx reverse proxy, API key auth

## v0.4.0

- **Heap objects** — reference-counted strings, arrays, byte arrays
- 31 new primitive words

## v0.3.0

- Compiled words, control flow, defining words, inner interpreter

## v0.2.0

- Dictionary, 57 primitive words, interactive REPL

## v0.1.0

- Initial implementation: WordImpl, ExecutionContext, ValueStack, SIMD detection
