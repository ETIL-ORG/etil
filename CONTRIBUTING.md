# Contributing to ETIL

Thank you for your interest in contributing to the Evolutionary Threaded
Interpretive Language.

## Getting Started

### Prerequisites

- GCC 12+ (C++20)
- CMake 3.20+
- Ninja build system
- LLVM 18+ development libraries
- Docker (for MCP server testing)

### Building

```bash
# Configure + build (debug)
mkdir build-debug && cd build-debug
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ../evolutionary-til
ninja

# Run tests
ctest --output-on-failure

# Or use the scripts from the workspace root:
scripts/build.sh debug
scripts/test.sh debug
```

### Optional build flags

| Flag | Default | Purpose |
|------|---------|---------|
| `ETIL_BUILD_JWT` | OFF | JWT authentication (requires OpenSSL) |
| `ETIL_BUILD_MONGODB` | ON | MongoDB integration |
| `ETIL_BUILD_HTTP_CLIENT` | OFF | Outbound HTTP (`http-get` word, requires OpenSSL) |
| `ETIL_BUILD_EXAMPLES` | ON | REPL and MCP server executables |
| `ETIL_USE_JEMALLOC` | OFF | jemalloc allocator |

## Coding Standards

### C++

- **C++20** — do not use C++23 features (`std::expected`, `std::print`, etc.)
- **Namespaces**: `etil::<component>` (e.g., `etil::core`, `etil::mcp`)
- **Classes**: PascalCase (`ExecutionContext`)
- **Methods/functions**: snake_case (`register_word`)
- **Members**: trailing underscore (`word_count_`)
- **Constants**: `SCREAMING_SNAKE_CASE` or `constexpr`
- **Enums**: PascalCase enum, PascalCase values
- Primitives return `bool` (true = success, false = error)
- Use `std::optional` for fallible lookups
- Thread-local execution contexts — no locks in hot paths

### Python (TUI)

- Standard Python style (PEP 8)
- Type hints encouraged
- `asyncio` for all I/O

### Commit Messages

- Short summary line (imperative mood)
- Blank line, then details if needed
- Reference issue numbers where applicable

## Adding a New Primitive Word

1. Declare in `include/etil/core/primitives.hpp`
2. Implement in `src/core/primitives.cpp` (or appropriate `*_primitives.cpp`)
3. Register in the corresponding `register_*_primitives()` function
4. Add tests in `tests/unit/`
5. Add help metadata in `data/help.til`

## Testing

- **Unit tests** (`tests/unit/`) — Google Test, run under AddressSanitizer
- **TIL integration tests** (`tests/til/`) — end-to-end `.til` scripts
- **Docker tests** (`tests/docker/`) — MCP server E2E tests

All tests must pass before submitting a PR:

```bash
ctest --test-dir build-debug --output-on-failure
```

## Security

- All MCP transports must run inside Docker (see `SECURITY.md`)
- Never expose network listeners outside a sandbox
- Validate all external input at system boundaries
- See `docs/claude-design/20260214A-ETIL-Server-Security-Rules.md`

## Pull Requests

1. Fork the repository and create a feature branch
2. Make your changes with tests
3. Ensure all tests pass
4. Submit a PR with a clear description of what and why

## License

By contributing, you agree that your contributions will be licensed under the
BSD-3-Clause license (see `LICENSE.md`).
