// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/heap_primitives.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_matrix.hpp"
#include "etil/core/heap_observable.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/mcp/role_permissions.hpp"

#include "etil/core/heap_byte_array.hpp"
#include "etil/core/heap_map.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace etil::core {

namespace {

// (FORTH_TRUE/FORTH_FALSE removed — Boolean type used instead)

/// A string value with its taint bit preserved.
struct TaintedString {
    std::string str;
    bool tainted{false};
};

/// Pop a value and convert to TaintedString.
/// Only Value::Type::String can be tainted; all coerced types are clean.
std::optional<TaintedString> pop_as_tainted_string(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return std::nullopt;
    switch (opt->type) {
    case Value::Type::String: {
        if (!opt->as_ptr) return std::nullopt;
        auto* hs = opt->as_string();
        TaintedString result{std::string(hs->view()), hs->is_tainted()};
        hs->release();
        return result;
    }
    case Value::Type::Integer:
        return TaintedString{std::to_string(opt->as_int), false};
    case Value::Type::Float: {
        std::ostringstream oss;
        oss << opt->as_float;
        return TaintedString{oss.str(), false};
    }
    case Value::Type::Boolean:
        return TaintedString{opt->as_bool() ? "true" : "false", false};
    case Value::Type::Array: {
        auto* arr = opt->as_array();
        auto len = arr ? arr->length() : 0;
        if (arr) arr->release();
        return TaintedString{"array(" + std::to_string(len) + ")", false};
    }
    case Value::Type::ByteArray: {
        auto* ba = opt->as_byte_array();
        auto len = ba ? ba->length() : 0;
        if (ba) ba->release();
        return TaintedString{"bytearray(" + std::to_string(len) + ")", false};
    }
    case Value::Type::Map: {
        auto* m = opt->as_map();
        auto len = m ? m->size() : 0;
        if (m) m->release();
        return TaintedString{"map(" + std::to_string(len) + ")", false};
    }
    case Value::Type::Matrix: {
        auto* mat = opt->as_matrix();
        auto r = mat ? mat->rows() : 0;
        auto c = mat ? mat->cols() : 0;
        if (mat) mat->release();
        return TaintedString{"matrix(" + std::to_string(r) + "x" + std::to_string(c) + ")", false};
    }
    case Value::Type::Json: {
        if (opt->as_ptr) {
            auto* hj = opt->as_json();
            std::string r = hj->dump();
            hj->release();
            return TaintedString{std::move(r), false};
        }
        return TaintedString{"null", false};
    }
    case Value::Type::Observable: {
        if (opt->as_ptr) {
            auto* obs = opt->as_observable();
            std::string r = "observable(" + std::string(obs->kind_name()) + ")";
            obs->release();
            return TaintedString{std::move(r), false};
        }
        return TaintedString{"observable", false};
    }
    case Value::Type::Xt: {
        if (opt->as_ptr) {
            auto* impl = opt->as_xt_impl();
            std::string r = "xt(" + impl->name() + ")";
            impl->release();
            return TaintedString{std::move(r), false};
        }
        return TaintedString{"xt(?)", false};
    }
    case Value::Type::DataRef:
        return TaintedString{"dataref", false};
    }
    return std::nullopt;
}

} // anonymous namespace

// type ( str -- ) — print string, release ref
bool prim_type(ExecutionContext& ctx) {
    auto* s = pop_string(ctx);
    if (!s) return false;
    ctx.out() << s->view();
    s->release();
    return true;
}

