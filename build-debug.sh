#!/bin/bash -x

BUILD_DIRECTORY=/workspace/build-debug

cmake \
	-GNinja \
	-DCMAKE_BUILD_TYPE=Debug \
	-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined" \
        -B ${BUILD_DIRECTORY}
