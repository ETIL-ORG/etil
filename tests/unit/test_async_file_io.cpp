// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/fileio/uv_session.hpp"
#include "etil/lvfs/lvfs.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/heap_array.hpp"
#include "etil/core/heap_object.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace etil::lvfs;
using namespace etil::core;
using namespace etil::fileio;
namespace fs = std::filesystem;

class AsyncFileIOTest : public ::testing::Test {
protected:
    std::string home_dir;
    std::string library_dir;
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    std::unique_ptr<Interpreter> interp;
    std::unique_ptr<Lvfs> lvfs;
    std::unique_ptr<UvSession> uv_session;

    void SetUp() override {
        home_dir = fs::temp_directory_path().string() + "/etil_async_home_" +
                   std::to_string(::getpid());
        library_dir = fs::temp_directory_path().string() + "/etil_async_lib_" +
                      std::to_string(::getpid());
        fs::create_directories(home_dir);
        fs::create_directories(library_dir);

        register_primitives(dict);
        interp = std::make_unique<Interpreter>(dict, out, err);
        interp->set_home_dir(home_dir);
        interp->set_library_dir(library_dir);
        lvfs = std::make_unique<Lvfs>(home_dir, library_dir);
        interp->set_lvfs(lvfs.get());

        // Wire UvSession for async I/O
        uv_session = std::make_unique<UvSession>();
        interp->context().set_uv_session(uv_session.get());
    }

    void TearDown() override {
        interp->shutdown();
        uv_session.reset();
        std::error_code ec;
        fs::remove_all(home_dir, ec);
        fs::remove_all(library_dir, ec);
    }

    void reset_output() {
        out.str(""); out.clear();
        err.str(""); err.clear();
    }

    void write_file(const std::string& dir, const std::string& rel,
                    const std::string& content) {
        fs::path full = fs::path(dir) / rel;
        fs::create_directories(full.parent_path());
        std::ofstream ofs(full);
        ofs << content;
    }

    std::string read_fs_file(const std::string& path) {
        std::ifstream ifs(path);
        std::ostringstream oss;
        oss << ifs.rdbuf();
        return oss.str();
    }
};

// =================================================================
// exists?
// =================================================================