// s+ ( v1 v2 -- s3 ) — concatenate, auto-converting non-strings
bool prim_splus(ExecutionContext& ctx) {
    auto s2 = pop_as_tainted_string(ctx);
    if (!s2) return false;
    auto s1 = pop_as_tainted_string(ctx);
    if (!s1) {
        auto* hs = HeapString::create(s2->str);
        if (s2->tainted) hs->set_tainted(true);
        ctx.data_stack().push(Value::from(hs));
        return false;
    }
    std::string result;
    result.reserve(s1->str.size() + s2->str.size());
    result.append(s1->str);
    result.append(s2->str);
    auto* hs = HeapString::create(result);
    if (s1->tainted || s2->tainted) hs->set_tainted(true);
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// s= ( s1 s2 -- flag ) — content equality, FORTH flag (-1/0)
bool prim_seq(ExecutionContext& ctx) {
    auto* s2 = pop_string(ctx);
    if (!s2) return false;
    auto* s1 = pop_string(ctx);
    if (!s1) {
        ctx.data_stack().push(Value::from(s2));
        return false;
    }
    bool eq = (s1->view() == s2->view());
    s1->release();
    s2->release();
    ctx.data_stack().push(Value(eq));
    return true;
}

// s<> ( s1 s2 -- flag ) — content inequality
bool prim_sneq(ExecutionContext& ctx) {
    auto* s2 = pop_string(ctx);
    if (!s2) return false;
    auto* s1 = pop_string(ctx);
    if (!s1) {
        ctx.data_stack().push(Value::from(s2));
        return false;
    }
    bool neq = (s1->view() != s2->view());
    s1->release();
    s2->release();
    ctx.data_stack().push(Value(neq));
    return true;
}

// slength ( str -- n ) — string length in bytes, releases str
bool prim_slength(ExecutionContext& ctx) {
    auto* s = pop_string(ctx);
    if (!s) return false;
    int64_t len = static_cast<int64_t>(s->length());
    s->release();
    ctx.data_stack().push(Value(len));
    return true;
}

// substr ( str start len -- str2 ) — extract substring
bool prim_substr(ExecutionContext& ctx) {
    auto opt_len = ctx.data_stack().pop();
    if (!opt_len) return false;
    auto opt_start = ctx.data_stack().pop();
    if (!opt_start) {
        ctx.data_stack().push(*opt_len);
        return false;
    }
    auto* s = pop_string(ctx);
    if (!s) {
        ctx.data_stack().push(*opt_start);
        ctx.data_stack().push(*opt_len);
        return false;
    }
    int64_t start = opt_start->as_int;
    int64_t len = opt_len->as_int;
    auto sv = s->view();
    bool tainted = s->is_tainted();
    // Clamp to valid range
    if (start < 0) start = 0;
    if (static_cast<size_t>(start) > sv.size()) start = static_cast<int64_t>(sv.size());
    if (len < 0) len = 0;
    if (static_cast<size_t>(start + len) > sv.size()) {
        len = static_cast<int64_t>(sv.size()) - start;
    }
    auto* result = HeapString::create(sv.substr(static_cast<size_t>(start),
                                                 static_cast<size_t>(len)));
    if (tainted) result->set_tainted(true);
    s->release();
    ctx.data_stack().push(Value::from(result));
    return true;
}

// strim ( str -- str2 ) — trim leading/trailing whitespace
bool prim_strim(ExecutionContext& ctx) {
    auto* s = pop_string(ctx);
    if (!s) return false;
    auto sv = s->view();
    bool tainted = s->is_tainted();
    size_t start = sv.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string_view::npos) {
        s->release();
        auto* empty = HeapString::create("");
        if (tainted) empty->set_tainted(true);
        ctx.data_stack().push(Value::from(empty));
        return true;
    }
    size_t end = sv.find_last_not_of(" \t\n\r\f\v");
    auto* result = HeapString::create(sv.substr(start, end - start + 1));
    if (tainted) result->set_tainted(true);
    s->release();
    ctx.data_stack().push(Value::from(result));
    return true;
}

// sfind ( haystack needle -- index ) — find first occurrence, -1 if not found
bool prim_sfind(ExecutionContext& ctx) {
    auto* needle = pop_string(ctx);
    if (!needle) return false;
    auto* haystack = pop_string(ctx);
    if (!haystack) {
        ctx.data_stack().push(Value::from(needle));
        return false;
    }
    auto pos = haystack->view().find(needle->view());
    int64_t result = (pos == std::string_view::npos) ? -1
                     : static_cast<int64_t>(pos);
    haystack->release();
    needle->release();
    ctx.data_stack().push(Value(result));
    return true;
}

