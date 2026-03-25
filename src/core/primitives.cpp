// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/primitives.hpp"
#include "etil/core/compiled_body.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_matrix.hpp"
#include "etil/core/heap_observable.hpp"
#include "etil/core/json_primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_primitives.hpp"
#include "etil/fileio/async_file_io.hpp"
#include "etil/fileio/file_io_primitives.hpp"
#include "etil/lvfs/lvfs.hpp"
#include "etil/mcp/role_permissions.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/selection/selection_engine.hpp"
#include "etil/evolution/evolution_engine.hpp"
#include "etil/core/metadata_json.hpp"
#include "etil/core/version.hpp"
#include "absl/container/flat_hash_map.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

namespace etil::core {

namespace {

// Helper: pop two operands. Returns false on underflow, restoring any
// partially popped value. On success, 'a' is deeper (pushed first),
// 'b' is top-of-stack (pushed second).
bool pop_two(ExecutionContext& ctx, Value& a, Value& b) {
    auto opt_b = ctx.data_stack().pop();
    if (!opt_b) return false;
    auto opt_a = ctx.data_stack().pop();
    if (!opt_a) {
        ctx.data_stack().push(*opt_b);
        return false;
    }
    a = *opt_a;
    b = *opt_b;
    return true;
}

// Helper: human-readable type name.
const char* type_name(Value::Type t) {
    switch (t) {
    case Value::Type::Integer:   return "integer";
    case Value::Type::Float:     return "float";
    case Value::Type::Boolean:   return "boolean";
    case Value::Type::String:    return "string";
    case Value::Type::DataRef:   return "dataref";
    case Value::Type::Array:     return "array";
    case Value::Type::ByteArray: return "bytearray";
    case Value::Type::Map:       return "map";
    case Value::Type::Json:      return "json";
    case Value::Type::Matrix:    return "matrix";
    case Value::Type::Observable: return "observable";
    case Value::Type::Xt:        return "xt";
    }
    return "unknown";
}

// Arithmetic helper: perform op on two values with type promotion.
// Int op Int → Int;  any Float → Float.  Booleans rejected.
template<typename IntOp, typename FloatOp>
bool arith_binary(ExecutionContext& ctx, IntOp int_op, FloatOp float_op) {
    Value a, b;
    if (!pop_two(ctx, a, b)) return false;

    if (a.type == Value::Type::Boolean || b.type == Value::Type::Boolean) {
        ctx.err() << "Error: arithmetic on boolean\n";
        ctx.data_stack().push(a);
        ctx.data_stack().push(b);
        return false;
    }

    if (is_heap_value(a) || is_heap_value(b)) {
        ctx.err() << "Error: arithmetic on " << type_name(is_heap_value(a) ? a.type : b.type) << "\n";
        ctx.data_stack().push(a);
        ctx.data_stack().push(b);
        return false;
    }

    if (a.type == Value::Type::Integer && b.type == Value::Type::Integer) {
        ctx.data_stack().push(Value(int_op(a.as_int, b.as_int)));
    } else {
        double fa = (a.type == Value::Type::Float) ? a.as_float
                                                    : static_cast<double>(a.as_int);
        double fb = (b.type == Value::Type::Float) ? b.as_float
                                                    : static_cast<double>(b.as_int);
        ctx.data_stack().push(Value(float_op(fa, fb)));
    }
    return true;
}

// Helper for division primitives: pops two values, checks for zero divisor.
// On failure (underflow or zero divisor), restores the stack and returns false.
// On success, sets a, b, and the promoted float values fa, fb.
// 'both_int' is true when both operands are integers.
bool pop_for_division(ExecutionContext& ctx, Value& a, Value& b,
                      double& fa, double& fb, bool& both_int) {
    if (!pop_two(ctx, a, b)) return false;

    if (a.type == Value::Type::Boolean || b.type == Value::Type::Boolean) {
        ctx.err() << "Error: arithmetic on boolean\n";
        ctx.data_stack().push(a);
        ctx.data_stack().push(b);
        return false;
    }

    both_int = (a.type == Value::Type::Integer && b.type == Value::Type::Integer);

    if (both_int) {
        if (b.as_int == 0) {
            ctx.data_stack().push(a);
            ctx.data_stack().push(b);
            return false;
        }
    } else {
        fa = (a.type == Value::Type::Float) ? a.as_float
                                             : static_cast<double>(a.as_int);
        fb = (b.type == Value::Type::Float) ? b.as_float
                                             : static_cast<double>(b.as_int);
        if (fb == 0.0) {
            ctx.data_stack().push(a);
            ctx.data_stack().push(b);
            return false;
        }
    }
    return true;
}

// Comparison helper: pops two values, applies comparator, pushes Boolean.
// Promotes to double if either operand is float.
// Booleans are treated as 0/1 integers for comparison.
template<typename IntCmp, typename FloatCmp>
bool compare_binary(ExecutionContext& ctx, IntCmp int_cmp, FloatCmp float_cmp) {
    Value a, b;
    if (!pop_two(ctx, a, b)) return false;

    // Helper: extract numeric value, treating Boolean as 0/1 integer.
    auto is_int_like = [](const Value& v) {
        return v.type == Value::Type::Integer || v.type == Value::Type::Boolean;
    };
    auto int_val = [](const Value& v) -> int64_t { return v.as_int; };

    bool result;
    if (is_int_like(a) && is_int_like(b)) {
        result = int_cmp(int_val(a), int_val(b));
    } else {
        double fa = (a.type == Value::Type::Float) ? a.as_float
                                                    : static_cast<double>(int_val(a));
        double fb = (b.type == Value::Type::Float) ? b.as_float
                                                    : static_cast<double>(int_val(b));
        result = float_cmp(fa, fb);
    }
    ctx.data_stack().push(Value(result));
    return true;
}

} // anonymous namespace

bool prim_add(ExecutionContext& ctx) {
    return arith_binary(ctx,
        [](int64_t a, int64_t b) { return a + b; },
        [](double a, double b) { return a + b; });
}

bool prim_sub(ExecutionContext& ctx) {
    return arith_binary(ctx,
        [](int64_t a, int64_t b) { return a - b; },
        [](double a, double b) { return a - b; });
}

bool prim_mul(ExecutionContext& ctx) {
    return arith_binary(ctx,
        [](int64_t a, int64_t b) { return a * b; },
        [](double a, double b) { return a * b; });
}

bool prim_div(ExecutionContext& ctx) {
    Value a, b;
    double fa, fb;
    bool both_int;
    if (!pop_for_division(ctx, a, b, fa, fb, both_int)) return false;

    if (both_int) {
        ctx.data_stack().push(Value(a.as_int / b.as_int));
    } else {
        ctx.data_stack().push(Value(fa / fb));
    }
    return true;
}

bool prim_mod(ExecutionContext& ctx) {
    Value a, b;
    double fa, fb;
    bool both_int;
    if (!pop_for_division(ctx, a, b, fa, fb, both_int)) return false;

    if (both_int) {
        ctx.data_stack().push(Value(a.as_int % b.as_int));
    } else {
        ctx.data_stack().push(Value(std::fmod(fa, fb)));
    }
    return true;
}

bool prim_divmod(ExecutionContext& ctx) {
    Value a, b;
    double fa, fb;
    bool both_int;
    if (!pop_for_division(ctx, a, b, fa, fb, both_int)) return false;

    if (both_int) {
        ctx.data_stack().push(Value(a.as_int % b.as_int));  // remainder
        ctx.data_stack().push(Value(a.as_int / b.as_int));  // quotient
    } else {
        ctx.data_stack().push(Value(std::fmod(fa, fb)));     // remainder
        ctx.data_stack().push(Value(fa / fb));               // quotient
    }
    return true;
}

bool prim_negate(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Boolean) {
        ctx.err() << "Error: arithmetic on boolean\n";
        ctx.data_stack().push(*opt);
        return false;
    }
    if (opt->type == Value::Type::Integer) {
        ctx.data_stack().push(Value(-opt->as_int));
    } else {
        ctx.data_stack().push(Value(-opt->as_float));
    }
    return true;
}

bool prim_abs(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Boolean) {
        ctx.err() << "Error: arithmetic on boolean\n";
        ctx.data_stack().push(*opt);
        return false;
    }
    if (opt->type == Value::Type::Integer) {
        ctx.data_stack().push(Value(opt->as_int < 0 ? -opt->as_int : opt->as_int));
    } else {
        ctx.data_stack().push(Value(std::fabs(opt->as_float)));
    }
    return true;
}

static bool prim_minmax(ExecutionContext& ctx, bool want_max) {
    auto opt_b = ctx.data_stack().pop();
    if (!opt_b) return false;
    auto opt_a = ctx.data_stack().pop();
    if (!opt_a) { ctx.data_stack().push(*opt_b); return false; }
    if (opt_a->type == Value::Type::Float || opt_b->type == Value::Type::Float) {
        double a = (opt_a->type == Value::Type::Float) ? opt_a->as_float : static_cast<double>(opt_a->as_int);
        double b = (opt_b->type == Value::Type::Float) ? opt_b->as_float : static_cast<double>(opt_b->as_int);
        bool pick_a = want_max ? (a >= b) : (a <= b);
        ctx.data_stack().push(Value(pick_a ? a : b));
    } else {
        bool pick_a = want_max ? (opt_a->as_int >= opt_b->as_int) : (opt_a->as_int <= opt_b->as_int);
        ctx.data_stack().push(Value(pick_a ? opt_a->as_int : opt_b->as_int));
    }
    return true;
}
bool prim_max(ExecutionContext& ctx) { return prim_minmax(ctx, true); }
bool prim_min(ExecutionContext& ctx) { return prim_minmax(ctx, false); }

// within ( n lo hi -- flag ) — true if lo <= n < hi
bool prim_within(ExecutionContext& ctx) {
    Value hi, lo, n;
    auto opt_hi = ctx.data_stack().pop();
    if (!opt_hi) return false;
    hi = *opt_hi;
    auto opt_lo = ctx.data_stack().pop();
    if (!opt_lo) { ctx.data_stack().push(hi); return false; }
    lo = *opt_lo;
    auto opt_n = ctx.data_stack().pop();
    if (!opt_n) { ctx.data_stack().push(lo); ctx.data_stack().push(hi); return false; }
    n = *opt_n;

    // All must be numeric (Integer or Float)
    auto to_double = [](const Value& v, double& out) -> bool {
        if (v.type == Value::Type::Integer) { out = static_cast<double>(v.as_int); return true; }
        if (v.type == Value::Type::Float) { out = v.as_float; return true; }
        return false;
    };
    double dn, dlo, dhi;
    if (!to_double(n, dn) || !to_double(lo, dlo) || !to_double(hi, dhi)) {
        ctx.data_stack().push(n);
        ctx.data_stack().push(lo);
        ctx.data_stack().push(hi);
        return false;
    }
    ctx.data_stack().push(Value(dlo <= dn && dn < dhi));
    return true;
}

// --- Stack manipulation primitives ---

bool prim_dup(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().top();
    if (!opt) return false;
    opt->addref();  // one value in two stack slots = 2 refs
    ctx.data_stack().push(*opt);
    return true;
}

bool prim_drop(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    opt->release();
    return true;
}

bool prim_swap(ExecutionContext& ctx) {
    Value a, b;
    if (!pop_two(ctx, a, b)) return false;
    ctx.data_stack().push(b);
    ctx.data_stack().push(a);
    return true;
}

bool prim_over(ExecutionContext& ctx) {
    Value a, b;
    if (!pop_two(ctx, a, b)) return false;
    a.addref();  // 'a' appears in two stack slots = +1 ref
    ctx.data_stack().push(a);
    ctx.data_stack().push(b);
    ctx.data_stack().push(a);
    return true;
}

bool prim_rot(ExecutionContext& ctx) {
    // ( a b c -- b c a )
    auto opt_c = ctx.data_stack().pop();
    if (!opt_c) return false;
    auto opt_b = ctx.data_stack().pop();
    if (!opt_b) {
        ctx.data_stack().push(*opt_c);
        return false;
    }
    auto opt_a = ctx.data_stack().pop();
    if (!opt_a) {
        ctx.data_stack().push(*opt_b);
        ctx.data_stack().push(*opt_c);
        return false;
    }
    ctx.data_stack().push(*opt_b);
    ctx.data_stack().push(*opt_c);
    ctx.data_stack().push(*opt_a);
    return true;
}

bool prim_pick(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    int64_t index = opt->as_int;
    size_t depth = ctx.data_stack().size();
    if (index < 0 || static_cast<size_t>(index) >= depth) {
        ctx.err() << "Illegal pick index " << index << ".\n";
        return false;
    }
    // operator[] is 0=bottom; we need 0=TOS, so convert
    size_t array_idx = depth - 1 - static_cast<size_t>(index);
    Value v = ctx.data_stack()[array_idx];
    v.addref();  // value now in two stack slots
    ctx.data_stack().push(v);
    return true;
}

bool prim_nip(ExecutionContext& ctx) {
    // ( x1 x2 -- x2 )
    auto opt_x2 = ctx.data_stack().pop();
    if (!opt_x2) return false;
    auto opt_x1 = ctx.data_stack().pop();
    if (!opt_x1) { ctx.data_stack().push(*opt_x2); return false; }
    opt_x1->release();  // x1 discarded
    ctx.data_stack().push(*opt_x2);
    return true;
}

