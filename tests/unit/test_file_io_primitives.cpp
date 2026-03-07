// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

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
#include <set>

using namespace etil::lvfs;
using namespace etil::core;
namespace fs = std::filesystem;

class FileIOTest : public ::testing::Test {
protected:
    std::string home_dir;
    std::string library_dir;
    Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    std::unique_ptr<Interpreter> interp;
    std::unique_ptr<Lvfs> lvfs;

    void SetUp() override {
        home_dir = fs::temp_directory_path().string() + "/etil_fio_home_" +
                   std::to_string(::getpid());
        library_dir = fs::temp_directory_path().string() + "/etil_fio_lib_" +
                      std::to_string(::getpid());
        fs::create_directories(home_dir);
        fs::create_directories(library_dir);

        register_primitives(dict);
        interp = std::make_unique<Interpreter>(dict, out, err);
        interp->set_home_dir(home_dir);
        interp->set_library_dir(library_dir);
        lvfs = std::make_unique<Lvfs>(home_dir, library_dir);
        interp->set_lvfs(lvfs.get());
    }

    void TearDown() override {
        interp->shutdown();
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

    // Run TIL code, return top-of-stack as int64
    int64_t run_int(const std::string& code) {
        interp->interpret_line(code);
        // Stack top should be an integer; we read it via .
        reset_output();
        interp->interpret_line(".");
        return std::stoll(out.str());
    }
};

// =================================================================
// exists-sync
// =================================================================

TEST_F(FileIOTest, ExistsSyncFileExists) {
    write_file(home_dir, "test.txt", "hello");
    interp->interpret_line("s\" /home/test.txt\" exists-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(FileIOTest, ExistsSyncFileNotExists) {
    interp->interpret_line("s\" /home/nope.txt\" exists-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, ExistsSyncDirectoryExists) {
    fs::create_directories(home_dir + "/subdir");
    interp->interpret_line("s\" /home/subdir\" exists-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(FileIOTest, ExistsSyncLibraryFile) {
    write_file(library_dir, "lib.til", "content");
    interp->interpret_line("s\" /library/lib.til\" exists-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

// =================================================================
// write-file-sync + read-file-sync round-trip
// =================================================================

TEST_F(FileIOTest, WriteReadRoundTrip) {
    interp->interpret_line("s\" Hello ETIL\" s\" /home/out.txt\" write-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");  // success flag

    // Verify on filesystem
    EXPECT_EQ(read_fs_file(home_dir + "/out.txt"), "Hello ETIL");

    // Read back via read-file-sync
    reset_output();
    interp->interpret_line("s\" /home/out.txt\" read-file-sync");
    // Stack: string flag
    reset_output();
    interp->interpret_line(".");  // flag
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("s.");  // string content
    EXPECT_EQ(out.str(), "Hello ETIL");
}

TEST_F(FileIOTest, WriteFileOverwrites) {
    interp->interpret_line("s\" first\" s\" /home/ow.txt\" write-file-sync drop");
    interp->interpret_line("s\" second\" s\" /home/ow.txt\" write-file-sync drop");
    EXPECT_EQ(read_fs_file(home_dir + "/ow.txt"), "second");
}

TEST_F(FileIOTest, WriteToLibraryFails) {
    interp->interpret_line("s\" data\" s\" /library/bad.txt\" write-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, ReadFileNonexistent) {
    interp->interpret_line("s\" /home/missing.txt\" read-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, ReadFileFromLibrary) {
    write_file(library_dir, "data.txt", "lib content");
    interp->interpret_line("s\" /library/data.txt\" read-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("s.");
    EXPECT_EQ(out.str(), "lib content");
}

// =================================================================
// append-file-sync
// =================================================================

TEST_F(FileIOTest, AppendFileSync) {
    interp->interpret_line("s\" AAA\" s\" /home/app.txt\" write-file-sync drop");
    interp->interpret_line("s\" BBB\" s\" /home/app.txt\" append-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/app.txt"), "AAABBB");
}

TEST_F(FileIOTest, AppendToLibraryFails) {
    interp->interpret_line("s\" data\" s\" /library/x.txt\" append-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// copy-file-sync
// =================================================================

TEST_F(FileIOTest, CopyFileSync) {
    write_file(home_dir, "src.txt", "copy me");
    interp->interpret_line("s\" /home/src.txt\" s\" /home/dst.txt\" copy-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/dst.txt"), "copy me");
}

TEST_F(FileIOTest, CopyFromLibraryToHome) {
    write_file(library_dir, "lib.txt", "library data");
    interp->interpret_line("s\" /library/lib.txt\" s\" /home/copy.txt\" copy-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/copy.txt"), "library data");
}

TEST_F(FileIOTest, CopyToLibraryFails) {
    write_file(home_dir, "src.txt", "data");
    interp->interpret_line("s\" /home/src.txt\" s\" /library/nope.txt\" copy-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// rename-sync
// =================================================================

TEST_F(FileIOTest, RenameSync) {
    write_file(home_dir, "old.txt", "rename me");
    interp->interpret_line("s\" /home/old.txt\" s\" /home/new.txt\" rename-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_FALSE(fs::exists(home_dir + "/old.txt"));
    EXPECT_EQ(read_fs_file(home_dir + "/new.txt"), "rename me");
}

TEST_F(FileIOTest, RenameLibraryFails) {
    write_file(library_dir, "lib.txt", "data");
    interp->interpret_line("s\" /library/lib.txt\" s\" /home/moved.txt\" rename-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, RenameToLibraryFails) {
    write_file(home_dir, "src.txt", "data");
    interp->interpret_line("s\" /home/src.txt\" s\" /library/dst.txt\" rename-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// lstat-sync
// =================================================================

TEST_F(FileIOTest, LstatSyncFile) {
    write_file(home_dir, "stat.txt", "12345");
    interp->interpret_line("s\" /home/stat.txt\" lstat-sync");
    // Stack: array flag
    reset_output();
    interp->interpret_line(".");  // flag
    EXPECT_EQ(out.str(), "true ");

    // Array should have 4 elements
    reset_output();
    interp->interpret_line("array-length");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "4 ");
}

TEST_F(FileIOTest, LstatSyncFileSize) {
    write_file(home_dir, "sized.txt", "ABCDE");
    interp->interpret_line("s\" /home/sized.txt\" lstat-sync drop");
    // Get element 0 (size)
    interp->interpret_line("0 array-get");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "5 ");
}

TEST_F(FileIOTest, LstatSyncDirFlag) {
    fs::create_directories(home_dir + "/mydir");
    interp->interpret_line("s\" /home/mydir\" lstat-sync drop");
    // Get element 2 (is_directory)
    interp->interpret_line("2 array-get");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(FileIOTest, LstatSyncReadOnlyFlag) {
    write_file(library_dir, "ro.txt", "readonly");
    interp->interpret_line("s\" /library/ro.txt\" lstat-sync drop");
    // Get element 3 (is_read_only)
    interp->interpret_line("3 array-get");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
}

TEST_F(FileIOTest, LstatSyncHomeNotReadOnly) {
    write_file(home_dir, "rw.txt", "readwrite");
    interp->interpret_line("s\" /home/rw.txt\" lstat-sync drop");
    // Get element 3 (is_read_only)
    interp->interpret_line("3 array-get");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, LstatSyncNonexistent) {
    interp->interpret_line("s\" /home/nope.txt\" lstat-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, LstatSyncMtime) {
    write_file(home_dir, "timed.txt", "data");
    interp->interpret_line("s\" /home/timed.txt\" lstat-sync drop");
    // Get element 1 (mtime_us) — should be a recent timestamp
    interp->interpret_line("1 array-get");
    reset_output();
    interp->interpret_line(".");
    int64_t mtime = std::stoll(out.str());
    // mtime should be a reasonable microsecond timestamp (after 2020)
    EXPECT_GT(mtime, 1577836800000000LL);  // 2020-01-01 in us
}

// =================================================================
// readdir-sync
// =================================================================

TEST_F(FileIOTest, ReaddirSync) {
    write_file(home_dir, "a.txt", "a");
    write_file(home_dir, "b.txt", "b");
    fs::create_directories(home_dir + "/subdir");

    interp->interpret_line("s\" /home\" readdir-sync");
    reset_output();
    interp->interpret_line(".");  // flag
    EXPECT_EQ(out.str(), "true ");

    // Array should have 3 entries
    reset_output();
    interp->interpret_line("array-length");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "3 ");
}

TEST_F(FileIOTest, ReaddirSyncNonexistent) {
    interp->interpret_line("s\" /home/nosuch\" readdir-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, ReaddirSyncOnFile) {
    write_file(home_dir, "file.txt", "data");
    interp->interpret_line("s\" /home/file.txt\" readdir-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// mkdir-sync
// =================================================================

TEST_F(FileIOTest, MkdirSync) {
    interp->interpret_line("s\" /home/newdir\" mkdir-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_TRUE(fs::is_directory(home_dir + "/newdir"));
}

TEST_F(FileIOTest, MkdirSyncNested) {
    interp->interpret_line("s\" /home/a/b/c\" mkdir-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_TRUE(fs::is_directory(home_dir + "/a/b/c"));
}

TEST_F(FileIOTest, MkdirSyncInLibraryFails) {
    interp->interpret_line("s\" /library/newdir\" mkdir-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// mkdir-tmp-sync
// =================================================================

TEST_F(FileIOTest, MkdirTmpSync) {
    interp->interpret_line("s\" test\" mkdir-tmp-sync");
    reset_output();
    interp->interpret_line(".");  // flag
    EXPECT_EQ(out.str(), "true ");

    // The string on stack should start with /home/test
    reset_output();
    interp->interpret_line("s.");
    std::string vpath = out.str();
    EXPECT_EQ(vpath.substr(0, 10), "/home/test");
    EXPECT_GT(vpath.size(), 10u);  // has random suffix
}

TEST_F(FileIOTest, MkdirTmpSyncUnique) {
    interp->interpret_line("s\" u\" mkdir-tmp-sync drop s.");
    std::string path1 = out.str();
    reset_output();
    interp->interpret_line("s\" u\" mkdir-tmp-sync drop s.");
    std::string path2 = out.str();
    EXPECT_NE(path1, path2);
}

// =================================================================
// rmdir-sync
// =================================================================

TEST_F(FileIOTest, RmdirSyncEmpty) {
    fs::create_directories(home_dir + "/emptydir");
    interp->interpret_line("s\" /home/emptydir\" rmdir-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_FALSE(fs::exists(home_dir + "/emptydir"));
}

TEST_F(FileIOTest, RmdirSyncNonEmpty) {
    fs::create_directories(home_dir + "/notempty");
    write_file(home_dir, "notempty/file.txt", "data");
    interp->interpret_line("s\" /home/notempty\" rmdir-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");  // non-empty dir removal fails
    EXPECT_TRUE(fs::exists(home_dir + "/notempty"));
}

TEST_F(FileIOTest, RmdirSyncLibraryFails) {
    interp->interpret_line("s\" /library\" rmdir-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// rm-sync
// =================================================================

TEST_F(FileIOTest, RmSyncFile) {
    write_file(home_dir, "delme.txt", "bye");
    interp->interpret_line("s\" /home/delme.txt\" rm-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_FALSE(fs::exists(home_dir + "/delme.txt"));
}

TEST_F(FileIOTest, RmSyncDirRecursive) {
    fs::create_directories(home_dir + "/tree/sub");
    write_file(home_dir, "tree/sub/file.txt", "data");
    interp->interpret_line("s\" /home/tree\" rm-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_FALSE(fs::exists(home_dir + "/tree"));
}

TEST_F(FileIOTest, RmSyncHomeRootGuard) {
    interp->interpret_line("s\" /home\" rm-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
    EXPECT_TRUE(fs::exists(home_dir));
}

TEST_F(FileIOTest, RmSyncLibraryFails) {
    interp->interpret_line("s\" /library\" rm-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, RmSyncNonexistent) {
    interp->interpret_line("s\" /home/nosuch\" rm-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");  // remove_all on nonexistent returns no error
}

// =================================================================
// truncate-sync
// =================================================================

TEST_F(FileIOTest, TruncateSync) {
    write_file(home_dir, "trunc.txt", "some data here");
    interp->interpret_line("s\" /home/trunc.txt\" truncate-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/trunc.txt"), "");
    EXPECT_TRUE(fs::exists(home_dir + "/trunc.txt"));
}

TEST_F(FileIOTest, TruncateLibraryFails) {
    write_file(library_dir, "lib.txt", "data");
    interp->interpret_line("s\" /library/lib.txt\" truncate-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, TruncateNonexistent) {
    interp->interpret_line("s\" /home/nope.txt\" truncate-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// Path traversal rejection
// =================================================================

TEST_F(FileIOTest, WriteTraversalRejected) {
    interp->interpret_line("s\" bad\" s\" /home/../../etc/passwd\" write-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

TEST_F(FileIOTest, ReadTraversalRejected) {
    interp->interpret_line("s\" /home/../../../etc/passwd\" read-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "false ");
}

// =================================================================
// Empty/binary content
// =================================================================

TEST_F(FileIOTest, WriteEmptyFile) {
    interp->interpret_line("s\" \" s\" /home/empty.txt\" write-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/empty.txt"), "");
}

TEST_F(FileIOTest, ReadEmptyFile) {
    write_file(home_dir, "empty.txt", "");
    interp->interpret_line("s\" /home/empty.txt\" read-file-sync");
    reset_output();
    interp->interpret_line(".");  // flag
    EXPECT_EQ(out.str(), "true ");

    reset_output();
    interp->interpret_line("slength");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "0 ");
}

// =================================================================
// Append to new file (creates it)
// =================================================================

TEST_F(FileIOTest, AppendCreatesFile) {
    interp->interpret_line("s\" new content\" s\" /home/newapp.txt\" append-file-sync");
    reset_output();
    interp->interpret_line(".");
    EXPECT_EQ(out.str(), "true ");
    EXPECT_EQ(read_fs_file(home_dir + "/newapp.txt"), "new content");
}
