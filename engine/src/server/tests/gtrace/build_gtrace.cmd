@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-001 (test tool)
rem Build the I1b tracing proxy game module: <repo>\build-I1b-x86\gtrace\gamex86.dll
setlocal
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set OUT=%~dp0..\..\..\..\..\build-I1b-x86\gtrace
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c11 /O2 /MT /LD /W3 /D_CRT_SECURE_NO_WARNINGS "%~dp0gtrace.c" /Fo"%OUT%\\" /Fe"%OUT%\gamex86.dll" /link /NOLOGO || exit /b 1
echo Built %OUT%\gamex86.dll
