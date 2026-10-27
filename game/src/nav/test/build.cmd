@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-740 (test vector 1)
rem Build and run the stand-alone navigation checker against the stock .nvb files.
rem usage: build.cmd [nvb directory]   (default C:\openmerc-data\g5\pak0\maps)
rem env: VCVARSALL (default C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat), OM_BUILD_DIR (default build-G5-x86)
setlocal
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set OUT=%~dp0..\..\..\..\build-G5-x86
if not "%OM_BUILD_DIR%"=="" set OUT=%OM_BUILD_DIR%
if not exist "%OUT%\navcheck" mkdir "%OUT%\navcheck"
cl /nologo /O2 /EHs-c- /D_HAS_EXCEPTIONS=0 /D_CRT_SECURE_NO_WARNINGS /std:c++17 /W3 /fp:precise ^
   /Fo"%OUT%\navcheck\\" /Fe"%OUT%\navcheck\navcheck.exe" "%~dp0navcheck.cpp" "%~dp0..\nav_data.cpp" setargv.obj || exit /b 1
set DIR=%1
if "%DIR%"=="" set DIR=C:\openmerc-data\g5\pak0\maps
"%OUT%\navcheck\navcheck.exe" "%DIR%\*.nvb"
