cmake_minimum_required(VERSION 3.20)
project(etil-deps LANGUAGES C CXX)

# Superbuild: install all ETIL dependencies into a prefix directory.
# Usage:
#   cmake -GNinja -DCMAKE_BUILD_TYPE=Debug \
#         -DDEPS_INSTALL_PREFIX=/opt/etil-deps/v1/debug \
#         -S ci/deps -B /tmp/deps-build-debug
#   ninja -C /tmp/deps-build-debug

include(ExternalProject)

set(DEPS_INSTALL_PREFIX "/opt/etil-deps/v1" CACHE PATH "Install prefix for dependencies")

# Propagate build type and sanitizer flags to all deps
set(DEPS_C_FLAGS "${CMAKE_C_FLAGS}")
set(DEPS_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    string(APPEND DEPS_C_FLAGS " -g -O0 -fsanitize=address,undefined")
    string(APPEND DEPS_CXX_FLAGS " -g -O0 -fsanitize=address,undefined")
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    string(APPEND DEPS_C_FLAGS " -O3")
    string(APPEND DEPS_CXX_FLAGS " -O3")
endif()

# Common CMake args for all ExternalProject_Add
set(COMMON_CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_PREFIX}
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    "-DCMAKE_C_FLAGS=${DEPS_C_FLAGS}"
    "-DCMAKE_CXX_FLAGS=${DEPS_CXX_FLAGS}"
    -DCMAKE_CXX_STANDARD=20
    -DCMAKE_CXX_STANDARD_REQUIRED=ON
)

# --- TBB ---
ExternalProject_Add(tbb
    GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
    GIT_TAG        v2021.11.0
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DTBB_TEST=OFF
        -DTBB_STRICT=OFF
        -DBUILD_SHARED_LIBS=OFF
)

# --- Abseil ---
ExternalProject_Add(abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG        20240116.1
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DABSL_PROPAGATE_CXX_STD=ON
        -DABSL_BUILD_TESTING=OFF
        -DBUILD_SHARED_LIBS=OFF
)

# --- GoogleTest ---
ExternalProject_Add(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -Dgtest_force_shared_crt=ON
        -DBUILD_SHARED_LIBS=OFF
)

# --- Google Benchmark ---
ExternalProject_Add(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.8.3
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DBENCHMARK_ENABLE_TESTING=OFF
        -DBUILD_SHARED_LIBS=OFF
)

# --- spdlog ---
ExternalProject_Add(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DSPDLOG_BUILD_EXAMPLE=OFF
        -DSPDLOG_BUILD_TESTS=OFF
        -DBUILD_SHARED_LIBS=OFF
)

# --- nlohmann_json ---
ExternalProject_Add(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DJSON_BuildTests=OFF
        -DJSON_Install=ON
)

# --- cpp-httplib ---
ExternalProject_Add(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DHTTPLIB_COMPILE=ON
        -DBUILD_SHARED_LIBS=OFF
)

# --- replxx ---
ExternalProject_Add(replxx
    GIT_REPOSITORY https://github.com/AmokHuginnsson/replxx.git
    GIT_TAG        release-0.0.4
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DREPLXX_BUILD_EXAMPLES=OFF
        -DBUILD_SHARED_LIBS=OFF
)

# --- libuv ---
ExternalProject_Add(libuv
    GIT_REPOSITORY https://github.com/libuv/libuv.git
    GIT_TAG        v1.48.0
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DLIBUV_BUILD_TESTS=OFF
        -DLIBUV_BUILD_BENCH=OFF
        -DBUILD_SHARED_LIBS=OFF
)

# --- jwt-cpp ---
ExternalProject_Add(jwt-cpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG        v0.7.2
    GIT_SHALLOW    ON
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DJWT_BUILD_EXAMPLES=OFF
        -DJWT_BUILD_TESTS=OFF
)

# --- mongo-c-driver ---
ExternalProject_Add(mongo-c-driver
    GIT_REPOSITORY https://github.com/mongodb/mongo-c-driver.git
    GIT_TAG        1.28.1
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DENABLE_TESTS=OFF
        -DENABLE_EXAMPLES=OFF
        -DENABLE_UNINSTALL=OFF
        -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF
        -DENABLE_MONGOC=ON
        -DENABLE_BSON=ON
        -DENABLE_STATIC=ON
        -DENABLE_SHARED=OFF
        -DENABLE_SASL=OFF
        -DENABLE_SNAPPY=OFF
        -DENABLE_ZSTD=OFF
        -DENABLE_SRV=ON
        -DENABLE_SSL=OPENSSL
        -DBUILD_TESTING=OFF
    BUILD_COMMAND ninja -j1
)

# --- mongo-cxx-driver (depends on mongo-c-driver) ---
ExternalProject_Add(mongo-cxx-driver
    GIT_REPOSITORY https://github.com/mongodb/mongo-cxx-driver.git
    GIT_TAG        r3.11.1
    CMAKE_ARGS     ${COMMON_CMAKE_ARGS}
        -DCMAKE_PREFIX_PATH=${DEPS_INSTALL_PREFIX}
        -DBUILD_SHARED_LIBS=OFF
        -DMONGOCXX_ENABLE_SSL=ON
        -DBUILD_SHARED_AND_STATIC_LIBS=OFF
        -DENABLE_TESTS=OFF
        -DENABLE_UNINSTALL=OFF
    BUILD_COMMAND ninja -j1
    DEPENDS mongo-c-driver
)
