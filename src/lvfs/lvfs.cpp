// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/lvfs/lvfs.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace etil::lvfs {

namespace fs = std::filesystem;

// Convert file_time_type to microseconds since the POSIX epoch.
// file_clock and system_clock may have different epochs (GCC's libstdc++
// offsets file_clock by ~204 years), so we compute the epoch difference
// once and apply it to each conversion.
int64_t Lvfs::file_time_to_us(fs::file_time_type ftime) {
    static const int64_t epoch_offset_us = []() {
        using namespace std::chrono;
        auto fn = duration_cast<microseconds>(
            fs::file_time_type::clock::now().time_since_epoch()).count();
        auto sn = duration_cast<microseconds>(
            system_clock::now().time_since_epoch()).count();
        return sn - fn;
    }();
    auto file_us = std::chrono::duration_cast<std::chrono::microseconds>(
        ftime.time_since_epoch()).count();
    return file_us + epoch_offset_us;
}

Lvfs::Lvfs(const std::string& home_dir, const std::string& library_dir)
    : cwd_("/home") {
    home_dir_ = home_dir;
    if (!home_dir_.empty() && home_dir_.back() != '/') {
        home_dir_ += '/';
    }
    library_dir_ = library_dir;
    if (!library_dir_.empty() && library_dir_.back() != '/') {
        library_dir_ += '/';
    }
}

// --- Path normalization ---

std::string Lvfs::normalize(const std::string& path) const {
    if (path.empty()) return cwd_;

    std::string working;
    if (path[0] == '/') {
        working = path;
    } else {
        // Relative: prepend CWD
        if (cwd_.back() == '/') {
            working = cwd_ + path;
        } else {
            working = cwd_ + "/" + path;
        }
    }

    // Split into components and resolve . and ..
    std::vector<std::string> parts;
    std::istringstream iss(working);
    std::string component;
    while (std::getline(iss, component, '/')) {
        if (component.empty() || component == ".") {
            continue;
        }
        if (component == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
            // Clamp at root — don't go above /
            continue;
        }
        parts.push_back(component);
    }

    if (parts.empty()) return "/";

    std::string result = "/";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += '/';
        result += parts[i];
    }
    return result;
}

std::string Lvfs::resolve_under(const std::string& base,
                                 const std::string& relative) {
    if (base.empty() || relative.empty()) return {};

    // Reject absolute paths in relative arg
    if (relative[0] == '/') return {};

    // Reject explicit traversal components
    fs::path rel(relative);
    for (const auto& component : rel) {
        if (component == "..") return {};
    }

    // Join and normalize (lexically, no filesystem access)
    fs::path joined = fs::path(base) / rel;
    fs::path canonical = joined.lexically_normal();

    // Verify the result stays under base
    std::string result = canonical.string();
    if (result.compare(0, base.size(), base) != 0) {
        return {};
    }

    return result;
}

std::string Lvfs::map_to_filesystem(const std::string& normalized) const {
    static const std::string home_prefix = "/home";
    static const std::string library_prefix = "/library";

    if (normalized == "/") {
        return {};  // Virtual root — no single fs path
    }

    // /home or /home/...
    if (normalized == home_prefix || normalized.compare(0, home_prefix.size() + 1, home_prefix + "/") == 0) {
        if (home_dir_.empty()) return {};
        if (normalized == home_prefix) {
            return home_dir_;
        }
        std::string rel = normalized.substr(home_prefix.size() + 1);  // after "/home/"
        return resolve_under(home_dir_, rel);
    }

    // /library or /library/...
    if (normalized == library_prefix || normalized.compare(0, library_prefix.size() + 1, library_prefix + "/") == 0) {
        if (library_dir_.empty()) return {};
        if (normalized == library_prefix) {
            return library_dir_;
        }
        std::string rel = normalized.substr(library_prefix.size() + 1);  // after "/library/"
        return resolve_under(library_dir_, rel);
    }

    return {};  // Unknown virtual path
}

// --- Public API ---

std::string Lvfs::resolve(const std::string& path) const {
    std::string norm = normalize(path);
    return map_to_filesystem(norm);
}

std::string Lvfs::resolve_home_path(const std::string& relative) const {
    if (home_dir_.empty()) {
        return relative;  // Backward compat
    }
    return resolve_under(home_dir_, relative);
}

std::string Lvfs::resolve_library_path(const std::string& relative) const {
    if (library_dir_.empty()) {
        return {};
    }
    return resolve_under(library_dir_, relative);
}

std::string Lvfs::resolve_logical_path(const std::string& path) const {
    static const std::string home_prefix = "/home/";
    static const std::string library_prefix = "/library/";

    if (path.compare(0, home_prefix.size(), home_prefix) == 0) {
        return resolve_home_path(path.substr(home_prefix.size()));
    }
    if (path.compare(0, library_prefix.size(), library_prefix) == 0) {
        return resolve_library_path(path.substr(library_prefix.size()));
    }

    // No logical prefix — return as-is (startup files, etc.)
    return path;
}

bool Lvfs::cd(const std::string& path) {
    if (path.empty()) {
        cwd_ = "/home";
        return true;
    }

    std::string norm = normalize(path);

    // Root is always valid
    if (norm == "/") {
        cwd_ = "/";
        return true;
    }

    // /home and /library are virtual dirs that always exist
    if (norm == "/home" || norm == "/library") {
        cwd_ = norm;
        return true;
    }

    // For real paths, verify the directory exists on the filesystem
    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return false;

    std::error_code ec;
    if (!fs::is_directory(fs_path, ec)) return false;

    cwd_ = norm;
    return true;
}

