@echo off
setlocal
rem Builds the game and packs a ready-to-play folder + zip for friends: dist\RyytliRNG.zip
rem Upload that zip to GitHub -> Releases. Players unzip it and run start.bat. No compiler needed.
set "CL=C:\Program Files\JetBrains\CLion 2025.3.3\bin"
set "PATH=%CL%\cmake\win\x64\bin;%CL%\ninja\win\x64;%CL%\mingw\bin;%PATH%"
cd /d "%~dp0"

echo [1/3] Building Release...
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ > build.log 2>&1 || goto fail
cmake --build build >> build.log 2>&1 || goto fail

echo [2/3] Copying files to dist\RyytliRNG ...
if exist dist rmdir /s /q dist
mkdir dist\RyytliRNG
copy /y build\bin\RyytliRNG.exe dist\RyytliRNG\ >nul || goto fail
xcopy /e /i /q /y assets dist\RyytliRNG\assets >nul || goto fail
if exist dist\RyytliRNG\assets\img\char\truusele.png del dist\RyytliRNG\assets\img\char\truusele.png
rem safety net: if the exe still wants the MinGW thread DLL, ship it
if exist "%CL%\mingw\bin\libwinpthread-1.dll" copy /y "%CL%\mingw\bin\libwinpthread-1.dll" dist\RyytliRNG\ >nul

(
  echo @echo off
  echo cd /d "%%~dp0"
  echo start "" "RyytliRNG.exe"
) > dist\RyytliRNG\start.bat

(
  echo RYYTLI RNG
  echo.
  echo Mangimiseks: tee start.bat peal topeltklikk.
  echo Kui Windows naitab "Windows protected your PC", vajuta "More info" ja siis "Run anyway".
  echo.
  echo Klahvid: 1-9 ja 0 = vali, Tab = telefon, Esc = paus, M = heli, -/+ = helitugevus
  echo Rekordid salvestuvad faili ryytlirng_rekordid.json siinsamas kaustas.
) > dist\RyytliRNG\LOEMIND.txt

echo [3/3] Zipping...
powershell -NoProfile -Command "Compress-Archive -Path 'dist\RyytliRNG' -DestinationPath 'dist\RyytliRNG.zip' -Force" || goto fail

echo.
echo Valmis: dist\RyytliRNG.zip  (lae see GitHubis Releases alla)
explorer dist
exit /b 0

:fail
echo.
echo ===== FAILED, last lines of build.log =====
powershell -NoProfile -Command "Get-Content build.log -Tail 40"
pause
exit /b 1
