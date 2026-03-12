// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// Shared prologue helpers for sync and async file I/O primitives.
// Included by file_io_primitives.cpp and async_file_io.cpp inside their
// anonymous namespace, so every function here is translation-unit-local.

#pragma once

#include "etil/core/execution_context.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/lvfs/lvfs.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <uv.h>

namespace etil::fileio::helpers {

using etil::core::ExecutionContext;
using etil::core::Value;
using etil::core::HeapString;
using etil::core::pop_string;
using etil::lvfs::Lvfs;

/// Tri-state return for pop-and-resolve helpers.
enum class Status { Ok, PushedFalse, Underflow };

/// Check lvfs_modify permission.  Returns true if permitted.
inline bool check_lvfs_modify(ExecutionContext& ctx) {
    auto* perms = ctx.permissions();
    if (perms && !perms->lvfs_modify) {
        ctx.err() << "Error: file modification not permitted\n";
        return false;
    }
    return true;
}

/// Check disk quota before a write.
inline bool check_disk_quota(ExecutionContext& ctx, size_t write_bytes) {
    auto* perms = ctx.permissions();
    if (!perms || perms->disk_quota <= 0) return true;
    auto* lvfs = ctx.lvfs();
    if (!lvfs) return true;
    uint64_t usage = lvfs->home_usage_bytes();
    if (usage + write_bytes > static_cast<uint64_t>(perms->disk_quota)) {
        ctx.err() << "Error: disk quota exceeded ("
                  << usage << " + " << write_bytes
                  << " > " << perms->disk_quota << " bytes)\n";
        return false;
    }
    return true;
}

/// Pop a path string and resolve it via LVFS.
/// On success: fs_path and lvfs are populated, returns Ok.
/// On stack underflow: returns Underflow (caller returns false).
/// On soft failure (no lvfs, empty resolve): pushes false, returns PushedFalse.
inline Status pop_and_resolve(ExecutionContext& ctx,
                              std::string& virtual_path,
                              std::string& fs_path,
                              Lvfs*& lvfs_out) {
    auto* hs = pop_string(ctx);
    if (!hs) return Status::Underflow;
    virtual_path.assign(hs->view());
    hs->release();

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    fs_path = lvfs->resolve(virtual_path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    lvfs_out = lvfs;
    return Status::Ok;
}

/// Pop a path, check lvfs_modify permission and read-only status, then resolve.
inline Status pop_and_resolve_writable(ExecutionContext& ctx,
                                       std::string& fs_path,
                                       Lvfs*& lvfs_out) {
    auto* hs = pop_string(ctx);
    if (!hs) return Status::Underflow;
    std::string path(hs->view());
    hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    if (lvfs->is_read_only(path)) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    lvfs_out = lvfs;
    return Status::Ok;
}

/// Pop two paths (dest first from TOS, then src).
/// Only the destination is checked for read-only (source can be /library).
inline Status pop_two_paths_copy(ExecutionContext& ctx,
                                 std::string& src_fs,
                                 std::string& dest_fs) {
    auto* dest_hs = pop_string(ctx);
    if (!dest_hs) return Status::Underflow;
    std::string dest(dest_hs->view());
    dest_hs->release();

    auto* src_hs = pop_string(ctx);
    if (!src_hs) return Status::Underflow;
    std::string src(src_hs->view());
    src_hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    if (lvfs->is_read_only(dest)) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    src_fs = lvfs->resolve(src);
    dest_fs = lvfs->resolve(dest);
    if (src_fs.empty() || dest_fs.empty()) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    return Status::Ok;
}

/// Pop two paths where both must be writable (for rename).
inline Status pop_two_paths_rename(ExecutionContext& ctx,
                                   std::string& old_fs,
                                   std::string& new_fs) {
    auto* new_hs = pop_string(ctx);
    if (!new_hs) return Status::Underflow;
    std::string new_path(new_hs->view());
    new_hs->release();

    auto* old_hs = pop_string(ctx);
    if (!old_hs) return Status::Underflow;
    std::string old_path(old_hs->view());
    old_hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    if (lvfs->is_read_only(old_path) || lvfs->is_read_only(new_path)) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    old_fs = lvfs->resolve(old_path);
    new_fs = lvfs->resolve(new_path);
    if (old_fs.empty() || new_fs.empty()) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    return Status::Ok;
}

/// Pop content + path, check write permission and quota, resolve path.
inline Status pop_write_args(ExecutionContext& ctx,
                             std::string& content,
                             std::string& fs_path) {
    auto* path_hs = pop_string(ctx);
    if (!path_hs) return Status::Underflow;
    std::string path(path_hs->view());
    path_hs->release();

    auto* content_hs = pop_string(ctx);
    if (!content_hs) return Status::Underflow;
    content.assign(content_hs->view());
    content_hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }
    if (!check_disk_quota(ctx, content.size())) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    if (lvfs->is_read_only(path)) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return Status::PushedFalse;
    }

    return Status::Ok;
}

/// Convert a filesystem path under home_dir to a virtual /home/ path.
inline std::string fs_path_to_virtual(const std::string& fs_result,
                                      const std::string& home_dir) {
    std::string home_with_slash = home_dir;
    if (!home_with_slash.empty() && home_with_slash.back() != '/') {
        home_with_slash += '/';
    }
    if (fs_result.compare(0, home_with_slash.size(), home_with_slash) == 0) {
        return "/home/" + fs_result.substr(home_with_slash.size());
    }
    return fs_result;
}

/// Check if fs_path is the home root (safety guard for rm).
inline bool is_home_root(const std::string& fs_path, Lvfs* lvfs) {
    std::string home = lvfs->home_dir();
    if (!home.empty() && home.back() == '/') home.pop_back();
    std::string fs_norm = fs_path;
    if (!fs_norm.empty() && fs_norm.back() == '/') fs_norm.pop_back();
    return fs_norm == home;
}

/// Build the standard lstat result array: [size, mtime_us, is_dir, is_ro].
inline etil::core::HeapArray* make_stat_array(const uv_stat_t& st,
                                              bool is_ro) {
    bool is_dir = (st.st_mode & S_IFDIR) != 0;
    int64_t size = is_dir ? 0 : static_cast<int64_t>(st.st_size);
    int64_t mtime_us = static_cast<int64_t>(st.st_mtim.tv_sec) * 1'000'000LL +
                       static_cast<int64_t>(st.st_mtim.tv_nsec) / 1'000LL;

    auto* arr = new etil::core::HeapArray();
    arr->push_back(Value(size));
    arr->push_back(Value(mtime_us));
    arr->push_back(Value(static_cast<bool>(is_dir)));
    arr->push_back(Value(static_cast<bool>(is_ro)));
    return arr;
}

} // namespace etil::fileio::helpers
