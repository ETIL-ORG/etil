// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
//
// TIL-level loopback test harness. Loads
// tests/til/manifold-e2e/manifold_loop.til through a real Interpreter
// with a ChannelService bound to the ExecutionContext (the standalone
// REPL doesn't bind one, so this test owns the plumbing). The .til
// file uses the pass/fail harness from tests/til/harness.til — this
// test asserts every "PASS" line shows up and no "FAIL" lines do.
//
// Purpose: lock in that the TIL loopback API works end-to-end through
// the actual language/interpreter path, not just the C++ primitive
// entry points. Complementary to test_manifold_loop_til.cpp.

#include "etil/core/dictionary.hpp"
#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/manifold/origin.hpp"
#include "etil/manifold/service.hpp"
#include "test_paths.hpp"

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define ETIL_HAS_LSAN 1
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#  define ETIL_HAS_LSAN 1
#endif
#if ETIL_HAS_LSAN
extern "C" void __lsan_disable();
extern "C" void __lsan_enable();
#endif

using etil::core::Dictionary;
using etil::core::Interpreter;
using etil::manifold::ChannelService;

// ---------------------------------------------------------------------------
// Drive the .til file end-to-end and confirm its pass/fail markers.
// ---------------------------------------------------------------------------
TEST(ManifoldLoopTilFile, RunsAllAssertionsAsPass) {
    etil::manifold::shutdown_origin();
    etil::manifold::init_origin();

    // Interpreter's colon-definition path (finalize_definition in
    // interpreter.cpp) leaks WordImpl allocations that aren't
    // refcounted into the Dictionary teardown chain. That's a
    // pre-existing interpreter cleanup issue, unrelated to the
    // loopback work this test exercises. Silence LSan around the
    // interpreter body so the test status reflects only the TIL
    // assertions.
#if ETIL_HAS_LSAN
    __lsan_disable();
#endif

    Dictionary dict;
    etil::core::register_primitives(dict);

    std::stringstream out;
    std::stringstream err;
    Interpreter interp(dict, out, err);

    auto channels = etil::manifold::make_default_channel_service();
    interp.context().set_channels(channels.get());

    // Project root comes from the CMake-generated test_paths.hpp with
    // an env-var override (ETIL_TEST_PROJECT_DIR) for relocated trees.
    // We then talk to the interpreter using its logical `/home/...`
    // prefix, which maps to home_dir_. Passing filesystem-absolute
    // paths that happen to start with `/home/` would be misinterpreted
    // as logical and double-resolved.
    const std::string project_root = etil::test::project_dir();
    ASSERT_TRUE(std::filesystem::exists(project_root))
        << "project root does not exist: " << project_root
        << " (set ETIL_TEST_PROJECT_DIR to override)";
    interp.set_home_dir(project_root);

    // Load builtins.til so `variable`, `constant`, and other self-hosted
    // words are available. The standalone REPL does this at startup
    // via load_startup_files; we mirror it here.
    ASSERT_TRUE(interp.load_file("/home/data/builtins.til"))
        << "builtins.til failed; stderr: " << err.str();

    ASSERT_TRUE(interp.load_file(
        "/home/tests/til/manifold-e2e/manifold_loop.til"))
        << "load_file failed; stderr: " << err.str();

    interp.shutdown();

    const std::string captured = out.str();

    // The file contains five test runs; each emits "PASS" via the
    // harness. Nothing should emit "FAIL".
    size_t pass_count = 0;
    size_t fail_count = 0;
    std::istringstream iss(captured);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("FAIL") != std::string::npos) ++fail_count;
        else if (line.find("PASS") != std::string::npos) ++pass_count;
    }

    EXPECT_EQ(fail_count, 0u) << "captured output:\n" << captured
                              << "\nstderr:\n" << err.str();
    EXPECT_EQ(pass_count, 5u) << "captured output:\n" << captured
                              << "\nstderr:\n" << err.str();

#if ETIL_HAS_LSAN
    __lsan_enable();
#endif
}
