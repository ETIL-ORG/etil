# Build Instructions for ETIL

## Quick Start

```bash
# Clone the project
git clone https://github.com/ETIL-ORG/etil.git
cd evolutionary-til

# Create a sibling build directory and configure
mkdir ../build-debug && cd ../build-debug
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ../evolutionary-til

# Build
ninja

# Run tests (1,362 deterministic tests)
ctest --output-on-failure

# Run the REPL
./bin/etil_repl
```

## Prerequisites

### Ubuntu/Debian (24.04+ or WSL2)

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    wget

# LLVM 18 (required)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 18
rm llvm.sh

sudo apt install -y \
    llvm-18-dev \
    clang-18 \
    clang-format-18 \
    clang-tidy-18

# OpenSSL (required for JWT and HTTP client features)
sudo apt install -y libssl-dev

# LAPACK (required for linear algebra)
sudo apt install -y liblapacke-dev
```

### macOS

```bash
brew install cmake ninja llvm@18 tbb openblas
export PATH="/usr/local/opt/llvm@18/bin:$PATH"
```

## Build Configurations

### Debug Build (with Sanitizers)

```bash
mkdir build-debug && cd build-debug
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ../evolutionary-til
ninja
```

Debug builds automatically enable AddressSanitizer and UndefinedBehaviorSanitizer.

### Release Build (Optimized)

```bash
mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ../evolutionary-til
ninja
```

### With Clang

```bash
mkdir build-clang && cd build-clang
cmake -GNinja \
    -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_BUILD_TYPE=Release \
    ../evolutionary-til
ninja
```

## Build Options

Configure with `-D<OPTION>=<VALUE>`:

| Option | Default | Description |
|--------|---------|-------------|
| `ETIL_BUILD_TESTS` | ON | Build unit and integration tests |
| `ETIL_BUILD_EXAMPLES` | ON | Build REPL, MCP server, benchmarks |
| `ETIL_BUILD_HTTP_CLIENT` | OFF | HTTP client (`http-get`, `http-post`) — requires OpenSSL |
| `ETIL_BUILD_JWT` | OFF | JWT authentication with RBAC — requires OpenSSL |
| `ETIL_BUILD_MONGODB` | OFF | MongoDB integration — requires `ETIL_BUILD_JWT` |
| `ETIL_ENABLE_PROFILING` | ON | Enable performance profiling support |
| `ETIL_USE_JEMALLOC` | OFF | Use jemalloc allocator |
| `CMAKE_BUILD_TYPE` | Debug | Build type: Debug, Release, RelWithDebInfo, MinSizeRel |

### Full-featured build (all optional features)

```bash
cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DETIL_BUILD_HTTP_CLIENT=ON \
    -DETIL_BUILD_JWT=ON \
    -DETIL_BUILD_MONGODB=ON \
    ../evolutionary-til
```

## Build Scripts (Recommended)

The `scripts/` directory provides convenience wrappers. All scripts auto-detect paths
from `scripts/env.sh`. Run from the **workspace** directory (parent of `evolutionary-til/`):

```bash
# Build debug (default) or release
scripts/build.sh debug
scripts/build.sh release
scripts/build.sh all              # both

# Test
scripts/test.sh debug
scripts/test.sh all
scripts/test.sh debug --filter Observable   # run matching tests only

# Debug-first development loop
scripts/dev.sh                    # build + test debug
scripts/dev.sh --release          # also build + test release

# Clean rebuild
scripts/build.sh debug --clean
```

Build output goes to sibling directories: `build-debug/` (debug) and `build/` (release).

## Pre-Built Dependencies (Fast Builds)

FetchContent downloads and compiles all 13 C++ dependencies on first build (~5-10 min).
To avoid this on subsequent clean builds, pre-build them once:

```bash
# Build dependencies into ~/workspace/lib/ (both debug and release)
scripts/build-deps.sh all

