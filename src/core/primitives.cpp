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
#include "etil/core/json_primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_primitives.hpp"
#include "etil/fileio/async_file_io.hpp"
#include "etil/fileio/file_io_primitives.hpp"
#include "etil/lvfs/lvfs.hpp"
#include "etil/mcp/role_permissions.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/interpreter.hpp"
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

bool prim_max(ExecutionContext& ctx) {
    auto opt_b = ctx.data_stack().pop();
    if (!opt_b) return false;
    auto opt_a = ctx.data_stack().pop();
    if (!opt_a) { ctx.data_stack().push(*opt_b); return false; }
    if (opt_a->type == Value::Type::Float || opt_b->type == Value::Type::Float) {
        double a = (opt_a->type == Value::Type::Float) ? opt_a->as_float : static_cast<double>(opt_a->as_int);
        double b = (opt_b->type == Value::Type::Float) ? opt_b->as_float : static_cast<double>(opt_b->as_int);
        ctx.data_stack().push(Value(a >= b ? a : b));
    } else {
        ctx.data_stack().push(Value(opt_a->as_int >= opt_b->as_int ? opt_a->as_int : opt_b->as_int));
    }
    return true;
}

bool prim_min(ExecutionContext& ctx) {
    auto opt_b = ctx.data_stack().pop();
    if (!opt_b) return false;
    auto opt_a = ctx.data_stack().pop();
    if (!opt_a) { ctx.data_stack().push(*opt_b); return false; }
    if (opt_a->type == Value::Type::Float || opt_b->type == Value::Type::Float) {
        double a = (opt_a->type == Value::Type::Float) ? opt_a->as_float : static_cast<double>(opt_a->as_int);
        double b = (opt_b->type == Value::Type::Float) ? opt_b->as_float : static_cast<double>(opt_b->as_int);
        ctx.data_stack().push(Value(a <= b ? a : b));
    } else {
        ctx.data_stack().push(Value(opt_a->as_int <= opt_b->as_int ? opt_a->as_int : opt_b->as_int));
    }
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

bool prim_lroll(ExecutionContext& ctx) {
    auto opt_count = ctx.data_stack().pop();
    if (!opt_count) return false;
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) { ctx.data_stack().push(*opt_count); return false; }
    int64_t count = opt_count->as_int;
    uint64_t val = static_cast<uint64_t>(opt_val->as_int);
    // Negative count → rotate in opposite direction
    if (count < 0) {
        count = -count;
        int n = static_cast<int>(static_cast<uint64_t>(count) % 64);
        if (n == 0) {
            ctx.data_stack().push(Value(static_cast<int64_t>(val)));
        } else {
            ctx.data_stack().push(Value(static_cast<int64_t>(
                (val >> n) | (val << (64 - n)))));
        }
    } else {
        int n = static_cast<int>(static_cast<uint64_t>(count) % 64);
        if (n == 0) {
            ctx.data_stack().push(Value(static_cast<int64_t>(val)));
        } else {
            ctx.data_stack().push(Value(static_cast<int64_t>(
                (val << n) | (val >> (64 - n)))));
        }
    }
    return true;
}

bool prim_rroll(ExecutionContext& ctx) {
    auto opt_count = ctx.data_stack().pop();
    if (!opt_count) return false;
    auto opt_val = ctx.data_stack().pop();
    if (!opt_val) { ctx.data_stack().push(*opt_count); return false; }
    int64_t count = opt_count->as_int;
    uint64_t val = static_cast<uint64_t>(opt_val->as_int);
    // Negative count → rotate in opposite direction
    if (count < 0) {
        count = -count;
        int n = static_cast<int>(static_cast<uint64_t>(count) % 64);
        if (n == 0) {
            ctx.data_stack().push(Value(static_cast<int64_t>(val)));
        } else {
            ctx.data_stack().push(Value(static_cast<int64_t>(
                (val << n) | (val >> (64 - n)))));
        }
    } else {
        int n = static_cast<int>(static_cast<uint64_t>(count) % 64);
        if (n == 0) {
            ctx.data_stack().push(Value(static_cast<int64_t>(val)));
        } else {
            ctx.data_stack().push(Value(static_cast<int64_t>(
                (val >> n) | (val << (64 - n)))));
        }
    }
    return true;
}

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
        ctx.out() << "<string> ";
    } else if (v.type == Value::Type::Array) {
        ctx.out() << "<array> ";
    } else if (v.type == Value::Type::ByteArray) {
        ctx.out() << "<bytes> ";
    } else if (v.type == Value::Type::Map) {
        ctx.out() << "<map> ";
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

bool prim_sqrt(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::sqrt(v)));
    return true;
}

