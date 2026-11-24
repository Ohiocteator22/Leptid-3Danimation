@echo off
REM ============================================================
REM  Leptid - single-file Windows build (no external DLLs)
REM  Option A: MinGW-w64 (MSYS2: pacman -S mingw-w64-x86_64-gcc make)
REM    mingw32-make static-windows
REM  Option B: MSVC (x64 Native Tools Command Prompt)
REM    cl /std:c++17 /O2 /MT /EHsc /Isrc /Ithird_party\stb src\*.cpp ... (see README)
REM  Both produce ONE portable leptid.exe with the app icon embedded.
REM ============================================================
where windres >nul 2>nul
if %errorlevel%==0 (
  windres packaging/leptid.rc -O coff -o leptid.res.obj
  g++ -std=c++17 -O2 -Isrc -Ithird_party/stb -o bin\leptid.exe src\app\*.cpp src\core\*.cpp src\formats\*.cpp src\model\*.cpp src\rig\*.cpp src\export\*.cpp src\ui\*.cpp src\platform\platform_win32.cpp src\ai\*.cpp bin\fontdata.cpp bin\assets.cpp leptid.res.obj -static -static-libgcc -static-libstdc++ -lgdi32 -luser32 -mwindows
  echo Built bin\leptid.exe (single file, icon embedded)
) else (
  echo windres not found - install MinGW-w64 or use MSVC, see README
)