bool prim_tuck(ExecutionContext& ctx) {
    // ( x1 x2 -- x2 x1 x2 )
    auto opt_x2 = ctx.data_stack().pop();
    if (!opt_x2) return false;
    auto opt_x1 = ctx.data_stack().pop();
    if (!opt_x1) { ctx.data_stack().push(*opt_x2); return false; }
    opt_x2->addref();  // x2 now in two stack slots
    ctx.data_stack().push(*opt_x2);
    ctx.data_stack().push(*opt_x1);
    ctx.data_stack().push(*opt_x2);
    return true;
}

bool prim_depth(ExecutionContext& ctx) {
    ctx.data_stack().push(Value(static_cast<int64_t>(ctx.data_stack().size())));
    return true;
}

bool prim_qdup(ExecutionContext& ctx) {
    // ( x -- x x | 0 )  duplicate if non-zero
    auto opt = ctx.data_stack().top();
    if (!opt) return false;
    bool is_zero = (opt->type == Value::Type::Integer && opt->as_int == 0) ||
                   (opt->type == Value::Type::Float && opt->as_float == 0.0) ||
                   (opt->type == Value::Type::Boolean && !opt->as_bool());
    if (!is_zero) {
        opt->addref();
        ctx.data_stack().push(*opt);
    }
    return true;
}

bool prim_roll(ExecutionContext& ctx) {
    // ( xu ... x0 u -- xu-1 ... x0 xu )  move Nth item to TOS, removing it
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    int64_t index = opt->as_int;
    size_t depth = ctx.data_stack().size();
    if (index < 0 || static_cast<size_t>(index) >= depth) {
        ctx.err() << "Illegal roll index " << index << ".\n";
        return false;
    }
    if (index == 0) return true;  // 0 roll is a no-op
    // operator[] is 0=bottom; convert to bottom-based index
    size_t array_idx = depth - 1 - static_cast<size_t>(index);
    Value v = ctx.data_stack()[array_idx];
    // Shift elements down to fill the gap
    for (size_t i = array_idx; i < depth - 1; ++i) {
        ctx.data_stack()[i] = ctx.data_stack()[i + 1];
    }
    // Overwrite TOS with the extracted value (no refcount change — moved, not copied)
    ctx.data_stack()[depth - 1] = v;
    return true;
}

// --- Comparison primitives ---

bool prim_eq(ExecutionContext& ctx) {
    return compare_binary(ctx,
        [](int64_t a, int64_t b) { return a == b; },
        [](double a, double b) { return a == b; });
}

bool prim_neq(ExecutionContext& ctx) {
    return compare_binary(ctx,
        [](int64_t a, int64_t b) { return a != b; },
        [](double a, double b) { return a != b; });
}

bool prim_lt(ExecutionContext& ctx) {
    return compare_binary(ctx,
        [](int64_t a, int64_t b) { return a < b; },
        [](double a, double b) { return a < b; });
}

bool prim_gt(ExecutionContext& ctx) {
    return compare_binary(ctx,
        [](int64_t a, int64_t b) { return a > b; },
        [](double a, double b) { return a > b; });
}

bool prim_le(ExecutionContext& ctx) {
    return compare_binary(ctx,
        [](int64_t a, int64_t b) { return a <= b; },
        [](double a, double b) { return a <= b; });
}

bool prim_ge(ExecutionContext& ctx) {
    return compare_binary(ctx,
        [](int64_t a, int64_t b) { return a >= b; },
        [](double a, double b) { return a >= b; });
}

bool prim_zero_eq(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    bool result;
    if (opt->type == Value::Type::Boolean) result = !opt->as_bool();
    else if (opt->type == Value::Type::Integer) result = (opt->as_int == 0);
    else result = (opt->as_float == 0.0);
    ctx.data_stack().push(Value(result));
    return true;
}

bool prim_zero_lt(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    bool result;
    if (opt->type == Value::Type::Boolean) result = false;  // booleans are 0 or 1, never < 0
    else if (opt->type == Value::Type::Integer) result = (opt->as_int < 0);
    else result = (opt->as_float < 0.0);
    ctx.data_stack().push(Value(result));
    return true;
}

bool prim_zero_gt(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    bool result;
    if (opt->type == Value::Type::Boolean) result = opt->as_bool();  // true(1) > 0
    else if (opt->type == Value::Type::Integer) result = (opt->as_int > 0);
    else result = (opt->as_float > 0.0);
    ctx.data_stack().push(Value(result));
    return true;
}

// --- Boolean primitives ---

bool prim_true(ExecutionContext& ctx) {
    ctx.data_stack().push(Value(true));
    return true;
}

bool prim_false(ExecutionContext& ctx) {
    ctx.data_stack().push(Value(false));
    return true;
}

bool prim_not(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Boolean) {
        ctx.data_stack().push(Value(!opt->as_bool()));
    } else if (opt->type == Value::Type::Integer) {
        ctx.data_stack().push(Value(opt->as_int == 0));
    } else if (opt->type == Value::Type::Float) {
        ctx.data_stack().push(Value(opt->as_float == 0.0));
    } else {
        ctx.err() << "Error: not expects boolean, integer, or float\n";
        ctx.data_stack().push(*opt);
        return false;
    }
    return true;
}

bool prim_to_bool(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Boolean) {
        ctx.data_stack().push(*opt);
    } else if (opt->type == Value::Type::Integer) {
        ctx.data_stack().push(Value(opt->as_int != 0));
    } else if (opt->type == Value::Type::Float) {
        ctx.data_stack().push(Value(opt->as_float != 0.0));
    } else {
        ctx.err() << "Error: bool expects boolean, integer, or float\n";
        ctx.data_stack().push(*opt);
        return false;
    }
    return true;
}

// --- Logic primitives ---

bool prim_and(ExecutionContext& ctx) {
    Value a, b;
    if (!pop_two(ctx, a, b)) return false;
    if (a.type == Value::Type::Boolean && b.type == Value::Type::Boolean) {
        ctx.data_stack().push(Value(a.as_bool() && b.as_bool()));
    } else if (a.type == Value::Type::Integer && b.type == Value::Type::Integer) {
        ctx.data_stack().push(Value(a.as_int & b.as_int));
    } else {
        ctx.err() << "Error: and requires both boolean or both integer\n";
        ctx.data_stack().push(a);
        ctx.data_stack().push(b);
        return false;
    }
    return true;
}

bool prim_or(ExecutionContext& ctx) {
    Value a, b;
    if (!pop_two(ctx, a, b)) return false;
    if (a.type == Value::Type::Boolean && b.type == Value::Type::Boolean) {
        ctx.data_stack().push(Value(a.as_bool() || b.as_bool()));
    } else if (a.type == Value::Type::Integer && b.type == Value::Type::Integer) {
        ctx.data_stack().push(Value(a.as_int | b.as_int));
    } else {
        ctx.err() << "Error: or requires both boolean or both integer\n";
        ctx.data_stack().push(a);
        ctx.data_stack().push(b);
        return false;
    }
    return true;
}

bool prim_xor(ExecutionContext& ctx) {
    Value a, b;
    if (!pop_two(ctx, a, b)) return false;
    if (a.type == Value::Type::Boolean && b.type == Value::Type::Boolean) {
        ctx.data_stack().push(Value(a.as_bool() != b.as_bool()));
    } else if (a.type == Value::Type::Integer && b.type == Value::Type::Integer) {
        ctx.data_stack().push(Value(a.as_int ^ b.as_int));
    } else {
        ctx.err() << "Error: xor requires both boolean or both integer\n";
        ctx.data_stack().push(a);
        ctx.data_stack().push(b);
        return false;
    }
    return true;
}

bool prim_invert(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Boolean) {
        ctx.data_stack().push(Value(!opt->as_bool()));
    } else if (opt->type == Value::Type::Integer) {
        ctx.data_stack().push(Value(~opt->as_int));
    } else {
        ctx.err() << "Error: invert requires boolean or integer\n";
        ctx.data_stack().push(*opt);
        return false;
    }
    return true;
}

bool prim_lshift(ExecutionContext& ctx) {
    auto opt_count = ctx.data_stack().pop();
    if (!opt_count) return false;
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) { ctx.data_stack().push(*opt_count); return false; }
    int64_t count = opt_count->as_int;
    if (count < 0 || count >= 64) {
        ctx.data_stack().push(Value(int64_t(0)));
    } else {
        ctx.data_stack().push(Value(opt_val->as_int << count));
    }
    return true;
}

bool prim_rshift(ExecutionContext& ctx) {
    auto opt_count = ctx.data_stack().pop();
    if (!opt_count) return false;
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) { ctx.data_stack().push(*opt_count); return false; }
    int64_t count = opt_count->as_int;
    if (count < 0 || count >= 64) {
        ctx.data_stack().push(Value(int64_t(0)));
    } else {
        // Logical right shift (unsigned)
        ctx.data_stack().push(Value(static_cast<int64_t>(
            static_cast<uint64_t>(opt_val->as_int) >> count)));
    }
    return true;
}

static bool prim_rotate(ExecutionContext& ctx, bool left) {
    auto opt_count = ctx.data_stack().pop();
    if (!opt_count) return false;
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) { ctx.data_stack().push(*opt_count); return false; }
    int64_t count = opt_count->as_int;
    uint64_t val = static_cast<uint64_t>(opt_val->as_int);
    // Negative count reverses direction
    bool effective_left = (count >= 0) ? left : !left;
    if (count < 0) count = -count;
    int n = static_cast<int>(static_cast<uint64_t>(count) % 64);
    if (n == 0) {
        ctx.data_stack().push(Value(static_cast<int64_t>(val)));
    } else if (effective_left) {
        ctx.data_stack().push(Value(static_cast<int64_t>((val << n) | (val >> (64 - n)))));
    } else {
        ctx.data_stack().push(Value(static_cast<int64_t>((val >> n) | (val << (64 - n)))));
    }
    return true;
}
bool prim_lroll(ExecutionContext& ctx) { return prim_rotate(ctx, true); }
bool prim_rroll(ExecutionContext& ctx) { return prim_rotate(ctx, false); }

// --- I/O primitives ---

bool prim_dot(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;

    const Value& v = *opt;
    if (v.type == Value::Type::Integer) {
        ctx.out() << v.as_int << " ";
    } else if (v.type == Value::Type::Float) {
        ctx.out() << v.as_float << " ";
    } else if (v.type == Value::Type::Boolean) {
        ctx.out() << (v.as_bool() ? "true" : "false") << " ";
    } else if (v.type == Value::Type::String) {
        if (v.as_ptr) {
            ctx.out() << v.as_string()->view() << " ";
        } else {
            ctx.out() << "<string:null> ";
        }
    } else if (v.type == Value::Type::Array) {
        ctx.out() << "<array:" << (v.as_ptr ? v.as_array()->length() : 0) << "> ";
    } else if (v.type == Value::Type::ByteArray) {
        ctx.out() << "<bytes:" << (v.as_ptr ? v.as_byte_array()->length() : 0) << "> ";
    } else if (v.type == Value::Type::Map) {
        ctx.out() << "<map:" << (v.as_ptr ? v.as_map()->size() : 0) << "> ";
    } else if (v.type == Value::Type::Matrix) {
        if (v.as_ptr) {
            auto* mat = v.as_matrix();
            ctx.out() << "<matrix " << mat->rows() << "x" << mat->cols() << "> ";
        } else {
            ctx.out() << "<matrix> ";
        }
    } else if (v.type == Value::Type::Json) {
        if (v.as_ptr) {
            ctx.out() << v.as_json()->dump() << " ";
        } else {
            ctx.out() << "<json> ";
        }
    } else if (v.type == Value::Type::Observable) {
        if (v.as_ptr) {
            ctx.out() << "<observable:" << v.as_observable()->kind_name() << "> ";
        } else {
            ctx.out() << "<observable> ";
        }
    } else if (v.type == Value::Type::Xt) {
        if (v.as_ptr) {
            ctx.out() << "<xt:" << v.as_xt_impl()->name() << "> ";
        } else {
            ctx.out() << "<xt:?> ";
        }
    } else if (v.type == Value::Type::DataRef) {
        ctx.out() << "<dataref> ";
    }
    v.release();
    return true;
}

// hex. ( n -- ) — print integer in hexadecimal
bool prim_hex_dot(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) {
        ctx.err() << "Error: hex. expects an integer\n";
        opt->release();
        return false;
    }
    auto n = static_cast<uint64_t>(opt->as_int);
    ctx.out() << "0x" << std::hex << std::uppercase << n << std::dec << " ";
    return true;
}

// bin. ( n -- ) — print integer in binary
bool prim_bin_dot(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) {
        ctx.err() << "Error: bin. expects an integer\n";
        opt->release();
        return false;
    }
    auto n = static_cast<uint64_t>(opt->as_int);
    // Find the highest set bit to avoid leading zeros (but always show at least "0")
    if (n == 0) {
        ctx.out() << "0b0 ";
    } else {
        ctx.out() << "0b";
        bool started = false;
        for (int i = 63; i >= 0; --i) {
            if (n & (uint64_t(1) << i)) started = true;
            if (started) ctx.out() << ((n >> i) & 1);
        }
        ctx.out() << " ";
    }
    return true;
}

// oct. ( n -- ) — print integer in octal
bool prim_oct_dot(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) {
        ctx.err() << "Error: oct. expects an integer\n";
        opt->release();
        return false;
    }
    auto n = static_cast<uint64_t>(opt->as_int);
    ctx.out() << "0o" << std::oct << n << std::dec << " ";
    return true;
}

bool prim_cr(ExecutionContext& ctx) {
    ctx.out() << "\n";
    return true;
}

bool prim_emit(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Integer) {
        ctx.out() << static_cast<char>(opt->as_int);
    }
    return true;
}

