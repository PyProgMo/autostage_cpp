#!/bin/bash

set -e  # stop on first error

echo "Building 64-bit RasterScan..."

# Use MinGW64 compiler explicitly
CXX="/c/msys64/mingw64/bin/x86_64-w64-mingw32-g++"

# Resolve script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ensure build dir exists
mkdir -p "$SCRIPT_DIR/build"

"$CXX" -O2 -m64 \
-o "$SCRIPT_DIR/build/RasterScan64.exe" \
"$SCRIPT_DIR/.src/RasterScan.cpp" \
"$SCRIPT_DIR/.src/PIStageProxy.cpp" \
"$SCRIPT_DIR/.src/AndorCamera.cpp" \
-I"$SCRIPT_DIR/.src" \
-Wall

echo "Successfully built build/RasterScan64.exe"