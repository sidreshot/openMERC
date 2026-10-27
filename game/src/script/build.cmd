@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-750
rem Build the DesignerScript VM library and omscript_test (32-bit, MSVC + Ninja, all cores).
rem usage: build.cmd [Release|Debug]
rem env: VCVARSALL (default C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat)
rem Output: <repo>\build-G6a-x86-<cfg>\omscript_test.exe
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set SRC=%~dp0.
set OUT=%~dp0..\..\..\build-G6a-x86-%CFG%
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% || exit /b 1
cmake --build "%OUT%" -j %NUMBER_OF_PROCESSORS% || exit /b 1
echo Built into %OUT%
