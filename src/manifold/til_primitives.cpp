// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/// Manifold TIL word implementations. Registers `channel-*`, role
/// introspection, identity, and stats words per doc B §21. Phase 2a
/// scope — subscribe/tap-observable (needing a live subject observable)
/// and mcp-on-* (needing §17 Phase B) land in later Phase 2 sub-commits.

#include "etil/manifold/til_primitives.hpp"

#include <algorithm>
#include <any>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include "etil/core/dictionary.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_observable.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/manifold/amqp_sink.hpp"
#include "etil/manifold/channel_action.hpp"
#include "etil/manifold/channel_name.hpp"
#include "etil/manifold/codec_resolver.hpp"
#include "etil/manifold/nats_sink.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/rbac.hpp"
#include "etil/manifold/service.hpp"
#include "etil/manifold/sinks.hpp"
#include "etil/manifold/subject.hpp"
#include "etil/mcp/role_permissions.hpp"

namespace etil::manifold {

using etil::core::ExecutionContext;
using etil::core::HeapArray;
using etil::core::HeapMap;
using etil::core::HeapString;
using etil::core::TypeSignature;
using etil::core::Value;
using etil::core::make_primitive;

namespace {

// --- helpers ----------------------------------------------------------------

std::string pop_string(ExecutionContext& ctx, bool* ok) {
    *ok = false;
    auto opt = ctx.data_stack().pop();
    if (!opt) return {};
    if (opt->type != Value::Type::String || !opt->as_ptr) {
        ctx.data_stack().push(*opt);
        return {};
    }
    auto* hs = opt->as_string();
    std::string s(hs->view());
    hs->release();
    *ok = true;
    return s;
}

void push_string(ExecutionContext& ctx, const std::string& s) {
    auto* hs = HeapString::create(s);
    ctx.data_stack().push(Value::from(hs));
}

/// Parse an actions string like "read,write,route,introspect" or the
/// single letters "r", "w", "ro", "i" into a ChannelAction bitmask.
/// Case-insensitive; unknown tokens ignored.
uint8_t parse_actions(const std::string& spec) {
    uint8_t mask = 0;
    std::string tok;
    auto commit = [&] {
        if (tok.empty()) return;
        for (auto& c : tok) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tok == "r" || tok == "read" || tok == "subscribe") mask |= to_mask(ChannelAction::Read);
        else if (tok == "w" || tok == "write" || tok == "publish") mask |= to_mask(ChannelAction::Write);
        else if (tok == "ro" || tok == "route") mask |= to_mask(ChannelAction::Route);
        else if (tok == "i" || tok == "introspect" || tok == "intro") mask |= to_mask(ChannelAction::Introspect);
        tok.clear();
    };
    for (char c : spec) {
        if (c == ',' || c == '|' || c == ' ') {
            commit();
        } else {
            tok.push_back(c);
        }
    }
    commit();
    return mask;
}

ChannelAction action_from_string(const std::string& s) {
    uint8_t m = parse_actions(s);
    if (m & to_mask(ChannelAction::Write)) return ChannelAction::Write;
    if (m & to_mask(ChannelAction::Read)) return ChannelAction::Read;
    if (m & to_mask(ChannelAction::Route)) return ChannelAction::Route;
    if (m & to_mask(ChannelAction::Introspect)) return ChannelAction::Introspect;
    return ChannelAction::None;
}

const char* action_to_short(ChannelAction a) {
    switch (a) {
        case ChannelAction::Read:       return "read";
        case ChannelAction::Write:      return "write";
        case ChannelAction::Route:      return "route";
        case ChannelAction::Introspect: return "introspect";
        default:                        return "none";
    }
}

std::string actions_mask_to_string(uint8_t m) {
    std::string s;
    auto add = [&](const char* tok) {
        if (!s.empty()) s += ",";
        s += tok;
    };
    if (m & to_mask(ChannelAction::Read))       add("read");
    if (m & to_mask(ChannelAction::Write))      add("write");
    if (m & to_mask(ChannelAction::Route))      add("route");
    if (m & to_mask(ChannelAction::Introspect)) add("introspect");
    if (s.empty()) s = "none";
    return s;
}

HeapMap* sink_stats_to_heap_map(const SinkStats& s) {
    auto* m = new HeapMap();
    m->set("handle", Value(static_cast<int64_t>(s.handle.id)));
    m->set("pattern", Value::from(HeapString::create(s.channel_pattern)));
    const char* mode = "ring-buffered";
    switch (s.delivery_mode) {
        case DeliveryMode::RingBuffered:  mode = "ring-buffered"; break;
        case DeliveryMode::Inline:        mode = "inline"; break;
        case DeliveryMode::InlineBounded: mode = "inline-bounded"; break;
    }
    m->set("delivery-mode", Value::from(HeapString::create(mode)));
    m->set("buffer-capacity", Value(static_cast<int64_t>(s.buffer_capacity)));
    m->set("buffer-depth",    Value(static_cast<int64_t>(s.buffer_depth)));
    m->set("accepted-count",  Value(static_cast<int64_t>(s.accepted_count)));
    m->set("dropped-count",   Value(static_cast<int64_t>(s.dropped_count)));
    return m;
}

/// Build a sink by short name. Returns nullptr on unknown name; caller
/// should report via ctx.err().
std::shared_ptr<ISink> make_sink_by_kind(const std::string& kind,
                                         const std::string& detail) {
    if (kind == "null") return make_null_sink();
    if (kind == "stderr") return make_stderr_sink();
    if (kind == "spdlog") return make_spdlog_sink(detail.empty() ? "etil.manifold" : detail);
    if (kind == "file") return make_file_sink(detail);
    if (kind == "ring") return make_ring_buffer_sink(16, true);
    if (kind == "observable") return make_observable_sink_stub();
    return nullptr;
}

// --- channel-publish ( msg-str channel-str -- ) -----------------------------

bool prim_channel_publish(ExecutionContext& ctx) {
    bool ok = false;
    std::string channel = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string payload = pop_string(ctx, &ok);
    if (!ok) {
        push_string(ctx, channel);
        return false;
    }
    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: channel-publish: no channel service bound to context\n";
        return false;
    }
    Message m;
    m.channel = channel;
    m.payload = payload;
    m.payload_type = std::type_index(typeid(std::string));
    if (!ctx.session_id().empty()) {
        m.tags["session_id"] = ctx.session_id();
    }
    svc->publish(std::move(m), ctx.permissions());
    return true;
}

