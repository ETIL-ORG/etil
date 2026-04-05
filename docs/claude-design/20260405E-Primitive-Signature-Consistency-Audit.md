# Primitive Word Signature Consistency Audit

**Date:** 2026-04-05
**Trigger:** After discovering `array-length` and `bytes-length` were silently non-consuming (fixed in v2.0.0), a comprehensive audit of ALL primitive words was requested to find similar signature inconsistencies.
**Ground Truth:** C++ primitive registrations in `src/**/*.cpp`. `data/help.til` is a secondary index that drifts.
**Scope:** ~390 primitive words across 32 categories. This audit focuses on the ~180 words with container interaction, variable I/O, conditional returns, or external-system boundaries. Simple arithmetic / stack / control flow words are listed at the end as **excluded**.

---

## Why This Matters

> "I like consistent signatures because having consistency makes remembering the signatures easier. Having consistent signatures might give mutation a slight boost also..." — project owner

Two stakeholders benefit from consistent signatures:

1. **Humans** — uniform stack-effect conventions reduce cognitive load when writing TIL.
2. **The evolution engine** — `signature_index.cpp` groups words by their declared input/output type vectors. When declarations lie about what a word actually does (e.g., `T::Unknown` instead of `T::Matrix`, or `arity_out = 0` for a word that actually pushes 1-2 values), type-directed mutation can't find the right candidates for type-compatible substitution.

---

## Methodology

For each category (array, map, json, byte-array, matrix, observable, string, conversion, hashing, file-io, http, mongodb, metadata, dictionary, evolution, execution, system, input, i/o) the audit:

1. Extracted the registered input and output type signatures from `dict.register_word(...)` / `make_primitive(...)` / `PRIMS[]` table entries.
2. Read each primitive's C++ implementation to determine actual stack behavior (consuming vs preserving, push-on-failure vs restore-on-failure, flag encoding).
3. Compared against the `data/help.til` stack-effect declaration.
4. Flagged mismatches between any two of these three sources.

The three sources of truth (in order of authority):
1. **Runtime behavior** — what the C++ impl actually pushes/pops
2. **Registration declaration** — the `{in-types}, {out-types}` vectors in registration calls
3. **help.til stack-effect** — human-facing documentation

---

## Major Inconsistency Patterns

### P1 — Flag type declarations split 3 ways: `T::Boolean` vs `T::Integer` vs `T::Unknown`

**The issue:** Every primitive that ends with a `flag` output pushes a `Value(bool)` (confirmed by grepping 88 instances of `push(Value(true))` / `push(Value(false))`). These flags have runtime type `Value::Type::Boolean`. But their *registered* output types are split across three `TypeSignature::Type` variants — and none of them match the runtime reality consistently.

| Registration | Example words | Count |
|---|---|---|
| `T::Boolean` (correct) | `evolve-register`, `dict-forget`, `dict-meta-set/get/del/keys`, `impl-meta-set/get`, `user-notification`, `from-hex`, `sha256=`, `s=`, `s<>`, `staint`, `xt?`, `obs?`, `map-has?`-is-`Integer`-wait-actually, `word-read`, `string-read-delim` | ~15 |
| `T::Integer` (wrong) | `exists?`, `read-file`, `write-file`, `append-file`, `copy-file`, `rename-file`, `lstat`, `readdir`, `mkdir`, `mkdir-tmp`, `rmdir`, `rm`, `truncate`, `mongo-find`, `mongo-count`, `mongo-insert`, `mongo-update`, `mongo-delete`, `http-get`, `http-post`, `map-has?` | ~21 |
| `T::Unknown` (wrong) | `mat-solve`, `mat-inv`, `mat-det`, `mat-eigen`, `mat-svd`, `mat-lstsq` | 6 |

**Concrete example — `exists?`:**
```cpp
// Registration (src/fileio/async_file_io.cpp:692-694)
dict.register_word("exists?",
    make_primitive("exists?", prim_exists,
        {T::String}, {T::Integer}));        // ← declares Integer output

// Implementation (src/fileio/async_file_io.cpp:63)
ctx.data_stack().push(Value(static_cast<bool>(exists)));   // ← pushes Boolean
```

**Impact:**
- The stack simulator and type-directed mutation engine see `T::Integer` outputs and will try to feed them into arithmetic operators — which will then reject them at runtime because they receive a Boolean.
- Words that need a Boolean input (like `if`, `while`, `until`) are blocked from following a file-I/O word in the type-directed bridge map.
- The evolution engine cannot treat `read-file`'s success flag and `evolve-register`'s success flag as the same type, even though both are Booleans at runtime.

