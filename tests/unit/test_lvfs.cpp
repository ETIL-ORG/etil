// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/lvfs/lvfs.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace etil::lvfs;
namespace fs = std::filesystem;

// Helper: create a temp directory for test isolation
class LvfsTest : public ::testing::Test {
protected:
    std::string home_dir;
    std::string library_dir;

    void SetUp() override {
        home_dir = fs::temp_directory_path().string() + "/etil_test_home_" +
                   std::to_string(::getpid());
        library_dir = fs::temp_directory_path().string() + "/etil_test_lib_" +
                      std::to_string(::getpid());
        fs::create_directories(home_dir);
        fs::create_directories(library_dir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(home_dir, ec);
        fs::remove_all(library_dir, ec);
    }

    void write_file(const std::string& dir, const std::string& rel_path,
                    const std::string& content) {
        fs::path full = fs::path(dir) / rel_path;
        fs::create_directories(full.parent_path());
        std::ofstream ofs(full);
        ofs << content;
    }
};

// --- Lvfs class tests ---

TEST_F(LvfsTest, DefaultCwdIsHome) {
    Lvfs lvfs(home_dir, library_dir);
    EXPECT_EQ(lvfs.cwd(), "/home");
}

TEST_F(LvfsTest, CdToRoot) {
    Lvfs lvfs(home_dir, library_dir);
    EXPECT_TRUE(lvfs.cd("/"));
    EXPECT_EQ(lvfs.cwd(), "/");
}

TEST_F(LvfsTest, CdToLibrary) {
    Lvfs lvfs(home_dir, library_dir);
    EXPECT_TRUE(lvfs.cd("/library"));
    EXPECT_EQ(lvfs.cwd(), "/library");
}

TEST_F(LvfsTest, CdNoArgResetsToHome) {
    Lvfs lvfs(home_dir, library_dir);
    lvfs.cd("/library");
    EXPECT_TRUE(lvfs.cd(""));
    EXPECT_EQ(lvfs.cwd(), "/home");
}

TEST_F(LvfsTest, CdToSubdir) {
    fs::create_directories(home_dir + "/subdir");
    Lvfs lvfs(home_dir, library_dir);
    EXPECT_TRUE(lvfs.cd("/home/subdir"));
    EXPECT_EQ(lvfs.cwd(), "/home/subdir");
}

TEST_F(LvfsTest, CdToNonexistent) {
    Lvfs lvfs(home_dir, library_dir);
    EXPECT_FALSE(lvfs.cd("/home/nonexistent"));
    EXPECT_EQ(lvfs.cwd(), "/home");  // unchanged
}

TEST_F(LvfsTest, CdRelative) {
    fs::create_directories(home_dir + "/sub");
    Lvfs lvfs(home_dir, library_dir);
    // CWD is /home, cd to "sub" -> /home/sub
    EXPECT_TRUE(lvfs.cd("sub"));
    EXPECT_EQ(lvfs.cwd(), "/home/sub");
}

TEST_F(LvfsTest, CdDotDotClampsAtRoot) {
    Lvfs lvfs(home_dir, library_dir);
    EXPECT_TRUE(lvfs.cd("/.."));
    EXPECT_EQ(lvfs.cwd(), "/");
}

TEST_F(LvfsTest, CdDotDotFromHome) {
    Lvfs lvfs(home_dir, library_dir);
    EXPECT_TRUE(lvfs.cd(".."));
    EXPECT_EQ(lvfs.cwd(), "/");
}

TEST_F(LvfsTest, ListRoot) {
    Lvfs lvfs(home_dir, library_dir);
    auto entries = lvfs.list_dir("/");
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 2u);
    // Should contain home and library
    bool has_home = false, has_library = false;
    for (const auto& e : *entries) {
        if (e.name == "home") has_home = true;
        if (e.name == "library") has_library = true;
        EXPECT_TRUE(e.is_directory);
    }
    EXPECT_TRUE(has_home);
    EXPECT_TRUE(has_library);
}

TEST_F(LvfsTest, ListHomeDir) {
    write_file(home_dir, "test.txt", "hello");
    fs::create_directories(home_dir + "/subdir");

    Lvfs lvfs(home_dir, library_dir);
    auto entries = lvfs.list_dir("/home");
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 2u);
}

TEST_F(LvfsTest, ListCwd) {
    write_file(home_dir, "file1.txt", "content1");
    write_file(home_dir, "file2.txt", "content2");

    Lvfs lvfs(home_dir, library_dir);
    // Default CWD is /home, list_dir with CWD path
    auto entries = lvfs.list_dir(lvfs.cwd());
    ASSERT_TRUE(entries.has_value());
    EXPECT_EQ(entries->size(), 2u);
}