// --- channel-route-add ( sink-detail-str sink-kind-str pattern-str -- handle )

bool prim_channel_route_add(ExecutionContext& ctx) {
    bool ok = false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string kind = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, pattern); return false; }
    std::string detail = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, kind); push_string(ctx, pattern); return false; }

    if (!validate_pattern(pattern)) {
        ctx.err() << "Error: channel-route-add: invalid pattern '" << pattern << "'\n";
        return false;
    }
    auto sink = make_sink_by_kind(kind, detail);
    if (!sink) {
        ctx.err() << "Error: channel-route-add: unknown sink kind '" << kind << "'\n";
        return false;
    }
    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: channel-route-add: no channel service\n";
        return false;
    }
    RouteSpec spec;
    spec.channel_pattern = pattern;
    spec.sink = std::move(sink);
    auto handle = svc->add_route(std::move(spec), ctx.permissions());
    ctx.data_stack().push(Value(static_cast<int64_t>(handle.id)));
    return true;
}

// --- channel-route-remove ( handle -- ) -------------------------------------

bool prim_channel_route_remove(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) { ctx.data_stack().push(*opt); return false; }
    RouteHandle h;
    h.id = static_cast<uint64_t>(opt->as_int);
    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: channel-route-remove: no channel service\n";
        return false;
    }
    svc->remove_route(h, ctx.permissions());
    return true;
}