bool prim_space(ExecutionContext& ctx) {
    ctx.out() << " ";
    return true;
}

bool prim_spaces(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Integer && opt->as_int > 0) {
        for (int64_t i = 0; i < opt->as_int; ++i) {
            ctx.out() << " ";
        }
    }
    return true;
}

bool prim_words(ExecutionContext& ctx) {
    auto* dict = ctx.dictionary();
    if (!dict) return false;
    auto names = dict->word_names();
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        ctx.out() << name << " ";
    }
    ctx.out() << "\n";
    return true;
}

// --- Memory primitives ---

bool prim_create(ExecutionContext& ctx) {
    auto* is = ctx.input_stream();
    if (!is) return false;
    std::string create_name;
    if (!(*is >> create_name)) return false;

    auto code = std::make_shared<ByteCode>();
    Instruction push_ptr;
    push_ptr.op = Instruction::Op::PushDataPtr;
    code->append(std::move(push_ptr));

    auto id = Dictionary::next_id();
    auto* impl = new WordImpl(create_name, id);
    impl->set_bytecode(code);
    impl->set_weight(1.0);
    impl->set_generation(0);
    ctx.dictionary()->register_word(create_name, WordImplPtr(impl));
    ctx.set_last_created(impl);
    return true;
}

bool prim_comma(ExecutionContext& ctx) {
    auto* last = ctx.last_created();
    if (!last || !last->bytecode()) return false;
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    last->bytecode()->data_field().push_back(*opt);
    return true;
}

bool prim_fetch(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::DataRef) return false;
    auto idx = opt->dataref_index();
    auto off = opt->dataref_offset();
    auto* field = ctx.data_field_registry().resolve(idx);
    if (!field || off >= field->size()) return false;
    auto& val = (*field)[off];
    val.addref();  // data field keeps its ref; stack gets a new one
    ctx.data_stack().push(val);
    return true;
}

bool prim_store(ExecutionContext& ctx) {
    auto opt_addr = ctx.data_stack().pop();
    if (!opt_addr) return false;
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) {
        ctx.data_stack().push(*opt_addr);
        return false;
    }
    if (opt_addr->type != Value::Type::DataRef) {
        ctx.data_stack().push(*opt_val);
        ctx.data_stack().push(*opt_addr);
        return false;
    }
    auto idx = opt_addr->dataref_index();
    auto off = opt_addr->dataref_offset();
    auto* field = ctx.data_field_registry().resolve(idx);
    if (!field || off >= field->size()) {
        ctx.data_stack().push(*opt_val);
        ctx.data_stack().push(*opt_addr);
        return false;
    }
    (*field)[off].release();  // release old value in data field
    (*field)[off] = *opt_val;      // transfer stack ref to data field
    return true;
}

bool prim_allot(ExecutionContext& ctx) {
    auto* last = ctx.last_created();
    if (!last || !last->bytecode()) return false;
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    int64_t n = opt->as_int;
    if (n > 0) {
        last->bytecode()->data_field().resize(
            last->bytecode()->data_field().size() + static_cast<size_t>(n),
            Value(int64_t(0)));
    }
    return true;
}

// --- Math primitives ---
// Helper: pop one value, convert to double
namespace {
bool pop_as_double(ExecutionContext& ctx, double& out) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Float) {
        out = opt->as_float;
    } else {
        out = static_cast<double>(opt->as_int);
    }
    return true;
}

bool pop_two_as_double(ExecutionContext& ctx, double& a, double& b) {
    Value va, vb;
    if (!pop_two(ctx, va, vb)) return false;
    a = (va.type == Value::Type::Float) ? va.as_float : static_cast<double>(va.as_int);
    b = (vb.type == Value::Type::Float) ? vb.as_float : static_cast<double>(vb.as_int);
    return true;
}
} // anonymous namespace

// --- Unary math primitive generator ---
// Each expands to: pop one double, apply fn, push result.
#define UNARY_MATH_PRIM(name, fn)                                   \
bool prim_##name(ExecutionContext& ctx) {                            \
    double v; if (!pop_as_double(ctx, v)) return false;              \
    ctx.data_stack().push(Value(fn(v)));                             \
    return true;                                                     \
}

UNARY_MATH_PRIM(sqrt,  std::sqrt)
UNARY_MATH_PRIM(sin,   std::sin)
UNARY_MATH_PRIM(cos,   std::cos)
UNARY_MATH_PRIM(tan,   std::tan)
UNARY_MATH_PRIM(asin,  std::asin)
UNARY_MATH_PRIM(acos,  std::acos)
UNARY_MATH_PRIM(atan,  std::atan)
UNARY_MATH_PRIM(log,   std::log)
UNARY_MATH_PRIM(log2,  std::log2)
UNARY_MATH_PRIM(log10, std::log10)
UNARY_MATH_PRIM(exp,   std::exp)
UNARY_MATH_PRIM(ceil,  std::ceil)
UNARY_MATH_PRIM(floor, std::floor)
UNARY_MATH_PRIM(round, std::round)
UNARY_MATH_PRIM(trunc, std::trunc)
UNARY_MATH_PRIM(tanh,  std::tanh)

#undef UNARY_MATH_PRIM

// --- Binary math primitive generator ---
// Each expands to: pop two doubles, apply fn, push result.
#define BINARY_MATH_PRIM(name, fn)                                  \
bool prim_##name(ExecutionContext& ctx) {                            \
    double a, b;                                                     \
    if (!pop_two_as_double(ctx, a, b)) return false;                 \
    ctx.data_stack().push(Value(fn(a, b)));                          \
    return true;                                                     \
}

BINARY_MATH_PRIM(atan2, std::atan2)
BINARY_MATH_PRIM(pow,   std::pow)
BINARY_MATH_PRIM(fmin,  std::fmin)
BINARY_MATH_PRIM(fmax,  std::fmax)

#undef BINARY_MATH_PRIM

bool prim_pi(ExecutionContext& ctx) {
    ctx.data_stack().push(Value(3.14159265358979323846));
    return true;
}

bool prim_fapprox(ExecutionContext& ctx) {
    double r3; if (!pop_as_double(ctx, r3)) return false;
    double r1, r2;
    if (!pop_two_as_double(ctx, r1, r2)) {
        ctx.data_stack().push(Value(r3)); // restore r3 on underflow
        return false;
    }
    bool result;
    if (r3 > 0.0) {
        result = std::fabs(r1 - r2) < r3;
    } else if (r3 == 0.0) {
        result = (r1 == r2);
    } else {
        result = std::fabs(r1 - r2) < std::fabs(r3) * (std::fabs(r1) + std::fabs(r2));
    }
    ctx.data_stack().push(Value(int64_t(result ? -1 : 0)));
    return true;
}

// --- PRNG helpers ---

std::mt19937_64& prng_engine() {
    thread_local std::mt19937_64 engine(
        static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    return engine;
}

bool prim_random(ExecutionContext& ctx) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    ctx.data_stack().push(Value(dist(prng_engine())));
    return true;
}

bool prim_random_seed(ExecutionContext& ctx) {
    auto val = ctx.data_stack().pop();
    if (!val) return false;
    int64_t seed;
    if (val->type == Value::Type::Integer) {
        seed = val->as_int;
    } else if (val->type == Value::Type::Float) {
        seed = static_cast<int64_t>(val->as_float);
    } else {
        ctx.data_stack().push(*val);
        return false;
    }
    prng_engine().seed(static_cast<uint64_t>(seed));
    return true;
}

bool prim_random_range(ExecutionContext& ctx) {
    Value lo, hi;
    if (!pop_two(ctx, lo, hi)) return false;

    int64_t lo_int, hi_int;
    if (lo.type == Value::Type::Integer) {
        lo_int = lo.as_int;
    } else if (lo.type == Value::Type::Float) {
        lo_int = static_cast<int64_t>(lo.as_float);
    } else {
        ctx.data_stack().push(lo);
        ctx.data_stack().push(hi);
        return false;
    }
    if (hi.type == Value::Type::Integer) {
        hi_int = hi.as_int;
    } else if (hi.type == Value::Type::Float) {
        hi_int = static_cast<int64_t>(hi.as_float);
    } else {
        ctx.data_stack().push(lo);
        ctx.data_stack().push(hi);
        return false;
    }

    if (lo_int >= hi_int) {
        ctx.data_stack().push(lo);
        ctx.data_stack().push(hi);
        return false;
    }

    std::uniform_int_distribution<int64_t> dist(lo_int, hi_int - 1);
    ctx.data_stack().push(Value(dist(prng_engine())));
    return true;
}

// --- Input-reading primitives ---

bool prim_word_read(ExecutionContext& ctx) {
    auto* is = ctx.input_stream();
    if (!is) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    std::string token;
    if (!(*is >> token)) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    auto* hs = HeapString::create(token);
    ctx.data_stack().push(Value::from(hs));
    ctx.data_stack().push(Value(true));
    return true;
}

bool prim_string_read_delim(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    char delim = static_cast<char>(opt->as_int);
    auto* is = ctx.input_stream();
    if (!is) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    // Skip everything up to and including the opening delimiter,
    // then read content until the closing delimiter.
    // For input: "hello world" with delim='"', this skips to the first "
    // then reads "hello world" until the second ".
    std::string discard;
    std::getline(*is, discard, delim);
    std::string content;
    std::getline(*is, content, delim);
    if (is->fail() && content.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    auto* hs = HeapString::create(content);
    ctx.data_stack().push(Value::from(hs));
    ctx.data_stack().push(Value(true));
    return true;
}

// --- Dictionary-operation primitives ---

bool prim_dict_forget(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::String || !opt->as_ptr) {
        opt->release();
        ctx.data_stack().push(Value(false));
        return true;
    }
    std::string name(opt->as_string()->view());
    opt->release();
    auto* dict = ctx.dictionary();
    if (dict->forget_word(name)) {
        ctx.data_stack().push(Value(true));
    } else {
        ctx.data_stack().push(Value(false));
    }
    return true;
}

bool prim_dict_forget_all(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::String || !opt->as_ptr) {
        opt->release();
        ctx.data_stack().push(Value(false));
        return true;
    }
    std::string name(opt->as_string()->view());
    opt->release();
    auto* dict = ctx.dictionary();
    if (dict->forget_all(name)) {
        ctx.data_stack().push(Value(true));
    } else {
        ctx.data_stack().push(Value(false));
    }
    return true;
}

bool prim_file_load(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::String || !opt->as_ptr) {
        opt->release();
        ctx.data_stack().push(Value(false));
        return true;
    }
    std::string path(opt->as_string()->view());
    opt->release();
    auto* interp = ctx.interpreter();
    if (!interp) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    if (interp->load_file(path)) {
        ctx.data_stack().push(Value(true));
    } else {
        ctx.data_stack().push(Value(false));
    }
    return true;
}

bool prim_include(ExecutionContext& ctx) {
    auto* is = ctx.input_stream();
    if (!is) return false;
    std::string path;
    if (!(*is >> path)) return false;
    auto* interp = ctx.interpreter();
    if (!interp) return false;

    // Absolute LVFS paths (/home/..., /library/...) go through load_file
    // which resolves via resolve_logical_path. Relative paths resolve
    // against the home directory.
    if (path.size() > 0 && path[0] == '/') {
        return interp->load_file(path);
    }
    std::string resolved = interp->resolve_home_path(path);
    if (resolved.empty()) {
        ctx.err() << "Error: include path rejected '" << path << "'\n";
        return false;
    }
    return interp->load_file(resolved);
}

bool prim_evaluate(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::String || !opt->as_ptr) {
        ctx.err() << "Error: evaluate requires a string\n";
        opt->release();
        return false;
    }
    auto* hs = opt->as_string();

    // Check evaluate permission
    auto* perms = ctx.permissions();
    if (perms && !perms->evaluate) {
        ctx.err() << "Error: evaluate not permitted for this role\n";
        hs->release();
        return false;
    }

    // Check taint permission
    if (perms && hs->is_tainted() && !perms->evaluate_tainted) {
        ctx.err() << "Error: evaluate on tainted input not permitted\n";
        hs->release();
        return false;
    }

    std::string code(hs->view());
    hs->release();

    auto* interp = ctx.interpreter();
    if (!interp) {
        ctx.err() << "Error: evaluate requires interpreter context\n";
        return false;
    }

    // Track call depth so recursive evaluate is visible to the limit
    if (!ctx.enter_call()) {
        ctx.err() << "Error: maximum call depth exceeded\n";
        return false;
    }

    bool ok = interp->evaluate_string(code);

    ctx.exit_call();
    return ok;
}

bool prim_library(ExecutionContext& ctx) {
    auto* is = ctx.input_stream();
    if (!is) return false;
    std::string path;
    if (!(*is >> path)) return false;
    auto* interp = ctx.interpreter();
    if (!interp) return false;

    std::string resolved = interp->resolve_library_path(path);
    if (resolved.empty()) {
        ctx.err() << "Error: library path rejected '" << path << "'\n";
        return false;
    }
    return interp->load_file(resolved);
}

// --- Metadata primitives (stack-based) ---

