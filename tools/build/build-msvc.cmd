@echo off
rem Build OpenMerc with MSVC + Ninja + vcpkg (classic mode).
rem usage: build-msvc.cmd x86|x64 [Release|Debug]
rem env: VCVARSALL (default C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat)
rem      VCPKG_ROOT (default %USERPROFILE%\openttd-mod\vcpkg)
setlocal
set ARCH=%1
if "%ARCH%"=="" set ARCH=x86
set CFG=%2
if "%CFG%"=="" set CFG=Release
if "%VCVARSALL%"=="" set VCVARSALL=C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
if "%OM_VCPKG%"=="" set OM_VCPKG=%USERPROFILE%\openttd-mod\vcpkg
call "%VCVARSALL%" %ARCH% >nul || exit /b 1
rem vcvarsall points VCPKG_ROOT at the Build Tools copy; use ours
set VCPKG_ROOT=%OM_VCPKG%
set SRC=%~dp0..\..\engine
set OUT=%~dp0..\..\build-%ARCH%-%CFG%
rem OM_BUILD_DIR: separate build tree per concurrent build (e.g. build-I4a-x86)
if not "%OM_BUILD_DIR%"=="" set OUT=%OM_BUILD_DIR%
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=%CFG% ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=%ARCH%-windows -DVCPKG_MANIFEST_MODE=OFF ^
  -DCURL_SUPPORT=OFF -DGLES3_RENDERER=ON || exit /b 1
cmake --build "%OUT%" -j %NUMBER_OF_PROCESSORS% || exit /b 1
rem OpenMerc's own data archive (data\base -> release\zz_openmerc.pak, V1)
call "%~dp0..\..\data\build.cmd" "%OUT%" || exit /b 1
echo Built into %OUT%
