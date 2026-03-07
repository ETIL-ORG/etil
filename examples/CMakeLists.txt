# Simple REPL example
add_executable(etil_repl simple_repl.cpp)
target_link_libraries(etil_repl PRIVATE etil replxx::replxx)
etil_add_link_map(etil_repl)

# Benchmark example
add_executable(etil_benchmark benchmark.cpp)
target_link_libraries(etil_benchmark PRIVATE etil benchmark::benchmark)
etil_add_link_map(etil_benchmark)

# MCP server
add_executable(etil_mcp_server mcp_server.cpp)
target_link_libraries(etil_mcp_server PRIVATE etil_mcp)
etil_add_link_map(etil_mcp_server)