// Shared helper: pop and validate 4 string args for meta-set operations.
// Returns 1 on success (params filled), 0 on type/format error (caller pushes false),
// -1 on underflow (caller returns false).
struct MetaSetParams {
    std::string word_name, key, content;
    MetadataFormat fmt;
};
static int pop_meta_set_params(ExecutionContext& ctx, MetaSetParams& p) {
    auto opt_content = ctx.data_stack().pop();
    if (!opt_content) return -1;
    auto opt_fmt = ctx.data_stack().pop();
    if (!opt_fmt) { ctx.data_stack().push(*opt_content); return -1; }
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) { ctx.data_stack().push(*opt_fmt); ctx.data_stack().push(*opt_content); return -1; }
    auto opt_word = ctx.data_stack().pop();
    if (!opt_word) { ctx.data_stack().push(*opt_key); ctx.data_stack().push(*opt_fmt); ctx.data_stack().push(*opt_content); return -1; }

    if (opt_word->type != Value::Type::String || !opt_word->as_ptr ||
        opt_key->type != Value::Type::String || !opt_key->as_ptr ||
        opt_fmt->type != Value::Type::String || !opt_fmt->as_ptr ||
        opt_content->type != Value::Type::String || !opt_content->as_ptr) {
        opt_word->release(); opt_key->release();
        opt_fmt->release(); opt_content->release();
        return 0;
    }

    std::string fmt_str(opt_fmt->as_string()->view());
    p.word_name = std::string(opt_word->as_string()->view());
    p.key = std::string(opt_key->as_string()->view());
    p.content = std::string(opt_content->as_string()->view());
    opt_word->release(); opt_key->release();
    opt_fmt->release(); opt_content->release();

    auto fmt = parse_metadata_format(fmt_str);
    if (!fmt) return 0;
    p.fmt = *fmt;
    return 1;
}

bool prim_dict_meta_set(ExecutionContext& ctx) {
    MetaSetParams p;
    int r = pop_meta_set_params(ctx, p);
    if (r < 0) return false;
    if (r == 0) { ctx.data_stack().push(Value(false)); return true; }
    auto* dict = ctx.dictionary();
    if (!dict || !dict->set_concept_metadata(p.word_name, p.key, p.fmt, std::move(p.content))) {
        ctx.data_stack().push(Value(false));
    } else {
        ctx.data_stack().push(Value(true));
    }
    return true;
}

// Shared helper: pop and validate 2 string args for meta-get operations.
struct MetaGetParams { std::string word_name, key; };
static int pop_meta_get_params(ExecutionContext& ctx, MetaGetParams& p) {
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) return -1;
    auto opt_word = ctx.data_stack().pop();
    if (!opt_word) { ctx.data_stack().push(*opt_key); return -1; }
    if (opt_word->type != Value::Type::String || !opt_word->as_ptr ||
        opt_key->type != Value::Type::String || !opt_key->as_ptr) {
        opt_word->release(); opt_key->release();
        return 0;
    }
    p.word_name = std::string(opt_word->as_string()->view());
    p.key = std::string(opt_key->as_string()->view());
    opt_word->release(); opt_key->release();
    return 1;
}

bool prim_dict_meta_get(ExecutionContext& ctx) {
    MetaGetParams p;
    int r = pop_meta_get_params(ctx, p);
    if (r < 0) return false;
    if (r == 0) { ctx.data_stack().push(Value(false)); return true; }
    auto* dict = ctx.dictionary();
    auto entry = dict->get_concept_metadata(p.word_name, p.key);
    if (entry) {
        ctx.data_stack().push(Value::from(HeapString::create(entry->content)));
        ctx.data_stack().push(Value(true));
    } else {
        ctx.data_stack().push(Value(false));
    }
    return true;
}

bool prim_dict_meta_del(ExecutionContext& ctx) {
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) return false;
    auto opt_word = ctx.data_stack().pop();
    if (!opt_word) { ctx.data_stack().push(*opt_key); return false; }

    if (opt_word->type != Value::Type::String || !opt_word->as_ptr ||
        opt_key->type != Value::Type::String || !opt_key->as_ptr) {
        opt_word->release();
        opt_key->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string word_name(opt_word->as_string()->view());
    std::string key(opt_key->as_string()->view());
    opt_word->release();
    opt_key->release();

    auto* dict = ctx.dictionary();
    if (!dict || !dict->remove_concept_metadata(word_name, key)) {
        ctx.data_stack().push(Value(false));
    } else {
        ctx.data_stack().push(Value(true));
    }
    return true;
}