// --- channel-tap-file ( path-str pattern-str -- handle ) --------------------
//
// Pattern first on the stack so users can write:
//   "/tmp/log.txt" "etil.**" channel-tap-file
// which reads left-to-right as "log this pattern to this file."
bool prim_channel_tap_file(ExecutionContext& ctx) {
    bool ok = false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string path = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, pattern); return false; }
    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: channel-tap-file: no channel service\n";
        return false;
    }
    RouteSpec spec;
    spec.channel_pattern = pattern;
    spec.sink = make_file_sink(path);
    auto h = svc->add_route(std::move(spec), ctx.permissions());
    ctx.data_stack().push(Value(static_cast<int64_t>(h.id)));
    return true;
}

// --- channel-list-routes ( -- array ) ---------------------------------------

bool prim_channel_list_routes(ExecutionContext& ctx) {
    auto* svc = ctx.channels();
    auto* arr = new HeapArray();
    if (svc) {
        for (const auto& s : svc->all_sink_stats()) {
            auto* m = sink_stats_to_heap_map(s);
            arr->push_back(Value::from(m));
        }
    }
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// --- channel-list ( -- array ) ----------------------------------------------
// Returns the distinct channel patterns for which at least one route
// is registered; equivalent to `channel-list-routes` projected to
// patterns. Phase 2 doesn't yet have a channel registry distinct from
// routes.

bool prim_channel_list(ExecutionContext& ctx) {
    auto* svc = ctx.channels();
    auto* arr = new HeapArray();
    if (svc) {
        std::vector<std::string> seen;
        for (const auto& s : svc->all_sink_stats()) {
            bool dup = false;
            for (auto& v : seen) if (v == s.channel_pattern) { dup = true; break; }
            if (!dup) {
                seen.push_back(s.channel_pattern);
                arr->push_back(Value::from(HeapString::create(s.channel_pattern)));
            }
        }
    }
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// --- channel-origin ( -- map ) ----------------------------------------------
// A-6 resolution: HeapMap with keys host / startup / session / origintype.

bool prim_channel_origin(ExecutionContext& ctx) {
    if (!origin_is_initialized()) init_origin();
    auto* m = new HeapMap();
    m->set("host",
           Value::from(HeapString::create(std::string(origin_hostname()))));
    m->set("startup", Value(origin_app_startup_us()));
    m->set("session", Value::from(HeapString::create(ctx.session_id())));
    m->set("origintype",
           Value::from(HeapString::create(
               origin_type() == OriginType::Browser ? "browser" : "native")));
    ctx.data_stack().push(Value::from(m));
    return true;
}

// --- channel-seq ( -- seq ) -------------------------------------------------
// Returns the next sequence value (non-consuming).

bool prim_channel_seq(ExecutionContext& ctx) {
    if (!origin_is_initialized()) init_origin();
    ctx.data_stack().push(Value(origin_next_seq_value()));
    return true;
}

// --- channel-last-published ( -- msg-id-str ) -------------------------------
// Stub — returns the current seq value as a decimal string. Phase 2b
// will replace with a proper origin-tuple string once the per-ctx
// last-published tracker lands.

bool prim_channel_last_published(ExecutionContext& ctx) {
    if (!origin_is_initialized()) init_origin();
    int64_t seq = origin_next_seq_value();
    push_string(ctx, std::to_string(seq));
    return true;
}

// --- channel-cycle-stats ( -- map ) -----------------------------------------

bool prim_channel_cycle_stats(ExecutionContext& ctx) {
    auto* m = new HeapMap();
    auto* svc = ctx.channels();
    CycleStats s;
    if (svc) s = svc->cycle_stats();
    m->set("cycles-detected",  Value(static_cast<int64_t>(s.cycles_detected)));
    m->set("ttl-exhausted",    Value(static_cast<int64_t>(s.ttl_exhausted)));
    m->set("echo-dropped",     Value(static_cast<int64_t>(s.echo_dropped)));
    m->set("static-warnings",  Value(static_cast<int64_t>(s.static_warnings)));
    ctx.data_stack().push(Value::from(m));
    return true;
}

// --- channel-sink-stats ( handle -- map ) -----------------------------------

bool prim_channel_sink_stats(ExecutionContext& ctx) {
    auto opt = ctx.data_stack().pop();
    if (!opt) return false;
    if (opt->type != Value::Type::Integer) { ctx.data_stack().push(*opt); return false; }
    auto target = static_cast<uint64_t>(opt->as_int);
    auto* svc = ctx.channels();
    if (!svc) {
        ctx.data_stack().push(Value::from(new HeapMap()));
        return true;
    }
    for (const auto& s : svc->all_sink_stats()) {
        if (s.handle.id == target) {
            ctx.data_stack().push(Value::from(sink_stats_to_heap_map(s)));
            return true;
        }
    }
    ctx.data_stack().push(Value::from(new HeapMap()));
    return true;
}

// --- channel-all-sink-stats ( -- array ) ------------------------------------

bool prim_channel_all_sink_stats(ExecutionContext& ctx) {
    return prim_channel_list_routes(ctx);  // alias
}

// --- channel-trace ( -- array ) ---------------------------------------------
// Phase 2a stub — returns an empty array. Meaningful inside a
// subscription handler once the subject-observable lands (Phase 2b).

bool prim_channel_trace(ExecutionContext& ctx) {
    ctx.data_stack().push(Value::from(new HeapArray()));
    return true;
}

// --- channel-hops-left ( -- n ) ---------------------------------------------
// Phase 2a stub — returns the default starting TTL.

bool prim_channel_hops_left(ExecutionContext& ctx) {
    ctx.data_stack().push(Value(static_cast<int64_t>(kDefaultHopsRemaining)));
    return true;
}

// --- channel-perm-check ( action-str pattern-str -- bool ) ------------------

bool prim_channel_perm_check(ExecutionContext& ctx) {
    bool ok = false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string action_str = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, pattern); return false; }
    auto action = action_from_string(action_str);
    if (action == ChannelAction::None) {
        ctx.err() << "Error: channel-perm-check: unknown action '"
                  << action_str << "'\n";
        return false;
    }
    auto d = evaluate_access(ctx.permissions(), pattern, action);
    ctx.data_stack().push(Value(d.allowed));
    return true;
}

