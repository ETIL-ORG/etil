# Build Instructions for ETIL

## Quick Start

```bash
# Clone the project
git clone https://github.com/krystalmonolith/evolutionary-til.git
cd evolutionary-til

# Create a sibling build directory and configure
mkdir ../build-debug && cd ../build-debug
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug ../evolutionary-til

# Build
ninja

# Run tests (1,272 tests)
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

## Dependencies

All C++ dependencies are fetched automatically by CMake FetchContent on first build.
See [ATTRIBUTION.md](ATTRIBUTION.md) for the complete list with versions and licenses.

System dependencies (must be installed separately): LLVM 18+, OpenSSL (if JWT or HTTP
enabled), LAPACK/OpenBLAS (for linear algebra).

Dependencies can also be pre-built and resolved via `CMAKE_PREFIX_PATH` with
`FETCHCONTENT_FULLY_DISCONNECTED=ON` to avoid network access during builds.

## Testing

### Run All Tests

```bash
ctest --test-dir build-debug --output-on-failure
```

### Run Specific Tests

```bash
# By test name pattern
ctest --test-dir build-debug -R Dictionary

# By GTest filter
./build-debug/bin/etil_tests --gtest_filter=PrimitivesTest.*
```

### Run Benchmarks

```bash
./build-debug/bin/etil_benchmark
```

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
[github.com/krystalmonolith/evolutionary-til](https://github.com/krystalmonolith/evolutionary-til/issues).