// sreplace ( str old new -- str2 ) — replace all occurrences
bool prim_sreplace(ExecutionContext& ctx) {
    auto* repl = pop_string(ctx);
    if (!repl) return false;
    auto* old_s = pop_string(ctx);
    if (!old_s) {
        ctx.data_stack().push(Value::from(repl));
        return false;
    }
    auto* src = pop_string(ctx);
    if (!src) {
        ctx.data_stack().push(Value::from(old_s));
        ctx.data_stack().push(Value::from(repl));
        return false;
    }
    bool tainted = src->is_tainted() || repl->is_tainted();
    std::string result(src->view());
    auto old_sv = old_s->view();
    auto repl_sv = repl->view();
    if (!old_sv.empty()) {
        size_t pos = 0;
        while ((pos = result.find(old_sv, pos)) != std::string::npos) {
            result.replace(pos, old_sv.size(), repl_sv);
            pos += repl_sv.size();
        }
    }
    src->release();
    old_s->release();
    repl->release();
    auto* hs = HeapString::create(result);
    if (tainted) hs->set_tainted(true);
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// ssplit ( str delim -- array ) — split into HeapArray of HeapStrings
bool prim_ssplit(ExecutionContext& ctx);  // Forward declaration — needs HeapArray

// sjoin ( array delim -- str ) — join array of strings with delimiter
bool prim_sjoin(ExecutionContext& ctx);  // Forward declaration — needs HeapArray

// sregex-find ( str pattern -- index ) — regex search, -1 if no match
bool prim_sregex_find(ExecutionContext& ctx) {
    auto* pattern = pop_string(ctx);
    if (!pattern) return false;
    auto* str = pop_string(ctx);
    if (!str) {
        ctx.data_stack().push(Value::from(pattern));
        return false;
    }
    int64_t result = -1;
    try {
        std::regex re(pattern->c_str(), pattern->length());
        std::cmatch match;
        if (std::regex_search(str->c_str(), match, re)) {
            result = static_cast<int64_t>(match.position(0));
        }
    } catch (const std::regex_error&) {
        // Invalid pattern — return -1
    }
    str->release();
    pattern->release();
    ctx.data_stack().push(Value(result));
    return true;
}

// sregex-replace ( str pattern repl -- str2 ) — regex replace all
bool prim_sregex_replace(ExecutionContext& ctx) {
    auto* repl = pop_string(ctx);
    if (!repl) return false;
    auto* pattern = pop_string(ctx);
    if (!pattern) {
        ctx.data_stack().push(Value::from(repl));
        return false;
    }
    auto* str = pop_string(ctx);
    if (!str) {
        ctx.data_stack().push(Value::from(pattern));
        ctx.data_stack().push(Value::from(repl));
        return false;
    }
    bool str_was_tainted = str->is_tainted();
    std::string result(str->view());
    try {
        std::regex re(pattern->c_str(), pattern->length());
        result = std::regex_replace(result, re, std::string(repl->view()));
    } catch (const std::regex_error&) {
        // Invalid pattern — return original string
    }
    str->release();
    pattern->release();
    repl->release();
    auto* result_hs = HeapString::create(result);
    if (str_was_tainted) {
        // Untaint only if the role has evaluate_tainted permission.
        // nullptr perms = standalone mode = untaint (backward compat).
        auto* perms = ctx.permissions();
        bool can_untaint = !perms || perms->evaluate_tainted;
        result_hs->set_tainted(!can_untaint);
    }
    ctx.data_stack().push(Value::from(result_hs));
    return true;
}

// --- sregex-search / sregex-match ---

// Shared helper: regex search or full match, returning capture groups as HeapArray.
// full_match=false → std::regex_search (find first match anywhere)
// full_match=true  → std::regex_match  (entire string must match)
static bool regex_match_common(ExecutionContext& ctx, bool full_match) {
    auto* pattern = pop_string(ctx);
    if (!pattern) return false;
    auto* str = pop_string(ctx);
    if (!str) {
        ctx.data_stack().push(Value::from(pattern));
        return false;
    }
    bool tainted = str->is_tainted();
    HeapArray* arr = nullptr;
    try {
        std::string s(str->view());
        std::smatch match;
        std::regex re(pattern->c_str(), pattern->length());
        bool matched = full_match
            ? std::regex_match(s, match, re)
            : std::regex_search(s, match, re);
        if (matched) {
            arr = new HeapArray();
            for (size_t i = 0; i < match.size(); ++i) {
                auto* elem = HeapString::create(match[i].str());
                if (tainted) elem->set_tainted(true);
                arr->push_back(Value::from(elem));
            }
        }
    } catch (const std::regex_error&) {
        // Invalid pattern — treat as no match
    }
    str->release();
    pattern->release();
    if (arr) {
        ctx.data_stack().push(Value::from(arr));
        ctx.data_stack().push(Value(true));
    } else {
        ctx.data_stack().push(Value(false));
    }
    return true;
}

// sregex-search ( str pattern -- match-array -1 | 0 )
bool prim_sregex_search(ExecutionContext& ctx) {
    return regex_match_common(ctx, false);
}

// sregex-match ( str pattern -- match-array -1 | 0 )
bool prim_sregex_match(ExecutionContext& ctx) {
    return regex_match_common(ctx, true);
}

// --- sprintf helpers ---

enum class ArgType { Int, Float, Str, Char, None };

struct FormatSpec {
    size_t pos;     // start position in format string
    size_t len;     // length of the specifier (e.g., "%-10.2f" = 7)
    ArgType arg;    // type of argument to consume
};

// Parse format string into a list of format specifiers.
// Returns empty on encountering %n (security rejection).
std::vector<FormatSpec> parse_format_specs(std::string_view fmt) {
    std::vector<FormatSpec> specs;
    size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] != '%') { ++i; continue; }
        size_t start = i;
        ++i;
        if (i >= fmt.size()) break;
        // %% — literal percent, no arg consumed
        if (fmt[i] == '%') { specs.push_back({start, 2, ArgType::None}); ++i; continue; }
        // Skip flags: -, +, space, 0, #
        while (i < fmt.size() && (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' ||
                                   fmt[i] == '0' || fmt[i] == '#')) ++i;
        // Skip width (digits)
        while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') ++i;
        // Skip precision (.digits)
        if (i < fmt.size() && fmt[i] == '.') {
            ++i;
            while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') ++i;
        }
        // Skip length modifiers (h, hh, l, ll, L, z, j, t, q)
        while (i < fmt.size() && (fmt[i] == 'h' || fmt[i] == 'l' || fmt[i] == 'L' ||
                                   fmt[i] == 'z' || fmt[i] == 'j' || fmt[i] == 't' ||
                                   fmt[i] == 'q')) ++i;
        if (i >= fmt.size()) break;
        char conv = fmt[i];
        ++i;
        ArgType at;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
            at = ArgType::Int; break;
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            at = ArgType::Float; break;
        case 's':
            at = ArgType::Str; break;
        case 'c':
            at = ArgType::Char; break;
        case 'n':
            return {}; // Security: reject %n
        default:
            at = ArgType::Str; break;
        }
        specs.push_back({start, i - start, at});
    }
    return specs;
}

