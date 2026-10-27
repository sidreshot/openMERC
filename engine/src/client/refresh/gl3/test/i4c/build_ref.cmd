@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-002 test harness (build helper for the renderer module)
rem I4c: rebuilds only the renderer targets (ref_omgl, ref_gl3, ref_gles3) of an already configured tree, so a
rem half-finished edit of another package elsewhere in the engine does not block renderer tests.
rem usage: build_ref.cmd [build dir, default build-I4c-x86]
setlocal
set OUT=%1
if "%OUT%"=="" set OUT=%~dp0..\..\..\..\..\..\..\build-I4c-x86
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul 2>nul
cmake --build "%OUT%" -j %NUMBER_OF_PROCESSORS% --target ref_omgl ref_gl3 ref_gles3 || exit /b 1
echo Built renderers into %OUT%
