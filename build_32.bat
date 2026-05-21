@echo off
echo Building 32-bit StageServer...

:: Assumes mingw32 is in your PATH. If you have a specific path to x86 compiler, set it here.
set CXX=i686-w64-mingw32-g++

%CXX% -O2 -m32 -o build\StageServer.exe .src\StageServer.cpp .src\PIStage.cpp -I.src -Wall
if %ERRORLEVEL% equ 0 (
    echo Successfully built build\StageServer.exe
) else (
    echo Build failed.
)