// Rebuild a format specifier string with the correct length modifier for int64_t.
// Strips any existing length modifiers and inserts "ll" before the conversion char.
std::string adjust_int_spec(std::string_view spec) {
    // spec is e.g. "%-10d" or "%#08x"
    std::string result;
    result.reserve(spec.size() + 2);
    size_t i = 0;
    // Copy % and flags
    result += spec[i++]; // '%'
    while (i < spec.size() && (spec[i] == '-' || spec[i] == '+' || spec[i] == ' ' ||
                                spec[i] == '0' || spec[i] == '#')) {
        result += spec[i++];
    }
    // Copy width
    while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') result += spec[i++];
    // Copy precision
    if (i < spec.size() && spec[i] == '.') {
        result += spec[i++];
        while (i < spec.size() && spec[i] >= '0' && spec[i] <= '9') result += spec[i++];
    }
    // Skip existing length modifiers
    while (i < spec.size() && (spec[i] == 'h' || spec[i] == 'l' || spec[i] == 'L' ||
                                spec[i] == 'z' || spec[i] == 'j' || spec[i] == 't' ||
                                spec[i] == 'q')) ++i;
    // Insert ll and conversion char
    char conv = (i < spec.size()) ? spec[i] : 'd';
    if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o') {
        result += "ll";
    } else {
        result += "ll";
    }
    result += conv;
    return result;
}

// Format a single specifier with an int64_t value.
std::string safe_sprintf_int(std::string_view spec, int64_t val) {
    auto adjusted = adjust_int_spec(spec);
    int needed = std::snprintf(nullptr, 0, adjusted.c_str(), val);
    if (needed < 0) return "";
    std::string buf(static_cast<size_t>(needed) + 1, '\0');
    std::snprintf(buf.data(), buf.size(), adjusted.c_str(), val);
    buf.resize(static_cast<size_t>(needed));
    return buf;
}

