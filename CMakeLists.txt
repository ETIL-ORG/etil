cmake_minimum_required(VERSION 3.20)
project(EvolutionaryTIL VERSION 0.8.5 LANGUAGES C CXX)

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

# GNU C++ specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fsanitize=address,undefined")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto")
endif()

# Options
option(ETIL_BUILD_TESTS "Build tests" ON)
option(ETIL_BUILD_EXAMPLES "Build examples" ON)
option(ETIL_ENABLE_PROFILING "Enable profiling support" ON)
option(ETIL_USE_JEMALLOC "Use jemalloc allocator" OFF)
option(ETIL_BUILD_HTTP_CLIENT "Build HTTP client primitives (http-get)" OFF)
option(ETIL_BUILD_JWT "Build JWT authentication" OFF)
option(ETIL_BUILD_MONGODB "Build MongoDB integration (requires ETIL_BUILD_JWT)" OFF)

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find dependencies
include(Dependencies)

# Generate version header at build time (stamps build timestamp)
add_custom_target(generate_version
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/include/etil/core/version.hpp.in
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/include/etil/core/version.hpp
        -DETIL_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        -DETIL_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        -DETIL_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake
    COMMENT "Generating version header"
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
