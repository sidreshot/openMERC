@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-001 (test tool) - builds g3shim.dll (32-bit) into %OM_BUILD_DIR% or build-G3-x86
setlocal
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set OUT=%~dp0..\..\..\..\build-G3-x86
if not "%OM_BUILD_DIR%"=="" set OUT=%OM_BUILD_DIR%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /LD /MT "%~dp0g3shim.cpp" /Fe"%OUT%\g3shim.dll" /Fo"%OUT%\g3shim.obj" kernel32.lib || exit /b 1
echo Built %OUT%\g3shim.dll