// Format a single specifier with a uint64_t value (for %u, %x, %X, %o).
std::string safe_sprintf_uint(std::string_view spec, uint64_t val) {
    auto adjusted = adjust_int_spec(spec);
    int needed = std::snprintf(nullptr, 0, adjusted.c_str(), val);
    if (needed < 0) return "";
    std::string buf(static_cast<size_t>(needed) + 1, '\0');
    std::snprintf(buf.data(), buf.size(), adjusted.c_str(), val);
    buf.resize(static_cast<size_t>(needed));
    return buf;
}

// Format a single specifier with a double value.
std::string safe_sprintf_float(std::string_view spec, double val) {
    // spec already has the conversion char, no length modifier needed for double
    std::string fmt_str(spec);
    int needed = std::snprintf(nullptr, 0, fmt_str.c_str(), val);
    if (needed < 0) return "";
    std::string buf(static_cast<size_t>(needed) + 1, '\0');
    std::snprintf(buf.data(), buf.size(), fmt_str.c_str(), val);
    buf.resize(static_cast<size_t>(needed));
    return buf;
}

// Format a single specifier with a string value.
std::string safe_sprintf_str(std::string_view spec, const std::string& val) {
    std::string fmt_str(spec);
    int needed = std::snprintf(nullptr, 0, fmt_str.c_str(), val.c_str());
    if (needed < 0) return "";
    std::string buf(static_cast<size_t>(needed) + 1, '\0');
    std::snprintf(buf.data(), buf.size(), fmt_str.c_str(), val.c_str());
    buf.resize(static_cast<size_t>(needed));
    return buf;
}

// Convert a Value to a string representation (for %s).
// Releases any heap refs.  Sets *out_tainted if the value was a tainted string.
std::string value_to_str_taint(const Value& v, bool* out_tainted) {
    if (out_tainted) *out_tainted = false;
    switch (v.type) {
    case Value::Type::String: {
        if (!v.as_ptr) return "";
        auto* hs = v.as_string();
        std::string r(hs->view());
        if (out_tainted) *out_tainted = hs->is_tainted();
        hs->release();
        return r;
    }
    case Value::Type::Integer:
        return std::to_string(v.as_int);
    case Value::Type::Float: {
        std::ostringstream oss;
        oss << v.as_float;
        return oss.str();
    }
    case Value::Type::Boolean:
        return v.as_bool() ? "true" : "false";
    case Value::Type::Array: {
        auto* arr = v.as_array();
        auto len = arr ? arr->length() : 0;
        if (arr) arr->release();
        return "array(" + std::to_string(len) + ")";
    }
    case Value::Type::ByteArray: {
        auto* ba = v.as_byte_array();
        auto len = ba ? ba->length() : 0;
        if (ba) ba->release();
        return "bytearray(" + std::to_string(len) + ")";
    }
    case Value::Type::Map: {
        auto* m = v.as_map();
        auto len = m ? m->size() : 0;
        if (m) m->release();
        return "map(" + std::to_string(len) + ")";
    }
    case Value::Type::Xt: {
        if (v.as_ptr) {
            auto* impl = v.as_xt_impl();
            std::string r = "xt(" + impl->name() + ")";
            impl->release();
            return r;
        }
        return "xt(?)";
    }
    case Value::Type::Json:
        if (v.as_ptr) v.as_json()->release();
        return "json";
    case Value::Type::Matrix:
        if (v.as_ptr) v.as_matrix()->release();
        return "matrix";
    case Value::Type::Observable:
        if (v.as_ptr) v.as_observable()->release();
        return "observable";
    case Value::Type::DataRef:
        return "dataref";
    }
    return "";
}