// --- channel-subscribe ( pattern-str -- observable ) ------------------------

bool prim_channel_subscribe(ExecutionContext& ctx) {
    bool ok = false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) return false;
    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: channel-subscribe: no channel service\n";
        return false;
    }
    auto obs_sp = svc->observe(pattern, ctx.permissions());
    if (!obs_sp) {
        ctx.err() << "Error: channel-subscribe: observe() denied or "
                     "pattern invalid\n";
        return false;
    }
    // shared_ptr → raw ptr: observe() returned a shared_ptr with a
    // release() deleter, which decrements the HeapObservable's
    // intrusive refcount. We need to transfer ownership to the data
    // stack — addref once and let the shared_ptr's deleter release
    // its own ref when it goes out of scope.
    auto* raw = obs_sp.get();
    raw->add_ref();
    ctx.data_stack().push(Value::from(raw));
    return true;
}

// --- channel-tap-observable ( pattern-str -- observable ) -------------------
// Alias for channel-subscribe — the plan (doc B §21.1) lists both
// names; they share an implementation since both return a live
// observable of the channel.

bool prim_channel_tap_observable(ExecutionContext& ctx) {
    return prim_channel_subscribe(ctx);
}

// ---------------------------------------------------------------------------
// MCP SSE inbound — convenience wrappers over channel-subscribe for
// specific inbound channels (doc B §21.3 / §17.3).
// ---------------------------------------------------------------------------

/// §17.4 receive_* gate. Returns true if the current role is allowed
/// to subscribe to the given etil.mcp.in.* channel. Standalone and
/// hard-wired channels bypass the check.
bool inbound_receive_allowed(const etil::mcp::RolePermissions* p,
                             std::string_view channel) {
    if (p == nullptr) return true;  // standalone
    if (channel == "etil.mcp.in.progress")
        return p->receive_progress;
    if (channel == "etil.mcp.in.cancelled")
        return true;  // hard-wired Read — see kHardwiredChannels
    if (channel == "etil.mcp.in.roots.changed")
        return p->receive_roots_changed;
    if (channel.rfind("etil.mcp.in.notification.", 0) == 0)
        return p->receive_client_notification;
    return true;  // other channels fall through to generic RBAC
}

