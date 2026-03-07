# GenerateVersion.cmake — run at build time to stamp the build timestamp
string(TIMESTAMP ETIL_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S UTC" UTC)
configure_file("${INPUT_FILE}" "${OUTPUT_FILE}" @ONLY)
