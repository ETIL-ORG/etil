// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/fileio/async_file_io.hpp"
#include "etil/fileio/uv_session.hpp"
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
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// exists? ( path -- flag )
// ---------------------------------------------------------------------------
bool prim_exists(ExecutionContext& ctx) {
    std::string path, fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve(ctx, path, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::error_code ec;
        bool exists = fs::exists(fs_path, ec);
        ctx.data_stack().push(Value(static_cast<bool>(exists)));
        return true;
    }

    FsRequest req;
    uv_fs_stat(uv->loop(), &req.req, fs_path.c_str(), FsRequest::on_complete);
    if (!uv->await_completion(ctx, req)) return false;

    // UV_ENOENT means file doesn't exist — not an error for exists?
    bool exists = (req.result >= 0);
    ctx.data_stack().push(Value(static_cast<bool>(exists)));
    return true;
}

// ---------------------------------------------------------------------------
// read-file ( path -- string? flag )
// ---------------------------------------------------------------------------
bool prim_read_file(ExecutionContext& ctx) {
    std::string path, fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve(ctx, path, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::error_code ec;
        if (!fs::is_regular_file(fs_path, ec)) {
            ctx.data_stack().push(Value(false));
            return true;
        }
        std::ifstream ifs(fs_path, std::ios::in | std::ios::binary);
        if (!ifs.is_open()) {
            ctx.data_stack().push(Value(false));
            return true;
        }
        std::ostringstream oss;
        oss << ifs.rdbuf();
        auto* result = HeapString::create(oss.str());
        result->set_tainted(true);  // File data is untrusted
        ctx.data_stack().push(make_heap_value(result));
        ctx.data_stack().push(Value(true));
        return true;
    }

    // Step 1: stat to get file size and verify it's a regular file
    FsRequest stat_req;
    uv_fs_stat(uv->loop(), &stat_req.req, fs_path.c_str(), FsRequest::on_complete);
    if (!uv->await_completion(ctx, stat_req)) return false;

    if (stat_req.result < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto& st = stat_req.req.statbuf;
    if (!(st.st_mode & S_IFREG)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    size_t file_size = static_cast<size_t>(st.st_size);

    // Step 2: open
    FsRequest open_req;
    uv_fs_open(uv->loop(), &open_req.req, fs_path.c_str(),
               UV_FS_O_RDONLY, 0, FsRequest::on_complete);
    if (!uv->await_completion(ctx, open_req)) return false;

    if (open_req.result < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    uv_file fd = static_cast<uv_file>(open_req.result);

    // Step 3: read (handle empty files)
    std::string content;
    if (file_size > 0) {
        content.resize(file_size);
        uv_buf_t buf = uv_buf_init(content.data(), static_cast<unsigned int>(file_size));

        FsRequest read_req;
        uv_fs_read(uv->loop(), &read_req.req, fd, &buf, 1, 0,
                    FsRequest::on_complete);
        if (!uv->await_completion(ctx, read_req)) {
            // Clean up fd on cancellation
            uv_fs_t close_req;
            uv_fs_close(uv->loop(), &close_req, fd, nullptr);
            uv_fs_req_cleanup(&close_req);
            return false;
        }

        if (read_req.result < 0) {
            uv_fs_t close_req;
            uv_fs_close(uv->loop(), &close_req, fd, nullptr);
            uv_fs_req_cleanup(&close_req);
            ctx.data_stack().push(Value(false));
            return true;
        }

        // Adjust size if we read less than expected
        content.resize(static_cast<size_t>(read_req.result));
    }

    // Step 4: close (sync is fine)
    {
        uv_fs_t close_req;
        uv_fs_close(uv->loop(), &close_req, fd, nullptr);
        uv_fs_req_cleanup(&close_req);
    }

    auto* result = HeapString::create(content);
    result->set_tainted(true);  // File data is untrusted
    ctx.data_stack().push(make_heap_value(result));
    ctx.data_stack().push(Value(true));
    return true;
}

// ---------------------------------------------------------------------------
// Helper: async write/append with open flags
// ---------------------------------------------------------------------------
bool write_file_impl(ExecutionContext& ctx, int flags) {
    std::string content, fs_path;
    auto s = pop_write_args(ctx, content, fs_path);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::ofstream ofs(fs_path, (flags & UV_FS_O_APPEND)
            ? (std::ios::out | std::ios::app | std::ios::binary)
            : (std::ios::out | std::ios::trunc | std::ios::binary));
        if (!ofs.is_open()) {
            ctx.data_stack().push(Value(false));
            return true;
        }
        ofs << content;
        ctx.data_stack().push(Value(static_cast<bool>(ofs.good())));
        return true;
    }

    // Open
    FsRequest open_req;
    uv_fs_open(uv->loop(), &open_req.req, fs_path.c_str(),
               flags, 0644, FsRequest::on_complete);
    if (!uv->await_completion(ctx, open_req)) return false;

    if (open_req.result < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    uv_file fd = static_cast<uv_file>(open_req.result);

    // Write
    if (!content.empty()) {
        uv_buf_t buf = uv_buf_init(const_cast<char*>(content.data()),
                                    static_cast<unsigned int>(content.size()));
        FsRequest write_req;
        uv_fs_write(uv->loop(), &write_req.req, fd, &buf, 1,
                     (flags & UV_FS_O_APPEND) ? -1 : 0,
                     FsRequest::on_complete);
        if (!uv->await_completion(ctx, write_req)) {
            uv_fs_t close_req;
            uv_fs_close(uv->loop(), &close_req, fd, nullptr);
            uv_fs_req_cleanup(&close_req);
            return false;
        }

        if (write_req.result < 0) {
            uv_fs_t close_req;
            uv_fs_close(uv->loop(), &close_req, fd, nullptr);
            uv_fs_req_cleanup(&close_req);
            ctx.data_stack().push(Value(false));
            return true;
        }
    }

    // Close
    {
        uv_fs_t close_req;
        uv_fs_close(uv->loop(), &close_req, fd, nullptr);
        uv_fs_req_cleanup(&close_req);
    }

    ctx.data_stack().push(Value(true));
    return true;
}

// ---------------------------------------------------------------------------
// write-file ( string path -- flag )
// ---------------------------------------------------------------------------
bool prim_write_file(ExecutionContext& ctx) {
    return write_file_impl(ctx, UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC);
}

// ---------------------------------------------------------------------------
// append-file ( string path -- flag )
// ---------------------------------------------------------------------------
bool prim_append_file(ExecutionContext& ctx) {
    return write_file_impl(ctx, UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND);
}

// ---------------------------------------------------------------------------
// copy-file ( src dest -- flag )
// ---------------------------------------------------------------------------
bool prim_copy_file(ExecutionContext& ctx) {
    std::string src_fs, dest_fs;
    auto s = pop_two_paths_copy(ctx, src_fs, dest_fs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::error_code ec;
        bool ok = fs::copy_file(src_fs, dest_fs,
                                fs::copy_options::overwrite_existing, ec);
        ctx.data_stack().push(Value(static_cast<bool>(ok && !ec)));
        return true;
    }

    FsRequest req;
    uv_fs_copyfile(uv->loop(), &req.req, src_fs.c_str(), dest_fs.c_str(),
                   UV_FS_COPYFILE_FICLONE, FsRequest::on_complete);
    if (!uv->await_completion(ctx, req)) return false;

    ctx.data_stack().push(Value(static_cast<bool>(req.result >= 0)));
    return true;
}

// ---------------------------------------------------------------------------
// rename-file ( old new -- flag )
// ---------------------------------------------------------------------------
bool prim_rename_file(ExecutionContext& ctx) {
    std::string old_fs, new_fs;
    auto s = pop_two_paths_rename(ctx, old_fs, new_fs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::error_code ec;
        fs::rename(old_fs, new_fs, ec);
        ctx.data_stack().push(Value(static_cast<bool>(!ec)));
        return true;
    }

    FsRequest req;
    uv_fs_rename(uv->loop(), &req.req, old_fs.c_str(), new_fs.c_str(),
                 FsRequest::on_complete);
    if (!uv->await_completion(ctx, req)) return false;

    ctx.data_stack().push(Value(static_cast<bool>(req.result >= 0)));
    return true;
}

// ---------------------------------------------------------------------------
// lstat ( path -- array? flag )
// Returns [size, mtime_us, is_directory, is_read_only]
// ---------------------------------------------------------------------------
bool prim_lstat(ExecutionContext& ctx) {
    std::string path, fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve(ctx, path, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::error_code ec;
        auto status = fs::status(fs_path, ec);
        if (ec || !fs::exists(status)) {
            ctx.data_stack().push(Value(false));
            return true;
        }
        bool is_dir = fs::is_directory(status);
        int64_t size = 0;
        if (!is_dir) {
            size = static_cast<int64_t>(fs::file_size(fs_path, ec));
            if (ec) size = 0;
        }
        int64_t mtime_us = 0;
        auto ftime = fs::last_write_time(fs_path, ec);
        if (!ec) mtime_us = Lvfs::file_time_to_us(ftime);
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

    FsRequest req;
    uv_fs_lstat(uv->loop(), &req.req, fs_path.c_str(), FsRequest::on_complete);
    if (!uv->await_completion(ctx, req)) return false;

    if (req.result < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    ctx.data_stack().push(make_heap_value(
        make_stat_array(req.req.statbuf, lvfs->is_read_only(path))));
    ctx.data_stack().push(Value(true));
    return true;
}

// ---------------------------------------------------------------------------
// readdir ( path -- array? flag )
// Returns array of filename strings.
// ---------------------------------------------------------------------------
bool prim_readdir(ExecutionContext& ctx) {
    std::string path, fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve(ctx, path, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::error_code ec;
        if (!fs::is_directory(fs_path, ec)) {
            ctx.data_stack().push(Value(false));
            return true;
        }
        auto* arr = new HeapArray();
        for (const auto& entry : fs::directory_iterator(fs_path, ec)) {
            auto* name = HeapString::create(entry.path().filename().string());
            name->set_tainted(true);  // Filesystem data is untrusted
            arr->push_back(make_heap_value(name));
        }
        ctx.data_stack().push(make_heap_value(arr));
        ctx.data_stack().push(Value(true));
        return true;
    }

    FsRequest req;
    uv_fs_scandir(uv->loop(), &req.req, fs_path.c_str(), 0,
                  FsRequest::on_complete);
    if (!uv->await_completion(ctx, req)) return false;

    if (req.result < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* arr = new HeapArray();
    uv_dirent_t ent;
    while (uv_fs_scandir_next(&req.req, &ent) != UV_EOF) {
        auto* name = HeapString::create(ent.name);
        name->set_tainted(true);  // Filesystem data is untrusted
        arr->push_back(make_heap_value(name));
    }

    ctx.data_stack().push(make_heap_value(arr));
    ctx.data_stack().push(Value(true));
    return true;
}

// ---------------------------------------------------------------------------
// mkdir ( path -- flag )
// Creates directory and parents.
// ---------------------------------------------------------------------------
bool prim_mkdir(ExecutionContext& ctx) {
    std::string fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve_writable(ctx, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    // libuv's uv_fs_mkdir only creates one level.
    // Use std::filesystem for recursive creation.
    std::error_code ec;
    fs::create_directories(fs_path, ec);
    ctx.data_stack().push(Value(static_cast<bool>(!ec)));
    return true;
}

// ---------------------------------------------------------------------------
// mkdir-tmp ( prefix -- string? flag )
// Creates unique temp dir under /home using mkdtemp().
// ---------------------------------------------------------------------------
bool prim_mkdir_tmp(ExecutionContext& ctx) {
    auto* hs = pop_string(ctx);
    if (!hs) return false;
    std::string prefix(hs->view());
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

    std::string home_fs = lvfs->home_dir();
    if (!home_fs.empty() && home_fs.back() == '/') {
        home_fs.pop_back();
    }
    std::string tmpl = home_fs + "/" + prefix + "XXXXXX";

    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        char* result = mkdtemp(buf.data());
        if (!result) {
            ctx.data_stack().push(Value(false));
            return true;
        }
        auto* out_str = HeapString::create(
            fs_path_to_virtual(std::string(result), lvfs->home_dir()));
        ctx.data_stack().push(make_heap_value(out_str));
        ctx.data_stack().push(Value(true));
        return true;
    }

    FsRequest req;
    uv_fs_mkdtemp(uv->loop(), &req.req, buf.data(), FsRequest::on_complete);
    if (!uv->await_completion(ctx, req)) return false;

    if (req.result < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    auto* out_str = HeapString::create(
        fs_path_to_virtual(std::string(req.req.path), lvfs->home_dir()));
    ctx.data_stack().push(make_heap_value(out_str));
    ctx.data_stack().push(Value(true));
    return true;
}

// ---------------------------------------------------------------------------
// rmdir ( path -- flag )
// Removes empty directory.
// ---------------------------------------------------------------------------
bool prim_rmdir(ExecutionContext& ctx) {
    std::string fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve_writable(ctx, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::error_code ec;
        if (!fs::is_directory(fs_path, ec)) {
            ctx.data_stack().push(Value(false));
            return true;
        }
        bool ok = fs::remove(fs_path, ec);
        ctx.data_stack().push(Value(static_cast<bool>(ok && !ec)));
        return true;
    }

    // Verify it's a directory first
    {
        std::error_code ec;
        if (!fs::is_directory(fs_path, ec)) {
            ctx.data_stack().push(Value(false));
            return true;
        }
    }

    FsRequest req;
    uv_fs_rmdir(uv->loop(), &req.req, fs_path.c_str(), FsRequest::on_complete);
    if (!uv->await_completion(ctx, req)) return false;

    ctx.data_stack().push(Value(static_cast<bool>(req.result >= 0)));
    return true;
}

// ---------------------------------------------------------------------------
// rm ( path -- flag )
// Removes file or directory recursively. Guards against deleting home root.
// ---------------------------------------------------------------------------
bool prim_rm(ExecutionContext& ctx) {
    std::string fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve_writable(ctx, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    if (is_home_root(fs_path, lvfs)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Recursive removal is a metadata-heavy operation. libuv doesn't have
    // a recursive remove. Use std::filesystem::remove_all which is efficient.
    std::error_code ec;
    fs::remove_all(fs_path, ec);
    ctx.data_stack().push(Value(static_cast<bool>(!ec)));
    return true;
}

// ---------------------------------------------------------------------------
// truncate ( path -- flag )
// Truncates file to zero length.
// ---------------------------------------------------------------------------
bool prim_truncate(ExecutionContext& ctx) {
    std::string fs_path;
    Lvfs* lvfs = nullptr;
    auto s = pop_and_resolve_writable(ctx, fs_path, lvfs);
    if (s == Status::Underflow) return false;
    if (s == Status::PushedFalse) return true;

    auto* uv = ctx.uv_session();
    if (!uv) {
        // Fallback to sync
        std::error_code ec;
        if (!fs::is_regular_file(fs_path, ec)) {
            ctx.data_stack().push(Value(false));
            return true;
        }
        fs::resize_file(fs_path, 0, ec);
        ctx.data_stack().push(Value(static_cast<bool>(!ec)));
        return true;
    }

    // Stat first to verify regular file
    FsRequest stat_req;
    uv_fs_stat(uv->loop(), &stat_req.req, fs_path.c_str(), FsRequest::on_complete);
    if (!uv->await_completion(ctx, stat_req)) return false;

    if (stat_req.result < 0 || !(stat_req.req.statbuf.st_mode & S_IFREG)) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    // Open, then ftruncate, then close
    FsRequest open_req;
    uv_fs_open(uv->loop(), &open_req.req, fs_path.c_str(),
               UV_FS_O_WRONLY, 0, FsRequest::on_complete);
    if (!uv->await_completion(ctx, open_req)) return false;

    if (open_req.result < 0) {
        ctx.data_stack().push(Value(false));
        return true;
    }

    uv_file fd = static_cast<uv_file>(open_req.result);

    FsRequest trunc_req;
    uv_fs_ftruncate(uv->loop(), &trunc_req.req, fd, 0, FsRequest::on_complete);
    if (!uv->await_completion(ctx, trunc_req)) {
        uv_fs_t close_req;
        uv_fs_close(uv->loop(), &close_req, fd, nullptr);
        uv_fs_req_cleanup(&close_req);
        return false;
    }

    bool ok = (trunc_req.result >= 0);

    {
        uv_fs_t close_req;
        uv_fs_close(uv->loop(), &close_req, fd, nullptr);
        uv_fs_req_cleanup(&close_req);
    }

    ctx.data_stack().push(Value(static_cast<bool>(ok)));
    return true;
}

} // anonymous namespace

void register_async_file_io_primitives(etil::core::Dictionary& dict) {
    using TS = etil::core::TypeSignature;
    using T = TS::Type;


    dict.register_word("exists?",
        make_primitive("exists?", prim_exists,
            {T::String}, {T::Integer}));

    dict.register_word("read-file",
        make_primitive("read-file", prim_read_file,
            {T::String}, {T::String, T::Integer}));

    dict.register_word("write-file",
        make_primitive("write-file", prim_write_file,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("append-file",
        make_primitive("append-file", prim_append_file,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("copy-file",
        make_primitive("copy-file", prim_copy_file,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("rename-file",
        make_primitive("rename-file", prim_rename_file,
            {T::String, T::String}, {T::Integer}));

    dict.register_word("lstat",
        make_primitive("lstat", prim_lstat,
            {T::String}, {T::Array, T::Integer}));

    dict.register_word("readdir",
        make_primitive("readdir", prim_readdir,
            {T::String}, {T::Array, T::Integer}));

    dict.register_word("mkdir",
        make_primitive("mkdir", prim_mkdir,
            {T::String}, {T::Integer}));

    dict.register_word("mkdir-tmp",
        make_primitive("mkdir-tmp", prim_mkdir_tmp,
            {T::String}, {T::String, T::Integer}));

    dict.register_word("rmdir",
        make_primitive("rmdir", prim_rmdir,
            {T::String}, {T::Integer}));

    dict.register_word("rm",
        make_primitive("rm", prim_rm,
            {T::String}, {T::Integer}));

    dict.register_word("truncate",
        make_primitive("truncate", prim_truncate,
            {T::String}, {T::Integer}));
}

} // namespace etil::fileio
