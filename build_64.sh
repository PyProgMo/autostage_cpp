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
-o "$SCRIPT_DIR/build/SpectrometerServer.exe" \
"$SCRIPT_DIR/.src/SpectrometerServer.cpp" \
"$SCRIPT_DIR/.src/Logger.cpp" \
"$SCRIPT_DIR/.src/AndorCamera.cpp" \
-I"$SCRIPT_DIR/.src" \
-I"C:/msys64/mingw64/include/opencv4" \
-L"C:/msys64/mingw64/lib" -lopencv_core -lopencv_imgcodecs -lopencv_imgproc \
-Wall

echo "Successfully built build/SpectrometerServer.exe"

echo "Building 64-bit ConsoleApp..."
"$CXX" -O2 -m64 \
-o "$SCRIPT_DIR/build/ConsoleApp.exe" \
"$SCRIPT_DIR/.src/ConsoleApp.cpp" \
"$SCRIPT_DIR/.src/PIStageProxy.cpp" \
"$SCRIPT_DIR/.src/AndorCameraProxy.cpp" \
"$SCRIPT_DIR/.src/Logger.cpp" \
-I"$SCRIPT_DIR/.src" \
-I"C:/msys64/mingw64/include/opencv4" \
-L"C:/msys64/mingw64/lib" -lopencv_core -lopencv_imgcodecs -lopencv_imgproc \
-Wall

echo "Successfully built build/ConsoleApp.exe"