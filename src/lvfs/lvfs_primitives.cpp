// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/lvfs/lvfs.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/word_impl.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace etil::lvfs {

namespace {

// Format microseconds-since-epoch as "YYYY-MM-DD HH:MM:SS"
std::string format_mtime(int64_t mtime_us) {
    time_t secs = static_cast<time_t>(mtime_us / 1'000'000);
    struct tm tm{};
    gmtime_r(&secs, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// Format file size right-justified in a fixed-width field
std::string format_size(size_t size) {
    std::ostringstream oss;
    oss << std::setw(8) << std::right << size;
    return oss.str();
}

// cwd ( -- ) — print current working directory
bool prim_cwd(etil::core::ExecutionContext& ctx) {
    auto* lvfs = ctx.lvfs();
    auto& os = ctx.out();
    if (!lvfs) {
        os << "/\n";
        return true;
    }
    os << lvfs->cwd() << "\n";
    return true;
}

// cd [path] — change working directory; no arg resets to /home
bool prim_cd(etil::core::ExecutionContext& ctx) {
    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.err() << "Error: LVFS not available\n";
        return false;
    }

    std::string path;
    auto* is = ctx.input_stream();
    if (is) *is >> path;

    if (!lvfs->cd(path)) {
        ctx.err() << "Error: cannot cd to '" << path << "'\n";
        return false;
    }
    return true;
}

// ls [path] — list directory; no arg lists CWD. Dirs get trailing /
bool prim_ls(etil::core::ExecutionContext& ctx) {
    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.err() << "Error: LVFS not available\n";
        return false;
    }

    std::string path;
    auto* is = ctx.input_stream();
    if (is) *is >> path;
    if (path.empty()) path = lvfs->cwd();

    auto entries = lvfs->list_dir(path);
    if (!entries) {
        ctx.err() << "Error: cannot list '" << path << "'\n";
        return false;
    }

    // Sort alphabetically
    auto& vec = *entries;
    std::sort(vec.begin(), vec.end(), [](const LvfsEntry& a, const LvfsEntry& b) {
        return a.name < b.name;
    });

    auto& os = ctx.out();
    for (const auto& e : vec) {
        os << e.name;
        if (e.is_directory) os << '/';
        os << '\n';
    }
    return true;
}

// ll [path] — long format listing, sorted by mtime descending
bool prim_ll(etil::core::ExecutionContext& ctx) {
    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.err() << "Error: LVFS not available\n";
        return false;
    }

    std::string path;
    auto* is = ctx.input_stream();
    if (is) *is >> path;
    if (path.empty()) path = lvfs->cwd();

    auto entries = lvfs->list_dir(path);
    if (!entries) {
        ctx.err() << "Error: cannot list '" << path << "'\n";
        return false;
    }

    // Sort by mtime descending
    auto& vec = *entries;
    std::sort(vec.begin(), vec.end(), [](const LvfsEntry& a, const LvfsEntry& b) {
        return a.mtime_us > b.mtime_us;
    });

    auto& os = ctx.out();
    for (const auto& e : vec) {
        os << format_size(e.size) << "  "
           << format_mtime(e.mtime_us) << "  "
           << e.name;
        if (e.is_directory) os << '/';
        os << '\n';
    }
    return true;
}

// lr [path] — recursive long format listing, sorted by mtime descending
bool prim_lr(etil::core::ExecutionContext& ctx) {
    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.err() << "Error: LVFS not available\n";
        return false;
    }

    std::string path;
    auto* is = ctx.input_stream();
    if (is) *is >> path;
    if (path.empty()) path = lvfs->cwd();

    auto entries = lvfs->list_dir_recursive(path);
    if (!entries) {
        ctx.err() << "Error: cannot list '" << path << "'\n";
        return false;
    }

    // Sort by mtime descending
    auto& vec = *entries;
    std::sort(vec.begin(), vec.end(), [](const LvfsEntry& a, const LvfsEntry& b) {
        return a.mtime_us > b.mtime_us;
    });

    auto& os = ctx.out();
    for (const auto& e : vec) {
        os << format_size(e.size) << "  "
           << format_mtime(e.mtime_us) << "  "
           << e.name;
        if (e.is_directory) os << '/';
        os << '\n';
    }
    return true;
}

// cat <path> — print file contents (required argument)
bool prim_cat(etil::core::ExecutionContext& ctx) {
    auto* lvfs = ctx.lvfs();
    if (!lvfs) {
        ctx.err() << "Error: LVFS not available\n";
        return false;
    }

    std::string path;
    auto* is = ctx.input_stream();
    if (is) *is >> path;

    if (path.empty()) {
        ctx.err() << "Error: cat requires a file path\n";
        return false;
    }

    auto content = lvfs->read_file(path);
    if (!content) {
        ctx.err() << "Error: cannot read '" << path << "'\n";
        return false;
    }

    ctx.out() << *content;
    return true;
}

} // anonymous namespace

void register_lvfs_primitives(etil::core::Dictionary& dict) {


    dict.register_word("cwd", etil::core::make_primitive("cwd", prim_cwd, {}, {}));
    dict.register_word("cd", etil::core::make_primitive("cd", prim_cd, {}, {}));
    dict.register_word("ls", etil::core::make_primitive("ls", prim_ls, {}, {}));
    dict.register_word("ll", etil::core::make_primitive("ll", prim_ll, {}, {}));
    dict.register_word("lr", etil::core::make_primitive("lr", prim_lr, {}, {}));
    dict.register_word("cat", etil::core::make_primitive("cat", prim_cat, {}, {}));
}

} // namespace etil::lvfs