TEST_F(LvfsTest, ListNonexistent) {
    Lvfs lvfs(home_dir, library_dir);
    auto entries = lvfs.list_dir("/home/nonexistent");
    EXPECT_FALSE(entries.has_value());
}

TEST_F(LvfsTest, ListDirRecursive) {
    write_file(home_dir, "a.txt", "aa");
    write_file(home_dir, "sub/b.txt", "bb");

    Lvfs lvfs(home_dir, library_dir);
    auto entries = lvfs.list_dir_recursive("/home");
    ASSERT_TRUE(entries.has_value());

    bool found_a = false, found_sub_b = false, found_sub = false;
    for (const auto& e : *entries) {
        if (e.name == "a.txt") found_a = true;
        if (e.name == "sub/b.txt") found_sub_b = true;
        if (e.name == "sub") found_sub = true;
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_sub_b);
    EXPECT_TRUE(found_sub);
}

TEST_F(LvfsTest, ReadFile) {
    write_file(home_dir, "hello.txt", "Hello, LVFS!");

    Lvfs lvfs(home_dir, library_dir);
    auto content = lvfs.read_file("/home/hello.txt");
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, "Hello, LVFS!");
}

TEST_F(LvfsTest, ReadNonexistent) {
    Lvfs lvfs(home_dir, library_dir);
    auto content = lvfs.read_file("/home/nope.txt");
    EXPECT_FALSE(content.has_value());
}

TEST_F(LvfsTest, ReadLibrary) {
    write_file(library_dir, "lib.til", ": lib-word 42 ;");

    Lvfs lvfs(home_dir, library_dir);
    auto content = lvfs.read_file("/library/lib.til");
    ASSERT_TRUE(content.has_value());
    EXPECT_EQ(*content, ": lib-word 42 ;");
}

TEST_F(LvfsTest, IsReadOnly) {
    Lvfs lvfs(home_dir, library_dir);
    EXPECT_TRUE(lvfs.is_read_only("/library/foo.txt"));
    EXPECT_TRUE(lvfs.is_read_only("/library"));
    EXPECT_FALSE(lvfs.is_read_only("/home/foo.txt"));
    EXPECT_FALSE(lvfs.is_read_only("/"));
}

TEST_F(LvfsTest, ResolveHomePath) {
    Lvfs lvfs(home_dir, library_dir);
    std::string resolved = lvfs.resolve_home_path("test.txt");
    EXPECT_EQ(resolved, home_dir + "/test.txt");
}

TEST_F(LvfsTest, ResolveLibraryPath) {
    Lvfs lvfs(home_dir, library_dir);
    std::string resolved = lvfs.resolve_library_path("lib.til");
    EXPECT_EQ(resolved, library_dir + "/lib.til");
}

TEST_F(LvfsTest, ResolveLogicalPath) {
    Lvfs lvfs(home_dir, library_dir);
    std::string resolved = lvfs.resolve_logical_path("/home/test.txt");
    EXPECT_EQ(resolved, home_dir + "/test.txt");

    resolved = lvfs.resolve_logical_path("/library/lib.til");
    EXPECT_EQ(resolved, library_dir + "/lib.til");

    // No prefix — pass through
    resolved = lvfs.resolve_logical_path("data/builtins.til");
    EXPECT_EQ(resolved, "data/builtins.til");
}

TEST_F(LvfsTest, ResolveRejectsTraversal) {
    Lvfs lvfs(home_dir, library_dir);
    std::string resolved = lvfs.resolve_home_path("../../../etc/passwd");
    EXPECT_TRUE(resolved.empty());
}

TEST_F(LvfsTest, ResolveCwdRelative) {
    write_file(home_dir, "sub/file.txt", "content");

    Lvfs lvfs(home_dir, library_dir);
    lvfs.cd("/home/sub");

    // Relative resolve from CWD /home/sub
    std::string resolved = lvfs.resolve("file.txt");
    EXPECT_EQ(resolved, home_dir + "/sub/file.txt");
}

// --- Primitive tests via Interpreter ---

class LvfsPrimTest : public LvfsTest {
protected:
    etil::core::Dictionary dict;
    std::ostringstream out;
    std::ostringstream err;
    std::unique_ptr<etil::core::Interpreter> interp;
    std::unique_ptr<Lvfs> lvfs;

