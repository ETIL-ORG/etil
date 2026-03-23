#pragma once

// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause


#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace etil::core { class Dictionary; }

namespace etil::lvfs {

/// Register LVFS primitives (cwd, cd, ls, ll, lr, cat) into the dictionary.
void register_lvfs_primitives(etil::core::Dictionary& dict);

/// Entry returned by directory listing operations.
struct LvfsEntry {
    std::string name;
    bool is_directory;
    size_t size;        // 0 for directories
    int64_t mtime_us;   // microseconds since epoch
};

/// Little Virtual File System — unified virtual filesystem with
/// /home (writable, per-session) and /library (read-only, shared).
///
/// A per-session CWD (default /home) enables relative path navigation.
class Lvfs {
public:
    Lvfs(const std::string& home_dir, const std::string& library_dir);

    /// CWD-aware resolution: relative paths resolve against CWD,
    /// then maps to the real filesystem path.
    std::string resolve(const std::string& path) const;

    /// Backward-compatible wrappers (existing include/library/file-load callers).
    std::string resolve_home_path(const std::string& relative) const;
    std::string resolve_library_path(const std::string& relative) const;
    std::string resolve_logical_path(const std::string& path) const;

    /// Navigation: get current working directory (virtual path).
    const std::string& cwd() const { return cwd_; }

    /// Change CWD to path (relative or absolute virtual path).
    /// No arg or empty string resets to /home.
    /// Returns true if target exists and is a directory.
    bool cd(const std::string& path);

    /// List directory contents. Returns nullopt if path is not a directory.
    std::optional<std::vector<LvfsEntry>> list_dir(const std::string& path) const;

    /// Recursive directory listing. Returns nullopt if path is not a directory.
    std::optional<std::vector<LvfsEntry>> list_dir_recursive(const std::string& path) const;

    /// Read entire file contents. Returns nullopt if file cannot be read.
    std::optional<std::string> read_file(const std::string& path) const;

    /// Write entire file contents. Creates parent directories as needed.
    /// Returns false if path is read-only or write fails.
    bool write_file(const std::string& path, const std::string& content);

    /// Append to file. Creates the file if it doesn't exist.
    bool append_file(const std::string& path, const std::string& content);

    /// Create a directory (and parents). Returns false if read-only or fails.
    bool make_dir(const std::string& path);

    /// Remove a file. Returns false if read-only, not found, or is a directory.
    bool remove_file(const std::string& path);

    /// Remove a directory recursively. Returns false if read-only or fails.
    bool remove_dir(const std::string& path);

    /// Check if a path exists.
    bool exists(const std::string& path) const;

    /// True if the resolved path is under /library (read-only).
    bool is_read_only(const std::string& path) const;

    // ---- Write callback (Separation of Concerns) ----
    // After any filesystem mutation (write, append, mkdir, rm),
    // the registered callback is invoked. The JS layer hooks this
    // to trigger FS.syncfs() for IDBFS persistence.

    using WriteCallback = std::function<void()>;

    /// Register a callback invoked after every write operation.
    void set_write_callback(WriteCallback cb) { write_callback_ = std::move(cb); }

    const std::string& home_dir() const { return home_dir_; }
    const std::string& library_dir() const { return library_dir_; }

    /// Sum of file sizes under home_dir_ (for disk quota enforcement).
    /// O(n) in file count but session dirs are small.
    uint64_t home_usage_bytes() const;

    /// Convert file_time_type to microseconds since the POSIX epoch.
    static int64_t file_time_to_us(std::filesystem::file_time_type ftime);

private:
    std::string home_dir_;     // Real filesystem path (with trailing /)
    std::string library_dir_;  // Real filesystem path (with trailing /)
    std::string cwd_;          // Virtual CWD, defaults to "/home"
    WriteCallback write_callback_;  // Called after every write operation

    /// Invoke write callback if registered.
    void notify_write() { if (write_callback_) write_callback_(); }

    /// Normalize a virtual path: prepend CWD if relative, process . and ..
    std::string normalize(const std::string& path) const;

    /// Join base + relative, verify result stays under base.
    /// Returns empty string on traversal violation.
    static std::string resolve_under(const std::string& base,
                                     const std::string& relative);

    /// Map a normalized virtual path to a real filesystem path.
    /// Returns empty string for the virtual root (no single fs path).
    std::string map_to_filesystem(const std::string& normalized) const;

    /// Synthesize root directory entries (/home, /library).
    std::vector<LvfsEntry> root_entries() const;

    /// Recursive listing helper.
    void list_dir_recursive_impl(const std::string& fs_path,
                                 const std::string& prefix,
                                 std::vector<LvfsEntry>& out) const;
};

} // namespace etil::lvfs
