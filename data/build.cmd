@echo off
rem Build OpenMerc's own data archive base\zz_openmerc.pak from data\base (tools\mkpak.py).
rem usage: data\build.cmd [<build dir>]   (default: build-x86-Release; output <build dir>\release\zz_openmerc.pak)
rem The archive carries OpenMerc's replacement menu pages and string packages (data/README.md). It is
rem an "extra" archive (SPEC-300 order: scanned after pak0..9, zmain, zsond), so its files override the
rem stock ones; its name must not end in PAK<digit>.PAK. tools\build\build-msvc.cmd calls this script.
setlocal
set OUT=%1
if "%OUT%"=="" set OUT=%~dp0..\build-x86-Release
rem call: python may be a .cmd shim, which would not return without it
call python "%~dp0..\tools\mkpak.py" "%~dp0base" "%OUT%\release\zz_openmerc.pak" || exit /b 1