bool prim_dict_meta_keys(ExecutionContext& ctx) {
    auto opt_word = ctx.data_stack().pop();
    if (!opt_word) return false;

    if (opt_word->type != Value::Type::String || !opt_word->as_ptr) {
        opt_word->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string word_name(opt_word->as_string()->view());
    opt_word->release();

    auto* dict = ctx.dictionary();
    auto keys = dict->concept_metadata_keys(word_name);
    auto* arr = new HeapArray();
    for (const auto& k : keys) {
        arr->push_back(Value::from(HeapString::create(k)));
    }
    ctx.data_stack().push(Value::from(arr));
    ctx.data_stack().push(Value(true));
    return true;
}

bool prim_impl_meta_set(ExecutionContext& ctx) {
    MetaSetParams p;
    int r = pop_meta_set_params(ctx, p);
    if (r < 0) return false;
    if (r == 0) { ctx.data_stack().push(Value(false)); return true; }
    auto* dict = ctx.dictionary();
    auto impl = dict->lookup(p.word_name);
    if (!impl) { ctx.data_stack().push(Value(false)); return true; }
    (*impl)->metadata().set(p.key, p.fmt, std::move(p.content));
    ctx.data_stack().push(Value(true));
    return true;
}

bool prim_impl_meta_get(ExecutionContext& ctx) {
    MetaGetParams p;
    int r = pop_meta_get_params(ctx, p);
    if (r < 0) return false;
    if (r == 0) { ctx.data_stack().push(Value(false)); return true; }
    auto* dict = ctx.dictionary();
    auto impl = dict->lookup(p.word_name);
    if (!impl) { ctx.data_stack().push(Value(false)); return true; }
    auto entry = (*impl)->metadata().get(p.key);
    if (entry) {
        ctx.data_stack().push(Value::from(HeapString::create(entry->content)));
        ctx.data_stack().push(Value(true));
    } else {
        ctx.data_stack().push(Value(false));
    }
    return true;
}

// --- System primitives ---

bool prim_sys_semver(ExecutionContext& ctx) {
    ctx.out() << ETIL_VERSION;
    return true;
}

bool prim_sys_timestamp(ExecutionContext& ctx) {
    ctx.out() << ETIL_BUILD_TIMESTAMP;
    return true;
}

// --- Selection primitives ---

// select-strategy ( n -- )  0=latest, 1=weighted, 2=epsilon-greedy, 3=ucb1
bool prim_select_strategy(ExecutionContext& ctx) {
    auto val = ctx.data_stack().pop();
    if (!val) return false;
    if (val->type != Value::Type::Integer) {
        ctx.err() << "Error: select-strategy requires an integer\n";
        return false;
    }
    auto* engine = ctx.selection_engine();
    if (!engine) {
        ctx.err() << "Error: no selection engine configured\n";
        return false;
    }
    switch (val->as_int) {
        case 0: engine->set_strategy(etil::selection::Strategy::Latest); break;
        case 1: engine->set_strategy(etil::selection::Strategy::WeightedRandom); break;
        case 2: engine->set_strategy(etil::selection::Strategy::EpsilonGreedy); break;
        case 3: engine->set_strategy(etil::selection::Strategy::UCB1); break;
        default:
            ctx.err() << "Error: invalid strategy " << val->as_int
                      << " (0=latest, 1=weighted, 2=epsilon, 3=ucb1)\n";
            return false;
    }
    return true;
}

// select-epsilon ( f -- )  Set epsilon for epsilon-greedy
bool prim_select_epsilon(ExecutionContext& ctx) {
    auto val = ctx.data_stack().pop();
    if (!val) return false;
    double eps;
    if (val->type == Value::Type::Float) eps = val->as_float;
    else if (val->type == Value::Type::Integer) eps = static_cast<double>(val->as_int);
    else {
        ctx.err() << "Error: select-epsilon requires a number\n";
        return false;
    }
    auto* engine = ctx.selection_engine();
    if (!engine) {
        ctx.err() << "Error: no selection engine configured\n";
        return false;
    }
    engine->set_epsilon(eps);
    return true;
}

// select-off ( -- )  Revert to Latest (deterministic)
bool prim_select_off(ExecutionContext& ctx) {
    auto* engine = ctx.selection_engine();
    if (!engine) return true;  // no-op if no engine
    engine->set_strategy(etil::selection::Strategy::Latest);
    return true;
}

// --- Evolution primitives ---

// evolve-register ( word-str tests-array -- flag )
// Register test cases for a word. Each test is a map: { "in": [values...], "out": [values...] }
bool prim_evolve_register(ExecutionContext& ctx) {
    auto* arr = pop_array(ctx);
    if (!arr) return false;
    auto* word_str = pop_string(ctx);
    if (!word_str) { ctx.data_stack().push(Value::from(arr)); return false; }

    auto* engine = ctx.evolution_engine();
    if (!engine) {
        ctx.err() << "Error: no evolution engine configured\n";
        word_str->release();
        arr->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string word(word_str->c_str(), word_str->length());
    word_str->release();

    // Cap test case count to prevent DoS
    if (arr->length() > 1000) {
        ctx.err() << "Error: evolve-register max 1000 test cases\n";
        arr->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Parse test cases from array of maps
    std::vector<etil::evolution::TestCase> tests;
    for (size_t i = 0; i < arr->length(); ++i) {
        Value map_val;
        arr->get(i, map_val);
        if (map_val.type != Value::Type::Map) continue;
        auto* m = map_val.as_map();

        etil::evolution::TestCase tc;

        // Get "in" array
        auto in_it = m->entries().find("in");
        if (in_it != m->entries().end() && in_it->second.type == Value::Type::Array) {
            auto* in_arr = in_it->second.as_array();
            for (size_t j = 0; j < in_arr->length(); ++j) {
                Value v;
                in_arr->get(j, v);
                tc.inputs.push_back(v);
            }
        }

        // Get "out" array
        auto out_it = m->entries().find("out");
        if (out_it != m->entries().end() && out_it->second.type == Value::Type::Array) {
            auto* out_arr = out_it->second.as_array();
            for (size_t j = 0; j < out_arr->length(); ++j) {
                Value v;
                out_arr->get(j, v);
                tc.expected.push_back(v);
            }
        }

        tests.push_back(std::move(tc));
    }
    arr->release();

    engine->register_tests(word, std::move(tests));
    ctx.data_stack().push(Value(true));
    return true;
}

// evolve-register-pool ( word-str tests-array pool-array -- flag )
// Register test cases with a restricted word pool.
bool prim_evolve_register_pool(ExecutionContext& ctx) {
    auto* pool_arr = pop_array(ctx);
    if (!pool_arr) return false;
    auto* test_arr = pop_array(ctx);
    if (!test_arr) { ctx.data_stack().push(Value::from(pool_arr)); return false; }
    auto* word_str = pop_string(ctx);
    if (!word_str) {
        ctx.data_stack().push(Value::from(test_arr));
        ctx.data_stack().push(Value::from(pool_arr));
        return false;
    }

    auto* engine = ctx.evolution_engine();
    if (!engine) {
        ctx.err() << "Error: no evolution engine configured\n";
        word_str->release(); test_arr->release(); pool_arr->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string word(word_str->c_str(), word_str->length());
    word_str->release();

    if (test_arr->length() > 1000) {
        ctx.err() << "Error: evolve-register-pool max 1000 test cases\n";
        test_arr->release(); pool_arr->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Parse test cases (same as evolve-register)
    std::vector<etil::evolution::TestCase> tests;
    for (size_t i = 0; i < test_arr->length(); ++i) {
        Value map_val;
        test_arr->get(i, map_val);
        if (map_val.type != Value::Type::Map) continue;
        auto* m = map_val.as_map();
        etil::evolution::TestCase tc;
        auto in_it = m->entries().find("in");
        if (in_it != m->entries().end() && in_it->second.type == Value::Type::Array) {
            auto* in_arr = in_it->second.as_array();
            for (size_t j = 0; j < in_arr->length(); ++j) {
                Value v; in_arr->get(j, v); tc.inputs.push_back(v);
            }
        }
        auto out_it = m->entries().find("out");
        if (out_it != m->entries().end() && out_it->second.type == Value::Type::Array) {
            auto* out_arr = out_it->second.as_array();
            for (size_t j = 0; j < out_arr->length(); ++j) {
                Value v; out_arr->get(j, v); tc.expected.push_back(v);
            }
        }
        tests.push_back(std::move(tc));
    }
    test_arr->release();

    // Parse pool array (array of word name strings)
    std::vector<std::string> pool;
    for (size_t i = 0; i < pool_arr->length(); ++i) {
        Value v;
        pool_arr->get(i, v);
        if (v.type == Value::Type::String) {
            auto* s = v.as_string();
            pool.emplace_back(s->c_str(), s->length());
        }
        value_release(v);
    }
    pool_arr->release();

    engine->register_tests_with_pool(word, std::move(tests), std::move(pool));
    ctx.data_stack().push(Value(true));
    return true;
}

// evolve-word ( word-str -- n )
// Run one generation of evolution for a word. Returns number of children created.
bool prim_evolve_word(ExecutionContext& ctx) {
    auto* word_str = pop_string(ctx);
    if (!word_str) return false;

    auto* engine = ctx.evolution_engine();
    if (!engine) {
        ctx.err() << "Error: no evolution engine configured\n";
        word_str->release();
        ctx.data_stack().push(Value(int64_t(0)));
        return true;
    }

    std::string word(word_str->c_str(), word_str->length());
    word_str->release();

    size_t created = engine->evolve_word(word);
    ctx.data_stack().push(Value(static_cast<int64_t>(created)));
    return true;
}

// evolve-all ( -- )
// Evolve all words that have registered test cases.
bool prim_evolve_all(ExecutionContext& ctx) {
    auto* engine = ctx.evolution_engine();
    if (!engine) {
        ctx.err() << "Error: no evolution engine configured\n";
        return true;
    }
    engine->evolve_all();
    return true;
}

// evolve-status ( word-str -- n )
// Returns the number of generations evolved for a word.
bool prim_evolve_status(ExecutionContext& ctx) {
    auto* word_str = pop_string(ctx);
    if (!word_str) return false;

    auto* engine = ctx.evolution_engine();
    if (!engine) {
        ctx.err() << "Error: no evolution engine configured\n";
        word_str->release();
        ctx.data_stack().push(Value(int64_t(0)));
        return true;
    }

    std::string word(word_str->c_str(), word_str->length());
    word_str->release();

    size_t gens = engine->generations_run(word);
    ctx.data_stack().push(Value(static_cast<int64_t>(gens)));
    return true;
}

// --- Evolution tagging primitives ---

// evolve-tag ( word-str tags-str -- )
bool prim_evolve_tag(ExecutionContext& ctx) {
    auto* tags_str = pop_string(ctx);
    if (!tags_str) return false;
    auto* word_str = pop_string(ctx);
    if (!word_str) { tags_str->release(); return false; }

    std::string word(word_str->c_str(), word_str->length());
    std::string tags(tags_str->c_str(), tags_str->length());
    word_str->release();
    tags_str->release();

    auto* dict = ctx.dictionary();
    if (!dict) return false;
    dict->set_concept_metadata(word, "semantic-tags", MetadataFormat::Text, std::move(tags));
    return true;
}

// evolve-untag ( word-str -- )
bool prim_evolve_untag(ExecutionContext& ctx) {
    auto* word_str = pop_string(ctx);
    if (!word_str) return false;
    std::string word(word_str->c_str(), word_str->length());
    word_str->release();

    auto* dict = ctx.dictionary();
    if (!dict) return false;
    dict->remove_concept_metadata(word, "semantic-tags");
    return true;
}

// evolve-bridge ( word-str from-type to-type -- )
bool prim_evolve_bridge(ExecutionContext& ctx) {
    auto* to_str = pop_string(ctx);
    if (!to_str) return false;
    auto* from_str = pop_string(ctx);
    if (!from_str) { to_str->release(); return false; }
    auto* word_str = pop_string(ctx);
    if (!word_str) { from_str->release(); to_str->release(); return false; }

    std::string word(word_str->c_str(), word_str->length());
    std::string from_type(from_str->c_str(), from_str->length());
    std::string to_type(to_str->c_str(), to_str->length());
    word_str->release();
    from_str->release();
    to_str->release();

    auto* dict = ctx.dictionary();
    if (!dict) return false;
    dict->set_concept_metadata(word, "bridge-from", MetadataFormat::Text, std::move(from_type));
    dict->set_concept_metadata(word, "bridge-to", MetadataFormat::Text, std::move(to_type));
    return true;
}

// --- Evolution logging primitives ---

// evolve-log-start ( level mask -- )
// evolve-fitness-mode ( n -- )   0=binary, 1=distance
bool prim_evolve_fitness_mode(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    auto* engine = ctx.evolution_engine();
    if (!engine) { ctx.err() << "Error: no evolution engine configured\n"; return true; }
    int64_t mode = (opt->type == Value::Type::Integer) ? opt->as_int : 0;
    engine->config().fitness_mode = (mode == 1)
        ? etil::evolution::FitnessMode::Distance
        : etil::evolution::FitnessMode::Binary;
    return true;
}

// evolve-fitness-alpha ( f -- )
bool prim_evolve_fitness_alpha(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    auto* engine = ctx.evolution_engine();
    if (!engine) { ctx.err() << "Error: no evolution engine configured\n"; return true; }
    double alpha = 1.0;
    if (opt->type == Value::Type::Float) alpha = opt->as_float;
    else if (opt->type == Value::Type::Integer) alpha = static_cast<double>(opt->as_int);
    engine->config().distance_alpha = alpha;
    return true;
}

bool prim_evolve_log_start(ExecutionContext& ctx) {
    auto opt_mask = ctx.data_stack().pop();
    if (!opt_mask) return false;
    auto opt_level = ctx.data_stack().pop();
    if (!opt_level) {
        ctx.data_stack().push(*opt_mask);
        return false;
    }

    auto* engine = ctx.evolution_engine();
    if (!engine) {
        ctx.err() << "Error: no evolution engine configured\n";
        return true;
    }

    int64_t level = opt_level->type == Value::Type::Integer ? opt_level->as_int : 0;
    int64_t mask = opt_mask->type == Value::Type::Integer ? opt_mask->as_int : 0;

    auto log_level = etil::evolution::EvolveLogLevel::Off;
    if (level == 1) log_level = etil::evolution::EvolveLogLevel::Logical;
    else if (level >= 2) log_level = etil::evolution::EvolveLogLevel::Granular;

    engine->logger().start(log_level, static_cast<uint32_t>(mask));
    return true;
}

// evolve-log-stop ( -- )
bool prim_evolve_log_stop(ExecutionContext& ctx) {
    auto* engine = ctx.evolution_engine();
    if (!engine) {
        ctx.err() << "Error: no evolution engine configured\n";
        return true;
    }
    engine->logger().stop();
    return true;
}

// evolve-log-dir ( path -- )
bool prim_evolve_log_dir(ExecutionContext& ctx) {
    auto* path_str = pop_string(ctx);
    if (!path_str) return false;

    auto* engine = ctx.evolution_engine();
    if (!engine) {
        ctx.err() << "Error: no evolution engine configured\n";
        path_str->release();
        return true;
    }

    std::string dir(path_str->c_str(), path_str->length());
    path_str->release();
    engine->logger().set_directory(dir);
    return true;
}

// --- Help primitive ---

namespace {

/// Print help for a single word. Returns true if help was found and printed.
bool print_help_for_word(const std::string& word, Dictionary* dict, std::ostream& os) {
    std::string description;
    std::string stack_effect;
    std::string category;
    bool found = false;

    // Source 1: Concept-level metadata (from help.til via meta!)
    if (dict) {
        auto desc_entry = dict->get_concept_metadata(word, "description");
        if (desc_entry) {
            description = desc_entry->content;
            found = true;
            auto se_entry = dict->get_concept_metadata(word, "stack-effect");
            if (se_entry) stack_effect = se_entry->content;
            auto cat_entry = dict->get_concept_metadata(word, "category");
            if (cat_entry) category = cat_entry->content;
        }
    }

    // Source 2: Impl-level metadata fallback
    if (!found && dict) {
        auto impl = dict->lookup(word);
        if (impl) {
            auto desc_entry = (*impl)->metadata().get("description");
            if (desc_entry) {
                description = desc_entry->content;
                found = true;
                auto se_entry = (*impl)->metadata().get("stack-effect");
                if (se_entry) stack_effect = se_entry->content;
                auto cat_entry = (*impl)->metadata().get("category");
                if (cat_entry) category = cat_entry->content;
            }
        }
    }

    if (!found) return false;

    os << word << "\n";
    if (!description.empty())
        os << "  Description:  " << description << "\n";
    if (!stack_effect.empty())
        os << "  Stack effect: " << stack_effect << "\n";
    if (!category.empty())
        os << "  Category:     " << category << "\n";

    return true;
}

} // anonymous namespace

bool prim_help(ExecutionContext& ctx) {
    auto* dict = ctx.dictionary();
    auto* is = ctx.input_stream();
    auto& os = ctx.out();

    std::string word;
    bool has_word = is && (*is >> word);

    if (!has_word) {
        // No argument: print help for all words
        std::vector<std::string> all_words;
        if (dict) {
            all_words = dict->word_names();
        }
        std::sort(all_words.begin(), all_words.end());

        for (const auto& w : all_words) {
            print_help_for_word(w, dict, os);
        }
        return true;
    }

    // Single word help
    if (!print_help_for_word(word, dict, os)) {
        os << "No help available for '" << word << "'\n";
    }
    return true;
}

// --- Dump primitive ---

namespace {

constexpr size_t DUMP_MAX_STRING_LEN = 80;
constexpr size_t DUMP_MAX_ARRAY_ITEMS = 20;
constexpr size_t DUMP_MAX_BYTE_DUMP = 64;
constexpr size_t DUMP_MAX_DEPTH = 4;

void dump_value(std::ostream& os, const Value& v, size_t depth, size_t max_depth) {
    std::string indent(depth * 2, ' ');

    switch (v.type) {
    case Value::Type::Integer:
        os << indent << v.as_int << " (integer)\n";
        break;
    case Value::Type::Float:
        os << indent << v.as_float << " (float)\n";
        break;
    case Value::Type::Boolean:
        os << indent << (v.as_bool() ? "true" : "false") << " (boolean)\n";
        break;
    case Value::Type::String: {
        auto sv = v.as_string()->view();
        os << indent << "\"";
        if (sv.size() > DUMP_MAX_STRING_LEN) {
            os << std::string(sv.substr(0, DUMP_MAX_STRING_LEN)) << "...";
        } else {
            os << sv;
        }
        os << "\" (string, " << sv.size() << " bytes)\n";
        break;
    }
    case Value::Type::Array: {
        auto* arr = v.as_array();
        size_t len = arr->length();
        if (depth >= max_depth) {
            os << indent << "[array, " << len << " elements] ...\n";
            break;
        }
        os << indent << "[array, " << len << " elements]\n";
        const auto& elems = arr->elements();
        size_t show = std::min(len, DUMP_MAX_ARRAY_ITEMS);
        for (size_t i = 0; i < show; ++i) {
            dump_value(os, elems[i], depth + 1, max_depth);
        }
        if (len > DUMP_MAX_ARRAY_ITEMS) {
            os << indent << "  ... (" << (len - DUMP_MAX_ARRAY_ITEMS) << " more)\n";
        }
        break;
    }
    case Value::Type::ByteArray: {
        auto* ba = v.as_byte_array();
        size_t len = ba->length();
        os << indent << "<bytes, " << len << " bytes>\n";
        size_t show = std::min(len, DUMP_MAX_BYTE_DUMP);
        for (size_t off = 0; off < show; off += 16) {
            os << indent << "  ";
            os << std::setw(4) << std::setfill('0') << std::hex << off << ":";
            for (size_t j = 0; j < 16 && (off + j) < show; ++j) {
                uint8_t byte = 0;
                ba->get(off + j, byte);
                os << " " << std::setw(2) << std::setfill('0') << std::hex
                   << static_cast<unsigned>(byte);
            }
            os << std::dec << "\n";
        }
        if (len > DUMP_MAX_BYTE_DUMP) {
            os << indent << "  ... (" << std::dec << (len - DUMP_MAX_BYTE_DUMP) << " more bytes)\n";
        }
        break;
    }
    case Value::Type::Map: {
        auto* m = v.as_map();
        size_t len = m ? m->size() : 0;
        os << indent << "{map, " << len << " entries}\n";
        if (m && depth < max_depth) {
            auto ks = m->keys();
            size_t show = std::min(ks.size(), DUMP_MAX_ARRAY_ITEMS);
            for (size_t i = 0; i < show; ++i) {
                Value val;
                if (m->get(ks[i], val)) {
                    os << indent << "  \"" << ks[i] << "\" => ";
                    // Inline simple types, recurse for heap types
                    if (val.type == Value::Type::Integer) {
                        os << val.as_int << "\n";
                    } else if (val.type == Value::Type::Float) {
                        os << val.as_float << "\n";
                    } else if (val.type == Value::Type::Boolean) {
                        os << (val.as_bool() ? "true" : "false") << "\n";
                    } else if (val.type == Value::Type::String && val.as_ptr) {
                        os << "\"" << val.as_string()->view() << "\"\n";
                    } else {
                        os << "\n";
                        dump_value(os, val, depth + 1, max_depth);
                    }
                    val.release();
                }
            }
            if (ks.size() > DUMP_MAX_ARRAY_ITEMS) {
                os << indent << "  ... (" << (ks.size() - DUMP_MAX_ARRAY_ITEMS) << " more)\n";
            }
        }
        break;
    }
    case Value::Type::Matrix: {
        if (v.as_ptr) {
            auto* mat = v.as_matrix();
            os << indent << "<matrix " << mat->rows() << "x" << mat->cols() << ">\n";
            for (int64_t r = 0; r < mat->rows() && r < static_cast<int64_t>(DUMP_MAX_ARRAY_ITEMS); ++r) {
                os << indent << "  [";
                for (int64_t c = 0; c < mat->cols(); ++c) {
                    if (c > 0) os << ", ";
                    os << mat->get(r, c);
                }
                os << "]\n";
            }
            if (mat->rows() > static_cast<int64_t>(DUMP_MAX_ARRAY_ITEMS)) {
                os << indent << "  ... (" << (mat->rows() - DUMP_MAX_ARRAY_ITEMS) << " more rows)\n";
            }
        } else {
            os << indent << "<matrix:null>\n";
        }
        break;
    }
    case Value::Type::Json: {
        if (v.as_ptr) {
            auto* hj = v.as_json();
            os << indent << hj->dump(2) << " (json)\n";
        } else {
            os << indent << "<json:null> (json)\n";
        }
        break;
    }
    case Value::Type::Observable:
        if (v.as_ptr) {
            os << indent << "<observable:" << v.as_observable()->kind_name() << "> (observable)\n";
        } else {
            os << indent << "<observable:null> (observable)\n";
        }
        break;
    case Value::Type::Xt:
        if (v.as_ptr) {
            os << indent << "<xt:" << v.as_xt_impl()->name() << "> (execution token)\n";
        } else {
            os << indent << "<xt:?> (execution token)\n";
        }
        break;
    case Value::Type::DataRef:
        os << indent << "dataref(index=" << v.dataref_index()
           << ", offset=" << v.dataref_offset() << ")\n";
        break;
    }
}

} // anonymous namespace

bool prim_dump(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) {
        ctx.out() << "Stack empty\n";
        return true;
    }
    ctx.data_stack().push(*opt);
    dump_value(ctx.out(), *opt, 0, DUMP_MAX_DEPTH);
    return true;
}

// --- See primitive ---

namespace {

std::string format_instruction(const Instruction& instr) {
    switch (instr.op) {
    case Instruction::Op::Call:
        return "Call " + instr.word_name;
    case Instruction::Op::PushInt:
        return "PushInt " + std::to_string(instr.int_val);
    case Instruction::Op::PushFloat:
        return "PushFloat " + std::to_string(instr.float_val);
    case Instruction::Op::PushBool:
        return std::string("PushBool ") + (instr.int_val ? "true" : "false");
    case Instruction::Op::Branch:
        return "Branch " + std::to_string(instr.int_val);
    case Instruction::Op::BranchIfFalse:
        return "BranchIfFalse " + std::to_string(instr.int_val);
    case Instruction::Op::DoSetup:
        return "DoSetup";
    case Instruction::Op::DoLoop:
        return "DoLoop " + std::to_string(instr.int_val);
    case Instruction::Op::DoPlusLoop:
        return "DoPlusLoop " + std::to_string(instr.int_val);
    case Instruction::Op::DoI:
        return "DoI";
    case Instruction::Op::PrintString:
        return "PrintString \"" + instr.word_name + "\"";
    case Instruction::Op::PushString:
        return "PushString \"" + instr.word_name + "\"";
    case Instruction::Op::PushDataPtr:
        return "PushDataPtr";
    case Instruction::Op::SetDoes:
        return "SetDoes " + std::to_string(instr.int_val);
    case Instruction::Op::PushXt:
        return "PushXt " + instr.word_name;
    case Instruction::Op::ToR:
        return "ToR";
    case Instruction::Op::FromR:
        return "FromR";
    case Instruction::Op::FetchR:
        return "FetchR";
    case Instruction::Op::DoJ:
        return "DoJ";
    case Instruction::Op::DoLeave:
        return "DoLeave " + std::to_string(instr.int_val);
    case Instruction::Op::DoExit:
        return "DoExit";
    case Instruction::Op::PushJson:
        return "PushJson " + instr.word_name;
    case Instruction::Op::BlockBegin:
        return "BlockBegin " + std::to_string(instr.int_val);
    case Instruction::Op::BlockEnd:
        return "BlockEnd " + std::to_string(instr.int_val);
    case Instruction::Op::BlockSeparator:
        return "BlockSeparator " + std::to_string(instr.int_val);
    }
    return "Unknown";
}

// Handler word table for see (same 16 compile-only words as help)
const absl::flat_hash_map<std::string, bool>& see_handler_table() {
    static const absl::flat_hash_map<std::string, bool> table = {
        {":", true}, {";", true}, {"does>", true},
        {"if", true}, {"else", true}, {"then", true},
        {"do", true}, {"loop", true}, {"+loop", true}, {"i", true}, {"j", true},
        {"leave", true}, {">r", true}, {"r>", true}, {"r@", true},
        {"exit", true}, {"recurse", true}, {"again", true},
        {"begin", true}, {"until", true}, {"while", true}, {"repeat", true},
        {".\"", true}, {"s\"", true}, {".|", true}, {"s|", true}, {"j|", true}, {"[']", true},
    };
    return table;
}

} // anonymous namespace

bool prim_see(ExecutionContext& ctx) {
    auto* dict = ctx.dictionary();
    auto* is = ctx.input_stream();
    auto& os = ctx.out();

    std::string word;
    bool has_word = is && (*is >> word);

    if (!has_word) {
        os << "Usage: see <word>\n";
        return true;
    }

    // Check dictionary first
    if (dict) {
        auto impls = dict->get_implementations(word);
        if (impls && !impls->empty()) {
            const auto& impl_vec = *impls;
            if (impl_vec.size() > 1) {
                os << word << " has " << impl_vec.size()
                   << " implementation(s), showing latest:\n";
            }
            // Show latest implementation
            const auto& impl = impl_vec.back();

            if (impl->native_code() && !impl->bytecode()) {
                // Pure native primitive
                os << word << " is a primitive (" << impl->name() << ")\n";
            } else if (impl->bytecode()) {
                // Compiled word
                auto* code = impl->bytecode().get();
                const auto& data = code->data_field();
                bool has_data = !data.empty();

                os << ": " << word;
                if (has_data) {
                    os << "   (CREATE'd)";
                }
                os << "\n";

                if (has_data) {
                    os << "  Data field: [";
                    for (size_t i = 0; i < data.size(); ++i) {
                        if (i > 0) os << ", ";
                        if (data[i].type == Value::Type::Integer)
                            os << data[i].as_int;
                        else if (data[i].type == Value::Type::Float)
                            os << data[i].as_float;
                        else if (data[i].type == Value::Type::Boolean)
                            os << (data[i].as_bool() ? "true" : "false");
                        else
                            os << "?";
                    }
                    os << "]\n";
                }

                const auto& instrs = code->instructions();
                for (size_t i = 0; i < instrs.size(); ++i) {
                    os << "  " << i << ": " << format_instruction(instrs[i]) << "\n";
                }
                os << ";\n";
            } else {
                // Word with no bytecode and no native code
                os << word << " has no visible implementation\n";
            }
            return true;
        }
    }

    // Check handler word table
    const auto& handlers = see_handler_table();
    if (handlers.find(word) != handlers.end()) {
        os << word << " is a compile-only handler word\n";
        return true;
    }

    os << "Unknown word: " << word << "\n";
    return true;
}

// --- Time primitives ---

bool prim_time_us(ExecutionContext& ctx) {
    auto now = std::chrono::system_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
    ctx.data_stack().push(Value(static_cast<int64_t>(us)));
    return true;
}

namespace {

// Convert microseconds since epoch to seconds + fractional microseconds.
// Uses floor-division so fractional part is always 0–999999 (even for negative/pre-epoch).
void us_to_parts(int64_t us, time_t& seconds, int64_t& frac) {
    seconds = static_cast<time_t>(us / 1000000);
    frac = us % 1000000;
    if (frac < 0) {
        frac += 1000000;
        seconds -= 1;
    }
}

} // anonymous namespace

bool prim_us_to_iso(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) {
        ctx.data_stack().push(*opt);
        return false;
    }
    time_t seconds;
    int64_t frac;
    us_to_parts(opt->as_int, seconds, frac);
    struct tm tm_buf;
    gmtime_r(&seconds, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm_buf);
    ctx.data_stack().push(Value::from(HeapString::create(buf)));
    return true;
}

bool prim_us_to_iso_us(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) {
        ctx.data_stack().push(*opt);
        return false;
    }
    time_t seconds;
    int64_t frac;
    us_to_parts(opt->as_int, seconds, frac);
    struct tm tm_buf;
    gmtime_r(&seconds, &tm_buf);
    char buf[64];
    size_t pos = std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm_buf);
    std::snprintf(buf + pos, sizeof(buf) - pos, ".%06ldZ", static_cast<long>(frac));
    ctx.data_stack().push(Value::from(HeapString::create(buf)));
    return true;
}

// --- Julian Date / Modified Julian Date primitives ---

namespace {

constexpr double JD_UNIX_EPOCH = 2440587.5;   // JD at 1970-01-01T00:00:00Z
constexpr double MJD_UNIX_EPOCH = 40587.0;     // MJD at 1970-01-01T00:00:00Z  (MJD = JD - 2400000.5)
constexpr double US_PER_DAY = 86400000000.0;   // microseconds per day

} // anonymous namespace

// Shared helpers for JD/MJD conversions (differ only in epoch constant)
static bool prim_us_to_epoch(ExecutionContext& ctx, double epoch) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) {
        ctx.data_stack().push(*opt);
        return false;
    }
    double result = (static_cast<double>(opt->as_int) / US_PER_DAY) + epoch;
    ctx.data_stack().push(Value(result));
    return true;
}
static bool prim_epoch_to_us(ExecutionContext& ctx, double epoch) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    double val;
    if (opt->type == Value::Type::Float) {
        val = opt->as_float;
    } else if (opt->type == Value::Type::Integer) {
        val = static_cast<double>(opt->as_int);
    } else {
        ctx.data_stack().push(*opt);
        return false;
    }
    auto us = static_cast<int64_t>((val - epoch) * US_PER_DAY);
    ctx.data_stack().push(Value(us));
    return true;
}
bool prim_us_to_jd(ExecutionContext& ctx)  { return prim_us_to_epoch(ctx, JD_UNIX_EPOCH); }
bool prim_jd_to_us(ExecutionContext& ctx)  { return prim_epoch_to_us(ctx, JD_UNIX_EPOCH); }
bool prim_us_to_mjd(ExecutionContext& ctx) { return prim_us_to_epoch(ctx, MJD_UNIX_EPOCH); }
bool prim_mjd_to_us(ExecutionContext& ctx) { return prim_epoch_to_us(ctx, MJD_UNIX_EPOCH); }

