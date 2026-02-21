#!/bin/bash

set -e

# Default build type
BUILD_TYPE=${1:-Release}

# Validate build type
case "$BUILD_TYPE" in
    Debug|Release|RelWithDebInfo|MinSizeRel|PerfProfile)
        ;;
    *)
        echo "Invalid build type: $BUILD_TYPE"
        echo "Allowed values:"
        echo "  Debug"
        echo "  Release"
        echo "  RelWithDebInfo"
        echo "  MinSizeRel"
        echo "  PerfProfile"
        exit 1
        ;;
esac

echo "Build Type: $BUILD_TYPE"

echo "Cleaning old build directory..."
rm -rf build

echo "Creating new build directory..."
mkdir build
cd build

echo "Running CMake..."
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE ..

echo "Building..."
make -j$(nproc)

echo "Build completed successfully!"
