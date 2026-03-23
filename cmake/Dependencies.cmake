# Dependencies management
#
# For each dependency, try find_package() first (pre-built prefix).
# Fall back to FetchContent with URL-based archives (not git clones)
# to avoid GitHub rate-limiting on git pack downloads.

include(FetchContent)

# Threading, LLVM, TBB — skipped for WASM (single-threaded, no JIT)
if(NOT ETIL_WASM_TARGET)
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
            URL https://github.com/oneapi-src/oneTBB/archive/refs/tags/v2021.11.0.tar.gz
            URL_HASH SHA256=782ce0cab62df9ea125cdea253a50534862b563f1d85d4cda7ad4e77550ac363
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(TBB_TEST OFF CACHE BOOL "")
        set(TBB_STRICT OFF CACHE BOOL "")
        FetchContent_MakeAvailable(tbb)
    else()
        message(STATUS "Using pre-built TBB")
    endif()
endif()

# Abseil (Google's C++ library) for containers and utilities
find_package(absl QUIET CONFIG)
if(NOT absl_FOUND)
    FetchContent_Declare(
        abseil
        URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20240116.1.tar.gz
        URL_HASH SHA256=3c743204df78366ad2eaf236d6631d83f6bc928d1705dd0000b872e53b73dc6a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
            URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        # No FORCE - FORCE causes CMakeCache.txt writes every configure,
        # which triggers infinite ninja re-run loops
        set(gtest_force_shared_crt ON CACHE BOOL "")
        FetchContent_MakeAvailable(googletest)
    else()
        message(STATUS "Using pre-built GoogleTest")
    endif()
endif()

# Google Benchmark for performance testing — skip for WASM
if(NOT ETIL_WASM_TARGET)
    find_package(benchmark QUIET CONFIG)
    if(NOT benchmark_FOUND)
        FetchContent_Declare(
            benchmark
            URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
            URL_HASH SHA256=6bc180a57d23d4d9515519f92b0c83d61b05b5bab188961f36ac7b06b0d9e9ce
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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
endif()

# spdlog for logging
if(ETIL_WASM_TARGET)
    # spdlog 1.13 has consteval issues with Emscripten's clang.
    # Use header-only mode with FMT_EXTERNAL disabled.
    find_package(spdlog QUIET CONFIG)
    if(NOT spdlog_FOUND)
        FetchContent_Declare(
            spdlog
            URL https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.tar.gz
            URL_HASH SHA256=534f2ee1a4dcbeb22249856edfb2be76a1cf4f708a20b0ac2ed090ee24cfdbc9
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "")
        set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "")
        # Workaround: Emscripten clang 20 rejects consteval in spdlog's bundled fmt.
        # Must be set before FetchContent processes spdlog's CMakeLists.txt.
        add_compile_definitions(FMT_USE_CONSTEVAL=0 FMT_CONSTEVAL=)
        FetchContent_MakeAvailable(spdlog)
        # Also add to spdlog target in case global defs didn't propagate
        target_compile_definitions(spdlog PUBLIC FMT_USE_CONSTEVAL=0 FMT_CONSTEVAL=)
    else()
        message(STATUS "Using pre-built spdlog")
    endif()
else()
    find_package(spdlog QUIET CONFIG)
    if(NOT spdlog_FOUND)
        FetchContent_Declare(
            spdlog
            URL https://github.com/gabime/spdlog/archive/refs/tags/v1.13.0.tar.gz
            URL_HASH SHA256=534f2ee1a4dcbeb22249856edfb2be76a1cf4f708a20b0ac2ed090ee24cfdbc9
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(spdlog)
    else()
        message(STATUS "Using pre-built spdlog")
    endif()
endif()

# nlohmann/json for metadata serialization
find_package(nlohmann_json QUIET CONFIG)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
        URL_HASH SHA256=0d8ef5af7f9794e3263480193c491549b2ba6cc74bb018906202ada498a79406
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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
            URL https://github.com/AmokHuginnsson/replxx/archive/refs/tags/release-0.0.4.tar.gz
            URL_HASH SHA256=a22988b2184e1d256e2d111b5749e16ffb1accbf757c7b248226d73c426844c4
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(REPLXX_BUILD_EXAMPLES OFF CACHE BOOL "")
        FetchContent_MakeAvailable(replxx)
    else()
        message(STATUS "Using pre-built replxx")
    endif()
endif()

# cpp-httplib for MCP HTTP transport and HTTP client primitives
# Skipped for WASM (no TCP sockets from browser)
if(NOT ETIL_WASM_TARGET)
    find_package(httplib QUIET CONFIG)
    if(NOT httplib_FOUND)
        FetchContent_Declare(
            httplib
            URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.3.tar.gz
            URL_HASH SHA256=a0567bcd6c3fe5cef1b329b96245119047f876b49e06cc129a36a7a8dffe173e
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(HTTPLIB_COMPILE ON CACHE BOOL "")
        FetchContent_MakeAvailable(httplib)
    else()
        message(STATUS "Using pre-built cpp-httplib")
    endif()
endif()

# jwt-cpp for JWT minting/validation (header-only, uses existing OpenSSL + nlohmann/json)
if(ETIL_BUILD_JWT)
    find_package(jwt-cpp QUIET CONFIG)
    if(NOT jwt-cpp_FOUND)
        FetchContent_Declare(
            jwt-cpp
            URL https://github.com/Thalhammer/jwt-cpp/archive/refs/tags/v0.7.2.tar.gz
            URL_HASH SHA256=6e815d86c168eb521a27937d603747dec0ca3c39ffc12d6fa72e2cf78a5b02d2
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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
            URL https://github.com/mongodb/mongo-c-driver/archive/refs/tags/1.28.1.tar.gz
            URL_HASH SHA256=249fd66d8d12aac2aec7dea1456e1bf24908c87971016c391a1a82a636029a87
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
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
            URL https://github.com/mongodb/mongo-cxx-driver/archive/refs/tags/r3.11.1.tar.gz
            URL_HASH SHA256=a70451cd421685ea5c6db6cd392e13720826adbcf5861f86116830b945acaa06
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            EXCLUDE_FROM_ALL
        )
        set(BUILD_VERSION "3.11.1" CACHE STRING "")
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

# libuv and OpenBLAS/LAPACK — skipped for WASM (no OS threads, no FORTRAN)
if(NOT ETIL_WASM_TARGET)
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
            URL https://github.com/libuv/libuv/archive/refs/tags/v1.48.0.tar.gz
            URL_HASH SHA256=8c253adb0f800926a6cbd1c6576abae0bc8eb86a4f891049b72f9e5b7dc58f33
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(LIBUV_BUILD_TESTS OFF CACHE BOOL "")
        set(LIBUV_BUILD_BENCH OFF CACHE BOOL "")
        FetchContent_MakeAvailable(libuv)
    endif()

    # OpenBLAS/LAPACK for linear algebra (mat-* primitives)
    find_package(BLAS QUIET)
    find_package(LAPACK QUIET)
    if(NOT BLAS_FOUND OR NOT LAPACK_FOUND)
        FetchContent_Declare(
            OpenBLAS
            URL https://github.com/OpenMathLib/OpenBLAS/archive/refs/tags/v0.3.28.tar.gz
            URL_HASH SHA256=f1003466ad074e9b0c8d421a204121100b0751c96fc6fcf3d1456bd12f8a00a1
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(NOFORTRAN ON CACHE BOOL "")
        set(BUILD_TESTING OFF CACHE BOOL "")
        FetchContent_MakeAvailable(OpenBLAS)
        set(BLAS_LIBRARIES openblas)
        set(LAPACK_LIBRARIES openblas)
        # OpenBLAS only sets INSTALL_INTERFACE include dirs, not BUILD_INTERFACE.
        # Expose headers (cblas.h, lapacke.h) for FetchContent consumers.
        # IMPORTANT: Do NOT add openblas_SOURCE_DIR directly — it contains a
        # cpuid.h that shadows GCC's system <cpuid.h> and breaks SIMD detection.
        # Use the generated/ directory for cblas.h instead.
        set(OPENBLAS_INCLUDE_DIRS
            ${CMAKE_BINARY_DIR}/generated
            ${CMAKE_BINARY_DIR}
            ${openblas_SOURCE_DIR}/lapack-netlib/LAPACKE/include
        )
    else()
        message(STATUS "Using pre-built BLAS/LAPACK")
        # System OpenBLAS doesn't bundle LAPACKE — link it separately.
        find_library(LAPACKE_LIBRARY lapacke)
        if(LAPACKE_LIBRARY)
            set(LAPACKE_LIBRARIES ${LAPACKE_LIBRARY})
        endif()
    endif()
else()
    # Eigen for WASM builds — header-only C++ linear algebra (replaces OpenBLAS/LAPACKE)
    find_package(Eigen3 QUIET CONFIG)
    if(NOT Eigen3_FOUND)
        FetchContent_Declare(
            eigen
            URL https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
            URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(EIGEN_BUILD_DOC OFF CACHE BOOL "")
        set(BUILD_TESTING OFF CACHE BOOL "")
        set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "")
        FetchContent_MakeAvailable(eigen)
    else()
        message(STATUS "Using pre-built Eigen3")
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