bool prim_sleep(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;

    int64_t us;
    if (opt->type == Value::Type::Integer) {
        us = opt->as_int;
    } else if (opt->type == Value::Type::Float) {
        us = static_cast<int64_t>(opt->as_float);
    } else {
        ctx.data_stack().push(*opt);
        return false;
    }

    if (us <= 0) return true;  // no-op for zero/negative

    auto now = std::chrono::steady_clock::now();
    auto sleep_dur = std::chrono::microseconds(us);

    // Reject if sleep would exceed the execution deadline
    if (now + sleep_dur > ctx.deadline()) {
        ctx.err() << "Error: sleep " << us
                  << "us would exceed execution deadline\n";
        return false;
    }

#ifdef ETIL_WASM_BUILD
    ctx.err() << "Error: sleep not available in browser\n";
    return false;
#else
    std::this_thread::sleep_for(sleep_dur);
    return true;
#endif
}

// elapsed ( xt -- us ) — execute xt and push elapsed microseconds
bool prim_elapsed(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Xt || !opt->as_ptr) {
        opt->release();
        ctx.err() << "Error: elapsed expects an xt\n";
        return false;
    }
    auto* impl = opt->as_xt_impl();

    auto start = std::chrono::steady_clock::now();

    bool ok = true;
    if (impl->native_code()) {
        ok = impl->native_code()(ctx);
    } else if (impl->bytecode()) {
        if (!ctx.enter_call()) {
            ctx.err() << "Error: maximum call depth exceeded\n";
            impl->release();
            return false;
        }
        ok = execute_compiled(*impl->bytecode(), ctx);
        ctx.exit_call();
    } else {
        ctx.err() << "Error: xt has no executable code\n";
        ok = false;
    }
    impl->release();

    if (!ok) return false;

    auto end = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    ctx.data_stack().push(Value(static_cast<int64_t>(us)));
    return true;
}

bool prim_sys_notification(ExecutionContext& ctx) {
    // Check send_system_notification permission
    auto* perms = ctx.permissions();
    if (perms && !perms->send_system_notification) {
        ctx.err() << "Error: sys-notification not permitted for this role\n";
        return false;
    }

    auto opt = ctx.data_stack().pop();
    if (!opt) return false;

    std::string msg;
    switch (opt->type) {
    case Value::Type::String:
        if (opt->as_ptr) {
            msg = std::string(opt->as_string()->view());
        } else {
            msg = "<null-string>";
        }
        break;
    case Value::Type::Integer:
        msg = std::to_string(opt->as_int);
        break;
    case Value::Type::Float: {
        std::ostringstream oss;
        oss << opt->as_float;
        msg = oss.str();
        break;
    }
    default:
        msg = "<value>";
        break;
    }

    opt->release();
    if (ctx.notification_sender()) {
        ctx.notification_sender()(msg);
    }
    ctx.queue_notification(std::move(msg));
    return true;
}

bool prim_user_notification(ExecutionContext& ctx) {
    // Check send_user_notification permission
    auto* perms = ctx.permissions();
    if (perms && !perms->send_user_notification) {
        ctx.err() << "Error: user-notification not permitted for this role\n";
        return false;
    }

    // ( msg-str user-id-str -- flag )
    auto user_id_opt = ctx.data_stack().pop();
    if (!user_id_opt) return false;
    auto msg_opt = ctx.data_stack().pop();
    if (!msg_opt) {
        user_id_opt->release();
        return false;
    }

    if (user_id_opt->type != Value::Type::String || !user_id_opt->as_ptr) {
        ctx.err() << "Error: user-notification: user-id must be a string\n";
        user_id_opt->release();
        msg_opt->release();
        return false;
    }
    if (msg_opt->type != Value::Type::String || !msg_opt->as_ptr) {
        ctx.err() << "Error: user-notification: message must be a string\n";
        user_id_opt->release();
        msg_opt->release();
        return false;
    }

    std::string user_id(user_id_opt->as_string()->view());
    std::string msg(msg_opt->as_string()->view());
    user_id_opt->release();
    msg_opt->release();

    bool sent = false;
    if (ctx.targeted_notification_sender()) {
        sent = ctx.targeted_notification_sender()(user_id, msg);
    }

    ctx.data_stack().push(Value(int64_t(sent ? -1 : 0)));
    return true;
}

