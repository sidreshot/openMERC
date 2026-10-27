@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Implements: SPEC-004, SPEC-040
rem Build the OpenMerc sound module (OMsnd.dll, 32-bit) with MSVC + Ninja + vcpkg (classic mode).
rem usage: build.cmd [Release|Debug]
rem env: VCVARSALL (default C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat)
rem      OM_VCPKG  (default %USERPROFILE%\openttd-mod\vcpkg; needs openal-soft:x86-windows)
rem Output: <repo>\build-sound-x86-<cfg>\OMsnd.dll, OpenAL32.dll and fmt.dll (needed by the vcpkg
rem OpenAL Soft build) - copy all three next to SoF.exe and start the game with +set snd_dll OMsnd
setlocal
set CFG=%1
if "%CFG%"=="" set CFG=Release
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
if "%OM_VCPKG%"=="" set OM_VCPKG=%USERPROFILE%\openttd-mod\vcpkg
call "%VCVARSALL%" x86 >nul || exit /b 1
rem vcvarsall points VCPKG_ROOT at the Build Tools copy; use ours
set VCPKG_ROOT=%OM_VCPKG%
set SRC=%~dp0.
set OUT=%~dp0..\..\build-sound-x86-%CFG%
rem OM_BUILD_DIR: separate build tree per concurrent build (e.g. build-I5s2-x86)
if not "%OM_BUILD_DIR%"=="" set OUT=%OM_BUILD_DIR%
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x86-windows -DVCPKG_MANIFEST_MODE=OFF || exit /b 1
cmake --build "%OUT%" || exit /b 1
echo Built into %OUT%
