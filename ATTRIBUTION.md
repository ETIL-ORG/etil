# Attribution and Third-Party Licenses

This document lists all third-party dependencies used by the Evolutionary TIL
project, their licenses, and copyright holders. It is provided for license
compliance and transparency.

---

## C++ Dependencies (Always Included)

These libraries are fetched via CMake FetchContent and linked into every build.

| Library | Version | License | Copyright |
|---------|---------|---------|-----------|
| [Abseil C++](https://github.com/abseil/abseil-cpp) | 20240116.1 | Apache-2.0 | Google Inc. |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | v0.18.3 | MIT | Yuji Hirose (2017) |
| [libuv](https://github.com/libuv/libuv) | v1.48.0 | MIT | libuv project contributors |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | MIT | Niels Lohmann (2013-2022) |
| [oneTBB](https://github.com/oneapi-src/oneTBB) | v2021.11.0 | Apache-2.0 | Intel Corporation |
| [spdlog](https://github.com/gabime/spdlog) | v1.13.0 | MIT | Gabi Melman (2016) |

## C++ Dependencies (Optional)

These libraries are conditionally included based on CMake options.

| Library | Version | License | Copyright | CMake Gate |
|---------|---------|---------|-----------|------------|
| [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) | v0.7.2 | MIT | Dominik Thalhammer (2018) | `ETIL_BUILD_JWT` |
| [mongo-c-driver](https://github.com/mongodb/mongo-c-driver) | 1.28.1 | Apache-2.0 | MongoDB, Inc. | `ETIL_BUILD_MONGODB` |
| [mongo-cxx-driver](https://github.com/mongodb/mongo-cxx-driver) | r3.11.1 | Apache-2.0 | MongoDB, Inc. | `ETIL_BUILD_MONGODB` |
| [replxx](https://github.com/AmokHuginnworworsson/replxx) | release-0.0.4 | BSD-3-Clause | Marcin Konarski, Salvatore Sanfilippo, Pieter Noordhuis | `ETIL_BUILD_EXAMPLES` |

## C++ Dependencies (Testing Only)

These are used exclusively in the test and benchmark suites.

| Library | Version | License | Copyright |
|---------|---------|---------|-----------|
| [Google Test](https://github.com/google/googletest) | v1.14.0 | BSD-3-Clause | Google Inc. (2008) |
| [Google Benchmark](https://github.com/google/benchmark) | v1.8.3 | Apache-2.0 | Google Inc. |

## System Dependencies

These are found via `find_package` and provided by the host system or Docker image.

| Library | License | Notes |
|---------|---------|-------|
| [LLVM](https://llvm.org/) | Apache-2.0 with LLVM Exception | Always required |
| [OpenSSL](https://www.openssl.org/) | Apache-2.0 | Required by `ETIL_BUILD_JWT` / `ETIL_BUILD_HTTP_CLIENT` |
| [jemalloc](https://jemalloc.net/) | BSD-2-Clause | Optional via `ETIL_USE_JEMALLOC` |

## Python Dependencies (TUI Client)

### Direct dependencies

Specified in `tui/pyproject.toml`.

| Package | Version | License |
|---------|---------|---------|
| [textual](https://github.com/Textualize/textual) | >=0.85.0 | MIT |
| [httpx](https://github.com/encode/httpx) | >=0.28.0 | BSD-3-Clause |

### Transitive dependencies

Pulled in automatically by pip.

| Package | License |
|---------|---------|
| anyio | MIT |
| certifi | MPL-2.0 |
| h11 | MIT |
| httpcore | BSD-3-Clause |
| idna | BSD-3-Clause |
| linkify-it-py | MIT |
| markdown-it-py | MIT |
| mdit-py-plugins | MIT |
| mdurl | MIT |
| packaging | Apache-2.0 OR BSD-2-Clause |
| platformdirs | MIT |
| Pygments | BSD-2-Clause |
| rich | MIT |
| typing_extensions | PSF-2.0 |
| uc-micro-py | MIT |
