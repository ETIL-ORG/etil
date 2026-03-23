cmake_minimum_required(VERSION 3.20)
project(EvolutionaryTIL VERSION 1.6.2 LANGUAGES C CXX)

# All executables go to <builddir>/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Generate linker map files for all executables (<builddir>/bin/<name>.map)
function(etil_add_link_map target)
    target_link_options(${target} PRIVATE
        "LINKER:-Map,$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.map")
endfunction()

# C++20 GCC 13's C++23 <format> support is broken (missing resize_and_overwrite)
# Upgrade to C++23 when moving to GCC 14+
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific flags
if(NOT ETIL_WASM_TARGET)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # -Wpedantic omitted: triggers 100+ warnings from Abseil's __int128 usage.
        # Our one pedantic case (HeapString flexible array member) is suppressed
        # with a targeted #pragma in heap_string.hpp.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fsanitize=address,undefined")
        # LTO disabled: GCC 13.3 ICE in try_forward_edges (cfgcleanup.cc:580)
        # Non-deterministic crash during LTO link phase, hits different files each build.
        # Re-enable when upgrading to GCC 14+.
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native")
    endif()
endif()

# Options
option(ETIL_BUILD_TESTS "Build tests" ON)
option(ETIL_BUILD_EXAMPLES "Build examples" ON)
option(ETIL_ENABLE_PROFILING "Enable profiling support" ON)
option(ETIL_USE_JEMALLOC "Use jemalloc allocator" OFF)
option(ETIL_BUILD_HTTP_CLIENT "Build HTTP client primitives (http-get)" ON)
option(ETIL_BUILD_JWT "Build JWT authentication" ON)
option(ETIL_BUILD_MONGODB "Build MongoDB integration (requires ETIL_BUILD_JWT)" ON)
option(ETIL_WASM_TARGET "Build for WebAssembly via Emscripten" OFF)

# WASM target forces off incompatible features
if(ETIL_WASM_TARGET)
    set(ETIL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ETIL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ETIL_BUILD_HTTP_CLIENT OFF CACHE BOOL "" FORCE)
    set(ETIL_BUILD_JWT OFF CACHE BOOL "" FORCE)
    set(ETIL_BUILD_MONGODB OFF CACHE BOOL "" FORCE)
endif()

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find dependencies
include(Dependencies)

# Generate version header at configure time.
# ninja auto-reconfigures when CMakeLists.txt changes, so a version bump
# regenerates version.hpp and recompiles all dependents automatically.
# (The old add_custom_target approach ran every build but didn't declare
# outputs, so ninja couldn't track file-level dependencies — version
# changes silently left stale object files.)
string(TIMESTAMP ETIL_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/etil/core/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/etil/core/version.hpp
    @ONLY
)

# Core library
add_subdirectory(src)

# Tests
if(ETIL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(ETIL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