// Convert a Value to a string representation (for %s).
// Releases any heap refs.
std::string value_to_str(const Value& v) {
    switch (v.type) {
    case Value::Type::String: {
        if (!v.as_ptr) return "";
        auto* hs = v.as_string();
        std::string result(hs->view());
        hs->release();
        return result;
    }
    case Value::Type::Integer:
        return std::to_string(v.as_int);
    case Value::Type::Float: {
        std::ostringstream oss;
        oss << v.as_float;
        return oss.str();
    }
    case Value::Type::Boolean:
        return v.as_bool() ? "true" : "false";
    case Value::Type::Array: {
        auto* arr = v.as_array();
        auto len = arr ? arr->length() : 0;
        if (arr) arr->release();
        return "array(" + std::to_string(len) + ")";
    }
    case Value::Type::ByteArray: {
        auto* ba = v.as_byte_array();
        auto len = ba ? ba->length() : 0;
        if (ba) ba->release();
        return "bytearray(" + std::to_string(len) + ")";
    }
    case Value::Type::Map: {
        auto* m = v.as_map();
        auto len = m ? m->size() : 0;
        if (m) m->release();
        return "map(" + std::to_string(len) + ")";
    }
    case Value::Type::Xt: {
        if (v.as_ptr) {
            auto* impl = v.as_xt_impl();
            std::string result = "xt(" + impl->name() + ")";
            impl->release();
            return result;
        }
        return "xt(?)";
    }
    case Value::Type::Json:
        if (v.as_ptr) v.as_json()->release();
        return "json";
    case Value::Type::Matrix:
        if (v.as_ptr) v.as_matrix()->release();
        return "matrix";
    case Value::Type::Observable:
        if (v.as_ptr) v.as_observable()->release();
        return "observable";
    case Value::Type::DataRef:
        return "dataref";
    }
    return "";
}

// Release a Value's heap ref without consuming it as data.
void release_value(const Value& v) {
    if (v.as_ptr) {
        switch (v.type) {
        case Value::Type::String:   v.as_string()->release(); break;
        case Value::Type::Array:    v.as_array()->release(); break;
        case Value::Type::ByteArray: v.as_byte_array()->release(); break;
        case Value::Type::Map:      v.as_map()->release(); break;
        case Value::Type::Xt:       v.as_xt_impl()->release(); break;
        default: break;
        }
    }
}