# Or just one mode
scripts/build-deps.sh debug
```

Once built, `scripts/build.sh` automatically detects them via `CMAKE_PREFIX_PATH` and
passes `FETCHCONTENT_FULLY_DISCONNECTED=ON`. Clean debug builds drop from ~5 min to ~25s.

The staleness check compares `ci/deps/manifest.json` against the installed manifest.
Use `--force` to rebuild even if up to date.

For manual CMake builds, pass the flags explicitly:

```bash
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH=$HOME/workspace/lib/debug \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    ../evolutionary-til
```

## Dependencies

All C++ dependencies are fetched automatically by CMake FetchContent on first build.
See [ATTRIBUTION.md](ATTRIBUTION.md) for the complete list with versions and licenses.

System dependencies (must be installed separately): LLVM 18+, OpenSSL (if JWT or HTTP
enabled), LAPACK/OpenBLAS (for linear algebra).

## Testing

```bash
# Via script (recommended)
scripts/test.sh debug
scripts/test.sh debug --filter Observable --parallel 4

# Via ctest directly
ctest --test-dir build-debug --output-on-failure

# Specific tests by GTest filter
./build-debug/bin/etil_tests --gtest_filter=PrimitivesTest.*

# Benchmarks
./build-debug/bin/etil_benchmark
```

### Deterministic vs Non-Deterministic Tests (DT / NDT)

ETIL tests are classified into two categories:

**Deterministic Tests (DT)** — produce the same result every run. These are the default tests run by `ctest` and CI. All DTs must pass for a build to be considered healthy.

**Non-Deterministic Tests (NDT)** — depend on random seeds, mutation outcomes, or evolutionary fitness landscapes. These test the AST-level evolution pipeline where results are inherently probabilistic. NDTs use GTest's `DISABLED_` prefix so CTest skips them by default.

```bash
# Run DTs only (default — what CI runs)
ctest --test-dir build-debug --output-on-failure

# Run NDTs locally (for manual investigation)
./build-debug/bin/etil_ast_genetic_ops_tests --gtest_also_run_disabled_tests

# Run all tests including NDTs
ctest --test-dir build-debug --output-on-failure \
  -- --gtest_also_run_disabled_tests
```

NDT failures are **expected to occur sometimes** and should be investigated rather than treated as bugs. Common causes: unfavorable random seed producing low mutation validity, fitness landscape that doesn't converge in the allotted generations, or type repair rejecting too many mutations.

**Currently disabled tests:**

| Suite | Test | Reason |
|-------|------|--------|
| `DISABLED_InterpreterFileTest` (14 tests) | All file tests | Cold filesystem timing on GitHub Actions VMs |
| `ASTGeneticOpsTest.DISABLED_MutationValidityRate` | Validity rate | Random mutation outcomes vary per run |
| `ASTGeneticOpsTest.DISABLED_ASTBetterThanBytecode` | AST vs bytecode | Comparative validity counts are probabilistic |
| `ASTGeneticOpsTest.DISABLED_EndToEndEvolution` | End-to-end | Evolution outcomes depend on random mutations |
| `EvolutionEngineTest.DISABLED_MultipleGenerations` | Multi-gen | Mutants can trigger ASan leaks from system-resource words |

## Docker (MCP Server)

The MCP server runs inside Docker per project security rules:

```bash
docker build -t etil-mcp .
docker run -d --rm --read-only \
  -p 127.0.0.1:8080:8080 \
  -e ETIL_MCP_API_KEY=your-secret-key \
  --tmpfs /tmp:size=10M \
  etil-mcp --port 8080
```

See `data/auth-config/*.json.example` for JWT/RBAC configuration.

## Troubleshooting

### LLVM Not Found

```bash
llvm-config-18 --version
cmake -DLLVM_DIR=/usr/lib/llvm-18/cmake ../evolutionary-til
```

### Missing TBB

TBB is fetched automatically by CMake. If you prefer the system package:

```bash
sudo apt install libtbb-dev
```

### Clean Rebuild

```bash
rm -rf build-debug
mkdir build-debug && cd build-debug
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ../evolutionary-til
ninja
```

## Support

For build issues, open an issue at
[github.com/ETIL-ORG/etil](https://github.com/ETIL-ORG/etil/issues).
