# Dependencies management

include(FetchContent)

# Threading support
find_package(Threads REQUIRED)

# LLVM for JIT compilation
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(llvm_libs
    core
    executionengine
    interpreter
    mc
    support
    nativecodegen
    orcjit
)

# TBB (Threading Building Blocks) for concurrent containers
FetchContent_Declare(
    tbb
    GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
    GIT_TAG v2021.11.0
)
set(TBB_TEST OFF CACHE BOOL "")
set(TBB_STRICT OFF CACHE BOOL "")
FetchContent_MakeAvailable(tbb)

# Abseil (Google's C++ library) for containers and utilities
FetchContent_Declare(
    abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20240116.1
)
set(ABSL_PROPAGATE_CXX_STD ON)
FetchContent_MakeAvailable(abseil)

# Google Test for unit testing
if(ETIL_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    # No FORCE - FORCE causes CMakeCache.txt writes every configure,
    # which triggers infinite ninja re-run loops
    set(gtest_force_shared_crt ON CACHE BOOL "")
    FetchContent_MakeAvailable(googletest)
endif()

# Google Benchmark for performance testing
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "")
# Pre-set benchmark try_compile results to prevent repeated checks
set(HAVE_STD_REGEX ON CACHE BOOL "")
set(HAVE_GNU_POSIX_REGEX OFF CACHE BOOL "")
set(HAVE_POSIX_REGEX ON CACHE BOOL "")
set(HAVE_STEADY_CLOCK ON CACHE BOOL "")
set(HAVE_PTHREAD_AFFINITY ON CACHE BOOL "")
FetchContent_MakeAvailable(benchmark)

# spdlog for logging
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# nlohmann/json for metadata serialization
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "")
FetchContent_MakeAvailable(nlohmann_json)

# replxx for REPL line editing, history, and tab-completion
if(ETIL_BUILD_EXAMPLES)
    FetchContent_Declare(
        replxx
        GIT_REPOSITORY https://github.com/AmokHuginnsson/replxx.git
        GIT_TAG release-0.0.4
    )
    set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "")
    FetchContent_MakeAvailable(replxx)
endif()

# cpp-httplib for MCP HTTP transport and HTTP client primitives
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)
set(HTTPLIB_COMPILE ON CACHE BOOL "")
FetchContent_MakeAvailable(httplib)

# jwt-cpp for JWT minting/validation (header-only, uses existing OpenSSL + nlohmann/json)
if(ETIL_BUILD_JWT)
    FetchContent_Declare(
        jwt-cpp
        GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
        GIT_TAG v0.7.2
    )
    set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "")
    set(JWT_BUILD_TESTS OFF CACHE BOOL "")
    FetchContent_MakeAvailable(jwt-cpp)
endif()

# MongoDB C++ driver (requires mongo-c-driver, both via FetchContent)
if(ETIL_BUILD_MONGODB)
    # mongo-c-driver (libbson + libmongoc)
    FetchContent_Declare(
        mongo-c-driver
        GIT_REPOSITORY https://github.com/mongodb/mongo-c-driver.git
        GIT_TAG 1.28.1
        EXCLUDE_FROM_ALL
    )
    set(ENABLE_TESTS OFF CACHE BOOL "")
    set(ENABLE_EXAMPLES OFF CACHE BOOL "")
    set(ENABLE_UNINSTALL OFF CACHE BOOL "")
    set(ENABLE_AUTOMATIC_INIT_AND_CLEANUP OFF CACHE BOOL "")
    set(ENABLE_MONGOC ON CACHE BOOL "")
    set(ENABLE_BSON ON CACHE BOOL "")
    set(ENABLE_STATIC ON CACHE BOOL "")
    set(ENABLE_SHARED OFF CACHE BOOL "")
    set(ENABLE_SASL OFF CACHE BOOL "")
    set(ENABLE_SNAPPY OFF CACHE BOOL "")
    set(ENABLE_ZSTD OFF CACHE BOOL "")
    set(ENABLE_SRV ON CACHE BOOL "")
    set(ENABLE_SSL OPENSSL CACHE STRING "")
    # Suppress mongo-c-driver's internal test fixtures (fake_imds)
    set(_etil_save_BUILD_TESTING ${BUILD_TESTING})
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mongo-c-driver)
    set(BUILD_TESTING ${_etil_save_BUILD_TESTING} CACHE BOOL "" FORCE)

    # mongo-cxx-driver (mongocxx + bsoncxx)
    FetchContent_Declare(
        mongo-cxx-driver
        GIT_REPOSITORY https://github.com/mongodb/mongo-cxx-driver.git
        GIT_TAG r3.11.1
        EXCLUDE_FROM_ALL
    )
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
    set(MONGOCXX_ENABLE_SSL ON CACHE BOOL "")
    set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "")
    set(ENABLE_TESTS OFF CACHE BOOL "")
    set(ENABLE_UNINSTALL OFF CACHE BOOL "")
    FetchContent_MakeAvailable(mongo-cxx-driver)
endif()

# libuv for async I/O (file I/O via thread pool + event loop)
FetchContent_Declare(
    libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG v1.48.0
)
set(LIBUV_BUILD_TESTS OFF CACHE BOOL "")
set(LIBUV_BUILD_BENCH OFF CACHE BOOL "")
FetchContent_MakeAvailable(libuv)

# Optional: jemalloc for better memory performance
if(ETIL_USE_JEMALLOC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JEMALLOC jemalloc)
    if(JEMALLOC_FOUND)
        link_directories(${JEMALLOC_LIBRARY_DIRS})
        add_definitions(-DETIL_USE_JEMALLOC)
    endif()
endif()