TEST_F(AsyncFileIOTest, ExistsFileExists) {
    write_file(home_dir, "test.txt", "hello");
    interp->interpret_line("s\" /home/test.txt\" exists?");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(AsyncFileIOTest, ExistsFileNotExists) {
    interp->interpret_line("s\" /home/nope.txt\" exists?");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, ExistsDirectoryExists) {
    fs::create_directories(home_dir + "/subdir");
    interp->interpret_line("s\" /home/subdir\" exists?");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(AsyncFileIOTest, ExistsLibraryFile) {
    write_file(library_dir, "lib.til", "content");
    interp->interpret_line("s\" /library/lib.til\" exists?");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

// =================================================================
// write-file + read-file round-trip
// =================================================================

TEST_F(AsyncFileIOTest, WriteReadRoundTrip) {
    interp->interpret_line("s\" Hello Async\" s\" /home/out.txt\" write-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    EXPECT_EQ(read_fs_file(home_dir + "/out.txt"), "Hello Async");

    reset_output();
    interp->interpret_line("s\" /home/out.txt\" read-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("s.");
    EXPECT_EQ(out.str(), "Hello Async");
}

TEST_F(AsyncFileIOTest, WriteFileOverwrites) {
    interp->interpret_line("s\" first\" s\" /home/ow.txt\" write-file drop");
    interp->interpret_line("s\" second\" s\" /home/ow.txt\" write-file drop");
    EXPECT_EQ(read_fs_file(home_dir + "/ow.txt"), "second");
}

TEST_F(AsyncFileIOTest, WriteToLibraryFails) {
    interp->interpret_line("s\" data\" s\" /library/bad.txt\" write-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, ReadFileNonexistent) {
    interp->interpret_line("s\" /home/missing.txt\" read-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, ReadFileFromLibrary) {
    write_file(library_dir, "data.txt", "lib content");
    interp->interpret_line("s\" /library/data.txt\" read-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("s.");
    EXPECT_EQ(out.str(), "lib content");
}

TEST_F(AsyncFileIOTest, WriteEmptyFile) {
    interp->interpret_line("s\" \" s\" /home/empty.txt\" write-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/empty.txt"), "");
}

TEST_F(AsyncFileIOTest, ReadEmptyFile) {
    write_file(home_dir, "empty.txt", "");
    interp->interpret_line("s\" /home/empty.txt\" read-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("slength");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "0 ");
}

// =================================================================
// append-file
// =================================================================

TEST_F(AsyncFileIOTest, AppendFile) {
    interp->interpret_line("s\" AAA\" s\" /home/app.txt\" write-file drop");
    interp->interpret_line("s\" BBB\" s\" /home/app.txt\" append-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/app.txt"), "AAABBB");
}

TEST_F(AsyncFileIOTest, AppendToLibraryFails) {
    interp->interpret_line("s\" data\" s\" /library/x.txt\" append-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, AppendCreatesFile) {
    interp->interpret_line("s\" new content\" s\" /home/newapp.txt\" append-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/newapp.txt"), "new content");
}

// =================================================================
// copy-file
// =================================================================

TEST_F(AsyncFileIOTest, CopyFile) {
    write_file(home_dir, "src.txt", "copy me");
    interp->interpret_line("s\" /home/src.txt\" s\" /home/dst.txt\" copy-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/dst.txt"), "copy me");
}

TEST_F(AsyncFileIOTest, CopyFromLibraryToHome) {
    write_file(library_dir, "lib.txt", "library data");
    interp->interpret_line("s\" /library/lib.txt\" s\" /home/copy.txt\" copy-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/copy.txt"), "library data");
}

TEST_F(AsyncFileIOTest, CopyToLibraryFails) {
    write_file(home_dir, "src.txt", "data");
    interp->interpret_line("s\" /home/src.txt\" s\" /library/nope.txt\" copy-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// rename-file
// =================================================================

TEST_F(AsyncFileIOTest, RenameFile) {
    write_file(home_dir, "old.txt", "rename me");
    interp->interpret_line("s\" /home/old.txt\" s\" /home/new.txt\" rename-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_FALSE(fs::exists(home_dir + "/old.txt"));
    EXPECT_EQ(read_fs_file(home_dir + "/new.txt"), "rename me");
}

TEST_F(AsyncFileIOTest, RenameLibraryFails) {
    write_file(library_dir, "lib.txt", "data");
    interp->interpret_line("s\" /library/lib.txt\" s\" /home/moved.txt\" rename-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, RenameToLibraryFails) {
    write_file(home_dir, "src.txt", "data");
    interp->interpret_line("s\" /home/src.txt\" s\" /library/dst.txt\" rename-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// lstat
// =================================================================

TEST_F(AsyncFileIOTest, LstatFile) {
    write_file(home_dir, "stat.txt", "12345");
    interp->interpret_line("s\" /home/stat.txt\" lstat");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("array-length");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "4 ");
}

TEST_F(AsyncFileIOTest, LstatFileSize) {
    write_file(home_dir, "sized.txt", "ABCDE");
    interp->interpret_line("s\" /home/sized.txt\" lstat drop");
    interp->interpret_line("0 array-get");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "5 ");
}

TEST_F(AsyncFileIOTest, LstatDirFlag) {
    fs::create_directories(home_dir + "/mydir");
    interp->interpret_line("s\" /home/mydir\" lstat drop");
    interp->interpret_line("2 array-get");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(AsyncFileIOTest, LstatReadOnlyFlag) {
    write_file(library_dir, "ro.txt", "readonly");
    interp->interpret_line("s\" /library/ro.txt\" lstat drop");
    interp->interpret_line("3 array-get");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(AsyncFileIOTest, LstatHomeNotReadOnly) {
    write_file(home_dir, "rw.txt", "readwrite");
    interp->interpret_line("s\" /home/rw.txt\" lstat drop");
    interp->interpret_line("3 array-get");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, LstatNonexistent) {
    interp->interpret_line("s\" /home/nope.txt\" lstat");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, LstatMtime) {
    write_file(home_dir, "timed.txt", "data");
    interp->interpret_line("s\" /home/timed.txt\" lstat drop");
    interp->interpret_line("1 array-get");
    reset_output();
    interp->interpret_line(".");
    int64_t mtime = std::stoll(out.str());
    EXPECT_GT(mtime, 1577836800000000LL);  // after 2020-01-01
}

// =================================================================
// readdir
// =================================================================

TEST_F(AsyncFileIOTest, Readdir) {
    write_file(home_dir, "a.txt", "a");
    write_file(home_dir, "b.txt", "b");
    fs::create_directories(home_dir + "/subdir");

    interp->interpret_line("s\" /home\" readdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("array-length");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "3 ");
}

TEST_F(AsyncFileIOTest, ReaddirNonexistent) {
    interp->interpret_line("s\" /home/nosuch\" readdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, ReaddirOnFile) {
    write_file(home_dir, "file.txt", "data");
    interp->interpret_line("s\" /home/file.txt\" readdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// mkdir
// =================================================================

TEST_F(AsyncFileIOTest, Mkdir) {
    interp->interpret_line("s\" /home/newdir\" mkdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_TRUE(fs::is_directory(home_dir + "/newdir"));
}

TEST_F(AsyncFileIOTest, MkdirNested) {
    interp->interpret_line("s\" /home/a/b/c\" mkdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_TRUE(fs::is_directory(home_dir + "/a/b/c"));
}

TEST_F(AsyncFileIOTest, MkdirInLibraryFails) {
    interp->interpret_line("s\" /library/newdir\" mkdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// mkdir-tmp
// =================================================================

TEST_F(AsyncFileIOTest, MkdirTmp) {
    interp->interpret_line("s\" test\" mkdir-tmp");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("s.");
    std::string vpath = out.str();
    EXPECT_EQ(vpath.substr(0, 10), "/home/test");
    EXPECT_GT(vpath.size(), 10u);
}

TEST_F(AsyncFileIOTest, MkdirTmpUnique) {
    interp->interpret_line("s\" u\" mkdir-tmp drop s.");
    std::string path1 = out.str();
    reset_output();
    interp->interpret_line("s\" u\" mkdir-tmp drop s.");
    std::string path2 = out.str();
    EXPECT_NE(path1, path2);
}

// =================================================================
// rmdir
// =================================================================

TEST_F(AsyncFileIOTest, RmdirEmpty) {
    fs::create_directories(home_dir + "/emptydir");
    interp->interpret_line("s\" /home/emptydir\" rmdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_FALSE(fs::exists(home_dir + "/emptydir"));
}

TEST_F(AsyncFileIOTest, RmdirNonEmpty) {
    fs::create_directories(home_dir + "/notempty");
    write_file(home_dir, "notempty/file.txt", "data");
    interp->interpret_line("s\" /home/notempty\" rmdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
    EXPECT_TRUE(fs::exists(home_dir + "/notempty"));
}

TEST_F(AsyncFileIOTest, RmdirLibraryFails) {
    interp->interpret_line("s\" /library\" rmdir");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// rm
// =================================================================

TEST_F(AsyncFileIOTest, RmFile) {
    write_file(home_dir, "delme.txt", "bye");
    interp->interpret_line("s\" /home/delme.txt\" rm");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_FALSE(fs::exists(home_dir + "/delme.txt"));
}

TEST_F(AsyncFileIOTest, RmDirRecursive) {
    fs::create_directories(home_dir + "/tree/sub");
    write_file(home_dir, "tree/sub/file.txt", "data");
    interp->interpret_line("s\" /home/tree\" rm");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_FALSE(fs::exists(home_dir + "/tree"));
}

TEST_F(AsyncFileIOTest, RmHomeRootGuard) {
    interp->interpret_line("s\" /home\" rm");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
    EXPECT_TRUE(fs::exists(home_dir));
}

TEST_F(AsyncFileIOTest, RmLibraryFails) {
    interp->interpret_line("s\" /library\" rm");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, RmNonexistent) {
    interp->interpret_line("s\" /home/nosuch\" rm");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

// =================================================================
// truncate
// =================================================================

TEST_F(AsyncFileIOTest, Truncate) {
    write_file(home_dir, "trunc.txt", "some data here");
    interp->interpret_line("s\" /home/trunc.txt\" truncate");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/trunc.txt"), "");
    EXPECT_TRUE(fs::exists(home_dir + "/trunc.txt"));
}

TEST_F(AsyncFileIOTest, TruncateLibraryFails) {
    write_file(library_dir, "lib.txt", "data");
    interp->interpret_line("s\" /library/lib.txt\" truncate");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, TruncateNonexistent) {
    interp->interpret_line("s\" /home/nope.txt\" truncate");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// Path traversal rejection
// =================================================================

TEST_F(AsyncFileIOTest, WriteTraversalRejected) {
    interp->interpret_line("s\" bad\" s\" /home/../../etc/passwd\" write-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(AsyncFileIOTest, ReadTraversalRejected) {
    interp->interpret_line("s\" /home/../../../etc/passwd\" read-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// Fallback (no UvSession) — verify sync codepath still works
// =================================================================

TEST_F(AsyncFileIOTest, FallbackWithoutUvSession) {
    // Remove UvSession — primitives should fall back to sync
    interp->context().set_uv_session(nullptr);

    write_file(home_dir, "sync.txt", "sync data");
    interp->interpret_line("s\" /home/sync.txt\" read-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("s.");
    EXPECT_EQ(out.str(), "sync data");
}

TEST_F(AsyncFileIOTest, FallbackExistsWithoutUvSession) {
    interp->context().set_uv_session(nullptr);

    write_file(home_dir, "test.txt", "hello");
    interp->interpret_line("s\" /home/test.txt\" exists?");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(AsyncFileIOTest, FallbackWriteWithoutUvSession) {
    interp->context().set_uv_session(nullptr);

    interp->interpret_line("s\" sync write\" s\" /home/syncw.txt\" write-file");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/syncw.txt"), "sync write");
}
