@echo off
setlocal
rem Builds and runs RyytliRNG using CLion's bundled CMake + Ninja + MinGW (no PATH setup needed).
set "CL=C:\Program Files\JetBrains\CLion 2025.3.3\bin"
set "PATH=%CL%\cmake\win\x64\bin;%CL%\ninja\win\x64;%CL%\mingw\bin;%PATH%"
cd /d "%~dp0"

where git >nul 2>nul || (echo [!] git not found in PATH - needed to download SFML. & pause & exit /b 1)

echo [1/2] Configuring (first time downloads SFML)...
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ > build.log 2>&1
if errorlevel 1 goto fail

echo [2/2] Building (first time compiles SFML, takes a few minutes)...
cmake --build build >> build.log 2>&1
if errorlevel 1 goto fail

echo Done. Starting game...
start "" "build\bin\RyytliRNG.exe"
exit /b 0

:fail
echo.
echo ===== BUILD FAILED, last lines of build.log =====
powershell -NoProfile -Command "Get-Content build.log -Tail 40"
pause
exit /b 1
