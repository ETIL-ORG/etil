include(GoogleTest)

# Unit tests
add_executable(etil_tests
    unit/test_word_impl.cpp
    unit/test_dictionary.cpp
    unit/test_primitives.cpp
    unit/test_compiled_body.cpp
    unit/test_interpreter.cpp
    unit/test_metadata.cpp
    unit/test_heap_objects.cpp
    unit/test_string_primitives.cpp
    unit/test_array_primitives.cpp
    unit/test_byte_primitives.cpp
    unit/test_map_primitives.cpp
    unit/test_json_primitives.cpp
    unit/test_observable_primitives.cpp
    unit/test_handler_sets.cpp
    unit/test_lvfs.cpp
    unit/test_async_file_io.cpp
    unit/test_taint.cpp
    unit/test_permissions.cpp
)

target_link_libraries(etil_tests
    PRIVATE
        etil
        GTest::gtest_main
)

etil_add_link_map(etil_tests)
gtest_discover_tests(etil_tests)

# MCP unit tests (no transport exposed — safe to run natively)
add_executable(etil_mcp_tests
    unit/test_json_rpc.cpp
    unit/test_mcp_server.cpp
    unit/test_mcp_tools.cpp
)

target_link_libraries(etil_mcp_tests
    PRIVATE
        etil_mcp
        GTest::gtest_main
)

etil_add_link_map(etil_mcp_tests)
gtest_discover_tests(etil_mcp_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# HTTP client unit tests (URL validation, SSRF blocklist — no network, safe to run natively)
if(ETIL_BUILD_HTTP_CLIENT)
    add_executable(etil_http_client_tests
        unit/test_url_validation.cpp
        unit/test_http_get.cpp
    )

    target_link_libraries(etil_http_client_tests
        PRIVATE
            etil
            GTest::gtest_main
    )

    etil_add_link_map(etil_http_client_tests)
    gtest_discover_tests(etil_http_client_tests)
endif()

# JWT auth unit tests (no network, safe to run natively)
if(ETIL_BUILD_JWT)
    add_executable(etil_jwt_tests
        unit/test_jwt_auth.cpp
        unit/test_oauth_provider.cpp
    )

    target_link_libraries(etil_jwt_tests
        PRIVATE
            etil_mcp
            GTest::gtest_main
    )

    etil_add_link_map(etil_jwt_tests)
    gtest_discover_tests(etil_jwt_tests)
endif()

# MongoDB unit tests (no network unless ETIL_MONGODB_TEST_URI is set)
if(ETIL_BUILD_MONGODB)
    add_executable(etil_mongo_tests
        unit/test_mongo_client.cpp
        unit/test_aaa.cpp
    )

    target_link_libraries(etil_mongo_tests
        PRIVATE
            etil_mcp
            GTest::gtest_main
    )

    etil_add_link_map(etil_mongo_tests)
    gtest_discover_tests(etil_mongo_tests)
endif()

# Linear algebra unit tests
add_executable(etil_linalg_tests
    unit/test_matrix_primitives.cpp
)

target_link_libraries(etil_linalg_tests
    PRIVATE
        etil
        GTest::gtest_main
)

etil_add_link_map(etil_linalg_tests)
gtest_discover_tests(etil_linalg_tests)

# Selection engine unit tests
add_executable(etil_selection_tests
    unit/test_selection_engine.cpp
)

target_link_libraries(etil_selection_tests
    PRIVATE
        etil
        GTest::gtest_main
)

etil_add_link_map(etil_selection_tests)
gtest_discover_tests(etil_selection_tests)

# Evolution engine unit tests
add_executable(etil_evolution_tests
    unit/test_evolution.cpp
)

target_link_libraries(etil_evolution_tests
    PRIVATE
        etil
        GTest::gtest_main
)

etil_add_link_map(etil_evolution_tests)
gtest_discover_tests(etil_evolution_tests)

# AST decompiler unit tests
add_executable(etil_decompiler_tests
    unit/test_decompiler.cpp
)

target_link_libraries(etil_decompiler_tests
    PRIVATE
        etil
        GTest::gtest_main
)

etil_add_link_map(etil_decompiler_tests)
gtest_discover_tests(etil_decompiler_tests)

# AST compiler unit tests (round-trip fidelity)
add_executable(etil_ast_compiler_tests
    unit/test_ast_compiler.cpp
)

target_link_libraries(etil_ast_compiler_tests
    PRIVATE
        etil
        GTest::gtest_main
)

etil_add_link_map(etil_ast_compiler_tests)
gtest_discover_tests(etil_ast_compiler_tests)

# Stack simulator unit tests
add_executable(etil_stack_simulator_tests
    unit/test_stack_simulator.cpp
)

target_link_libraries(etil_stack_simulator_tests
    PRIVATE
        etil
        GTest::gtest_main
)

etil_add_link_map(etil_stack_simulator_tests)
gtest_discover_tests(etil_stack_simulator_tests)

# Type repair unit tests
add_executable(etil_type_repair_tests
    unit/test_type_repair.cpp
)

target_link_libraries(etil_type_repair_tests
    PRIVATE
        etil
        GTest::gtest_main
)

etil_add_link_map(etil_type_repair_tests)
gtest_discover_tests(etil_type_repair_tests)

# HTTP transport unit tests (helper methods only — no socket, safe to run natively)
add_executable(etil_http_tests
    unit/test_http_transport.cpp
)

target_link_libraries(etil_http_tests
    PRIVATE
        etil_mcp
        GTest::gtest_main
)

etil_add_link_map(etil_http_tests)
gtest_discover_tests(etil_http_tests)

# TIL integration tests (bash scripts that run .til files through the REPL)
file(GLOB_RECURSE TIL_TEST_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/til/test_*.sh")
foreach(script ${TIL_TEST_SCRIPTS})
    get_filename_component(test_name ${script} NAME_WE)
    add_test(
        NAME "TIL.${test_name}"
        COMMAND bash ${script}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    set_tests_properties("TIL.${test_name}" PROPERTIES
        ENVIRONMENT "ETIL_REPL=${CMAKE_BINARY_DIR}/bin/etil_repl"
    )
endforeach()