    void SetUp() override {
        LvfsTest::SetUp();
        etil::core::register_primitives(dict);
        interp = std::make_unique<etil::core::Interpreter>(dict, out, err);
        interp->set_home_dir(home_dir);
        interp->set_library_dir(library_dir);
        lvfs = std::make_unique<Lvfs>(home_dir, library_dir);
        interp->set_lvfs(lvfs.get());
    }

    void reset_output() {
        out.str("");
        out.clear();
        err.str("");
        err.clear();
    }
};

TEST_F(LvfsPrimTest, CwdDefault) {
    interp->interpret_line("cwd");
    EXPECT_EQ(out.str(), "/home\n");
}

TEST_F(LvfsPrimTest, CdAndCwd) {
    interp->interpret_line("cd /library");
    EXPECT_TRUE(err.str().empty());
    reset_output();
    interp->interpret_line("cwd");
    EXPECT_EQ(out.str(), "/library\n");
}

TEST_F(LvfsPrimTest, CdNoArgResetsToHome) {
    interp->interpret_line("cd /library");
    reset_output();
    interp->interpret_line("cd");
    EXPECT_TRUE(err.str().empty());
    reset_output();
    interp->interpret_line("cwd");
    EXPECT_EQ(out.str(), "/home\n");
}

TEST_F(LvfsPrimTest, CdNonexistent) {
    interp->interpret_line("cd /home/nosuch");
    EXPECT_FALSE(err.str().empty());
}

TEST_F(LvfsPrimTest, LsRoot) {
    interp->interpret_line("ls /");
    std::string output = out.str();
    EXPECT_NE(output.find("home/"), std::string::npos);
    EXPECT_NE(output.find("library/"), std::string::npos);
}

TEST_F(LvfsPrimTest, LsHome) {
    write_file(home_dir, "test.txt", "hello");
    interp->interpret_line("ls /home");
    EXPECT_NE(out.str().find("test.txt"), std::string::npos);
}

TEST_F(LvfsPrimTest, LsCwd) {
    write_file(home_dir, "myfile.txt", "data");
    interp->interpret_line("ls");
    EXPECT_NE(out.str().find("myfile.txt"), std::string::npos);
}

TEST_F(LvfsPrimTest, LlFormat) {
    write_file(home_dir, "test.txt", "12345");
    interp->interpret_line("ll /home");
    std::string output = out.str();
    // Should contain size and date
    EXPECT_NE(output.find("5"), std::string::npos);  // file size
    EXPECT_NE(output.find("test.txt"), std::string::npos);
}

TEST_F(LvfsPrimTest, LrRecursive) {
    write_file(home_dir, "a.txt", "aa");
    write_file(home_dir, "sub/b.txt", "bb");
    interp->interpret_line("lr /home");
    std::string output = out.str();
    EXPECT_NE(output.find("a.txt"), std::string::npos);
    EXPECT_NE(output.find("sub/b.txt"), std::string::npos);
}

TEST_F(LvfsPrimTest, CatFile) {
    write_file(home_dir, "greet.txt", "Hello World!");
    interp->interpret_line("cat greet.txt");
    EXPECT_EQ(out.str(), "Hello World!");
}

TEST_F(LvfsPrimTest, CatNonexistent) {
    interp->interpret_line("cat nosuch.txt");
    EXPECT_FALSE(err.str().empty());
}

TEST_F(LvfsPrimTest, CatNoArg) {
    interp->interpret_line("cat");
    EXPECT_FALSE(err.str().empty());
}

TEST_F(LvfsPrimTest, CatLibraryFile) {
    write_file(library_dir, "lib.til", ": test 1 ;");
    interp->interpret_line("cat /library/lib.til");
    EXPECT_EQ(out.str(), ": test 1 ;");
}

TEST_F(LvfsPrimTest, CdThenCat) {
    write_file(home_dir, "sub/data.txt", "subdata");
    interp->interpret_line("cd /home/sub");
    reset_output();
    interp->interpret_line("cat data.txt");
    EXPECT_EQ(out.str(), "subdata");
}

TEST_F(LvfsPrimTest, LsDirTrailingSlash) {
    fs::create_directories(home_dir + "/mydir");
    write_file(home_dir, "myfile.txt", "content");
    interp->interpret_line("ls /home");
    std::string output = out.str();
    EXPECT_NE(output.find("mydir/"), std::string::npos);
    // File should NOT have trailing slash
    auto pos = output.find("myfile.txt");
    ASSERT_NE(pos, std::string::npos);
    // Check character after filename - should be newline, not /
    size_t after = pos + std::string("myfile.txt").size();
    EXPECT_NE(output[after], '/');
}