**Fix recommendation:** Change all flag outputs to `T::Boolean` in their registrations. ~27 registrations to update.

---

### P2 — Container types mostly declared as `T::Unknown`, not their actual type

**The issue:** `TypeSignature::Type` has explicit enum variants `ByteArray`, `Map`, `Json`, `Matrix`, `Observable`, and `Custom`. Most of them are barely used in registrations — instead, container inputs/outputs are declared as `T::Unknown`.

| Heap type | Registered as | Should be |
|---|---|---|
| `HeapArray` | `T::Array` ✓ | `T::Array` |
| `HeapString` | `T::String` ✓ | `T::String` |
| `HeapByteArray` | **`T::Unknown`** | `T::ByteArray` |
| `HeapMap` | **`T::Unknown`** | `T::Map` |
| `HeapJson` | **`T::Custom`** | `T::Json` |
| `HeapMatrix` | **`T::Unknown`** | `T::Matrix` |
| `HeapObservable` | **`T::Unknown`** | `T::Observable` |

**Concrete examples:**
```cpp
// bytes-new: declares Unknown output, actually pushes ByteArray
dict.register_word("bytes-new", make_primitive(..., {T::Integer}, {T::Unknown}));

// map-new: declares Unknown output, actually pushes Map
dict.register_word("map-new", make_primitive(..., {}, {T::Unknown}));

// mat-new: declares Unknown output, actually pushes Matrix
dict.register_word("mat-new", make_primitive(..., {T::Integer, T::Integer}, {T::Unknown}));

// obs-from: declares Unknown output, actually pushes Observable
dict.register_word("obs-from", mk(..., {T::Array}, {T::Unknown}));

// json-parse: uses T::Custom (inconsistent with the 5 other cases using Unknown)
dict.register_word("json-parse", make_primitive(..., {T::String}, {T::Custom}));
```

**Impact on mutation:** The evolution engine's type-directed substitution (TDB) relies on `SignatureIndex::find_type_compatible()` to filter candidate words by TOS type. When `bytes-new`, `map-new`, `mat-new`, `obs-from` all declare `T::Unknown` outputs, the mutation engine can't distinguish "which word produces a matrix" from "which word produces an observable" from "which word produces a byte array." They all look like the same `T::Unknown → T::Unknown` shape.

**Concrete miss:** A word that needs a `Matrix` input (e.g., `mat-transpose`) cannot be automatically inserted after `mat-new` by type-directed mutation, because `mat-new`'s output is declared as `Unknown`. The bridge map has to be used as a workaround, but that only works if someone manually registered the bridges.

**Fix recommendation:** Change registrations to use the specific type variant. ~80+ registrations to update across byte_primitives.cpp, map_primitives.cpp, matrix_primitives.cpp, observable_primitives.cpp, json_primitives.cpp. This is the single highest-impact change for mutation quality.

---

### P3 — Variable-arity outputs: declared but not flagged

**The issue:** The CLAUDE.md "Conditional Return Convention" says:
> Words that can succeed or fail use Boolean-flag-at-TOS:
> - Success: TOS = true, result values below
> - Failure: TOS = false, no additional results

This means conditional-return words push 2 (or more) values on success but only 1 (`false`) on failure. `TypeSignature` has a `variable_outputs` flag for exactly this case. But `make_primitive()`'s public signature doesn't expose it, so **no primitive registration sets it**. The flag is only ever set by `stack_simulator.cpp`.

As a consequence, `signature_index.cpp:23` explicitly **skips** any word with `variable_outputs = true` from type-directed mutation — but since no primitive sets this flag, no primitive is actually skipped. The engine treats all conditional-return words as if they unconditionally produced their "success-case" arity, which is wrong.

**Affected primitives** (push N outputs on success, 1 on failure):

