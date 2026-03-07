# Build Instructions for ETIL

## Quick Start

```bash
# Clone or extract project
cd evolutionary-til

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake -GNinja -DCMAKE_BUILD_TYPE=Release ..

# Build
ninja

# Run tests
ctest

# Run examples
./bin/etil_repl
```

## Prerequisites

### Ubuntu/Debian (WSL2 or Native)

```bash
# System packages
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    wget

# Install LLVM 18
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 18
rm llvm.sh

# Install development tools
sudo apt install -y \
    llvm-18-dev \
    clang-18 \
    clang-format-18 \
    clang-tidy-18 \
    gdb \
    valgrind

# Set clang as default (optional)
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100
```

### macOS

```bash
# Install Homebrew (if not already installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake ninja llvm@18 tbb

# Add LLVM to PATH
export PATH="/usr/local/opt/llvm@18/bin:$PATH"
```

## Build Configurations

### Debug Build (with Sanitizers)

```bash
mkdir build-debug && cd build-debug

cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" \
    ..

ninja
```

### Release Build (Optimized)

```bash
mkdir build-release && cd build-release

cmake -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -march=native -flto" \
    ..

ninja
```

### With Clang

```bash
mkdir build-clang && cd build-clang

cmake -GNinja \
    -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_BUILD_TYPE=Release \
    ..

ninja
```

### With jemalloc

```bash
# Install jemalloc
sudo apt install libjemalloc-dev

mkdir build && cd build

cmake -GNinja \
    -DETIL_USE_JEMALLOC=ON \
    -DCMAKE_BUILD_TYPE=Release \
    ..

ninja
```

## Build Options

Configure with `-D<OPTION>=<VALUE>`:

| Option | Default | Description |
|--------|---------|-------------|
| `ETIL_BUILD_TESTS` | ON | Build unit tests |
| `ETIL_BUILD_EXAMPLES` | ON | Build example programs |
| `ETIL_ENABLE_PROFILING` | ON | Enable profiling support |
| `ETIL_USE_JEMALLOC` | OFF | Use jemalloc allocator |
| `CMAKE_BUILD_TYPE` | Debug | Build type: Debug, Release, RelWithDebInfo, MinSizeRel |

## Testing

### Run All Tests

```bash
cd build
ctest --output-on-failure
```

### Run Specific Test

```bash
./etil_tests --gtest_filter=DictionaryTest.*
```

### Run with Verbose Output

```bash
ctest -V
```

### Run Benchmarks

```bash
./bin/etil_benchmark
```

## CLion Configuration

### 1. Open Project
- `File` → `Open` → Select `evolutionary-til` directory
- CLion will detect CMakeLists.txt and configure automatically

### 2. Set Toolchain (WSL)
- `File` → `Settings` → `Build, Execution, Deployment` → `Toolchains`
- Add WSL toolchain (Ubuntu-24.04)
- Move to top (default)

### 3. Configure CMake Profile
- `File` → `Settings` → `Build, Execution, Deployment` → `CMake`
- Build type: `Debug` or `Release`
- Generator: `Ninja`
- Build options: `-j 8` (adjust for your CPU)

### 4. Build and Run
- Click hammer icon to build
- Select `etil_repl` from run configurations dropdown
- Click play button to run

## Troubleshooting

### LLVM Not Found

```bash
# Verify LLVM installation
llvm-config-18 --version

# Set LLVM_DIR explicitly
cmake -DLLVM_DIR=/usr/lib/llvm-18/cmake ..
```

### Missing TBB

```bash
# Install manually
sudo apt install libtbb-dev

# Or let CMake fetch it (automatic)
```

### Compilation Errors

```bash
# Check compiler version
g++ --version  # Should be 13+
clang++ --version  # Should be 18+

# Clean and rebuild
rm -rf build
mkdir build && cd build
cmake -GNinja ..
ninja
```

### Test Failures

```bash
# Run with verbose output
ctest -V

# Run with gdb
gdb ./etil_tests
(gdb) run
```

### Sanitizer Errors

```bash
# Disable sanitizers for initial testing
cmake -DCMAKE_BUILD_TYPE=Debug ..
ninja
```

## Performance Profiling

### With perf (Linux)

```bash
# Build with profiling
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
ninja

# Run with perf
perf record -g ./bin/etil_benchmark
perf report
```

### With Valgrind

```bash
valgrind --tool=callgrind ./bin/etil_benchmark
kcachegrind callgrind.out.*
```

### With gprof

```bash
cmake -DCMAKE_CXX_FLAGS="-pg" ..
ninja
./bin/etil_benchmark
gprof ./bin/etil_benchmark gmon.out > analysis.txt
```

## Continuous Integration

### GitHub Actions (planned)

```yaml
# .github/workflows/ci.yml
name: CI

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y build-essential cmake ninja-build llvm-18-dev
      - name: Build
        run: |
          mkdir build && cd build
          cmake -GNinja ..
          ninja
      - name: Test
        run: cd build && ctest --output-on-failure
```

## Docker Build

If you prefer containerized builds, see the `docker/` directory in the dev environment setup package.

## Next Steps

After successful build:

1. Run the REPL: `./bin/etil_repl`
2. Run tests: `ctest`
3. Run benchmarks: `./bin/etil_benchmark`
4. Explore the code in `include/etil/`
5. Read the architecture docs in `docs/`

## Support

For build issues:
- Check this document first
- Review CMake output for specific errors
- Ensure all prerequisites are installed
- Try a clean build (`rm -rf build && mkdir build`)
