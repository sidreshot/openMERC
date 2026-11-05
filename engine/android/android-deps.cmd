@echo off
rem openMERC Android: SDL3 and OpenAL Soft for arm64-v8a with the NDK (shared libraries)
setlocal
set A=C:\Users\User0\src\deps\android
set NDK=%A%\ndk\26.3.11579264
set CM=%A%\cmake\3.22.1\bin
set OUT=C:\Users\User0\src\build-android-arm64
set TC=-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_MAKE_PROGRAM=%CM%\ninja.exe -G Ninja -DCMAKE_BUILD_TYPE=Release
set PATH=%CM%;%PATH%

if "%1"=="openal" goto openal
"%CM%\cmake.exe" -S "%A%\src\SDL3-3.4.18" -B "%OUT%\sdl3" %TC% -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DCMAKE_INSTALL_PREFIX=%OUT%\prefix || exit /b 1
"%CM%\cmake.exe" --build "%OUT%\sdl3" --target install || exit /b 1
if "%1"=="sdl" goto :eof

:openal
"%CM%\cmake.exe" -S "%A%\src\openal-soft-1.24.3" -B "%OUT%\openal" %TC% -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_INSTALL_EXAMPLES=OFF -DALSOFT_INSTALL_UTILS=OFF -DCMAKE_INSTALL_PREFIX=%OUT%\prefix || exit /b 1
"%CM%\cmake.exe" --build "%OUT%\openal" --target install || exit /b 1