bool mcp_on_channel_internal(ExecutionContext& ctx, const std::string& channel) {
    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: mcp-on: no channel service\n";
        return false;
    }
    if (!inbound_receive_allowed(ctx.permissions(), channel)) {
        ctx.err() << "Error: mcp-on: role lacks the receive_* permission "
                     "for channel " << channel << "\n";
        return false;
    }
    auto obs_sp = svc->observe(channel, ctx.permissions());
    if (!obs_sp) {
        ctx.err() << "Error: mcp-on: observe denied or pattern invalid\n";
        return false;
    }
    auto* raw = obs_sp.get();
    raw->add_ref();
    ctx.data_stack().push(Value::from(raw));
    return true;
}

// mcp-on-notification ( method-pattern -- observable )
// method-pattern is a subchannel under etil.mcp.in.notification.* —
// e.g., "foo.bar" subscribes to etil.mcp.in.notification.foo.bar.
// Pass "**" for all inbound notifications.
bool prim_mcp_on_notification(ExecutionContext& ctx) {
    bool ok = false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string channel = "etil.mcp.in.notification.";
    if (pattern == "**" || pattern.empty()) channel += "**";
    else channel += pattern;
    return mcp_on_channel_internal(ctx, channel);
}

bool prim_mcp_on_progress(ExecutionContext& ctx) {
    return mcp_on_channel_internal(ctx, "etil.mcp.in.progress");
}

bool prim_mcp_on_cancelled(ExecutionContext& ctx) {
    return mcp_on_channel_internal(ctx, "etil.mcp.in.cancelled");
}

bool prim_mcp_on_roots_changed(ExecutionContext& ctx) {
    return mcp_on_channel_internal(ctx, "etil.mcp.in.roots.changed");
}

// mcp-on-request ( method-pattern -- observable )
// Inbound client-initiated requests routed onto etil.mcp.in.request.*.
// Phase 2c publishes notifications only; request-routing lands when
// the server gains sampling / roots/list support (Phase 3+). The
// word exists now as a stable entry point — it just observes an
// (initially empty) channel tree.
bool prim_mcp_on_request(ExecutionContext& ctx) {
    bool ok = false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string channel = "etil.mcp.in.request.";
    if (pattern == "**" || pattern.empty()) channel += "**";
    else channel += pattern;
    return mcp_on_channel_internal(ctx, channel);
}

// ---------------------------------------------------------------------------
// Role-admin words (doc B §15.8 / §21.2). Mutate the session's
// mutable_permissions() block. Target is always the CURRENT session's
// role record — distributed role registries or cross-session grant
// propagation are out of scope for Phase 2d (AuthConfig persistence
// lands with Phase 3). The role-name argument is recorded as a tag
// on the audit record but does not select a remote target.
// ---------------------------------------------------------------------------

/// Require role_admin + a mutable permissions slot. Returns nullptr
/// and prints an error if either is missing.
etil::mcp::RolePermissions* require_role_admin(ExecutionContext& ctx,
                                               const char* word) {
    const auto* perms = ctx.permissions();
    // Standalone mode allows everything including role admin.
    bool role_admin_ok = (perms == nullptr) || perms->role_admin;
    if (!role_admin_ok) {
        ctx.err() << "Error: " << word
                  << ": role_admin permission required\n";
        return nullptr;
    }
    auto* mut = ctx.mutable_permissions();
    if (!mut) {
        ctx.err() << "Error: " << word
                  << ": mutable permissions slot not configured "
                     "(this context's role record is read-only)\n";
        return nullptr;
    }
    return mut;
}