std::vector<LvfsEntry> Lvfs::root_entries() const {
    std::vector<LvfsEntry> entries;
    if (!home_dir_.empty()) {
        entries.push_back({"home", true, 0, 0});
    }
    if (!library_dir_.empty()) {
        entries.push_back({"library", true, 0, 0});
    }
    return entries;
}

std::optional<std::vector<LvfsEntry>> Lvfs::list_dir(const std::string& path) const {
    std::string norm = normalize(path);

    // Virtual root
    if (norm == "/") {
        return root_entries();
    }

    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return std::nullopt;

    std::error_code ec;
    if (!fs::is_directory(fs_path, ec)) return std::nullopt;

    std::vector<LvfsEntry> entries;
    for (const auto& entry : fs::directory_iterator(fs_path, ec)) {
        LvfsEntry e;
        e.name = entry.path().filename().string();
        e.is_directory = entry.is_directory(ec);
        e.size = e.is_directory ? 0 : entry.file_size(ec);

        auto ftime = entry.last_write_time(ec);
        if (!ec) {
            e.mtime_us = Lvfs::file_time_to_us(ftime);
        } else {
            e.mtime_us = 0;
        }

        entries.push_back(std::move(e));
    }

    return entries;
}

void Lvfs::list_dir_recursive_impl(const std::string& fs_path,
                                    const std::string& prefix,
                                    std::vector<LvfsEntry>& out) const {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs_path, ec)) {
        LvfsEntry e;
        std::string name = entry.path().filename().string();
        e.name = prefix.empty() ? name : prefix + "/" + name;
        e.is_directory = entry.is_directory(ec);
        e.size = e.is_directory ? 0 : entry.file_size(ec);

        auto ftime = entry.last_write_time(ec);
        if (!ec) {
            e.mtime_us = Lvfs::file_time_to_us(ftime);
        } else {
            e.mtime_us = 0;
        }

        out.push_back(e);

        if (e.is_directory) {
            list_dir_recursive_impl(entry.path().string(), e.name, out);
        }
    }
}

std::optional<std::vector<LvfsEntry>> Lvfs::list_dir_recursive(const std::string& path) const {
    std::string norm = normalize(path);

    // Recursive listing of root: list both home and library recursively
    if (norm == "/") {
        std::vector<LvfsEntry> entries;
        if (!home_dir_.empty()) {
            entries.push_back({"home", true, 0, 0});
            list_dir_recursive_impl(home_dir_, "home", entries);
        }
        if (!library_dir_.empty()) {
            entries.push_back({"library", true, 0, 0});
            list_dir_recursive_impl(library_dir_, "library", entries);
        }
        return entries;
    }

    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return std::nullopt;

    std::error_code ec;
    if (!fs::is_directory(fs_path, ec)) return std::nullopt;

    std::vector<LvfsEntry> entries;
    list_dir_recursive_impl(fs_path, "", entries);
    return entries;
}

std::optional<std::string> Lvfs::read_file(const std::string& path) const {
    std::string norm = normalize(path);
    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return std::nullopt;

    std::error_code ec;
    if (!fs::is_regular_file(fs_path, ec)) return std::nullopt;

    std::ifstream ifs(fs_path, std::ios::in);
    if (!ifs.is_open()) return std::nullopt;

    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool Lvfs::is_read_only(const std::string& path) const {
    std::string norm = normalize(path);
    return norm.compare(0, 8, "/library") == 0;
}

uint64_t Lvfs::home_usage_bytes() const {
    if (home_dir_.empty()) return 0;
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(home_dir_, ec)) {
        if (entry.is_regular_file(ec)) {
            total += entry.file_size(ec);
        }
    }
    return total;
}

bool Lvfs::write_file(const std::string& path, const std::string& content) {
    std::string norm = normalize(path);
    if (is_read_only(norm)) return false;
    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return false;

    // Create parent directories
    fs::path p(fs_path);
    std::error_code ec;
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
        if (ec) return false;
    }

    std::ofstream ofs(fs_path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << content;
    ofs.close();

    notify_write();
    return true;
}

bool Lvfs::append_file(const std::string& path, const std::string& content) {
    std::string norm = normalize(path);
    if (is_read_only(norm)) return false;
    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return false;

    std::ofstream ofs(fs_path, std::ios::out | std::ios::app);
    if (!ofs.is_open()) return false;
    ofs << content;
    ofs.close();

    notify_write();
    return true;
}

bool Lvfs::make_dir(const std::string& path) {
    std::string norm = normalize(path);
    if (is_read_only(norm)) return false;
    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return false;

    std::error_code ec;
    fs::create_directories(fs_path, ec);
    if (ec) return false;

    notify_write();
    return true;
}

bool Lvfs::remove_file(const std::string& path) {
    std::string norm = normalize(path);
    if (is_read_only(norm)) return false;
    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return false;

    std::error_code ec;
    if (!fs::is_regular_file(fs_path, ec)) return false;
    if (!fs::remove(fs_path, ec)) return false;

    notify_write();
    return true;
}

bool Lvfs::remove_dir(const std::string& path) {
    std::string norm = normalize(path);
    if (is_read_only(norm)) return false;
    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return false;

    std::error_code ec;
    auto count = fs::remove_all(fs_path, ec);
    if (ec || count == 0) return false;

    notify_write();
    return true;
}

bool Lvfs::exists(const std::string& path) const {
    std::string norm = normalize(path);
    std::string fs_path = map_to_filesystem(norm);
    if (fs_path.empty()) return norm == "/" || norm == "/home" || norm == "/library";

    std::error_code ec;
    return fs::exists(fs_path, ec);
}

} // namespace etil::lvfs
