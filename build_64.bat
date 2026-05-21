@echo off
echo Building 64-bit RasterScan...

:: Assumes mingw64 is in your PATH.
set CXX=x86_64-w64-mingw32-g++

%CXX% -O2 -m64 -o build\RasterScan64.exe .src\RasterScan.cpp .src\PIStageProxy.cpp .src\AndorCamera.cpp -I.src -Wall
if %ERRORLEVEL% equ 0 (
    echo Successfully built build\RasterScan64.exe
) else (
    echo Build failed.
)
