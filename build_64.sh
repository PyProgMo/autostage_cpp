#!/bin/bash

set -e  # stop on first error

echo "Building 64-bit RasterScan..."

# Use MinGW64 compiler explicitly
CXX="/c/msys64/mingw64/bin/x86_64-w64-mingw32-g++"
CXXFLAGS="-O2 -m64 -I$SCRIPT_DIR/.src -I/c/msys64/mingw64/include/opencv4 -Wall"
LDFLAGS="-L/c/msys64/mingw64/lib -lopencv_core -lopencv_imgcodecs -lopencv_imgproc"

# Resolve script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ensure build dir exists
mkdir -p "$SCRIPT_DIR/build"

# Global tracking variable to know if we need to re-link an executable
NEEDS_LINKING=false

# Helper function to compile a single .cpp file to a .o file if it has changed
compile_if_changed() {
    local src="$1"
    local obj="$2"

    # Check if .o file doesn't exist, or if .cpp file is newer than .o file
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  Compiling $(basename "$src")..."
        "$CXX" $CXXFLAGS -c "$src" -o "$obj"
        NEEDS_LINKING=true
    else
        echo "  $(basename "$src") is up to date."
    fi
}

# ==============================================================================
# TARGET 1: SpectrometerServer.exe
# ==============================================================================
echo "Checking dependencies for SpectrometerServer.exe..."
NEEDS_LINKING=false

SERVER_EXE="$SCRIPT_DIR/build/SpectrometerServer.exe"
SERVER_SRCS=(
    "$SCRIPT_DIR/.src/SpectrometerServer.cpp"
    "$SCRIPT_DIR/.src/Logger.cpp"
    "$SCRIPT_DIR/.src/AndorCamera.cpp"
)
SERVER_OBJS=()

# Process object files for Server
for src in "${SERVER_SRCS[@]}"; do
    filename=$(basename "${src%.cpp}")
    obj="$SCRIPT_DIR/build/${filename}_server.o" # Distinct suffix to prevent collision
    SERVER_OBJS+=("$obj")
    compile_if_changed "$src" "$obj"
done

# Link Server if required
if [ "$NEEDS_LINKING" = true ] || [ ! -f "$SERVER_EXE" ]; then
    echo "Linking $SERVER_EXE..."
    "$CXX" -m64 "${SERVER_OBJS[@]}" -o "$SERVER_EXE" $LDFLAGS
    echo "Successfully built build/SpectrometerServer.exe"
else
    echo "SpectrometerServer.exe is up to date."
fi

echo "----------------------------------------------------"

# ==============================================================================
# TARGET 2: ConsoleApp.exe
# ==============================================================================
echo "Checking dependencies for ConsoleApp.exe..."
NEEDS_LINKING=false

CONSOLE_EXE="$SCRIPT_DIR/build/ConsoleApp.exe"
CONSOLE_SRCS=(
    "$SCRIPT_DIR/.src/ConsoleApp.cpp"
    "$SCRIPT_DIR/.src/RasterScan.cpp"
    "$SCRIPT_DIR/.src/PIStageProxy.cpp"
    "$SCRIPT_DIR/.src/AndorCameraProxy.cpp"
    "$SCRIPT_DIR/.src/Logger.cpp"
)
CONSOLE_OBJS=()

# Process object files for ConsoleApp
for src in "${CONSOLE_SRCS[@]}"; do
    filename=$(basename "${src%.cpp}")
    obj="$SCRIPT_DIR/build/${filename}_console.o"
    CONSOLE_OBJS+=("$obj")
    compile_if_changed "$src" "$obj"
done

# Link ConsoleApp if required
if [ "$NEEDS_LINKING" = true ] || [ ! -f "$CONSOLE_EXE" ]; then
    echo "Linking $CONSOLE_EXE..."
    "$CXX" -m64 "${CONSOLE_OBJS[@]}" -o "$CONSOLE_EXE" $LDFLAGS
    echo "Successfully built build/ConsoleApp.exe"
else
    echo "ConsoleApp.exe is up to date."
fi