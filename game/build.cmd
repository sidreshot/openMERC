@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-001
rem Build the OpenMerc game module (gamex86.dll, 32-bit) and the interface probe (g18probe.dll)
rem with MSVC + Ninja. No third-party libraries.
rem usage: build.cmd [Release|Debug]
rem env: VCVARSALL (default C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat)
rem Output: <repo>\build-game-x86-<cfg>\gamex86.dll - copy it over <game>\base\gamex86.dll
rem (keep the original as gamex86.dll.orig).
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set SRC=%~dp0.
set OUT=%~dp0..\build-game-x86-%CFG%
rem OM_BUILD_DIR: separate build tree per concurrent build (e.g. build-G2-x86)
if not "%OM_BUILD_DIR%"=="" set OUT=%OM_BUILD_DIR%
rem OM_CMAKE_ARGS: extra configure arguments, e.g. -DOM_GAME_SKIP_PACKAGES=ON
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% %OM_CMAKE_ARGS% || exit /b 1
cmake --build "%OUT%" -j %NUMBER_OF_PROCESSORS% || exit /b 1
echo Built into %OUT%