// role-grant-channel ( actions pattern role-name -- )
bool prim_role_grant_channel(ExecutionContext& ctx) {
    bool ok = false;
    std::string role_name = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, role_name); return false; }
    std::string actions_str = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, pattern); push_string(ctx, role_name); return false; }

    auto* mut = require_role_admin(ctx, "role-grant-channel");
    if (!mut) return false;
    if (!validate_pattern(pattern)) {
        ctx.err() << "Error: role-grant-channel: invalid pattern '"
                  << pattern << "'\n";
        return false;
    }
    uint8_t mask = parse_actions(actions_str);
    if (mask == 0) {
        ctx.err() << "Error: role-grant-channel: unknown actions '"
                  << actions_str << "'\n";
        return false;
    }
    ChannelGrant g;
    g.pattern = std::move(pattern);
    g.actions = mask;
    g.effect = ChannelGrant::Effect::Allow;
    mut->channel_grants.push_back(std::move(g));
    (void)role_name;  // audit recording is Phase 3 — role arg ignored for now
    return true;
}

// role-revoke-channel ( pattern role-name -- )
bool prim_role_revoke_channel(ExecutionContext& ctx) {
    bool ok = false;
    std::string role_name = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, role_name); return false; }

    auto* mut = require_role_admin(ctx, "role-revoke-channel");
    if (!mut) return false;
    auto& grants = mut->channel_grants;
    grants.erase(std::remove_if(grants.begin(), grants.end(),
        [&pattern](const ChannelGrant& g) { return g.pattern == pattern; }),
        grants.end());
    (void)role_name;
    return true;
}

// role-channel-enable! ( bool role-name -- )
bool prim_role_channel_enable(ExecutionContext& ctx) {
    bool ok = false;
    std::string role_name = pop_string(ctx, &ok);
    if (!ok) return false;
    auto bval = ctx.data_stack().pop();
    if (!bval) { push_string(ctx, role_name); return false; }
    if (bval->type != Value::Type::Boolean) {
        ctx.data_stack().push(*bval);
        push_string(ctx, role_name);
        return false;
    }
    auto* mut = require_role_admin(ctx, "role-channel-enable!");
    if (!mut) return false;
    mut->channels_enabled = (bval->as_int != 0);
    (void)role_name;
    return true;
}

// role-network-sink! ( bool role-name -- )
bool prim_role_network_sink(ExecutionContext& ctx) {
    bool ok = false;
    std::string role_name = pop_string(ctx, &ok);
    if (!ok) return false;
    auto bval = ctx.data_stack().pop();
    if (!bval) { push_string(ctx, role_name); return false; }
    if (bval->type != Value::Type::Boolean) {
        ctx.data_stack().push(*bval);
        push_string(ctx, role_name);
        return false;
    }
    auto* mut = require_role_admin(ctx, "role-network-sink!");
    if (!mut) return false;
    mut->channels_network_sink = (bval->as_int != 0);
    (void)role_name;
    return true;
}

// --- channel-perm-list ( -- array ) -----------------------------------------

bool prim_channel_perm_list(ExecutionContext& ctx) {
    auto* arr = new HeapArray();
    auto* perms = ctx.permissions();
    if (perms) {
        for (const auto& g : perms->channel_grants) {
            auto* m = new HeapMap();
            m->set("pattern", Value::from(HeapString::create(g.pattern)));
            m->set("actions", Value::from(HeapString::create(
                actions_mask_to_string(g.actions))));
            m->set("effect", Value::from(HeapString::create(
                g.effect == ChannelGrant::Effect::Allow ? "allow" : "deny")));
            arr->push_back(Value::from(m));
        }
    }
    ctx.data_stack().push(Value::from(arr));
    return true;
}

// --- channel-tap-nats ( url codec pattern -- handle ) -----------------------
//
// Install a NATS broker sink on `pattern` that serializes via `codec`
// and publishes to `url`. Returns the route handle or 0 on failure
// (unknown codec, connect failure, or binary not built with
// ETIL_BUILD_NATS_SINK). Plan doc 20260419A §Phase 3b.

