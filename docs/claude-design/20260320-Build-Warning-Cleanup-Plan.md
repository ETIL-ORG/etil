# Build Warning Cleanup Plan

**Date:** 2026-03-20
**Current Version:** v1.6.0
**Build Environment:** GCC 13.3.0, CMake 3.28, Ninja 1.12, Ubuntu 24.04

---

## Summary

Clean builds produce **131 unique warnings** in debug and **2 unique warnings** in release. Of these, **100 are from dependencies** (Abseil `__int128` pedantic warnings) and **27 are from our code**. The dependency warnings cannot be suppressed without patching upstream headers; the ETIL code warnings are all fixable.

## Warning Census

### Common to Both Builds (ETIL Code)

These warnings appear in both debug and release configurations.

| # | Warning | File | Severity | Fix Complexity |
|---|---------|------|----------|---------------|
| 1 | `[-Wreorder]` member initialization order | `word_impl.hpp:259,254,105` | Low | Easy — reorder `generation_` and `weight_` members to match constructor init list |
| 2 | `[-Wpedantic]` flexible array member | `heap_string.hpp:55` | Low | Suppress — intentional design for single-allocation strings. Add `#pragma GCC diagnostic ignored` around the member |
| 3 | `[-Wmaybe-uninitialized]` `byte` variable | `primitives.cpp:1791` | Low | Easy — initialize `uint8_t byte = 0;` |

### Debug Build Only (ETIL Code)

| # | Warning | File(s) | Severity | Fix Complexity |
|---|---------|---------|----------|---------------|
| 4 | `[-Wswitch]` 8 unhandled enum values | `metadata_json.cpp:68` | Medium | Easy — add `default: break;` or handle all `TypeSignature::Type` values (Boolean, ByteArray, DataRef, Json, Map, Matrix, Observable, Xt added in v1.4.0) |
| 5 | `[-Wswitch]` 3+3 unhandled enum values | `string_primitives.cpp:571,625` | Medium | Easy — add `default: break;` for Json, Matrix, Observable in two `pop_as_tainted_string` switches |
| 6 | `[-Wunused-parameter]` `dict` | `genetic_ops.cpp:43,91` | Low | Easy — `clone()` and `crossover()` take `Dictionary& dict` but don't use it. Remove parameter or `(void)dict;` |
| 7 | `[-Wunused-parameter]` `word` | `evolution_engine.cpp:146` | Low | Easy — `update_weights()` takes `word` but doesn't use it. Remove parameter |
| 8 | `[-Wunused-parameter]` `c` | `observable_primitives.cpp:810` | Low | Easy — lambda captures unused parameter |
| 9 | `[-Wunused-variable]` `quiet_us` | `observable_primitives.cpp:804` | Low | Easy — remove or use the variable |
| 10 | `[-Wunused-variable]` `depth_before` | `stack_simulator.cpp:140` | Low | Easy — was used before Phase 1 cleanup. Remove |
| 11 | `[-Wunused-variable]` `net` | `stack_simulator.cpp:33` | Low | Easy — remove unused variable |
| 12 | `[-Wunused-but-set-variable]` `needs_repair` | `type_repair.cpp:90` | Low | Easy — remove or act on the variable |
| 13 | `[-Wunused-local-typedefs]` `T` | `lvfs_primitives.cpp:204` | Low | Easy — remove unused `using T = ...;` |
| 14 | `[-Wunused-result]` `json::parse()` | `compile_handlers.cpp:145` | Low | Easy — assign or cast to `(void)` |
| 15 | `[-Woverflow]` | (if present, from integer arithmetic) | Low | Check context |

### Debug Build Only (Test Code)

| # | Warning | File | Severity | Fix Complexity |
|---|---------|------|----------|---------------|
| 16 | `[-Wunused-but-set-variable]` `any_changed` | `test_evolution.cpp:63` | Low | Easy — remove or use the variable |
| 17 | `[-Wunused-but-set-variable]` `v1` | `test_matrix_primitives.cpp:751` | Low | Easy — remove the unused pop |

