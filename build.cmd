@echo off
rem ---------------------------------------------------------------------------
rem GridEngine: builds the Windows x64 Release executables and prints their paths.
rem
rem Just double-click this file (or run it from the repository folder). It uses
rem the bundled CMake when dependencies\cmake\<version>\bin\cmake.exe exists and
rem falls back to a CMake from PATH otherwise. Visual Studio (MSVC, x64) must be
rem installed - it is what actually compiles the code.
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion
cd /d "%~dp0"

set "CMAKE="
for /d %%D in ("dependencies\cmake\*") do if exist "%%~fD\bin\cmake.exe" set "CMAKE=%%~fD\bin\cmake.exe"
if not defined CMAKE for %%C in (cmake.exe) do if not defined CMAKE set "CMAKE=%%~$PATH:C"
if not defined CMAKE (
    echo.
    echo CMake was not found.
    echo Install CMake 3.20 or newer ^(https://cmake.org/download/^), or restore the
    echo bundled toolchain into dependencies\cmake\^<version^>\bin\cmake.exe.
    echo.
    pause
    exit /b 1
)

echo Using CMake: !CMAKE!
echo Configuring...
"!CMAKE!" -S . -B build -A x64
if errorlevel 1 (
    echo Retrying the configuration without the explicit x64 platform...
    "!CMAKE!" -S . -B build
    if errorlevel 1 goto :failed
)

echo Building (Release)...
"!CMAKE!" --build build --config Release --parallel
if errorlevel 1 goto :failed

echo.
echo Done. Executables:
for %%F in (build\Release\gridengine_editor.exe build\Release\gridengine_demo.exe build\Release\gridengine_game.exe) do if exist "%%~fF" echo   %%~fF
echo.
echo Start the editor from this folder (it loads fonts and dependencies relative
echo to the working directory):
echo   build\Release\gridengine_editor.exe --project ^<path\to\YourGame.grid^>
echo.
pause
exit /b 0

:failed
echo.
echo Build failed - see the messages above.
pause
exit /b 1
