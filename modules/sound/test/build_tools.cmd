@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-004, SPEC-040 (black-box test tools for OMsnd; no game code)
rem Builds the OMsnd test tools with MSVC (x86): proccap.exe (process-loopback recorder) and
rem dsfocus.dll (dsound.dll proxy that adds DSBCAPS_GLOBALFOCUS, see dsfocus.c).
rem usage: build_tools.cmd        output: %OM_BUILD_DIR%\test (default <repo>\build-I5s2-x86\test)
setlocal
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set OUT=%OM_BUILD_DIR%
if "%OUT%"=="" set OUT=%~dp0..\..\..\build-I5s2-x86
set OUT=%OUT%\test
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /EHsc /W3 /Fo"%OUT%\\" /Fe"%OUT%\proccap.exe" "%~dp0proccap.cpp" || exit /b 1
if exist "%~dp0dsfocus.c" (
  cl /nologo /O2 /W3 /LD /Fo"%OUT%\\" /Fe"%OUT%\dsound.dll" "%~dp0dsfocus.c" /link /DEF:"%~dp0dsfocus.def" user32.lib uuid.lib || exit /b 1
)
echo Built into %OUT%
