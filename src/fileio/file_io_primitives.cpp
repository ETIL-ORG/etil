// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/fileio/file_io_primitives.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/value_helpers.hpp"
#include "etil/core/word_impl.hpp"
#include "etil/lvfs/lvfs.hpp"
#include "etil/mcp/role_permissions.hpp"

#include <filesystem>

#include <uv.h>

namespace etil::fileio {

namespace fs = std::filesystem;
using etil::core::ExecutionContext;
using etil::core::Value;
using etil::core::HeapString;
using etil::core::HeapArray;
using etil::core::HeapObject;
using etil::core::make_heap_value;
using etil::core::pop_string;
using etil::core::make_primitive;
using etil::lvfs::Lvfs;

namespace {


/// Check lvfs_modify permission.  Returns true if permitted, false if denied.
static bool check_lvfs_modify(ExecutionContext& ctx) {
    auto* perms = ctx.permissions();
    if (perms && !perms->lvfs_modify) {
        ctx.err() << "Error: file modification not permitted\n";
        return false;
    }
    return true;
}

/// Check disk quota before a write.
static bool check_disk_quota(ExecutionContext& ctx, size_t write_bytes) {
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

// Thread-local loop for synchronous libuv operations.
// libuv sync mode (callback=NULL) requires a valid loop parameter
// but doesn't process events through it.
uv_loop_t* sync_loop() {
    thread_local uv_loop_t loop;
    thread_local bool initialized = false;
    if (!initialized) {
        uv_loop_init(&loop);
        initialized = true;
    }
    return &loop;
}

// RAII wrapper for uv_fs_t cleanup in synchronous operations.
struct SyncReq {
    uv_fs_t req{};
    ~SyncReq() { uv_fs_req_cleanup(&req); }
    SyncReq() = default;
    SyncReq(const SyncReq&) = delete;
    SyncReq& operator=(const SyncReq&) = delete;
};

// exists-sync ( path -- flag )
bool prim_exists_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string path(hs->view());
    hs->release();

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    SyncReq r;
    int rc = uv_fs_stat(sync_loop(), &r.req, fs_path.c_str(), nullptr);
    ctx.data_stack().push(Value(static_cast<bool>(rc >= 0)));
    return true;
}

// read-file-sync ( path -- string? flag )
bool prim_read_file_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string path(hs->view());
    hs->release();

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Step 1: stat to get file size and verify regular file
    SyncReq stat_r;
    int rc = uv_fs_stat(sync_loop(), &stat_r.req, fs_path.c_str(), nullptr);
    if (rc < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto& st = stat_r.req.statbuf;
    if (!(st.st_mode & S_IFREG)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    size_t file_size = static_cast<size_t>(st.st_size);

    // Step 2: open
    SyncReq open_r;
    rc = uv_fs_open(sync_loop(), &open_r.req, fs_path.c_str(),
                    UV_FS_O_RDONLY, 0, nullptr);
    if (rc < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    uv_file fd = static_cast<uv_file>(rc);

    // Step 3: read (handle empty files)
    std::string content;
    if (file_size > 0) {
        content.resize(file_size);
        uv_buf_t buf = uv_buf_init(content.data(),
                                    static_cast<unsigned int>(file_size));

        SyncReq read_r;
        rc = uv_fs_read(sync_loop(), &read_r.req, fd, &buf, 1, 0, nullptr);
        if (rc < 0) {
            SyncReq close_r;
            uv_fs_close(sync_loop(), &close_r.req, fd, nullptr);
            ctx.data_stack().push(Value(false));
            return true;
        }

        // Adjust size if we read less than expected
        content.resize(static_cast<size_t>(rc));
    }

    // Step 4: close
    {
        SyncReq close_r;
        uv_fs_close(sync_loop(), &close_r.req, fd, nullptr);
    }

    auto* result = HeapString::create(content);
    result->set_tainted(true);  // File data is untrusted
    ctx.data_stack().push(make_heap_value(result));
    ctx.data_stack().push(Value(true));
    return true;
}

// Helper: sync write/append with open flags
bool write_file_sync_impl(ExecutionContext& ctx, int flags) {
    auto* path_hs = pop_string(ctx);
    if (!path_hs) return false;
    std::string path(path_hs->view());
    path_hs->release();

    auto* content_hs = pop_string(ctx);
    if (!content_hs) return false;
    std::string content(content_hs->view());
    content_hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return true;
    }
    if (!check_disk_quota(ctx, content.size())) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    if (lvfs->is_read_only(path)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Open
    SyncReq open_r;
    int rc = uv_fs_open(sync_loop(), &open_r.req, fs_path.c_str(),
                        flags, 0644, nullptr);
    if (rc < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    uv_file fd = static_cast<uv_file>(rc);

    // Write
    bool ok = true;
    if (!content.empty()) {
        uv_buf_t buf = uv_buf_init(const_cast<char*>(content.data()),
                                    static_cast<unsigned int>(content.size()));
        SyncReq write_r;
        rc = uv_fs_write(sync_loop(), &write_r.req, fd, &buf, 1,
                         (flags & UV_FS_O_APPEND) ? -1 : 0, nullptr);
        if (rc < 0) ok = false;
    }

    // Close
    {
        SyncReq close_r;
        uv_fs_close(sync_loop(), &close_r.req, fd, nullptr);
    }

    ctx.data_stack().push(Value(static_cast<bool>(ok)));
    return true;
}

// write-file-sync ( string path -- flag )
bool prim_write_file_sync(ExecutionContext& ctx) {
    return write_file_sync_impl(ctx,
        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC);
}

// append-file-sync ( string path -- flag )
bool prim_append_file_sync(ExecutionContext& ctx) {
    return write_file_sync_impl(ctx,
        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND);
}

// copy-file-sync ( src dest -- flag )
// Source can be /library (read-only OK). Only dest is checked.
bool prim_copy_file_sync(ExecutionContext& ctx) {
    auto* dest_hs = pop_string(ctx);
    if (!dest_hs) return false;
    std::string dest(dest_hs->view());
    dest_hs->release();

    auto* src_hs = pop_string(ctx);
    if (!src_hs) return false;
    std::string src(src_hs->view());
    src_hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Only dest is checked for read-only
    if (lvfs->is_read_only(dest)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string src_fs = lvfs->resolve(src);
    std::string dest_fs = lvfs->resolve(dest);
    if (src_fs.empty() || dest_fs.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    SyncReq r;
    int rc = uv_fs_copyfile(sync_loop(), &r.req, src_fs.c_str(), dest_fs.c_str(),
                            UV_FS_COPYFILE_FICLONE, nullptr);
    ctx.data_stack().push(Value(static_cast<bool>(rc >= 0)));
    return true;
}

// rename-sync ( old new -- flag )
// Both paths must be writable (under /home).
bool prim_rename_sync(ExecutionContext& ctx) {
    auto* new_hs = pop_string(ctx);
    if (!new_hs) return false;
    std::string new_path(new_hs->view());
    new_hs->release();

    auto* old_hs = pop_string(ctx);
    if (!old_hs) return false;
    std::string old_path(old_hs->view());
    old_hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    if (lvfs->is_read_only(old_path) || lvfs->is_read_only(new_path)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string old_fs = lvfs->resolve(old_path);
    std::string new_fs = lvfs->resolve(new_path);
    if (old_fs.empty() || new_fs.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    SyncReq r;
    int rc = uv_fs_rename(sync_loop(), &r.req, old_fs.c_str(), new_fs.c_str(),
                          nullptr);
    ctx.data_stack().push(Value(static_cast<bool>(rc >= 0)));
    return true;
}

// lstat-sync ( path -- array? flag )
// Returns [size, mtime_us, is_directory, is_read_only]
bool prim_lstat_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string path(hs->view());
    hs->release();

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    SyncReq r;
    int rc = uv_fs_lstat(sync_loop(), &r.req, fs_path.c_str(), nullptr);
    if (rc < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto& st = r.req.statbuf;
    bool is_dir = (st.st_mode & S_IFDIR) != 0;
    int64_t size = is_dir ? 0 : static_cast<int64_t>(st.st_size);

    // Convert timespec to microseconds since epoch
    int64_t mtime_us = static_cast<int64_t>(st.st_mtim.tv_sec) * 1'000'000LL +
                       static_cast<int64_t>(st.st_mtim.tv_nsec) / 1'000LL;

    bool is_ro = lvfs->is_read_only(path);

    auto* arr = new HeapArray();
    arr->push_back(Value(size));
    arr->push_back(Value(mtime_us));
    arr->push_back(Value(static_cast<bool>(is_dir)));
    arr->push_back(Value(static_cast<bool>(is_ro)));

    ctx.data_stack().push(make_heap_value(arr));
    ctx.data_stack().push(Value(true));
    return true;
}

// readdir-sync ( path -- array? flag )
// Returns array of strings (filenames).
bool prim_readdir_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string path(hs->view());
    hs->release();

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    SyncReq r;
    int rc = uv_fs_scandir(sync_loop(), &r.req, fs_path.c_str(), 0, nullptr);
    if (rc < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* arr = new HeapArray();
    uv_dirent_t ent;
    while (uv_fs_scandir_next(&r.req, &ent) != UV_EOF) {
        auto* name = HeapString::create(ent.name);
        name->set_tainted(true);  // Filesystem data is untrusted
        arr->push_back(make_heap_value(name));
    }

    ctx.data_stack().push(make_heap_value(arr));
    ctx.data_stack().push(Value(true));
    return true;
}

// mkdir-sync ( path -- flag )
// Creates directory and parents. Stays stdlib (recursive mkdir needs
// path component iteration; fs::create_directories() is simpler).
bool prim_mkdir_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string path(hs->view());
    hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    if (lvfs->is_read_only(path)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::error_code ec;
    fs::create_directories(fs_path, ec);
    ctx.data_stack().push(Value(static_cast<bool>(!ec)));
    return true;
}

// mkdir-tmp-sync ( prefix -- string? flag )
// Creates unique temp dir under /home using uv_fs_mkdtemp().
bool prim_mkdir_tmp_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string prefix(hs->view());
    hs->release();

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Build template: {home_fs}/{prefix}XXXXXX
    std::string home_fs = lvfs->home_dir();
    // Remove trailing slash for template construction
    if (!home_fs.empty() && home_fs.back() == '/') {
        home_fs.pop_back();
    }
    std::string tmpl = home_fs + "/" + prefix + "XXXXXX";

    SyncReq r;
    int rc = uv_fs_mkdtemp(sync_loop(), &r.req, tmpl.c_str(), nullptr);
    if (rc < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Convert filesystem path back to virtual path
    // The result starts with home_fs, so strip home_fs prefix and prepend /home
    std::string fs_result(r.req.path);
    std::string home_with_slash = lvfs->home_dir();
    if (!home_with_slash.empty() && home_with_slash.back() != '/') {
        home_with_slash += '/';
    }
    std::string virtual_path;
    if (fs_result.compare(0, home_with_slash.size(), home_with_slash) == 0) {
        virtual_path = "/home/" + fs_result.substr(home_with_slash.size());
    } else {
        // Fallback — shouldn't happen
        virtual_path = fs_result;
    }

    auto* out_str = HeapString::create(virtual_path);
    ctx.data_stack().push(make_heap_value(out_str));
    ctx.data_stack().push(Value(true));
    return true;
}

// rmdir-sync ( path -- flag )
// Removes empty directory.
bool prim_rmdir_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string path(hs->view());
    hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    if (lvfs->is_read_only(path)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Verify it's a directory
    {
        SyncReq stat_r;
        int rc = uv_fs_stat(sync_loop(), &stat_r.req, fs_path.c_str(), nullptr);
        if (rc < 0 || !(stat_r.req.statbuf.st_mode & S_IFDIR)) {
            ctx.data_stack().push(Value(false));
            return true;
        }
    }

    SyncReq r;
    int rc = uv_fs_rmdir(sync_loop(), &r.req, fs_path.c_str(), nullptr);
    ctx.data_stack().push(Value(static_cast<bool>(rc >= 0)));
    return true;
}

// rm-sync ( path -- flag )
// Removes file or directory recursively. Guards against deleting home root.
// Stays stdlib (libuv has no recursive remove).
bool prim_rm_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string path(hs->view());
    hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    if (lvfs->is_read_only(path)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Safety guard: prevent deleting home root directory itself
    std::string home = lvfs->home_dir();
    // Normalize both for comparison (remove trailing slash)
    if (!home.empty() && home.back() == '/') home.pop_back();
    std::string fs_norm = fs_path;
    if (!fs_norm.empty() && fs_norm.back() == '/') fs_norm.pop_back();
    if (fs_norm == home) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::error_code ec;
    fs::remove_all(fs_path, ec);
    ctx.data_stack().push(Value(static_cast<bool>(!ec)));
    return true;
}

// truncate-sync ( path -- flag )
// Truncates file to zero length.
bool prim_truncate_sync(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string path(hs->view());
    hs->release();

    if (!check_lvfs_modify(ctx)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    if (lvfs->is_read_only(path)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::string fs_path = lvfs->resolve(path);
    if (fs_path.empty()) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Verify regular file
    SyncReq stat_r;
    int rc = uv_fs_stat(sync_loop(), &stat_r.req, fs_path.c_str(), nullptr);
    if (rc < 0 || !(stat_r.req.statbuf.st_mode & S_IFREG)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Open
    SyncReq open_r;
    rc = uv_fs_open(sync_loop(), &open_r.req, fs_path.c_str(),
                    UV_FS_O_WRONLY, 0, nullptr);
    if (rc < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    uv_file fd = static_cast<uv_file>(rc);

    // Truncate
    SyncReq trunc_r;
    rc = uv_fs_ftruncate(sync_loop(), &trunc_r.req, fd, 0, nullptr);
    bool ok = (rc >= 0);

    // Close
    {
        SyncReq close_r;
        uv_fs_close(sync_loop(), &close_r.req, fd, nullptr);
    }

    ctx.data_stack().push(Value(static_cast<bool>(ok)));
    return true;
}

} // anonymous namespace

void register_file_io_primitives(etil::core::Dictionary& dict) {
    using TS = etil::core::TypeSignature;
    using T = TS::Type;


    dict.register_word("exists-sync",
        make_primitive("exists-sync", prim_exists_sync,
            {T::String}, {T::Integer}));

    dict.register_word("read-file-sync",
        make_primitive("read-file-sync", prim_read_file_sync,
            {T::String}, {T::String, T::Integer}));

    dict.register_word("write-file-sync",
        make_primitive("write-file-sync", prim_write_file_sync,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("append-file-sync",
        make_primitive("append-file-sync", prim_append_file_sync,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("copy-file-sync",
        make_primitive("copy-file-sync", prim_copy_file_sync,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("rename-sync",
        make_primitive("rename-sync", prim_rename_sync,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("lstat-sync",
        make_primitive("lstat-sync", prim_lstat_sync,
            {T::String}, {T::Array, T::Integer}));

    dict.register_word("readdir-sync",
        make_primitive("readdir-sync", prim_readdir_sync,
            {T::String}, {T::Array, T::Integer}));

    dict.register_word("mkdir-sync",
        make_primitive("mkdir-sync", prim_mkdir_sync,
            {T::String}, {T::Integer}));

    dict.register_word("mkdir-tmp-sync",
        make_primitive("mkdir-tmp-sync", prim_mkdir_tmp_sync,
            {T::String}, {T::String, T::Integer}));

    dict.register_word("rmdir-sync",
        make_primitive("rmdir-sync", prim_rmdir_sync,
            {T::String}, {T::Integer}));

    dict.register_word("rm-sync",
        make_primitive("rm-sync", prim_rm_sync,
            {T::String}, {T::Integer}));

    dict.register_word("truncate-sync",
        make_primitive("truncate-sync", prim_truncate_sync,
            {T::String}, {T::Integer}));
}

} // namespace etil::fileio
