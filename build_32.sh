#!/bin/bash

# Ensure MSYS2 MinGW32 is first in PATH
export PATH="/c/msys64/mingw32/bin:$PATH"

# Build the project

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"

echo "Compiling  (32-bit)..."
g++ -m32 -O2 -std=gnu++11 -static-libgcc -static-libstdc++ -Wl,-Bstatic -lpthread -Wl,-Bdynamic -lpthread -Wl,-Bdynamic -o ./StageServer.exe ../.src/StageServer.cpp ../.src/PIStage.cpp -I../.src -Wall