bool prim_sys_datafields(ExecutionContext& ctx) {
    auto& reg = ctx.data_field_registry();
    auto live = reg.live_count();
    auto total = reg.entry_count();
    auto cells = reg.total_cells();
    ctx.out() << "DataFieldRegistry: " << total << " entries ("
              << live << " live), "
              << cells << " cells, "
              << (cells * sizeof(Value)) << " bytes\n";
    return true;
}

// --- Program abort ---

bool prim_abort(ExecutionContext& ctx) {
    // ( flag -- ) or ( error-string false -- )
    auto flag_opt = ctx.data_stack().pop();
    if (!flag_opt) return false;

    bool success = false;
    if (flag_opt->type == Value::Type::Boolean) {
        success = flag_opt->as_bool();
    } else if (flag_opt->type == Value::Type::Integer) {
        success = (flag_opt->as_int != 0);
    } else if (flag_opt->type == Value::Type::Float) {
        success = (flag_opt->as_float != 0.0);
    }
    // Any other type (string, array, etc.) is treated as false

    if (success) {
        ctx.request_abort(true);
    } else {
        // Pop error message (if available)
        std::string msg = "abort";
        auto msg_opt = ctx.data_stack().pop();
        if (msg_opt) {
            switch (msg_opt->type) {
            case Value::Type::String:
                if (msg_opt->as_ptr) {
                    msg = std::string(msg_opt->as_string()->view());
                }
                break;
            case Value::Type::Integer:
                msg = std::to_string(msg_opt->as_int);
                break;
            case Value::Type::Float: {
                std::ostringstream oss;
                oss << msg_opt->as_float;
                msg = oss.str();
                break;
            }
            default:
                break;
            }
            msg_opt->release();
        }
        ctx.request_abort(false, std::move(msg));
    }

    // Release the flag value (heap types as flag would be unusual but safe)
    flag_opt->release();

    // Return true — abort propagates via cancel() → tick() returns false.
    return true;
}

// --- Execution token (xt) primitives ---

bool prim_tick(ExecutionContext& ctx) {
    auto* is = ctx.input_stream();
    if (!is) {
        ctx.err() << "Error: ' requires input stream\n";
        return false;
    }
    std::string token;
    if (!(*is >> token)) {
        ctx.err() << "Error: ' expects a word name\n";
        return false;
    }
    auto* dict = ctx.dictionary();
    if (!dict) return false;
    auto result = dict->lookup(token);
    if (!result) {
        ctx.err() << "Error: ' unknown word '" << token << "'\n";
        return false;
    }
    auto* impl = result->get();
    impl->add_ref();
    ctx.data_stack().push(Value::from_xt(impl));
    return true;
}

bool prim_execute(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Xt || !opt->as_ptr) {
        opt->release();
        ctx.err() << "Error: execute expects an xt\n";
        return false;
    }
    auto* impl = opt->as_xt_impl();
    bool ok = true;
    if (impl->native_code()) {
        ok = impl->native_code()(ctx);
    } else if (impl->bytecode()) {
        if (!ctx.enter_call()) {
            ctx.err() << "Error: maximum call depth exceeded\n";
            impl->release();
            return false;
        }
        ok = execute_compiled(*impl->bytecode(), ctx);
        ctx.exit_call();
    } else {
        ctx.err() << "Error: xt has no executable code\n";
        ok = false;
    }
    impl->release();
    return ok;
}

bool prim_xt_query(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    bool is_xt = (opt->type == Value::Type::Xt);
    opt->release();
    ctx.data_stack().push(Value(is_xt));
    return true;
}

bool prim_xt_to_name(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Xt || !opt->as_ptr) {
        opt->release();
        ctx.err() << "Error: >name expects an xt\n";
        return false;
    }
    auto* impl = opt->as_xt_impl();
    auto* hs = HeapString::create(impl->name());
    impl->release();
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// --- Defining word primitives ---

bool prim_immediate(ExecutionContext& ctx) {
    auto* w = ctx.last_created();
    if (!w) {
        ctx.err() << "immediate: no word defined yet\n";
        return false;
    }
    w->set_immediate(true);
    return true;
}

bool prim_xt_body(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Xt || !opt->as_ptr) {
        ctx.err() << "xt-body: expected xt\n";
        opt->release();
        return false;
    }
    auto* impl = opt->as_xt_impl();
    auto bc = impl->bytecode();
    impl->release();
    if (!bc || bc->data_field().empty()) {
        ctx.err() << "xt-body: word has no data field\n";
        return false;
    }
    // Lazy-register the data field if not already registered
    if (bc->registry_index() < 0) {
        auto reg_ptr = ctx.data_field_registry_ptr();
        auto idx = reg_ptr->register_field(&bc->data_field());
        bc->set_registry_index(static_cast<int64_t>(idx));
        bc->set_registry(reg_ptr);
    }
    ctx.data_stack().push(make_dataref(
        static_cast<uint32_t>(bc->registry_index()), 0));
    return true;
}

// --- Stack display ---

bool prim_dot_s(ExecutionContext& ctx) {
    auto& stack = ctx.data_stack();
    size_t depth = stack.size();
    ctx.out() << "<" << depth << ">";
    for (size_t i = 0; i < depth; ++i) {
        const Value& v = stack[i];
        ctx.out() << " ";
        if (v.type == Value::Type::Integer) {
            ctx.out() << v.as_int;
        } else if (v.type == Value::Type::Float) {
            ctx.out() << v.as_float;
        } else if (v.type == Value::Type::Boolean) {
            ctx.out() << (v.as_bool() ? "true" : "false");
        } else if (v.type == Value::Type::String) {
            if (v.as_ptr) {
                auto sv = v.as_string()->view();
                if (sv.size() <= 64) {
                    ctx.out() << sv;
                } else {
                    ctx.out() << sv.substr(0, 64) << "...";
                }
            } else {
                ctx.out() << "<string:null>";
            }
        } else if (v.type == Value::Type::Array) {
            ctx.out() << "<array:" << (v.as_ptr ? v.as_array()->length() : 0) << ">";
        } else if (v.type == Value::Type::ByteArray) {
            ctx.out() << "<bytes:" << (v.as_ptr ? v.as_byte_array()->length() : 0) << ">";
        } else if (v.type == Value::Type::Map) {
            ctx.out() << "<map:" << (v.as_ptr ? v.as_map()->size() : 0) << ">";
        } else if (v.type == Value::Type::Json) {
            ctx.out() << "<json>";
        } else if (v.type == Value::Type::Observable) {
            if (v.as_ptr) {
                ctx.out() << "<observable:" << v.as_observable()->kind_name() << ">";
            } else {
                ctx.out() << "<observable>";
            }
        } else if (v.type == Value::Type::Xt) {
            if (v.as_ptr) {
                ctx.out() << "<xt:" << v.as_xt_impl()->name() << ">";
            } else {
                ctx.out() << "<xt:?>";
            }
        } else if (v.type == Value::Type::DataRef) {
            ctx.out() << "<dataref>";
        }
    }
    ctx.out() << "\n";
    return true;
}

// --- Type conversion primitives ---

bool prim_int_to_float(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Integer) {
        ctx.data_stack().push(Value(static_cast<double>(opt->as_int)));
    } else if (opt->type == Value::Type::Float) {
        ctx.data_stack().push(*opt);
    } else {
        ctx.err() << "int->float: expected number\n";
        opt->release();
        return false;
    }
    return true;
}

bool prim_float_to_int(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type == Value::Type::Float) {
        ctx.data_stack().push(Value(static_cast<int64_t>(opt->as_float)));
    } else if (opt->type == Value::Type::Integer) {
        ctx.data_stack().push(*opt);
    } else {
        ctx.err() << "float->int: expected number\n";
        opt->release();
        return false;
    }
    return true;
}

bool prim_number_to_string(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    std::string s;
    if (opt->type == Value::Type::Integer) {
        s = std::to_string(opt->as_int);
    } else if (opt->type == Value::Type::Float) {
        std::ostringstream oss;
        oss << opt->as_float;
        s = oss.str();
    } else {
        ctx.err() << "number->string: expected number\n";
        opt->release();
        return false;
    }
    auto* hs = HeapString::create(s);
    ctx.data_stack().push(Value::from(hs));
    return true;
}

bool prim_string_to_number(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::String || !opt->as_ptr) {
        ctx.err() << "string->number: expected string\n";
        opt->release();
        return false;
    }
    std::string s(opt->as_string()->view());
    opt->release();

    // Try integer first (matches interpreter parsing order)
    try {
        size_t pos;
        int64_t ival = std::stoll(s, &pos);
        if (pos == s.size()) {
            ctx.data_stack().push(Value(ival));
            ctx.data_stack().push(Value(true));
            return true;
        }
    } catch (...) {}

    // Try float
    try {
        size_t pos;
        double fval = std::stod(s, &pos);
        if (pos == s.size()) {
            ctx.data_stack().push(Value(fval));
            ctx.data_stack().push(Value(true));
            return true;
        }
    } catch (...) {}

    // Parse failure
    ctx.data_stack().push(Value(false));
    return true;
}

// --- Dictionary checkpoint/restore ---

bool prim_marker(ExecutionContext& ctx) {
    auto* is = ctx.input_stream();
    if (!is) { ctx.err() << "marker: no input stream\n"; return false; }
    std::string name;
    if (!(*is >> name)) { ctx.err() << "marker: expected name\n"; return false; }

    auto* dict = ctx.dictionary();

    // Snapshot current state: { "word": impl_count, ... }
    nlohmann::json snapshot;
    auto names = dict->word_names();
    for (const auto& n : names) {
        auto impls = dict->get_implementations(n);
        if (impls) snapshot[n] = impls->size();
    }

    // Create compiled word: PushString(name) + Call("marker-restore")
    auto code = std::make_shared<ByteCode>();
    Instruction push_name;
    push_name.op = Instruction::Op::PushString;
    push_name.word_name = name;
    code->append(std::move(push_name));
    Instruction call_restore;
    call_restore.op = Instruction::Op::Call;
    call_restore.word_name = "marker-restore";
    code->append(std::move(call_restore));

    auto id = Dictionary::next_id();
    auto* impl = new WordImpl(name, id);
    impl->set_bytecode(code);
    impl->set_weight(1.0);
    impl->set_generation(0);
    dict->register_word(name, WordImplPtr(impl));

    // Store snapshot as metadata on the marker concept
    dict->set_concept_metadata(name, "_marker_snapshot",
                                MetadataFormat::Json, snapshot.dump());
    return true;
}

bool prim_marker_restore(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::String || !opt->as_ptr) {
        opt->release();
        ctx.err() << "marker-restore: expected string\n";
        return false;
    }
    std::string marker_name(opt->as_string()->view());
    opt->release();

    auto* dict = ctx.dictionary();
    auto meta = dict->get_concept_metadata(marker_name, "_marker_snapshot");
    if (!meta) {
        ctx.err() << "marker-restore: no snapshot for '" << marker_name << "'\n";
        return false;
    }

    auto snapshot = nlohmann::json::parse(meta->content);
    auto current_names = dict->word_names();  // copy — safe to mutate dict

    // Remove concepts not in snapshot
    for (const auto& n : current_names) {
        if (!snapshot.contains(n)) {
            dict->forget_all(n);
        }
    }

    // Trim concepts that gained implementations
    for (auto& [word, count_json] : snapshot.items()) {
        size_t target = count_json.get<size_t>();
        auto impls = dict->get_implementations(word);
        if (impls && impls->size() > target) {
            size_t excess = impls->size() - target;
            for (size_t i = 0; i < excess; ++i) {
                dict->forget_word(word);
            }
        }
    }

    // Marker word itself was not in snapshot, so already forgotten above
    return true;
}

WordImplPtr make_primitive(const char* name, WordImpl::FunctionPtr fn,
                           std::vector<TypeSignature::Type> inputs,
                           std::vector<TypeSignature::Type> outputs) {
    auto id = Dictionary::next_id();
    auto* impl = new WordImpl(name, id);
    impl->set_native_code(fn);
    impl->set_weight(1.0);
    impl->set_generation(0);
    TypeSignature sig;
    sig.inputs = std::move(inputs);
    sig.outputs = std::move(outputs);
    impl->set_signature(std::move(sig));
    impl->mark_as_primitive();
    return WordImplPtr(impl);
}

// --- Table-driven primitive registration ---

using T = TypeSignature::Type;

// Sentinel marking the end of a type list in fixed-size arrays.
constexpr auto N = static_cast<T>(255);

struct PrimEntry {
    const char* word_name;
    WordImpl::FunctionPtr fn;
    uint8_t n_in;
    uint8_t n_out;
    T in[4];
    T out[3];
};