| Word | Success | Failure | Registered arity_out | help.til |
|---|---|---|---|---|
| `read-file` | `(string, true)` | `(false)` | 2 | `( path -- string? flag )` ✓ |
| `from-hex` | `(byte-array, true)` | `(false)` | 2 | `( string -- byte-array true \| false )` ✓ |
| `string->number` | `(value, true)` | `(false)` | **0** ✗ (bug) | `( str -- value -1 \| 0 )` ✗ |
| `sregex-search` | `(array, true)` | `(false)` | 2 | `( str pattern -- match-array -1 \| 0 )` ✗ |
| `sregex-match` | `(array, true)` | `(false)` | 2 | `( str pattern -- match-array -1 \| 0 )` ✗ |
| `word-read` | `(str, true)` | `(false)` | 2 | `( -- str flag )` ~ |
| `string-read-delim` | `(str, true)` | `(false)` | 2 | `( char -- str flag )` ~ |
| `lstat` | `(array, true)` | `(false)` | 2 | `( path -- array? flag )` ✓ |
| `readdir` | `(array, true)` | `(false)` | 2 | `( path -- array? flag )` ✓ |
| `mkdir-tmp` | `(string, true)` | `(false)` | 2 | (missing) |
| `dict-meta-get` | `(content, true)` | `(false)` | 2 | `( word-str key-str -- content-str flag )` ~ |
| `dict-meta-keys` | `(array, true)` | `(false)` | 2 | `( word-str -- array flag )` ~ |
| `impl-meta-get` | `(content, true)` | `(false)` | 2 | `( word-str key-str -- content-str flag )` ~ |
| `mongo-find` | `(json, true)` | `(false)` | 2 | `( coll filter opts -- json flag )` ~ |
| `mongo-count` | `(int, true)` | `(false)` | 2 | `( coll filter opts -- count flag )` ~ |
| `mongo-insert` | `(str, true)` | `(false)` | 2 | `( coll doc -- inserted-id flag )` ~ |
| `mongo-update` | `(int, true)` | `(false)` | 2 | `( coll filter update opts -- count flag )` ~ |
| `mongo-delete` | `(int, true)` | `(false)` | 2 | `( coll filter opts -- count flag )` ~ |
| `http-get` | `(bytes, code, true)` | `(false)` ?? | 3 | (missing) |
| `http-post` | `(bytes, code, true)` | `(false)` ?? | 3 | (missing) |
| `mat-solve` | `(x, true)` | `(false)` | 2 | `( A b -- x flag )` ~ |
| `mat-inv` | `(inv, true)` | `(false)` | 2 | `( mat -- inv flag )` ~ |
| `mat-det` | `(det, true)` | `(false)` | 2 | `( mat -- det flag )` ~ |
| `mat-eigen` | `(vals, vecs, true)` | `(false)` | 3 | `( mat -- eigenvalues eigenvectors flag )` ~ |
| `mat-svd` | `(U, S, Vt, true)` | `(false)` | 4 | `( mat -- U S Vt flag )` ~ |
| `mat-lstsq` | `(x, true)` | `(false)` | 2 | `( A b -- x flag )` ~ |
| `s'` | `(xt, true)` | `(false)` | 2 | `( name-str -- xt true \| false )` ✓ |

Legend: ✓ = help.til matches the `X true | false` convention explicitly; ~ = help.til shows the success arity only, doesn't show failure; ✗ = help.til wrong.

**Count:** ~26 primitives with variable arity, none of them flagged as such in their registration.

**Fix recommendation:**
1. Extend `make_primitive()` with an overload that accepts `variable_outputs = true`.
2. Update all 26 registrations to set the flag.
3. The evolution engine already handles this correctly — it will automatically exclude these from candidate pools.
4. Alternative (more ambitious): introduce a `ConditionalReturn` construct that declares both the success and failure output vectors, so the mutation engine can still reason about them.

---

### P4 — `string->number` arity_out is 0 but it pushes 1–2 values

**The issue:** `string->number` is declared as:
```cpp
// src/core/primitives.cpp:3539
{"string->number", prim_string_to_number, 1, 0, {T::String},  {}},
```

`arity_out = 0`, empty output type vector. But the impl pushes either `(value, true)` on success or `(false)` on failure. This is an outright bug in the registration — the stack simulator and evolution engine believe this word produces nothing, so they will never compose it with downstream consumers.

**Fix recommendation:** Change to `arity_out = 2, {T::Unknown, T::Boolean}` with `variable_outputs = true` once the flag is exposed (per P3). Interim fix: `arity_out = 2`.

---

### P5 — Boolean flags encoded as `true`/`false` in code, documented as `-1`/`0` in help.til

**The issue:** Classical FORTH used `-1` (all-bits-set) and `0` for true/false. ETIL switched to a dedicated `Boolean` value type (see CLAUDE.md: "Comparisons, predicates, and logical ops return `Value(bool)`. Control flow requires Boolean on TOS"). But three help.til entries still describe the classical-FORTH convention:

| Word | help.til says | Actual |
|---|---|---|
| `string->number` | `( str -- value -1 \| 0 )` | Pushes `true`/`false` Booleans |
| `sregex-search` | `( str pattern -- match-array -1 \| 0 )` | Pushes `true`/`false` Booleans |
| `sregex-match` | `( str pattern -- match-array -1 \| 0 )` | Pushes `true`/`false` Booleans |

**Fix recommendation:** Update help.til to use `true | false` or `array? flag` notation consistently.

---

### P6 — `sregex-find` documented with wrong arity

**The issue:** help.til declares `sregex-find ( str pattern -- match flag )` (arity 2 out), but the C++ impl returns a single integer position (or `-1` if no match):

```cpp
// src/core/string_primitives.cpp:312
int64_t result = -1;
// ... regex_search sets result = match.position(0) ...
ctx.data_stack().push(Value(result));   // ← single integer, not (match, flag)
```

Registration correctly says `{T::Integer}` (arity_out=1). help.til is wrong.

**Fix recommendation:** Update help.til to `( str pattern -- index )` — matches `sfind` convention.

---

### P7 — help.til missing entries for live primitives

**The issue:** Several registered primitives have no stack-effect entry in help.til:

- `exists?` (file-io-async)
- `append-file` (file-io-async)
- `mkdir-tmp` (file-io-async)
- `rmdir` (file-io-async)
- `rm` (file-io-async)
- `truncate` (file-io-async)
- `copy-file` (file-io-async)
- `rename-file` (file-io-async)
- `http-get` (http)
- `http-post` (http)
- (others, worth a systematic help-coverage audit)

**Fix recommendation:** Run a diff of registered primitive names vs documented primitive names in help.til and add the missing entries.

---

### P8 — `json->array` and `json->map` declare their outputs as `T::Custom`

**The issue:** `json->array` should produce an `Array`, and `json->map` should produce a `Map`. But both are declared as:
```cpp
dict.register_word("json->array", make_primitive(..., {T::Custom}, {T::Custom}));
dict.register_word("json->map",   make_primitive(..., {T::Custom}, {T::Custom}));
```

Similarly, `json-keys` returns an array but is declared `{T::Custom}, {T::Custom}`.

**Fix recommendation:** Set correct output types: `{T::Array}`, `{T::Map}`, `{T::Array}` respectively. This overlaps with P2.

---

### P9 — `http-get` / `http-post` return arity is ambiguous

**The issue:** Both are registered as `{T::Unknown, T::Integer, T::Integer}` — **3 outputs**. But the variable-arity question (P3) applies: what happens on failure? The audit didn't fully verify their failure path. help.til doesn't document them. Need impl inspection to confirm whether they follow `(bytes, status-code, true)` / `(false)` or always push 3 values.

**Fix recommendation:** Verify failure path; document in help.til; apply variable_outputs flag if conditional.

---

## Consistency Wins — Patterns That ARE Working

These patterns were verified across the entire interesting subset and are **internally consistent**:

### W1 — Length-get ops are all consuming (post v2.0.0)

All five length-get words consume their container and push only the length:

| Word | Signature |
|---|---|
| `slength` | `( str -- n )` |
| `array-length` | `( array -- n )` |
| `bytes-length` | `( bytes -- n )` |
| `map-length` | `( map -- n )` |
| `json-length` | `( json -- n )` |

### W2 — Getter ops are all consuming

All four single-element getters consume the container, return the element:

| Word | Signature |
|---|---|
| `array-get` | `( array index -- value )` |
| `bytes-get` | `( bytes index -- value )` |
| `map-get` | `( map key -- val )` |
| `json-get` | `( json key\|index -- value )` |

### W3 — Mutator ops consistently return the container

All container-mutating ops return the container as TOS:

| Word | Signature |
|---|---|
| `array-push` | `( array value -- array )` |
| `array-pop` | `( array -- array value )` |
| `array-set` | `( array index value -- array )` |
| `array-shift` | `( array -- array value )` |
| `array-unshift` | `( array value -- array )` |
| `bytes-set` | `( bytes index value -- bytes )` |
| `bytes-resize` | `( bytes n -- bytes )` |
| `map-set` | `( map key val -- map )` |
| `map-remove` | `( map key -- map )` |
| `mat-set` | `( mat row col val -- mat )` |

### W4 — Reader ops that return projections consume the container

Readers that return a projection (keys, values, transformed view) consume the source:

| Word | Behavior |
|---|---|
| `map-keys` / `map-values` | Consume map, return new array |
| `json-keys` | Consumes json, returns array |
| `array-reverse` / `array-sort` / `array-compact` | Consume in, return new |
| `array-map` / `array-filter` | Consume in, return new |
| `array-reduce` | Consumes, returns scalar |
| `array-each` | Consumes, returns nothing (iteration) |
| `bytes->string` / `string->bytes` | Consume in, return converted |
| `mat-transpose`, `mat-relu`, etc. | Consume in, return new |

### W5 — Observable pipeline ops are consistent

All `obs-*` transformation ops follow `( obs ...args -- obs' )` pattern. The input observable is consumed by the pipeline combinator; a new derived observable is returned. This is the RxJS-style chaining contract and it's held uniformly across ~60 observable primitives.

### W6 — Error handling via "restore stack on partial-pop failure"

The CLAUDE.md pattern ("restore stack on partial pop failure") is held consistently across multi-argument primitives. E.g., `prim_array_set` restores `idx` and `val` if the third pop (array) fails. Verified in array, byte, map, json, matrix primitives.

---

## Lesser Observations

### L1 — Boolean `true`/`false` vs Integer `-1`/`0` sentinel

- `sfind` / `sregex-find` use an **integer** sentinel (`-1` for not-found). This is an integer-returning word, not a flag-returning word. Consistent with each other.
- `map-has?` uses `{T::Integer}` in its registration but should probably use `{T::Boolean}`. Let me check the impl.
- Everything else uses typed Boolean `true`/`false`.

### L2 — help.til aliases for the long-form meta words

`data/builtins.til` defines aliases:
```til
: meta!      dict-meta-set ;
: meta@      dict-meta-get ;
: meta-del   dict-meta-del ;
: meta-keys  dict-meta-keys ;
: impl-meta! impl-meta-set ;
: impl-meta@ impl-meta-get ;
```

Both the primitives and the aliases are documented separately in help.til. This is minor duplication — acceptable for user ergonomics, but worth noting.

### L3 — `json->mat` / `mat->json` exist, no `array->mat`?

`matrix_primitives.cpp` has `array->mat` and `mat->array`, consistent naming. JSON conversions use `json->mat`, `mat->json`. Naming is consistent: `SOURCE->TARGET`.

### L4 — Evolution config words

The `evolve-*` setter words are uniformly `( value -- )` with no return, no flag. Clean and consistent. Example:
```
evolve-fitness-mode    ( n -- )
evolve-fitness-alpha   ( x -- )
evolve-instruction-budget ( n -- )
evolve-tbbp-enabled?   ( flag -- )
evolve-log-dir         ( dir-str -- )
```

The one exception: `evolve-mutation-weights ( sub per mov ctl grw shr -- )` — takes 6 floats, which is fine given the domain.

---

## Prioritized Fix Recommendations

**High impact (affects mutation engine quality):**

1. **P2** — Fix container type declarations. Change `T::Unknown` → `T::ByteArray` / `T::Map` / `T::Matrix` / `T::Observable` in ~80 registrations across byte/map/matrix/observable primitives. Also: `T::Custom` → `T::Json` for json primitives. **This is the biggest win for type-directed mutation.**

2. **P3** — Expose `variable_outputs` in `make_primitive()`. Set the flag on ~26 conditional-return words. Current behavior silently mis-types them.

3. **P1** — Fix flag output types. Change `T::Integer` → `T::Boolean` (and `T::Unknown` → `T::Boolean`) in ~27 flag-returning registrations.

**Medium impact:**

4. **P4** — Fix `string->number` `arity_out = 0 → 2`.

5. **P8** — Fix `json->array`, `json->map`, `json-keys` output types (specific case of P2).

**Low impact but easy:**

6. **P5** / **P6** / **P7** — help.til drift. Fix `-1/0` → `true/false` in docs, fix `sregex-find` arity, add missing entries (`exists?`, `append-file`, etc.).

**Future:**

7. **New feature (not a bug):** Introduce a notation for conditional-return words in help.til. Current `( str -- value -1 | 0 )` is inconsistent; propose standard like `( X -- value flag? )` or `( X -- value? flag )` where `?` marks values that only appear on success.

8. Consider an audit of `map-has?` — it returns a Boolean per the help.til stack-effect (`( map key -- bool )`) but is registered as `{T::Integer}`. Verify impl.

---

## Impact Summary