// sprintf ( arg1 arg2 ... argN fmt -- string )
bool prim_sprintf(ExecutionContext& ctx) {
    // Pop format string
    auto* fmt_hs = pop_string(ctx);
    if (!fmt_hs) return false;

    std::string_view fmt_sv = fmt_hs->view();
    auto specs = parse_format_specs(fmt_sv);

    // Check for %n rejection
    // parse_format_specs returns empty vector if %n found, but we need to
    // distinguish "no specifiers" from "rejected". Check for %n explicitly.
    if (fmt_sv.find("%n") != std::string_view::npos) {
        // Check it's actually a %n (not %%n)
        for (size_t i = 0; i + 1 < fmt_sv.size(); ++i) {
            if (fmt_sv[i] == '%') {
                if (i + 1 < fmt_sv.size() && fmt_sv[i + 1] == '%') { ++i; continue; }
                // Parse past flags/width/precision/length to find conversion
                size_t j = i + 1;
                while (j < fmt_sv.size() && (fmt_sv[j] == '-' || fmt_sv[j] == '+' ||
                       fmt_sv[j] == ' ' || fmt_sv[j] == '0' || fmt_sv[j] == '#')) ++j;
                while (j < fmt_sv.size() && fmt_sv[j] >= '0' && fmt_sv[j] <= '9') ++j;
                if (j < fmt_sv.size() && fmt_sv[j] == '.') {
                    ++j;
                    while (j < fmt_sv.size() && fmt_sv[j] >= '0' && fmt_sv[j] <= '9') ++j;
                }
                while (j < fmt_sv.size() && (fmt_sv[j] == 'h' || fmt_sv[j] == 'l' ||
                       fmt_sv[j] == 'L' || fmt_sv[j] == 'z' || fmt_sv[j] == 'j' ||
                       fmt_sv[j] == 't' || fmt_sv[j] == 'q')) ++j;
                if (j < fmt_sv.size() && fmt_sv[j] == 'n') {
                    ctx.err() << "sprintf: %n is not supported (security)\n";
                    fmt_hs->release();
                    return false;
                }
            }
        }
    }

    // Count how many args we need (specs with ArgType != None)
    size_t nargs = 0;
    for (auto& sp : specs) {
        if (sp.arg != ArgType::None) ++nargs;
    }

    // Pop nargs values from stack
    std::vector<Value> args;
    args.reserve(nargs);
    for (size_t i = 0; i < nargs; ++i) {
        auto opt = ctx.data_stack().pop();
        if (!opt) {
            // Underflow — push everything back (reverse order)
            for (auto it = args.rbegin(); it != args.rend(); ++it) {
                ctx.data_stack().push(*it);
            }
            ctx.data_stack().push(Value::from(fmt_hs));
            return false;
        }
        args.push_back(*opt);
    }
    // Args were popped LIFO, reverse to match left-to-right format order
    std::reverse(args.begin(), args.end());

    // Track taint: result is tainted if fmt or any string arg is tainted
    bool any_tainted = fmt_hs->is_tainted();

    // Build result string
    std::string result;
    result.reserve(fmt_sv.size() * 2);
    size_t prev = 0;
    size_t arg_idx = 0;

    for (auto& sp : specs) {
        // Append literal text before this specifier
        if (sp.pos > prev) {
            result.append(fmt_sv.data() + prev, sp.pos - prev);
        }

        std::string_view spec_str = fmt_sv.substr(sp.pos, sp.len);

        if (sp.arg == ArgType::None) {
            // %% → literal %
            result += '%';
        } else {
            const Value& v = args[arg_idx++];
            char conv = spec_str.back();

            switch (sp.arg) {
            case ArgType::Int: {
                int64_t ival;
                if (v.type == Value::Type::Integer)
                    ival = v.as_int;
                else if (v.type == Value::Type::Float)
                    ival = static_cast<int64_t>(v.as_float);
                else
                    ival = 0;
                if (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o')
                    result += safe_sprintf_uint(spec_str, static_cast<uint64_t>(ival));
                else
                    result += safe_sprintf_int(spec_str, ival);
                // Release heap ref if any
                if (v.type != Value::Type::Integer && v.type != Value::Type::Float)
                    release_value(v);
                break;
            }
            case ArgType::Float: {
                double fval;
                if (v.type == Value::Type::Float)
                    fval = v.as_float;
                else if (v.type == Value::Type::Integer)
                    fval = static_cast<double>(v.as_int);
                else
                    fval = 0.0;
                // Strip length modifiers for float spec
                std::string fspec;
                fspec.reserve(spec_str.size());
                size_t fi = 0;
                fspec += spec_str[fi++]; // '%'
                while (fi < spec_str.size() && (spec_str[fi] == '-' || spec_str[fi] == '+' ||
                       spec_str[fi] == ' ' || spec_str[fi] == '0' || spec_str[fi] == '#'))
                    fspec += spec_str[fi++];
                while (fi < spec_str.size() && spec_str[fi] >= '0' && spec_str[fi] <= '9')
                    fspec += spec_str[fi++];
                if (fi < spec_str.size() && spec_str[fi] == '.') {
                    fspec += spec_str[fi++];
                    while (fi < spec_str.size() && spec_str[fi] >= '0' && spec_str[fi] <= '9')
                        fspec += spec_str[fi++];
                }
                // Skip length modifiers
                while (fi < spec_str.size() && (spec_str[fi] == 'h' || spec_str[fi] == 'l' ||
                       spec_str[fi] == 'L' || spec_str[fi] == 'z' || spec_str[fi] == 'j' ||
                       spec_str[fi] == 't' || spec_str[fi] == 'q')) ++fi;
                fspec += spec_str.back(); // conversion char
                result += safe_sprintf_float(fspec, fval);
                if (v.type != Value::Type::Integer && v.type != Value::Type::Float)
                    release_value(v);
                break;
            }
            case ArgType::Str: {
                // value_to_str_taint releases heap refs and reports taint
                bool arg_tainted = false;
                std::string sval = value_to_str_taint(v, &arg_tainted);
                if (arg_tainted) any_tainted = true;
                result += safe_sprintf_str(spec_str, sval);
                break;
            }
            case ArgType::Char: {
                int cval;
                if (v.type == Value::Type::Integer)
                    cval = static_cast<int>(v.as_int);
                else if (v.type == Value::Type::Float)
                    cval = static_cast<int>(v.as_float);
                else
                    cval = 0;
                char buf[2] = {static_cast<char>(cval), '\0'};
                result += buf[0];
                if (v.type != Value::Type::Integer && v.type != Value::Type::Float)
                    release_value(v);
                break;
            }
            default:
                break;
            }
        }
        prev = sp.pos + sp.len;
    }
    // Append trailing literal text
    if (prev < fmt_sv.size()) {
        result.append(fmt_sv.data() + prev, fmt_sv.size() - prev);
    }

    fmt_hs->release();
    auto* result_hs = HeapString::create(result);
    if (any_tainted) result_hs->set_tainted(true);
    ctx.data_stack().push(Value::from(result_hs));
    return true;
}

// staint ( str -- flag ) — query taint bit, FORTH flag (-1/0)
bool prim_staint(ExecutionContext& ctx) {
    auto* s = pop_string(ctx);
    if (!s) return false;
    bool tainted = s->is_tainted();
    s->release();
    ctx.data_stack().push(Value(tainted));
    return true;
}

// s>upper ( str -- str' ) — convert to uppercase
bool prim_s_upper(ExecutionContext& ctx) {
    auto* s = pop_string(ctx);
    if (!s) return false;
    std::string result(s->view());
    bool tainted = s->is_tainted();
    s->release();
    for (auto& c : result) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    auto* hs = HeapString::create(result);
    if (tainted) hs->set_tainted(true);
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// s>lower ( str -- str' ) — convert to lowercase
bool prim_s_lower(ExecutionContext& ctx) {
    auto* s = pop_string(ctx);
    if (!s) return false;
    std::string result(s->view());
    bool tainted = s->is_tainted();
    s->release();
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto* hs = HeapString::create(result);
    if (tainted) hs->set_tainted(true);
    ctx.data_stack().push(Value::from(hs));
    return true;
}

// s. ( str -- ) — print string (alias for type)
bool prim_sdot(ExecutionContext& ctx) {
    return prim_type(ctx);
}

void register_string_primitives(Dictionary& dict) {
    using TS = TypeSignature;
    using T = TS::Type;


    dict.register_word("type", make_primitive("type", prim_type,
        {T::String}, {}));
    dict.register_word("s+", make_primitive("s+", prim_splus,
        {T::String, T::String}, {T::String}));
    dict.register_word("s=", make_primitive("s=", prim_seq,
        {T::String, T::String}, {T::Integer}));
    dict.register_word("s<>", make_primitive("s<>", prim_sneq,
        {T::String, T::String}, {T::Integer}));
    dict.register_word("slength", make_primitive("slength", prim_slength,
        {T::String}, {T::Integer}));
    dict.register_word("substr", make_primitive("substr", prim_substr,
        {T::String, T::Integer, T::Integer}, {T::String}));
    dict.register_word("strim", make_primitive("strim", prim_strim,
        {T::String}, {T::String}));
    dict.register_word("sfind", make_primitive("sfind", prim_sfind,
        {T::String, T::String}, {T::Integer}));
    dict.register_word("sreplace", make_primitive("sreplace", prim_sreplace,
        {T::String, T::String, T::String}, {T::String}));
    dict.register_word("ssplit", make_primitive("ssplit", prim_ssplit,
        {T::String, T::String}, {T::Array}));
    dict.register_word("sjoin", make_primitive("sjoin", prim_sjoin,
        {T::Array, T::String}, {T::String}));
    dict.register_word("sregex-find", make_primitive("sregex-find", prim_sregex_find,
        {T::String, T::String}, {T::Integer}));
    dict.register_word("sregex-replace", make_primitive("sregex-replace", prim_sregex_replace,
        {T::String, T::String, T::String}, {T::String}));
    dict.register_word("sregex-search", make_primitive("sregex-search", prim_sregex_search,
        {T::String, T::String}, {T::Array, T::Integer}));
    dict.register_word("sregex-match", make_primitive("sregex-match", prim_sregex_match,
        {T::String, T::String}, {T::Array, T::Integer}));
    dict.register_word("s.", make_primitive("s.", prim_sdot,
        {T::String}, {}));
    dict.register_word("sprintf", make_primitive("sprintf", prim_sprintf,
        {T::String}, {T::String}));
    dict.register_word("staint", make_primitive("staint", prim_staint,
        {T::String}, {T::Integer}));
    dict.register_word("s>upper", make_primitive("s>upper", prim_s_upper,
        {T::String}, {T::String}));
    dict.register_word("s>lower", make_primitive("s>lower", prim_s_lower,
        {T::String}, {T::String}));
}

} // namespace etil::core