// clang-format off
static const PrimEntry prim_table[] = {
    // Arithmetic
    {"+",       prim_add,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"-",       prim_sub,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"*",       prim_mul,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"/",       prim_div,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"mod",     prim_mod,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"/mod",    prim_divmod,  2, 2, {T::Unknown, T::Unknown},           {T::Unknown, T::Unknown}},
    {"negate",  prim_negate,  1, 1, {T::Unknown},                       {T::Unknown}},
    {"abs",     prim_abs,     1, 1, {T::Unknown},                       {T::Unknown}},
    {"max",     prim_max,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"min",     prim_min,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"within",  prim_within,  3, 1, {T::Unknown, T::Unknown, T::Unknown},{T::Unknown}},
    // Stack
    {"dup",     prim_dup,     1, 2, {T::Unknown},                       {T::Unknown, T::Unknown}},
    {"drop",    prim_drop,    1, 0, {T::Unknown},                       {}},
    {"swap",    prim_swap,    2, 2, {T::Unknown, T::Unknown},           {T::Unknown, T::Unknown}},
    {"over",    prim_over,    2, 3, {T::Unknown, T::Unknown},           {T::Unknown, T::Unknown, T::Unknown}},
    {"rot",     prim_rot,     3, 3, {T::Unknown, T::Unknown, T::Unknown},{T::Unknown, T::Unknown, T::Unknown}},
    {"pick",    prim_pick,    1, 0, {T::Integer},                       {}},
    {"nip",     prim_nip,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"tuck",    prim_tuck,    2, 3, {T::Unknown, T::Unknown},           {T::Unknown, T::Unknown, T::Unknown}},
    {"depth",   prim_depth,   0, 1, {},                                 {T::Integer}},
    {"?dup",    prim_qdup,    1, 0, {T::Unknown},                       {}},
    {"roll",    prim_roll,    1, 0, {T::Integer},                       {}},
    // Comparison
    {"=",       prim_eq,      2, 1, {T::Unknown, T::Unknown},           {T::Integer}},
    {"<>",      prim_neq,     2, 1, {T::Unknown, T::Unknown},           {T::Integer}},
    {"<",       prim_lt,      2, 1, {T::Unknown, T::Unknown},           {T::Integer}},
    {">",       prim_gt,      2, 1, {T::Unknown, T::Unknown},           {T::Integer}},
    {"<=",      prim_le,      2, 1, {T::Unknown, T::Unknown},           {T::Integer}},
    {">=",      prim_ge,      2, 1, {T::Unknown, T::Unknown},           {T::Integer}},
    {"0=",      prim_zero_eq, 1, 1, {T::Unknown},                       {T::Integer}},
    {"0<",      prim_zero_lt, 1, 1, {T::Unknown},                       {T::Integer}},
    {"0>",      prim_zero_gt, 1, 1, {T::Unknown},                       {T::Integer}},
    // Boolean
    {"true",    prim_true,    0, 1, {},                                 {T::Unknown}},
    {"false",   prim_false,   0, 1, {},                                 {T::Unknown}},
    {"not",     prim_not,     1, 1, {T::Unknown},                       {T::Unknown}},
    {"bool",    prim_to_bool, 1, 1, {T::Unknown},                       {T::Unknown}},
    // Logic
    {"and",     prim_and,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"or",      prim_or,      2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"xor",     prim_xor,     2, 1, {T::Unknown, T::Unknown},           {T::Unknown}},
    {"invert",  prim_invert,  1, 1, {T::Unknown},                       {T::Unknown}},
    {"lshift",  prim_lshift,  2, 1, {T::Integer, T::Integer},           {T::Integer}},
    {"rshift",  prim_rshift,  2, 1, {T::Integer, T::Integer},           {T::Integer}},
    {"lroll",   prim_lroll,   2, 1, {T::Integer, T::Integer},           {T::Integer}},
    {"rroll",   prim_rroll,   2, 1, {T::Integer, T::Integer},           {T::Integer}},
    // I/O
    {".",        prim_dot,     1, 0, {T::Unknown},                       {}},
    {"cr",       prim_cr,      0, 0, {},                                 {}},
    {"emit",     prim_emit,    1, 0, {T::Integer},                       {}},
    {"space",    prim_space,   0, 0, {},                                 {}},
    {"spaces",   prim_spaces,  1, 0, {T::Integer},                       {}},
    {"words",    prim_words,   0, 0, {},                                 {}},
    {"hex.",     prim_hex_dot, 1, 0, {T::Integer},                       {}},
    {"bin.",     prim_bin_dot, 1, 0, {T::Integer},                       {}},
    {"oct.",     prim_oct_dot, 1, 0, {T::Integer},                       {}},
    // Memory
    {"create",   prim_create,  0, 0, {},                                 {}},
    {",",        prim_comma,   1, 0, {T::Unknown},                       {}},
    {"@",        prim_fetch,   1, 1, {T::Unknown},                       {T::Unknown}},
    {"!",        prim_store,   2, 0, {T::Unknown, T::Unknown},           {}},
    {"allot",    prim_allot,   1, 0, {T::Integer},                       {}},
    // Math (unary — all return Float)
    {"sqrt",     prim_sqrt,    1, 1, {T::Unknown},                       {T::Float}},
    {"sin",      prim_sin,     1, 1, {T::Unknown},                       {T::Float}},
    {"cos",      prim_cos,     1, 1, {T::Unknown},                       {T::Float}},
    {"tan",      prim_tan,     1, 1, {T::Unknown},                       {T::Float}},
    {"asin",     prim_asin,    1, 1, {T::Unknown},                       {T::Float}},
    {"acos",     prim_acos,    1, 1, {T::Unknown},                       {T::Float}},
    {"atan",     prim_atan,    1, 1, {T::Unknown},                       {T::Float}},
    {"atan2",    prim_atan2,   2, 1, {T::Unknown, T::Unknown},           {T::Float}},
    {"log",      prim_log,     1, 1, {T::Unknown},                       {T::Float}},
    {"log2",     prim_log2,    1, 1, {T::Unknown},                       {T::Float}},
    {"log10",    prim_log10,   1, 1, {T::Unknown},                       {T::Float}},
    {"exp",      prim_exp,     1, 1, {T::Unknown},                       {T::Float}},
    {"pow",      prim_pow,     2, 1, {T::Unknown, T::Unknown},           {T::Float}},
    {"ceil",     prim_ceil,    1, 1, {T::Unknown},                       {T::Float}},
    {"floor",    prim_floor,   1, 1, {T::Unknown},                       {T::Float}},
    {"round",    prim_round,   1, 1, {T::Unknown},                       {T::Float}},
    {"trunc",    prim_trunc,   1, 1, {T::Unknown},                       {T::Float}},
    {"fmin",     prim_fmin,    2, 1, {T::Unknown, T::Unknown},           {T::Float}},
    {"fmax",     prim_fmax,    2, 1, {T::Unknown, T::Unknown},           {T::Float}},
    {"pi",       prim_pi,      0, 1, {},                                 {T::Float}},
    {"tanh",     prim_tanh,    1, 1, {T::Unknown},                       {T::Float}},
    {"f~",       prim_fapprox, 3, 1, {T::Unknown, T::Unknown, T::Unknown},{T::Integer}},
    // PRNG
    {"random",       prim_random,       0, 1, {},                         {T::Float}},
    {"random-seed",  prim_random_seed,  1, 0, {T::Integer},              {}},
    {"random-range", prim_random_range, 2, 1, {T::Integer, T::Integer},  {T::Integer}},
    // System
    {"sys-semver",       prim_sys_semver,       0, 0, {},                 {}},
    {"sys-timestamp",    prim_sys_timestamp,    0, 0, {},                 {}},
    {"sys-datafields",   prim_sys_datafields,   0, 0, {},                 {}},
    {"sys-notification", prim_sys_notification, 1, 0, {T::Unknown},      {}},
    {"user-notification",prim_user_notification,2, 1, {T::String, T::String},{T::Integer}},
    {"abort",            prim_abort,            1, 0, {T::Unknown},      {}},
    // Selection
    {"select-strategy",  prim_select_strategy,  1, 0, {T::Integer},      {}},
    {"select-epsilon",   prim_select_epsilon,   1, 0, {T::Unknown},      {}},
    {"select-off",       prim_select_off,       0, 0, {},                {}},
    // Evolution
    {"evolve-register",  prim_evolve_register,  2, 1, {T::String, T::Array}, {T::Unknown}},
    {"evolve-register-pool", prim_evolve_register_pool, 3, 1, {T::String, T::Array, T::Array}, {T::Unknown}},
    {"evolve-word",      prim_evolve_word,      1, 1, {T::String},           {T::Integer}},
    {"evolve-all",       prim_evolve_all,       0, 0, {},                    {}},
    {"evolve-status",    prim_evolve_status,    1, 1, {T::String},           {T::Integer}},
    {"evolve-tag",       prim_evolve_tag,       2, 0, {T::String, T::String}, {}},
    {"evolve-untag",     prim_evolve_untag,     1, 0, {T::String}, {}},
    {"evolve-bridge",    prim_evolve_bridge,    3, 0, {T::String, T::String, T::String}, {}},
    {"evolve-fitness-mode",  prim_evolve_fitness_mode,  1, 0, {T::Integer}, {}},
    {"evolve-fitness-alpha", prim_evolve_fitness_alpha, 1, 0, {T::Unknown}, {}},
    {"evolve-log-start", prim_evolve_log_start, 2, 0, {T::Integer, T::Integer}, {}},
    {"evolve-log-stop",  prim_evolve_log_stop,  0, 0, {},                    {}},
    {"evolve-log-dir",   prim_evolve_log_dir,   1, 0, {T::String},           {}},
    // Time
    {"time-us",    prim_time_us,     0, 1, {},          {T::Integer}},
    {"us->iso",    prim_us_to_iso,   1, 1, {T::Integer},{T::Unknown}},
    {"us->iso-us", prim_us_to_iso_us,1, 1, {T::Integer},{T::Unknown}},
    {"us->jd",     prim_us_to_jd,    1, 1, {T::Integer},{T::Float}},
    {"jd->us",     prim_jd_to_us,    1, 1, {T::Unknown},{T::Integer}},
    {"us->mjd",    prim_us_to_mjd,   1, 1, {T::Integer},{T::Float}},
    {"mjd->us",    prim_mjd_to_us,   1, 1, {T::Unknown},{T::Integer}},
    {"sleep",      prim_sleep,       1, 0, {T::Integer},{}},
    {"elapsed",    prim_elapsed,     1, 1, {T::Unknown},{T::Integer}},
    // Input-reading
    {"word-read",        prim_word_read,        0, 2, {},          {T::Unknown, T::Integer}},
    {"string-read-delim",prim_string_read_delim,1, 2, {T::Integer},{T::Unknown, T::Integer}},
    // Dictionary operations
    {"dict-forget",     prim_dict_forget,     1, 1, {T::Unknown}, {T::Integer}},
    {"dict-forget-all", prim_dict_forget_all, 1, 1, {T::Unknown}, {T::Integer}},
    {"file-load",       prim_file_load,       1, 1, {T::Unknown}, {T::Integer}},
    {"include",         prim_include,         0, 0, {},            {}},
    {"library",         prim_library,         0, 0, {},            {}},
    {"evaluate",        prim_evaluate,        1, 0, {T::String},   {}},
    {"marker",          prim_marker,          0, 0, {},            {}},
    {"marker-restore",  prim_marker_restore,  1, 0, {T::String},   {}},
    // Metadata (stack-based)
    {"dict-meta-set",  prim_dict_meta_set,  4, 1, {T::Unknown, T::Unknown, T::Unknown, T::Unknown},{T::Integer}},
    {"dict-meta-get",  prim_dict_meta_get,  2, 2, {T::Unknown, T::Unknown},           {T::Unknown, T::Integer}},
    {"dict-meta-del",  prim_dict_meta_del,  2, 1, {T::Unknown, T::Unknown},           {T::Integer}},
    {"dict-meta-keys", prim_dict_meta_keys, 1, 2, {T::Unknown},                       {T::Unknown, T::Integer}},
    {"impl-meta-set",  prim_impl_meta_set,  4, 1, {T::Unknown, T::Unknown, T::Unknown, T::Unknown},{T::Integer}},
    {"impl-meta-get",  prim_impl_meta_get,  2, 2, {T::Unknown, T::Unknown},           {T::Unknown, T::Integer}},
    // Help & Debug
    {"help",    prim_help,    0, 0, {},                                 {}},
    {"dump",    prim_dump,    1, 1, {T::Unknown},                       {T::Unknown}},
    {"see",     prim_see,     0, 0, {},                                 {}},
    // Execution tokens
    {"'",       prim_tick,        0, 1, {},          {T::Unknown}},
    {"execute", prim_execute,     1, 0, {T::Unknown},{}},
    {"xt?",     prim_xt_query,    1, 1, {T::Unknown},{T::Integer}},
    {">name",   prim_xt_to_name,  1, 1, {T::Unknown},{T::String}},
    // Defining words
    {"immediate",prim_immediate,  0, 0, {},          {}},
    {"xt-body",  prim_xt_body,    1, 1, {T::Unknown},{T::Unknown}},
    // Stack display
    {".s",       prim_dot_s,      0, 0, {},          {}},
    // Type conversion
    {"int->float",     prim_int_to_float,     1, 1, {T::Integer}, {T::Float}},
    {"float->int",     prim_float_to_int,     1, 1, {T::Float},   {T::Integer}},
    {"number->string", prim_number_to_string, 1, 1, {T::Unknown}, {T::String}},
    {"string->number", prim_string_to_number, 1, 0, {T::String},  {}},
};
// clang-format on

void register_primitives(Dictionary& dict) {
    // Register all entries from the table
    for (const auto& e : prim_table) {
        std::vector<T> in(e.in, e.in + e.n_in);
        std::vector<T> out(e.out, e.out + e.n_out);
        dict.register_word(e.word_name,
            make_primitive(e.word_name, e.fn, std::move(in), std::move(out)));
    }

    // Register sub-module primitives
    register_string_primitives(dict);
    register_array_primitives(dict);
    register_byte_primitives(dict);
    register_map_primitives(dict);
    register_json_primitives(dict);
    register_matrix_primitives(dict);
    etil::lvfs::register_lvfs_primitives(dict);
    etil::fileio::register_file_io_primitives(dict);
    etil::fileio::register_async_file_io_primitives(dict);
    register_observable_primitives(dict);
}

} // namespace etil::core
