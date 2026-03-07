# Changelog

All notable changes to this project are documented here.

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
