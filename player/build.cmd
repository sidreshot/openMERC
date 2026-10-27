@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-003
rem Build the OpenMerc player module (player.dll, 32-bit) and the tracing proxy (omptrace.dll)
rem with MSVC + Ninja. No third-party libraries.
rem usage: build.cmd [Release|Debug]
rem env: VCVARSALL (default C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat)
rem Output: <repo>\build-player-x86-<cfg>\player.dll - copy it over <game>\base\player.dll
rem (keep the original as player.dll.orig).
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set SRC=%~dp0.
set OUT=%~dp0..\build-player-x86-%CFG%
rem OM_BUILD_DIR: separate build tree per concurrent build (X1, as in game\build.cmd)
if not "%OM_BUILD_DIR%"=="" set OUT=%OM_BUILD_DIR%
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
cmake --build "%OUT%" || exit /b 1
echo Built into %OUT%