### Release Build Only

| # | Warning | Source | Severity | Fix Complexity |
|---|---------|--------|----------|---------------|
| 18 | `[-Wmaybe-uninitialized]` `byte` | `primitives.cpp:1791` (via LTO) | Low | Same as #3 — initialize variable |
| 19 | `lto-wrapper: serial compilation` | LTO linker | None | Informational — GCC falls back to serial when parallel LTO compilation exceeds resource limits. Not fixable without more RAM |

### Dependency Warnings (Not Fixable)

| # | Warning | Source | Count | Mitigation |
|---|---------|--------|-------|-----------|
| 20 | `[-Wpedantic]` `__int128` | Abseil `int128.h` | 100 | Suppress with `-Wno-pedantic` for Abseil target, or remove `-Wpedantic` from global flags |
| 21 | CMake deprecation | TBB `CMakeLists.txt` | 1 | Upstream issue — `cmake_minimum_required` version too old |
| 22 | CMake message warning | mongo-cxx-driver | 1 | Upstream informational — harmless |

---

## Fix Plan

### Phase A: Zero-Effort Fixes (15 min)

Fix items 1, 3, 10, 11, 12, 13, 16, 17 — trivial one-line changes:

1. **Reorder members in `word_impl.hpp`** — move `weight_` before `generation_` to match init order
2. **Initialize `byte` in `primitives.cpp`** — `uint8_t byte = 0;`
3. **Remove `depth_before` from `stack_simulator.cpp`** — unused after Phase 1 cleanup
4. **Remove `net` from `stack_simulator.cpp`** — unused
5. **Remove `needs_repair` from `type_repair.cpp`** — set but never read
6. **Remove `using T` from `lvfs_primitives.cpp`** — unused typedef
7. **Remove `any_changed` from `test_evolution.cpp`** — set but never read
8. **Remove `v1` pop from `test_matrix_primitives.cpp`** — unused

### Phase B: Switch Coverage (10 min)

Fix items 4, 5 — add `default: break;` to switches on `TypeSignature::Type`:

- `metadata_json.cpp:68` — 8 new enum values since the switch was written
- `string_primitives.cpp:571,625` — 3 new types each

### Phase C: Unused Parameters (5 min)

Fix items 6, 7, 8 — mark unused parameters:

- `genetic_ops.cpp` — `clone(parent, /*dict*/)` and `crossover(a, b, /*dict*/)`
- `evolution_engine.cpp` — `update_weights(/*word*/, results)`
- `observable_primitives.cpp` — lambda parameter

### Phase D: Suppress Dependency Warnings (5 min)

Fix item 20 — suppress Abseil pedantic warnings:

Add to `src/CMakeLists.txt`:
```cmake
# Abseil uses __int128 which triggers -Wpedantic; suppress for Abseil targets
if(TARGET absl::base)
    target_compile_options(absl::base INTERFACE -Wno-pedantic)
endif()
```

Or alternatively, add system include path for Abseil so its headers are treated as system headers (warnings suppressed):
```cmake
target_include_directories(etil SYSTEM PRIVATE ${absl_SOURCE_DIR})
```

### Phase E: Intentional Suppression (2 min)

Fix item 2 — add pragma for `heap_string.hpp`:

```cpp
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    char data_[];  // flexible array member (intentional, single allocation)
#pragma GCC diagnostic pop
```

---

## Expected Result

| Build | Before | After |
|-------|--------|-------|
| Debug (our code) | 27 warnings | 0 warnings |
| Debug (deps) | 100 warnings | 0 warnings (suppressed) |
| Release | 2 warnings | 0 warnings |

All 5 phases can be done in a single commit (~35 min total).

---

## Version

No version bump needed — these are build hygiene changes with zero behavioral impact.
