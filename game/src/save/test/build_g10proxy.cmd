@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-001
rem Build the G10 test proxy game module (test tool): <repo>\build-G10-x86\g10proxy\gamex86.dll
setlocal
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set OUT=%~dp0..\..\..\..\build-G10-x86\g10proxy
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /MT /LD /EHs-c- /GR- /D_CRT_SECURE_NO_WARNINGS "%~dp0g10proxy.cpp" /Fo"%OUT%\\" /Fe"%OUT%\gamex86.dll" /link /NOLOGO || exit /b 1
echo Built %OUT%\gamex86.dll