bool prim_sin(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::sin(v)));
    return true;
}

bool prim_cos(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::cos(v)));
    return true;
}

bool prim_tan(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::tan(v)));
    return true;
}

bool prim_asin(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::asin(v)));
    return true;
}

bool prim_acos(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::acos(v)));
    return true;
}

bool prim_atan(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::atan(v)));
    return true;
}

bool prim_atan2(ExecutionContext& ctx) {
    double y, x;
    if (!pop_two_as_double(ctx, y, x)) return false;
    ctx.data_stack().push(Value(std::atan2(y, x)));
    return true;
}

bool prim_log(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::log(v)));
    return true;
}

bool prim_log2(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::log2(v)));
    return true;
}

bool prim_log10(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::log10(v)));
    return true;
}

bool prim_exp(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::exp(v)));
    return true;
}

bool prim_pow(ExecutionContext& ctx) {
    double base, exponent;
    if (!pop_two_as_double(ctx, base, exponent)) return false;
    ctx.data_stack().push(Value(std::pow(base, exponent)));
    return true;
}

bool prim_ceil(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::ceil(v)));
    return true;
}

bool prim_floor(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::floor(v)));
    return true;
}

bool prim_round(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::round(v)));
    return true;
}

bool prim_trunc(ExecutionContext& ctx) {
    double v; if (!pop_as_double(ctx, v)) return false;
    ctx.data_stack().push(Value(std::trunc(v)));
    return true;
}

bool prim_fmin(ExecutionContext& ctx) {
    double a, b;
    if (!pop_two_as_double(ctx, a, b)) return false;
    ctx.data_stack().push(Value(std::fmin(a, b)));
    return true;
}

bool prim_fmax(ExecutionContext& ctx) {
    double a, b;
    if (!pop_two_as_double(ctx, a, b)) return false;
    ctx.data_stack().push(Value(std::fmax(a, b)));
    return true;
}

bool prim_pi(ExecutionContext& ctx) {
    ctx.data_stack().push(Value(3.14159265358979323846));
    return true;
}