bool prim_channel_tap_nats(ExecutionContext& ctx) {
    bool ok = false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string codec = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, pattern); return false; }
    std::string url = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, codec); push_string(ctx, pattern); return false; }

    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: channel-tap-nats: no channel service\n";
        return false;
    }

    auto codec_xform = resolve_codec(codec);
    if (!codec_xform) {
        ctx.err() << "Error: channel-tap-nats: unknown codec '"
                  << codec << "' (use json|msgpack|cbor|raw)\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    BrokerSinkConfig cfg;
    cfg.broker_url = std::move(url);
    cfg.codec = codec.empty() ? std::string("json") : codec;
    auto channels_sp = svc->shared_from_this();
    auto sink = make_nats_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(channels_sp));
    if (!sink) {
        // make_nats_sink logs the reason.
        ctx.data_stack().push(Value(false));
        return true;
    }

    RouteSpec spec;
    spec.channel_pattern = std::move(pattern);
    spec.transforms.push_back(std::move(codec_xform));
    spec.sink = std::move(sink);

    auto h = svc->add_route(std::move(spec), ctx.permissions());
    if (!h.valid()) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    ctx.data_stack().push(Value(static_cast<int64_t>(h.id)));
    return true;
}

// --- channel-tap-amqp ( url codec pattern -- handle ) -----------------------
//
// AMQP 1.0 parallel of channel-tap-nats. Same codec parameter
// semantics, same RBAC gates. Plan doc 20260419A §Phase 3c.

bool prim_channel_tap_amqp(ExecutionContext& ctx) {
    bool ok = false;
    std::string pattern = pop_string(ctx, &ok);
    if (!ok) return false;
    std::string codec = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, pattern); return false; }
    std::string url = pop_string(ctx, &ok);
    if (!ok) { push_string(ctx, codec); push_string(ctx, pattern); return false; }

    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: channel-tap-amqp: no channel service\n";
        return false;
    }

    auto codec_xform = resolve_codec(codec);
    if (!codec_xform) {
        ctx.err() << "Error: channel-tap-amqp: unknown codec '"
                  << codec << "' (use json|msgpack|cbor|raw)\n";
        ctx.data_stack().push(Value(false));
        return true;
    }

    BrokerSinkConfig cfg;
    cfg.broker_url = std::move(url);
    cfg.codec = codec.empty() ? std::string("json") : codec;
    auto channels_sp = svc->shared_from_this();
    auto sink = make_amqp_sink(std::move(cfg),
                               std::weak_ptr<ChannelService>(channels_sp));
    if (!sink) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    RouteSpec spec;
    spec.channel_pattern = std::move(pattern);
    spec.transforms.push_back(std::move(codec_xform));
    spec.sink = std::move(sink);

    auto h = svc->add_route(std::move(spec), ctx.permissions());
    if (!h.valid()) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    ctx.data_stack().push(Value(static_cast<int64_t>(h.id)));
    return true;
}

// --- channel-session-hmac ( session-str -- hmac-str ) -----------------------
//
// Compute the Session-Hmac token the broker sinks put on outbound
// messages for this session_id. Local subscribers that know the
// plaintext session_id call this to obtain the same token and
// filter inbound broker traffic by it. Raw session_id never leaves
// the process (A-5). Empty input yields empty output.

bool prim_channel_session_hmac(ExecutionContext& ctx) {
    bool ok = false;
    std::string session_id = pop_string(ctx, &ok);
    if (!ok) return false;
    auto* svc = ctx.channels();
    if (!svc) {
        ctx.err() << "Error: channel-session-hmac: no channel service\n";
        return false;
    }
    std::string hmac = svc->session_hmac(session_id);
    ctx.data_stack().push(Value::from(HeapString::create(hmac)));
    return true;
}

} // namespace

