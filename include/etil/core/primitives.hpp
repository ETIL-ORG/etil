#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include "etil/core/dictionary.hpp"

namespace etil::core {

class ExecutionContext;

/// Create a primitive WordImpl with the given name, native function, and type signature.
WordImplPtr make_primitive(const char* name, WordImpl::FunctionPtr fn,
                           std::vector<TypeSignature::Type> inputs,
                           std::vector<TypeSignature::Type> outputs);

/// Register all built-in primitive words into the dictionary.
void register_primitives(Dictionary& dict);

// Arithmetic primitives
bool prim_add(ExecutionContext& ctx);
bool prim_sub(ExecutionContext& ctx);
bool prim_mul(ExecutionContext& ctx);
bool prim_div(ExecutionContext& ctx);
bool prim_mod(ExecutionContext& ctx);
bool prim_divmod(ExecutionContext& ctx);
bool prim_negate(ExecutionContext& ctx);
bool prim_abs(ExecutionContext& ctx);
bool prim_max(ExecutionContext& ctx);
bool prim_min(ExecutionContext& ctx);

// Stack manipulation primitives
bool prim_dup(ExecutionContext& ctx);
bool prim_drop(ExecutionContext& ctx);
bool prim_swap(ExecutionContext& ctx);
bool prim_over(ExecutionContext& ctx);
bool prim_rot(ExecutionContext& ctx);
bool prim_pick(ExecutionContext& ctx);
bool prim_nip(ExecutionContext& ctx);
bool prim_tuck(ExecutionContext& ctx);
bool prim_depth(ExecutionContext& ctx);
bool prim_qdup(ExecutionContext& ctx);
bool prim_roll(ExecutionContext& ctx);

// Comparison primitives
bool prim_eq(ExecutionContext& ctx);
bool prim_neq(ExecutionContext& ctx);
bool prim_lt(ExecutionContext& ctx);
bool prim_gt(ExecutionContext& ctx);
bool prim_le(ExecutionContext& ctx);
bool prim_ge(ExecutionContext& ctx);
bool prim_zero_eq(ExecutionContext& ctx);
bool prim_zero_lt(ExecutionContext& ctx);
bool prim_zero_gt(ExecutionContext& ctx);

// Logic primitives (bitwise on integers, logical on flags)
bool prim_and(ExecutionContext& ctx);
bool prim_or(ExecutionContext& ctx);
bool prim_xor(ExecutionContext& ctx);
bool prim_invert(ExecutionContext& ctx);
bool prim_lshift(ExecutionContext& ctx);
bool prim_rshift(ExecutionContext& ctx);
bool prim_lroll(ExecutionContext& ctx);
bool prim_rroll(ExecutionContext& ctx);

// I/O primitives
bool prim_dot(ExecutionContext& ctx);
bool prim_cr(ExecutionContext& ctx);
bool prim_emit(ExecutionContext& ctx);
bool prim_space(ExecutionContext& ctx);
bool prim_spaces(ExecutionContext& ctx);
bool prim_words(ExecutionContext& ctx);

// Memory primitives (for CREATE/DOES>)
bool prim_create(ExecutionContext& ctx);  // create — read next token from input stream, define word
bool prim_comma(ExecutionContext& ctx);   // , — pop value, append to last_created's data_field
bool prim_fetch(ExecutionContext& ctx);   // @ — pop pointer, push value at that address
bool prim_store(ExecutionContext& ctx);   // ! — pop value and pointer, store value at address
bool prim_allot(ExecutionContext& ctx);   // allot — pop n, extend last_created's data_field by n cells

// Input-reading primitives
bool prim_word_read(ExecutionContext& ctx);          // word-read ( -- str flag )
bool prim_string_read_delim(ExecutionContext& ctx);  // string-read-delim ( char -- str flag )

// Dictionary-operation primitives
bool prim_dict_forget(ExecutionContext& ctx);        // dict-forget ( name-str -- flag )
bool prim_dict_forget_all(ExecutionContext& ctx);    // dict-forget-all ( name-str -- flag )
bool prim_file_load(ExecutionContext& ctx);          // file-load ( path-str -- flag )
bool prim_include(ExecutionContext& ctx);            // include — read path from input stream, load from home dir
bool prim_library(ExecutionContext& ctx);            // library — read path from input stream, load from library dir
bool prim_evaluate(ExecutionContext& ctx);           // evaluate ( str -- ) — interpret string as code

// Metadata primitives (stack-based)
bool prim_dict_meta_set(ExecutionContext& ctx);      // dict-meta-set ( word-str key-str fmt-str content-str -- flag )
bool prim_dict_meta_get(ExecutionContext& ctx);      // dict-meta-get ( word-str key-str -- content-str flag )
bool prim_dict_meta_del(ExecutionContext& ctx);      // dict-meta-del ( word-str key-str -- flag )
bool prim_dict_meta_keys(ExecutionContext& ctx);     // dict-meta-keys ( word-str -- array flag )
bool prim_impl_meta_set(ExecutionContext& ctx);      // impl-meta-set ( word-str key-str fmt-str content-str -- flag )
bool prim_impl_meta_get(ExecutionContext& ctx);      // impl-meta-get ( word-str key-str -- content-str flag )

// Math primitives (all operate as float, int inputs promoted)
bool prim_sqrt(ExecutionContext& ctx);
bool prim_sin(ExecutionContext& ctx);
bool prim_cos(ExecutionContext& ctx);
bool prim_tan(ExecutionContext& ctx);
bool prim_asin(ExecutionContext& ctx);
bool prim_acos(ExecutionContext& ctx);
bool prim_atan(ExecutionContext& ctx);
bool prim_atan2(ExecutionContext& ctx);   // ( y x -- atan2(y,x) )
bool prim_log(ExecutionContext& ctx);     // natural log
bool prim_log2(ExecutionContext& ctx);
bool prim_log10(ExecutionContext& ctx);
bool prim_exp(ExecutionContext& ctx);
bool prim_pow(ExecutionContext& ctx);     // ( base exp -- base^exp )
bool prim_ceil(ExecutionContext& ctx);
bool prim_floor(ExecutionContext& ctx);
bool prim_round(ExecutionContext& ctx);
bool prim_trunc(ExecutionContext& ctx);
bool prim_fmin(ExecutionContext& ctx);    // ( a b -- min(a,b) )
bool prim_fmax(ExecutionContext& ctx);    // ( a b -- max(a,b) )
bool prim_pi(ExecutionContext& ctx);      // ( -- pi )

// Float comparison
bool prim_fapprox(ExecutionContext& ctx); // f~ ( r1 r2 r3 -- flag )

// PRNG primitives
bool prim_random(ExecutionContext& ctx);        // ( -- f ) pseudo-random float [0,1)
bool prim_random_seed(ExecutionContext& ctx);   // ( n -- ) seed the PRNG
bool prim_random_range(ExecutionContext& ctx);  // ( lo hi -- n ) random integer [lo,hi)

// System primitives
bool prim_sys_semver(ExecutionContext& ctx);        // sys-semver — print project SEMVER
bool prim_sys_timestamp(ExecutionContext& ctx);     // sys-timestamp — print build timestamp
bool prim_sys_datafields(ExecutionContext& ctx);    // sys-datafields — print DataFieldRegistry stats
bool prim_sys_notification(ExecutionContext& ctx);  // sys-notification ( x -- ) — queue notification for TUI
bool prim_user_notification(ExecutionContext& ctx); // user-notification ( msg-str user-id-str -- flag )
bool prim_abort(ExecutionContext& ctx);             // abort ( flag -- ) or ( error-string false -- )

// Time primitives
bool prim_time_us(ExecutionContext& ctx);           // time-us ( -- n ) — push UTC microseconds since epoch
bool prim_us_to_iso(ExecutionContext& ctx);         // us->iso ( n -- str ) — format as YYYYMMDDTHHMMSSZ
bool prim_us_to_iso_us(ExecutionContext& ctx);      // us->iso-us ( n -- str ) — format as YYYYMMDDTHHMMSS.ddddddZ
bool prim_us_to_jd(ExecutionContext& ctx);          // us->jd ( n -- f ) — UTC microseconds to Julian Date
bool prim_jd_to_us(ExecutionContext& ctx);          // jd->us ( f -- n ) — Julian Date to UTC microseconds
bool prim_us_to_mjd(ExecutionContext& ctx);         // us->mjd ( n -- f ) — UTC microseconds to Modified Julian Date
bool prim_mjd_to_us(ExecutionContext& ctx);         // mjd->us ( f -- n ) — Modified Julian Date to UTC microseconds
bool prim_sleep(ExecutionContext& ctx);             // sleep ( n -- ) — sleep n microseconds

// Help primitive
bool prim_help(ExecutionContext& ctx);           // help — parsing word, prints help for next token

// Debug primitives
bool prim_dump(ExecutionContext& ctx);           // dump — deep-inspect TOS (non-destructive)
bool prim_see(ExecutionContext& ctx);            // see — decompile a word definition

// Execution token (xt) primitives
bool prim_tick(ExecutionContext& ctx);           // ' ( "name" -- xt ) — parse next token, push xt
bool prim_execute(ExecutionContext& ctx);        // execute ( xt -- ) — execute the referenced word
bool prim_xt_query(ExecutionContext& ctx);       // xt? ( val -- flag ) — type check for Xt
bool prim_xt_to_name(ExecutionContext& ctx);     // >name ( xt -- str ) — extract word name from xt

// Defining word primitives
bool prim_immediate(ExecutionContext& ctx);   // immediate ( -- ) — mark last-defined word as immediate
bool prim_xt_body(ExecutionContext& ctx);     // xt-body ( xt -- dataref ) — get data field from xt

// Type conversion primitives
bool prim_dot_s(ExecutionContext& ctx);              // .s ( -- ) — non-destructive stack display
bool prim_int_to_float(ExecutionContext& ctx);       // int->float ( n -- f )
bool prim_float_to_int(ExecutionContext& ctx);       // float->int ( f -- n )
bool prim_number_to_string(ExecutionContext& ctx);   // number->string ( n -- str )
bool prim_string_to_number(ExecutionContext& ctx);   // string->number ( str -- value -1 | 0 )

// Dictionary checkpoint/restore
bool prim_marker(ExecutionContext& ctx);         // marker ( "name" -- ) — parsing word
bool prim_marker_restore(ExecutionContext& ctx); // marker-restore ( name-str -- )

// Map primitives
bool prim_map_new(ExecutionContext& ctx);        // map-new ( -- map )
bool prim_map_set(ExecutionContext& ctx);        // map-set ( map key val -- map )
bool prim_map_get(ExecutionContext& ctx);        // map-get ( map key -- val )
bool prim_map_remove(ExecutionContext& ctx);     // map-remove ( map key -- map )
bool prim_map_length(ExecutionContext& ctx);     // map-length ( map -- n )
bool prim_map_keys(ExecutionContext& ctx);       // map-keys ( map -- arr )
bool prim_map_values(ExecutionContext& ctx);     // map-values ( map -- arr )
bool prim_map_has(ExecutionContext& ctx);        // map-has? ( map key -- flag )

} // namespace etil::core
