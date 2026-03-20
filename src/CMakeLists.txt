# Core library sources
set(ETIL_CORE_SOURCES
    core/word_impl.cpp
    core/execution_context.cpp
    core/dictionary.cpp
    core/primitives.cpp
    core/compiled_body.cpp
    core/interpreter.cpp
    core/interpret_handlers.cpp
    core/compile_handlers.cpp
    core/control_flow_handlers.cpp
    core/escape_processing.cpp
    core/metadata.cpp
    core/metadata_json.cpp
    core/string_primitives.cpp
    core/array_primitives.cpp
    core/byte_primitives.cpp
    core/map_primitives.cpp
    core/json_primitives.cpp
    core/observable_primitives.cpp
    lvfs/lvfs.cpp
    lvfs/lvfs_primitives.cpp
    fileio/file_io_primitives.cpp
    fileio/uv_session.cpp
    fileio/async_file_io.cpp
)

set(ETIL_SELECTION_SOURCES
    selection/selection_engine.cpp
)

set(ETIL_EVOLUTION_SOURCES
    evolution/ast.cpp
    evolution/decompiler.cpp
    evolution/ast_compiler.cpp
    evolution/stack_simulator.cpp
    evolution/signature_index.cpp
    evolution/type_repair.cpp
    evolution/ast_genetic_ops.cpp
    evolution/genetic_ops.cpp
    evolution/fitness.cpp
    evolution/evolution_engine.cpp
)

set(ETIL_JIT_SOURCES
    # JIT sources will go here
)

set(ETIL_UTILS_SOURCES
    # Utils sources will go here
)

# Main library
add_library(etil
    ${ETIL_CORE_SOURCES}
    ${ETIL_SELECTION_SOURCES}
    ${ETIL_EVOLUTION_SOURCES}
    ${ETIL_JIT_SOURCES}
    ${ETIL_UTILS_SOURCES}
)

target_include_directories(etil
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(etil
    PUBLIC
        Threads::Threads
        ${llvm_libs}
        TBB::tbb
        absl::flat_hash_map
        absl::flat_hash_set
        absl::synchronization
        spdlog::spdlog
        nlohmann_json::nlohmann_json
        uv_a
)

# HTTP client primitives (http-get)
if(ETIL_BUILD_HTTP_CLIENT)
    target_sources(etil PRIVATE
        net/url_validation.cpp
        net/http_primitives.cpp
    )
    target_link_libraries(etil PUBLIC httplib::httplib)
    target_compile_definitions(etil PUBLIC ETIL_HTTP_CLIENT_ENABLED)
endif()

# MongoDB primitives (mongo-find, mongo-insert, mongo-update, mongo-delete)
if(ETIL_BUILD_MONGODB)
    target_sources(etil PRIVATE
        db/mongo_client.cpp
        db/mongo_primitives.cpp
    )
    target_link_libraries(etil PUBLIC mongocxx_static bsoncxx_static)
    target_compile_definitions(etil PUBLIC ETIL_MONGODB_ENABLED)
endif()

# Linear algebra primitives (mat-new, mat*, mat-solve, etc.)
target_sources(etil PRIVATE
    core/matrix_primitives.cpp
)
target_link_libraries(etil PUBLIC ${BLAS_LIBRARIES} ${LAPACK_LIBRARIES} ${LAPACKE_LIBRARIES})
if(DEFINED OPENBLAS_INCLUDE_DIRS)
    # FetchContent OpenBLAS: headers not on default include path
    target_include_directories(etil PRIVATE ${OPENBLAS_INCLUDE_DIRS})
endif()

if(ETIL_USE_JEMALLOC AND JEMALLOC_FOUND)
    target_link_libraries(etil PUBLIC ${JEMALLOC_LIBRARIES})
endif()

# Set compiler-specific options
target_compile_options(etil PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-march=native -mtune=native>
)

# Install rules
install(TARGETS etil
    EXPORT etil-targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY ../include/etil
    DESTINATION include
)

# MCP library (separate from core — MCP deps don't leak into base library)
set(ETIL_MCP_SOURCES
    mcp/json_rpc.cpp
    mcp/http_transport.cpp
    mcp/session.cpp
    mcp/mcp_server.cpp
    mcp/tool_handlers.cpp
    mcp/resource_handlers.cpp
)

if(ETIL_BUILD_JWT)
    list(APPEND ETIL_MCP_SOURCES
        mcp/auth_config.cpp
        mcp/jwt_auth.cpp
        mcp/oauth_github.cpp
        mcp/oauth_google.cpp
    )
endif()

if(ETIL_BUILD_MONGODB)
    list(APPEND ETIL_MCP_SOURCES
        aaa/user_store.cpp
        aaa/audit_log.cpp
    )
endif()

add_library(etil_mcp ${ETIL_MCP_SOURCES})

target_include_directories(etil_mcp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(etil_mcp
    PUBLIC
        etil
        nlohmann_json::nlohmann_json
        httplib::httplib
)

if(ETIL_BUILD_JWT)
    target_link_libraries(etil_mcp PUBLIC jwt-cpp::jwt-cpp)
    target_compile_definitions(etil_mcp PUBLIC ETIL_JWT_ENABLED)
endif()