bool prim_tanh(ExecutionContext& ctx) {
    double x;
    if (!pop_as_double(ctx, x)) return false;
    ctx.data_stack().push(Value(std::tanh(x)));
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

    // Resolve relative to home directory (if configured)
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

bool prim_dict_meta_set(ExecutionContext& ctx) {
    // Stack: word-str key-str fmt-str content-str (TOS)
    auto opt_content = ctx.data_stack().pop();
    if (!opt_content) return false;
    auto opt_fmt = ctx.data_stack().pop();
    if (!opt_fmt) { ctx.data_stack().push(*opt_content); return false; }
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) { ctx.data_stack().push(*opt_fmt); ctx.data_stack().push(*opt_content); return false; }
    auto opt_word = ctx.data_stack().pop();
    if (!opt_word) { ctx.data_stack().push(*opt_key); ctx.data_stack().push(*opt_fmt); ctx.data_stack().push(*opt_content); return false; }

    // Validate all are strings
    if (opt_word->type != Value::Type::String || !opt_word->as_ptr ||
        opt_key->type != Value::Type::String || !opt_key->as_ptr ||
        opt_fmt->type != Value::Type::String || !opt_fmt->as_ptr ||
        opt_content->type != Value::Type::String || !opt_content->as_ptr) {
        opt_word->release();
        opt_key->release();
        opt_fmt->release();
        opt_content->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string word_name(opt_word->as_string()->view());
    std::string key(opt_key->as_string()->view());
    std::string fmt_str(opt_fmt->as_string()->view());
    std::string content(opt_content->as_string()->view());
    opt_word->release();
    opt_key->release();
    opt_fmt->release();
    opt_content->release();

    auto fmt = parse_metadata_format(fmt_str);
    if (!fmt) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* dict = ctx.dictionary();
    if (!dict || !dict->set_concept_metadata(word_name, key, *fmt, std::move(content))) {
        ctx.data_stack().push(Value(false));
    } else {
        ctx.data_stack().push(Value(true));
    }
    return true;
}

bool prim_dict_meta_get(ExecutionContext& ctx) {
    // Stack: word-str key-str (TOS)
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
    auto entry = dict->get_concept_metadata(word_name, key);
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
    // Stack: word-str key-str fmt-str content-str (TOS)
    auto opt_content = ctx.data_stack().pop();
    if (!opt_content) return false;
    auto opt_fmt = ctx.data_stack().pop();
    if (!opt_fmt) { ctx.data_stack().push(*opt_content); return false; }
    auto opt_key = ctx.data_stack().pop();
    if (!opt_key) { ctx.data_stack().push(*opt_fmt); ctx.data_stack().push(*opt_content); return false; }
    auto opt_word = ctx.data_stack().pop();
    if (!opt_word) { ctx.data_stack().push(*opt_key); ctx.data_stack().push(*opt_fmt); ctx.data_stack().push(*opt_content); return false; }

    if (opt_word->type != Value::Type::String || !opt_word->as_ptr ||
        opt_key->type != Value::Type::String || !opt_key->as_ptr ||
        opt_fmt->type != Value::Type::String || !opt_fmt->as_ptr ||
        opt_content->type != Value::Type::String || !opt_content->as_ptr) {
        opt_word->release();
        opt_key->release();
        opt_fmt->release();
        opt_content->release();
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string word_name(opt_word->as_string()->view());
    std::string key(opt_key->as_string()->view());
    std::string fmt_str(opt_fmt->as_string()->view());
    std::string content(opt_content->as_string()->view());
    opt_word->release();
    opt_key->release();
    opt_fmt->release();
    opt_content->release();

    auto fmt = parse_metadata_format(fmt_str);
    if (!fmt) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* dict = ctx.dictionary();
    auto impl = dict->lookup(word_name);
    if (!impl) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    (*impl)->metadata().set(key, *fmt, std::move(content));
    ctx.data_stack().push(Value(true));
    return true;
}

bool prim_impl_meta_get(ExecutionContext& ctx) {
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
    auto impl = dict->lookup(word_name);
    if (!impl) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    auto entry = (*impl)->metadata().get(key);
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
                uint8_t byte;
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

bool prim_us_to_jd(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) {
        ctx.data_stack().push(*opt);
        return false;
    }
    double jd = (static_cast<double>(opt->as_int) / US_PER_DAY) + JD_UNIX_EPOCH;
    ctx.data_stack().push(Value(jd));
    return true;
}

bool prim_jd_to_us(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    double jd;
    if (opt->type == Value::Type::Float) {
        jd = opt->as_float;
    } else if (opt->type == Value::Type::Integer) {
        jd = static_cast<double>(opt->as_int);
    } else {
        ctx.data_stack().push(*opt);
        return false;
    }
    auto us = static_cast<int64_t>((jd - JD_UNIX_EPOCH) * US_PER_DAY);
    ctx.data_stack().push(Value(us));
    return true;
}

bool prim_us_to_mjd(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) {
        ctx.data_stack().push(*opt);
        return false;
    }
    double mjd = (static_cast<double>(opt->as_int) / US_PER_DAY) + MJD_UNIX_EPOCH;
    ctx.data_stack().push(Value(mjd));
    return true;
}

bool prim_mjd_to_us(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    double mjd;
    if (opt->type == Value::Type::Float) {
        mjd = opt->as_float;
    } else if (opt->type == Value::Type::Integer) {
        mjd = static_cast<double>(opt->as_int);
    } else {
        ctx.data_stack().push(*opt);
        return false;
    }
    auto us = static_cast<int64_t>((mjd - MJD_UNIX_EPOCH) * US_PER_DAY);
    ctx.data_stack().push(Value(us));
    return true;
}

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

    std::this_thread::sleep_for(sleep_dur);
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
            ctx.out() << "<string>";
        } else if (v.type == Value::Type::Array) {
            ctx.out() << "<array>";
        } else if (v.type == Value::Type::ByteArray) {
            ctx.out() << "<bytes>";
        } else if (v.type == Value::Type::Map) {
            ctx.out() << "<map>";
        } else if (v.type == Value::Type::Json) {
            ctx.out() << "<json>";
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

void register_primitives(Dictionary& dict) {
    using TS = TypeSignature;
    using T = TS::Type;

    auto make_word = [](const char* name, WordImpl::FunctionPtr fn,
                        std::vector<T> inputs, std::vector<T> outputs) {
        return make_primitive(name, fn, std::move(inputs), std::move(outputs));
    };

    dict.register_word("+", make_word("prim_add", prim_add,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("-", make_word("prim_sub", prim_sub,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("*", make_word("prim_mul", prim_mul,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("/", make_word("prim_div", prim_div,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("mod", make_word("prim_mod", prim_mod,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("/mod", make_word("prim_divmod", prim_divmod,
        {T::Unknown, T::Unknown}, {T::Unknown, T::Unknown}));

    dict.register_word("negate", make_word("prim_negate", prim_negate,
        {T::Unknown}, {T::Unknown}));

    dict.register_word("abs", make_word("prim_abs", prim_abs,
        {T::Unknown}, {T::Unknown}));

    dict.register_word("max", make_word("prim_max", prim_max,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("min", make_word("prim_min", prim_min,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("dup", make_word("prim_dup", prim_dup,
        {T::Unknown}, {T::Unknown, T::Unknown}));

    dict.register_word("drop", make_word("prim_drop", prim_drop,
        {T::Unknown}, {}));

    dict.register_word("swap", make_word("prim_swap", prim_swap,
        {T::Unknown, T::Unknown}, {T::Unknown, T::Unknown}));

    dict.register_word("over", make_word("prim_over", prim_over,
        {T::Unknown, T::Unknown}, {T::Unknown, T::Unknown, T::Unknown}));

    dict.register_word("rot", make_word("prim_rot", prim_rot,
        {T::Unknown, T::Unknown, T::Unknown}, {T::Unknown, T::Unknown, T::Unknown}));

    dict.register_word("pick", make_word("prim_pick", prim_pick,
        {T::Integer}, {}));

    dict.register_word("nip", make_word("prim_nip", prim_nip,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("tuck", make_word("prim_tuck", prim_tuck,
        {T::Unknown, T::Unknown}, {T::Unknown, T::Unknown, T::Unknown}));

    dict.register_word("depth", make_word("prim_depth", prim_depth,
        {}, {T::Integer}));

    dict.register_word("?dup", make_word("prim_qdup", prim_qdup,
        {T::Unknown}, {}));

    dict.register_word("roll", make_word("prim_roll", prim_roll,
        {T::Integer}, {}));

    // Comparison
    dict.register_word("=", make_word("prim_eq", prim_eq,
        {T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word("<>", make_word("prim_neq", prim_neq,
        {T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word("<", make_word("prim_lt", prim_lt,
        {T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word(">", make_word("prim_gt", prim_gt,
        {T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word("<=", make_word("prim_le", prim_le,
        {T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word(">=", make_word("prim_ge", prim_ge,
        {T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word("0=", make_word("prim_zero_eq", prim_zero_eq,
        {T::Unknown}, {T::Integer}));

    dict.register_word("0<", make_word("prim_zero_lt", prim_zero_lt,
        {T::Unknown}, {T::Integer}));

    dict.register_word("0>", make_word("prim_zero_gt", prim_zero_gt,
        {T::Unknown}, {T::Integer}));

    // Boolean
    dict.register_word("true", make_word("prim_true", prim_true,
        {}, {T::Unknown}));

    dict.register_word("false", make_word("prim_false", prim_false,
        {}, {T::Unknown}));

    dict.register_word("not", make_word("prim_not", prim_not,
        {T::Unknown}, {T::Unknown}));

    dict.register_word("bool", make_word("prim_to_bool", prim_to_bool,
        {T::Unknown}, {T::Unknown}));

    // Logic
    dict.register_word("and", make_word("prim_and", prim_and,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("or", make_word("prim_or", prim_or,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("xor", make_word("prim_xor", prim_xor,
        {T::Unknown, T::Unknown}, {T::Unknown}));

    dict.register_word("invert", make_word("prim_invert", prim_invert,
        {T::Unknown}, {T::Unknown}));

    dict.register_word("lshift", make_word("prim_lshift", prim_lshift,
        {T::Integer, T::Integer}, {T::Integer}));

    dict.register_word("rshift", make_word("prim_rshift", prim_rshift,
        {T::Integer, T::Integer}, {T::Integer}));

    dict.register_word("lroll", make_word("prim_lroll", prim_lroll,
        {T::Integer, T::Integer}, {T::Integer}));

    dict.register_word("rroll", make_word("prim_rroll", prim_rroll,
        {T::Integer, T::Integer}, {T::Integer}));

    // I/O
    dict.register_word(".", make_word("prim_dot", prim_dot,
        {T::Unknown}, {}));

    dict.register_word("cr", make_word("prim_cr", prim_cr,
        {}, {}));

    dict.register_word("emit", make_word("prim_emit", prim_emit,
        {T::Integer}, {}));

    dict.register_word("space", make_word("prim_space", prim_space,
        {}, {}));

    dict.register_word("spaces", make_word("prim_spaces", prim_spaces,
        {T::Integer}, {}));

    dict.register_word("words", make_word("prim_words", prim_words,
        {}, {}));

    // Memory
    dict.register_word("create", make_word("prim_create", prim_create,
        {}, {}));

    dict.register_word(",", make_word("prim_comma", prim_comma,
        {T::Unknown}, {}));

    dict.register_word("@", make_word("prim_fetch", prim_fetch,
        {T::Unknown}, {T::Unknown}));

    dict.register_word("!", make_word("prim_store", prim_store,
        {T::Unknown, T::Unknown}, {}));

    dict.register_word("allot", make_word("prim_allot", prim_allot,
        {T::Integer}, {}));

    // Math (unary — all return Float)
    dict.register_word("sqrt", make_word("prim_sqrt", prim_sqrt,
        {T::Unknown}, {T::Float}));

    dict.register_word("sin", make_word("prim_sin", prim_sin,
        {T::Unknown}, {T::Float}));

    dict.register_word("cos", make_word("prim_cos", prim_cos,
        {T::Unknown}, {T::Float}));

    dict.register_word("tan", make_word("prim_tan", prim_tan,
        {T::Unknown}, {T::Float}));

    dict.register_word("asin", make_word("prim_asin", prim_asin,
        {T::Unknown}, {T::Float}));

    dict.register_word("acos", make_word("prim_acos", prim_acos,
        {T::Unknown}, {T::Float}));

    dict.register_word("atan", make_word("prim_atan", prim_atan,
        {T::Unknown}, {T::Float}));

    dict.register_word("atan2", make_word("prim_atan2", prim_atan2,
        {T::Unknown, T::Unknown}, {T::Float}));

    dict.register_word("log", make_word("prim_log", prim_log,
        {T::Unknown}, {T::Float}));

    dict.register_word("log2", make_word("prim_log2", prim_log2,
        {T::Unknown}, {T::Float}));

    dict.register_word("log10", make_word("prim_log10", prim_log10,
        {T::Unknown}, {T::Float}));

    dict.register_word("exp", make_word("prim_exp", prim_exp,
        {T::Unknown}, {T::Float}));

    dict.register_word("pow", make_word("prim_pow", prim_pow,
        {T::Unknown, T::Unknown}, {T::Float}));

    dict.register_word("ceil", make_word("prim_ceil", prim_ceil,
        {T::Unknown}, {T::Float}));

    dict.register_word("floor", make_word("prim_floor", prim_floor,
        {T::Unknown}, {T::Float}));

    dict.register_word("round", make_word("prim_round", prim_round,
        {T::Unknown}, {T::Float}));

    dict.register_word("trunc", make_word("prim_trunc", prim_trunc,
        {T::Unknown}, {T::Float}));

    dict.register_word("fmin", make_word("prim_fmin", prim_fmin,
        {T::Unknown, T::Unknown}, {T::Float}));

    dict.register_word("fmax", make_word("prim_fmax", prim_fmax,
        {T::Unknown, T::Unknown}, {T::Float}));

    dict.register_word("pi", make_word("prim_pi", prim_pi,
        {}, {T::Float}));

    dict.register_word("tanh", make_word("prim_tanh", prim_tanh,
        {T::Unknown}, {T::Float}));

    dict.register_word("f~", make_word("prim_fapprox", prim_fapprox,
        {T::Unknown, T::Unknown, T::Unknown}, {T::Integer}));

    // PRNG
    dict.register_word("random", make_word("prim_random", prim_random,
        {}, {T::Float}));

    dict.register_word("random-seed", make_word("prim_random_seed", prim_random_seed,
        {T::Integer}, {}));

    dict.register_word("random-range", make_word("prim_random_range", prim_random_range,
        {T::Integer, T::Integer}, {T::Integer}));

    // System
    dict.register_word("sys-semver", make_word("prim_sys_semver", prim_sys_semver,
        {}, {}));

    dict.register_word("sys-timestamp", make_word("prim_sys_timestamp", prim_sys_timestamp,
        {}, {}));

    dict.register_word("sys-datafields", make_word("prim_sys_datafields", prim_sys_datafields,
        {}, {}));

    dict.register_word("sys-notification", make_word("prim_sys_notification", prim_sys_notification,
        {T::Unknown}, {}));

    dict.register_word("user-notification", make_word("prim_user_notification", prim_user_notification,
        {T::String, T::String}, {T::Integer}));

    dict.register_word("abort", make_word("prim_abort", prim_abort,
        {T::Unknown}, {}));

    // Time
    dict.register_word("time-us", make_word("prim_time_us", prim_time_us,
        {}, {T::Integer}));

    dict.register_word("us->iso", make_word("prim_us_to_iso", prim_us_to_iso,
        {T::Integer}, {T::Unknown}));

    dict.register_word("us->iso-us", make_word("prim_us_to_iso_us", prim_us_to_iso_us,
        {T::Integer}, {T::Unknown}));

    dict.register_word("us->jd", make_word("prim_us_to_jd", prim_us_to_jd,
        {T::Integer}, {T::Float}));

    dict.register_word("jd->us", make_word("prim_jd_to_us", prim_jd_to_us,
        {T::Unknown}, {T::Integer}));

    dict.register_word("us->mjd", make_word("prim_us_to_mjd", prim_us_to_mjd,
        {T::Integer}, {T::Float}));

    dict.register_word("mjd->us", make_word("prim_mjd_to_us", prim_mjd_to_us,
        {T::Unknown}, {T::Integer}));

    dict.register_word("sleep", make_word("prim_sleep", prim_sleep,
        {T::Integer}, {}));

    // Input-reading
    dict.register_word("word-read", make_word("prim_word_read", prim_word_read,
        {}, {T::Unknown, T::Integer}));

    dict.register_word("string-read-delim", make_word("prim_string_read_delim", prim_string_read_delim,
        {T::Integer}, {T::Unknown, T::Integer}));

    // Dictionary operations
    dict.register_word("dict-forget", make_word("prim_dict_forget", prim_dict_forget,
        {T::Unknown}, {T::Integer}));

    dict.register_word("dict-forget-all", make_word("prim_dict_forget_all", prim_dict_forget_all,
        {T::Unknown}, {T::Integer}));

    dict.register_word("file-load", make_word("prim_file_load", prim_file_load,
        {T::Unknown}, {T::Integer}));

    dict.register_word("include", make_word("prim_include", prim_include,
        {}, {}));

    dict.register_word("library", make_word("prim_library", prim_library,
        {}, {}));

    dict.register_word("evaluate", make_word("prim_evaluate", prim_evaluate,
        {T::String}, {}));

    dict.register_word("marker", make_word("prim_marker", prim_marker,
        {}, {}));

    dict.register_word("marker-restore", make_word("prim_marker_restore", prim_marker_restore,
        {T::String}, {}));

    // Metadata (stack-based)
    dict.register_word("dict-meta-set", make_word("prim_dict_meta_set", prim_dict_meta_set,
        {T::Unknown, T::Unknown, T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word("dict-meta-get", make_word("prim_dict_meta_get", prim_dict_meta_get,
        {T::Unknown, T::Unknown}, {T::Unknown, T::Integer}));

    dict.register_word("dict-meta-del", make_word("prim_dict_meta_del", prim_dict_meta_del,
        {T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word("dict-meta-keys", make_word("prim_dict_meta_keys", prim_dict_meta_keys,
        {T::Unknown}, {T::Unknown, T::Integer}));

    dict.register_word("impl-meta-set", make_word("prim_impl_meta_set", prim_impl_meta_set,
        {T::Unknown, T::Unknown, T::Unknown, T::Unknown}, {T::Integer}));

    dict.register_word("impl-meta-get", make_word("prim_impl_meta_get", prim_impl_meta_get,
        {T::Unknown, T::Unknown}, {T::Unknown, T::Integer}));

    // Help
    dict.register_word("help", make_word("prim_help", prim_help,
        {}, {}));

    // Debug
    dict.register_word("dump", make_word("prim_dump", prim_dump,
        {T::Unknown}, {T::Unknown}));

    dict.register_word("see", make_word("prim_see", prim_see,
        {}, {}));

    // Execution tokens
    dict.register_word("'", make_word("prim_tick", prim_tick,
        {}, {T::Unknown}));

    dict.register_word("execute", make_word("prim_execute", prim_execute,
        {T::Unknown}, {}));

    dict.register_word("xt?", make_word("prim_xt_query", prim_xt_query,
        {T::Unknown}, {T::Integer}));

    dict.register_word(">name", make_word("prim_xt_to_name", prim_xt_to_name,
        {T::Unknown}, {T::String}));

    // Defining words
    dict.register_word("immediate", make_word("prim_immediate", prim_immediate,
        {}, {}));

    dict.register_word("xt-body", make_word("prim_xt_body", prim_xt_body,
        {T::Unknown}, {T::Unknown}));

    // Stack display
    dict.register_word(".s", make_word("prim_dot_s", prim_dot_s,
        {}, {}));

    // Type conversion
    dict.register_word("int->float", make_word("prim_int_to_float", prim_int_to_float,
        {T::Integer}, {T::Float}));

    dict.register_word("float->int", make_word("prim_float_to_int", prim_float_to_int,
        {T::Float}, {T::Integer}));

    dict.register_word("number->string", make_word("prim_number_to_string", prim_number_to_string,
        {T::Unknown}, {T::String}));

    dict.register_word("string->number", make_word("prim_string_to_number", prim_string_to_number,
        {T::String}, {}));

    // Register heap object primitives (string, array, byte, map)
    register_string_primitives(dict);
    register_array_primitives(dict);
    register_byte_primitives(dict);
    register_map_primitives(dict);
    register_json_primitives(dict);

#ifdef ETIL_LINALG_ENABLED
    // Register matrix primitives (mat-new, mat*, mat-solve, etc.)
    register_matrix_primitives(dict);
#endif

    // Register LVFS primitives (cwd, cd, ls, ll, lr, cat)
    etil::lvfs::register_lvfs_primitives(dict);

    // Register file I/O primitives (exists-sync, read-file-sync, etc.)
    etil::fileio::register_file_io_primitives(dict);

    // Register async file I/O primitives (exists?, read-file, write-file, etc.)
    etil::fileio::register_async_file_io_primitives(dict);
}

} // namespace etil::core
