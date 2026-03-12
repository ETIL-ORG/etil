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
#include "file_io_helpers.hpp"

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
using namespace helpers;

namespace {


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
    std::string path, fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve(ctx, path, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    SyncReq r;
    int rc = uv_fs_stat(sync_loop(), &r.req, fs_path.c_str(), nullptr);
    ctx.data_stack().push(Value(static_cast<bool>(rc >= 0)));
    return true;
}

// read-file-sync ( path -- string? flag )
bool prim_read_file_sync(ExecutionContext& ctx) {
    std::string path, fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve(ctx, path, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

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
    std::string content, fs_path;
    auto s = pop_write_args(ctx, content, fs_path);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

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
bool prim_copy_file_sync(ExecutionContext& ctx) {
    std::string src_fs, dest_fs;
    auto s = pop_two_paths_copy(ctx, src_fs, dest_fs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    SyncReq r;
    int rc = uv_fs_copyfile(sync_loop(), &r.req, src_fs.c_str(), dest_fs.c_str(),
                            UV_FS_COPYFILE_FICLONE, nullptr);
    ctx.data_stack().push(Value(static_cast<bool>(rc >= 0)));
    return true;
}

// rename-sync ( old new -- flag )
bool prim_rename_sync(ExecutionContext& ctx) {
    std::string old_fs, new_fs;
    auto s = pop_two_paths_rename(ctx, old_fs, new_fs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    SyncReq r;
    int rc = uv_fs_rename(sync_loop(), &r.req, old_fs.c_str(), new_fs.c_str(),
                          nullptr);
    ctx.data_stack().push(Value(static_cast<bool>(rc >= 0)));
    return true;
}

// lstat-sync ( path -- array? flag )
bool prim_lstat_sync(ExecutionContext& ctx) {
    std::string path, fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve(ctx, path, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    SyncReq r;
    int rc = uv_fs_lstat(sync_loop(), &r.req, fs_path.c_str(), nullptr);
    if (rc < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    ctx.data_stack().push(make_heap_value(
        make_stat_array(r.req.statbuf, lvfs->is_read_only(path))));
    ctx.data_stack().push(Value(true));
    return true;
}

// readdir-sync ( path -- array? flag )
bool prim_readdir_sync(ExecutionContext& ctx) {
    std::string path, fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve(ctx, path, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

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
        name->set_tainted(true);
        arr->push_back(make_heap_value(name));
    }

    ctx.data_stack().push(make_heap_value(arr));
    ctx.data_stack().push(Value(true));
    return true;
}

// mkdir-sync ( path -- flag )
bool prim_mkdir_sync(ExecutionContext& ctx) {
    std::string fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve_writable(ctx, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    std::error_code ec;
    fs::create_directories(fs_path, ec);
    ctx.data_stack().push(Value(static_cast<bool>(!ec)));
    return true;
}

// mkdir-tmp-sync ( prefix -- string? flag )
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

    std::string home_fs = lvfs->home_dir();
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

    auto* out_str = HeapString::create(
        fs_path_to_virtual(std::string(r.req.path), lvfs->home_dir()));
    ctx.data_stack().push(make_heap_value(out_str));
    ctx.data_stack().push(Value(true));
    return true;
}

// rmdir-sync ( path -- flag )
bool prim_rmdir_sync(ExecutionContext& ctx) {
    std::string fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve_writable(ctx, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

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
bool prim_rm_sync(ExecutionContext& ctx) {
    std::string fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve_writable(ctx, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    if (is_home_root(fs_path, lvfs)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    std::error_code ec;
    fs::remove_all(fs_path, ec);
    ctx.data_stack().push(Value(static_cast<bool>(!ec)));
    return true;
}

// truncate-sync ( path -- flag )
bool prim_truncate_sync(ExecutionContext& ctx) {
    std::string fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve_writable(ctx, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

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
