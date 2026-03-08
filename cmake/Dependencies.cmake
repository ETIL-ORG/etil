# Dependencies management
#
# For each dependency, try find_package() first (pre-built prefix).
# Fall back to FetchContent for local development builds.

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
find_package(TBB QUIET CONFIG)
if(NOT TBB_FOUND)
    FetchContent_Declare(
        tbb
        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
        GIT_TAG v2021.11.0
    )
    set(TBB_TEST OFF CACHE BOOL "")
    set(TBB_STRICT OFF CACHE BOOL "")
    FetchContent_MakeAvailable(tbb)
else()
    message(STATUS "Using pre-built TBB")
endif()

# Abseil (Google's C++ library) for containers and utilities
find_package(absl QUIET CONFIG)
if(NOT absl_FOUND)
    FetchContent_Declare(
        abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20240116.1
    )
    set(ABSL_PROPAGATE_CXX_STD ON)
    FetchContent_MakeAvailable(abseil)
else()
    message(STATUS "Using pre-built Abseil")
endif()

# Google Test for unit testing
if(ETIL_BUILD_TESTS)
    find_package(GTest QUIET CONFIG)
    if(NOT GTest_FOUND)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
        )
        # No FORCE - FORCE causes CMakeCache.txt writes every configure,
        # which triggers infinite ninja re-run loops
        set(gtest_force_shared_crt ON CACHE BOOL "")
        FetchContent_MakeAvailable(googletest)
    else()
        message(STATUS "Using pre-built GoogleTest")
    endif()
endif()

# Google Benchmark for performance testing
find_package(benchmark QUIET CONFIG)
if(NOT benchmark_FOUND)
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
else()
    message(STATUS "Using pre-built Google Benchmark")
endif()

# spdlog for logging
find_package(spdlog QUIET CONFIG)
if(NOT spdlog_FOUND)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.13.0
    )
    FetchContent_MakeAvailable(spdlog)
else()
    message(STATUS "Using pre-built spdlog")
endif()

# nlohmann/json for metadata serialization
find_package(nlohmann_json QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    set(JSON_BuildTests OFF CACHE BOOL "")
    FetchContent_MakeAvailable(nlohmann_json)
else()
    message(STATUS "Using pre-built nlohmann_json")
endif()

# replxx for REPL line editing, history, and tab-completion
if(ETIL_BUILD_EXAMPLES)
    find_package(replxx QUIET CONFIG)
    if(NOT replxx_FOUND)
        FetchContent_Declare(
            replxx
            GIT_REPOSITORY https://github.com/AmokHuginnsson/replxx.git
            GIT_TAG release-0.0.4
        )
        set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "")
        FetchContent_MakeAvailable(replxx)
    else()
        message(STATUS "Using pre-built replxx")
    endif()
endif()

# cpp-httplib for MCP HTTP transport and HTTP client primitives
find_package(httplib QUIET CONFIG)
if(NOT httplib_FOUND)
    FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.18.3
    )
    set(HTTPLIB_COMPILE ON CACHE BOOL "")
    FetchContent_MakeAvailable(httplib)
else()
    message(STATUS "Using pre-built cpp-httplib")
endif()

# jwt-cpp for JWT minting/validation (header-only, uses existing OpenSSL + nlohmann/json)
if(ETIL_BUILD_JWT)
    find_package(jwt-cpp QUIET CONFIG)
    if(NOT jwt-cpp_FOUND)
        FetchContent_Declare(
            jwt-cpp
            GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
            GIT_TAG v0.7.2
        )
        set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "")
        set(JWT_BUILD_TESTS OFF CACHE BOOL "")
        FetchContent_MakeAvailable(jwt-cpp)
    else()
        message(STATUS "Using pre-built jwt-cpp")
    endif()
endif()

# MongoDB C++ driver (requires mongo-c-driver, both via FetchContent)
if(ETIL_BUILD_MONGODB)
    find_package(mongocxx QUIET CONFIG)
    if(NOT mongocxx_FOUND)
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
    else()
        message(STATUS "Using pre-built MongoDB drivers")
        # Alias installed target names to FetchContent target names
        # Installed: mongo::mongocxx_static, FetchContent: mongocxx_static
        if(NOT TARGET mongocxx_static)
            add_library(mongocxx_static ALIAS mongo::mongocxx_static)
        endif()
        if(NOT TARGET bsoncxx_static)
            add_library(bsoncxx_static ALIAS mongo::bsoncxx_static)
        endif()
    endif()
endif()

# libuv for async I/O (file I/O via thread pool + event loop)
find_package(libuv QUIET CONFIG)
if(libuv_FOUND)
    message(STATUS "Using pre-built libuv")
    # Alias installed target name to FetchContent target name
    # Installed: libuv::uv_a, FetchContent: uv_a
    if(NOT TARGET uv_a)
        add_library(uv_a ALIAS libuv::uv_a)
    endif()
else()
    FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG v1.48.0
    )
    set(LIBUV_BUILD_TESTS OFF CACHE BOOL "")
    set(LIBUV_BUILD_BENCH OFF CACHE BOOL "")
    FetchContent_MakeAvailable(libuv)
endif()

# OpenBLAS/LAPACK for linear algebra (mat-* primitives)
if(ETIL_BUILD_LINALG)
    find_package(BLAS QUIET)
    find_package(LAPACK QUIET)
    if(NOT BLAS_FOUND OR NOT LAPACK_FOUND)
        FetchContent_Declare(
            OpenBLAS
            GIT_REPOSITORY https://github.com/OpenMathLib/OpenBLAS.git
            GIT_TAG v0.3.28
        )
        set(NOFORTRAN ON CACHE BOOL "")
        set(BUILD_TESTING OFF CACHE BOOL "")
        FetchContent_MakeAvailable(OpenBLAS)
        set(BLAS_LIBRARIES openblas)
        set(LAPACK_LIBRARIES openblas)
    else()
        message(STATUS "Using pre-built BLAS/LAPACK")
    endif()
endif()

# Optional: jemalloc for better memory performance
if(ETIL_USE_JEMALLOC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JEMALLOC jemalloc)
    if(JEMALLOC_FOUND)
        link_directories(${JEMALLOC_LIBRARY_DIRS})
        add_definitions(-DETIL_USE_JEMALLOC)
    endif()
endif()
