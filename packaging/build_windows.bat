@echo off
REM ============================================================
REM  Leptid - single-file Windows build (MinGW-w64 / MSYS2)
REM  From a fresh clone, in an MSYS2 MinGW64 shell:
REM    pacman -S mingw-w64-x86_64-gcc
REM    packaging\build_windows.bat
REM  Produces bin\leptid.exe: ONE portable exe with fonts, brush
REM  tips and the app icon embedded (no runtime DLLs). The icon
REM  and version info come from packaging\leptid.rc via windres.
REM  Self-contained: generates bin\fontdata.cpp / bin\assets.cpp
REM  with tools\bin2cpp.cpp first, so no Makefile run is needed.
REM ============================================================
setlocal
where g++ >nul 2>nul || (echo g++ not found - run from an MSYS2 MinGW64 shell or install MinGW-w64 & exit /b 1)

if not exist bin mkdir bin

echo [1/4] building bin2cpp (asset embedder)...
g++ -std=c++17 -O2 -o bin\bin2cpp.exe tools\bin2cpp.cpp || goto :fail

echo [2/4] embedding fonts...
bin\bin2cpp.exe third_party\fonts\DejaVuSans.ttf DejaVuSans_font > bin\fontdata.cpp
bin\bin2cpp.exe third_party\fonts\DejaVuSans-Bold.ttf DejaVuSansBold_font >> bin\fontdata.cpp

echo [3/4] embedding brush tips and icons...
type nul > bin\assets.cpp
for %%f in (assets\brushes\tip_*.png) do call :embed "%%f"
for %%f in (assets\icons\leptid_*.png) do call :embed "%%f"

echo [4/4] compiling bin\leptid.exe...
set RESOBJ=
where windres >nul 2>nul && (windres packaging\leptid.rc -O coff -o bin\leptid.res.obj && set RESOBJ=bin\leptid.res.obj)
if "%RESOBJ%"=="" echo   (windres not found - building without the Explorer icon)
g++ -std=c++17 -O2 -Isrc -Ithird_party\stb -o bin\leptid.exe ^
  src\app\*.cpp src\core\*.cpp src\formats\*.cpp src\model\*.cpp ^
  src\rig\*.cpp src\export\*.cpp src\ui\*.cpp src\ai\*.cpp ^
  src\platform\platform_win32.cpp bin\fontdata.cpp bin\assets.cpp %RESOBJ% ^
  -static -static-libgcc -static-libstdc++ -lgdi32 -luser32 -lws2_32 -mwindows || goto :fail

echo Built bin\leptid.exe (single file, fonts/brushes/icon embedded)
exit /b 0

:embed
bin\bin2cpp.exe %~1 %~n1_png >> bin\assets.cpp
exit /b 0

:fail
echo BUILD FAILED - see the errors above
exit /b 1
