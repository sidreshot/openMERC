@echo off
rem openMERC Android arm64-v8a: engine (libmain.so), ref_gles3, game, player, OMsnd
setlocal
set A=C:\Users\User0\src\deps\android
set NDK=%A%\ndk\26.3.11579264
set CM=%A%\cmake\3.22.1\bin
set OUT=C:\Users\User0\src\build-android-arm64
set SRC=C:\Users\User0\src\openMERC
set TC=-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared -DCMAKE_MAKE_PROGRAM=%CM%\ninja.exe -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%OUT%\prefix -DCMAKE_FIND_ROOT_PATH=%OUT%\prefix
set VSCM=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set PATH=%CM%;%PATH%
set WHAT=%1
if "%WHAT%"=="" set WHAT=all

if not "%WHAT%"=="all" if not "%WHAT%"=="engine" goto game
if not exist "%OUT%\engine\build.ninja" "%VSCM%" -S "%SRC%\engine" -B "%OUT%\engine" %TC% -DCURL_SUPPORT=OFF -DGL3_RENDERER=OFF -DGLES3_RENDERER=ON -DOM_SOFPM_TESTS=OFF -DXDG_SUPPORT=OFF -DSDL3_DIR=%OUT%\prefix\lib\cmake\SDL3 || exit /b 1
"%VSCM%" --build "%OUT%\engine" --target openMerc ref_gles3 || exit /b 1

:game
if not "%WHAT%"=="all" if not "%WHAT%"=="game" goto player
if not exist "%OUT%\game\build.ninja" "%VSCM%" -S "%SRC%\game" -B "%OUT%\game" %TC% || exit /b 1
"%VSCM%" --build "%OUT%\game" || exit /b 1

:player
if not "%WHAT%"=="all" if not "%WHAT%"=="player" goto sound
if not exist "%OUT%\player\build.ninja" "%VSCM%" -S "%SRC%\player" -B "%OUT%\player" %TC% || exit /b 1
"%VSCM%" --build "%OUT%\player" || exit /b 1

:sound
if not "%WHAT%"=="all" if not "%WHAT%"=="sound" goto :eof
if not exist "%OUT%\sound\build.ninja" "%VSCM%" -S "%SRC%\modules\sound" -B "%OUT%\sound" %TC% -DOpenAL_DIR=%OUT%\prefix\lib\cmake\OpenAL || exit /b 1
"%VSCM%" --build "%OUT%\sound" || exit /b 1
