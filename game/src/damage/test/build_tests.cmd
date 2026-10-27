@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-720, SPEC-721, SPEC-722 (unit tests of package G3)
rem Builds and runs the G3 unit tests (32-bit MSVC). Output: <repo>\build-G3-x86\tests\g3tests.exe
rem Fixtures: python game\src\damage\test\gen_mockmodels.py (needs the extracted game data; tests skip without it)
setlocal
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
call "%VCVARSALL%" x86 >nul || exit /b 1
set T=%~dp0
set D=%T%..
set G=%T%..\..
set OUT=%T%..\..\..\..\build-G3-x86\tests
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /EHsc /std:c++17 /O1 /W3 /wd4244 /wd4305 /wd4996 /wd4018 /wd4267 /D_CRT_SECURE_NO_WARNINGS /DGAME_INCLUDE /DWIN32 /D_WINDOWS ^
  /I"%G%" /Fo"%OUT%\\" /Fe"%OUT%\g3tests.exe" ^
  "%T%g3_tests.cpp" "%T%g3_mock.cpp" "%T%g3_stubs.cpp" ^
  "%D%\g3_damage.cpp" "%D%\g3_shot.cpp" "%D%\g3_gore.cpp" "%D%\g3_body.cpp" "%D%\g3_human.cpp" "%D%\g3_animals.cpp" ^
  "%D%\g3_fx.cpp" "%D%\g3_ghoul.cpp" "%D%\g3_save.cpp" "%G%\effects\fx_emit.cpp" "%G%\q_shared.cpp" || exit /b 1
"%OUT%\g3tests.exe"