| Pattern | Primitives affected | Evolution engine impact |
|---|---|---|
| P1 — flag type misdeclared | ~27 | Breaks type-directed mutation across file-I/O / mongo / http / matrix solvers |
| P2 — container type → `Unknown` | ~80 | Major: ByteArray/Map/Matrix/Observable words can't be type-matched |
| P3 — variable_outputs unset | ~26 | Words silently produce wrong declared arity; mutation mispredicts stack |
| P4 — `string->number` arity wrong | 1 | Can't be chained |
| P5/P6/P7 — help.til drift | ~10+ | No engine impact, only human-facing |
| P8 — json conversion output types | 3 | Overlaps with P2 |
| P9 — http arity unverified | 2 | Unknown until verified |

**Approximately 130+ registrations need updating** to fully normalize the primitive signature space. The highest-value change by far is P2 (container types) — it directly affects the mutation engine's ability to find type-compatible substitutes.

---

## Excluded Words — Simple Words Not Audited

Per the request, simple math/stack-manipulation/control-flow words are excluded from the consistency audit. They are already uniform within their categories.

### Arithmetic (uniform: takes N numbers, returns 1 number or 2)
`+`, `-`, `*`, `/`, `mod`, `/mod`, `negate`, `abs`, `max`, `min`, `1+`, `1-`

### Math (uniform: takes 1-2 floats, returns 1 float)
`sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `log`, `log2`, `log10`, `exp`, `pow`, `ceil`, `floor`, `round`, `trunc`, `fmin`, `fmax`, `pi`, `f~`

### Stack manipulation (uniform: manipulates stack cells)
`dup`, `drop`, `swap`, `over`, `rot`, `pick`, `nip`, `tuck`, `depth`, `?dup`, `roll`, `-rot`

### Comparison (uniform: takes 2 values, returns Boolean)
`=`, `<>`, `<`, `>`, `<=`, `>=`, `0=`, `0<`, `0>`, `within`

### Logic / bit ops (uniform)
`and`, `or`, `xor`, `invert`, `not`, `bool`, `lshift`, `rshift`, `lroll`, `rroll`

### Control flow (uniform: compile-only structure)
`if`, `else`, `then`, `do`, `loop`, `+loop`, `i`, `j`, `begin`, `until`, `while`, `repeat`, `again`, `leave`, `exit`, `recurse`

### Return stack (uniform)
`>r`, `r>`, `r@`

### Compiling (uniform: no stack effect or fixed)
`:`, `;`, `does>`, `immediate`, `create`, `screate`, `variable`, `constant`

### Memory (uniform)
`,`, `@`, `!`, `allot`, `cell-get`, `cell-set`

### Boolean literals
`true`, `false`

### Number base printing
`hex.`, `bin.`, `oct.`

### Time words (uniform: take / return a time value)
`time-us`, `us->iso`, `us->iso-us`, `time-iso`, `time-iso-us`, `us->jd`, `jd->us`, `us->mjd`, `mjd->us`, `time-jd`, `time-mjd`, `sleep`, `elapsed`

### LVFS shortcuts (uniform: `( -- )`)
`cwd`, `cd`, `ls`, `ll`, `lr`, `cat`

### I/O output (uniform: consume what they print)
`.`, `cr`, `emit`, `space`, `spaces`, `words`, `s.`, `.s`, `dump`, `see`

### System
`sys-semver`, `sys-timestamp`, `sys-datafields`, `sys-notification`

### Conversion (simple, not container-related)
`int->float`, `float->int`, `number->string`

### Random
`random`, `random-seed`, `random-range`

### Forget / markers
`forget`, `forget-all`, `marker`, `marker-restore`, `include`, `library`, `evaluate`, `file-load`

### Selection engine config
`select-strategy`, `select-epsilon`, `select-off`

### Execution primitives (single-behavior)
`'`, `execute`, `>name`, `xt-body`, `xt?`

### Abort / help
`abort`, `help`

---

## Summary

The audit found **9 distinct inconsistency patterns** across the primitive surface. The two that most affect the evolution engine's mutation quality are **P2 (container type declarations using `T::Unknown`)** and **P3 (variable-arity not flagged on conditional-return words)**. Fixing these two would normalize ~100+ registrations and substantially improve type-directed mutation candidate selection.

The **consistency wins** section documents six patterns that *are* held uniformly — these are the conventions to preserve when fixing the inconsistencies: container consumption rules, error-restoration discipline, and the observable pipeline contract.

Post-v2.0.0 fixes (array-length/bytes-length consuming semantics) are already reflected in W1. The remaining work is primarily **registration metadata** (type declarations) rather than runtime behavior — which means it's a relatively safe refactor: no behavior change, only improved type-system fidelity.