void register_manifold_primitives(etil::core::Dictionary& dict) {
    using T = TypeSignature::Type;

    dict.register_word("channel-publish",
        make_primitive("channel-publish", prim_channel_publish,
            {T::String, T::String}, {}));

    dict.register_word("channel-route-add",
        make_primitive("channel-route-add", prim_channel_route_add,
            {T::String, T::String, T::String}, {T::Integer}));

    dict.register_word("channel-route-remove",
        make_primitive("channel-route-remove", prim_channel_route_remove,
            {T::Integer}, {}));

    dict.register_word("channel-tap-file",
        make_primitive("channel-tap-file", prim_channel_tap_file,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("channel-tap-nats",
        make_primitive("channel-tap-nats", prim_channel_tap_nats,
            {T::String, T::String, T::String}, {T::Integer}));

    dict.register_word("channel-tap-amqp",
        make_primitive("channel-tap-amqp", prim_channel_tap_amqp,
            {T::String, T::String, T::String}, {T::Integer}));

    dict.register_word("channel-session-hmac",
        make_primitive("channel-session-hmac", prim_channel_session_hmac,
            {T::String}, {T::String}));

    dict.register_word("channel-list",
        make_primitive("channel-list", prim_channel_list, {}, {T::Array}));

    dict.register_word("channel-list-routes",
        make_primitive("channel-list-routes", prim_channel_list_routes,
            {}, {T::Array}));

    dict.register_word("channel-origin",
        make_primitive("channel-origin", prim_channel_origin, {}, {T::Map}));

    dict.register_word("channel-seq",
        make_primitive("channel-seq", prim_channel_seq, {}, {T::Integer}));

    dict.register_word("channel-last-published",
        make_primitive("channel-last-published", prim_channel_last_published,
            {}, {T::String}));

    dict.register_word("channel-cycle-stats",
        make_primitive("channel-cycle-stats", prim_channel_cycle_stats,
            {}, {T::Map}));

    dict.register_word("channel-sink-stats",
        make_primitive("channel-sink-stats", prim_channel_sink_stats,
            {T::Integer}, {T::Map}));

    dict.register_word("channel-all-sink-stats",
        make_primitive("channel-all-sink-stats", prim_channel_all_sink_stats,
            {}, {T::Array}));

    dict.register_word("channel-trace",
        make_primitive("channel-trace", prim_channel_trace, {}, {T::Array}));

    dict.register_word("channel-hops-left",
        make_primitive("channel-hops-left", prim_channel_hops_left,
            {}, {T::Integer}));

    dict.register_word("channel-perm-check",
        make_primitive("channel-perm-check", prim_channel_perm_check,
            {T::String, T::String}, {T::Boolean}));

    dict.register_word("channel-perm-list",
        make_primitive("channel-perm-list", prim_channel_perm_list,
            {}, {T::Array}));

    dict.register_word("channel-subscribe",
        make_primitive("channel-subscribe", prim_channel_subscribe,
            {T::String}, {T::Observable}));

    dict.register_word("channel-tap-observable",
        make_primitive("channel-tap-observable", prim_channel_tap_observable,
            {T::String}, {T::Observable}));

    dict.register_word("mcp-on-notification",
        make_primitive("mcp-on-notification", prim_mcp_on_notification,
            {T::String}, {T::Observable}));

    dict.register_word("mcp-on-progress",
        make_primitive("mcp-on-progress", prim_mcp_on_progress,
            {}, {T::Observable}));

    dict.register_word("mcp-on-cancelled",
        make_primitive("mcp-on-cancelled", prim_mcp_on_cancelled,
            {}, {T::Observable}));

    dict.register_word("mcp-on-roots-changed",
        make_primitive("mcp-on-roots-changed", prim_mcp_on_roots_changed,
            {}, {T::Observable}));

    dict.register_word("mcp-on-request",
        make_primitive("mcp-on-request", prim_mcp_on_request,
            {T::String}, {T::Observable}));

    dict.register_word("role-grant-channel",
        make_primitive("role-grant-channel", prim_role_grant_channel,
            {T::String, T::String, T::String}, {}));

    dict.register_word("role-revoke-channel",
        make_primitive("role-revoke-channel", prim_role_revoke_channel,
            {T::String, T::String}, {}));

    dict.register_word("role-channel-enable!",
        make_primitive("role-channel-enable!", prim_role_channel_enable,
            {T::Boolean, T::String}, {}));

    dict.register_word("role-network-sink!",
        make_primitive("role-network-sink!", prim_role_network_sink,
            {T::Boolean, T::String}, {}));
}

} // namespace etil::manifold
