# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc sound module: OMsnd.dll, a drop-in sound backend for the original 32-bit SoF.exe 1.07f
# (select it with `snd_dll OMsnd`) and the only sound backend of the OpenMerc engine: the default backend's game-facing
# rules (SPEC-040) on OpenAL Soft voices (OMsnd 1.0, docs/impl/SND2.md).
# Implements: SPEC-004, SPEC-040
cmake_minimum_required(VERSION 3.20)
project(OMsnd C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(WARNING "OMsnd must be built for 32-bit x86 to load into SoF.exe")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/../../tools/build/OMModuleExports.cmake)

find_package(OpenAL CONFIG REQUIRED)

add_library(OMsnd SHARED
  src/snd_dma.c
  src/snd_mem.c
  src/snd_adp.c
  src/snd_voice.c
  src/snd_engine.c
  src/snd_ambient.c
  src/snd_music.c
  src/snd_efx.c
  src/snd_local.h
  OMsnd.def)

target_link_libraries(OMsnd PRIVATE OpenAL::OpenAL)
target_compile_definitions(OMsnd PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(OMsnd PRIVATE /W4 /wd4100 /wd4201 /wd4204 /wd4221)
endif()
set_target_properties(OMsnd PROPERTIES PREFIX "" OUTPUT_NAME "OMsnd")
if(NOT MSVC)
  target_compile_options(OMsnd PRIVATE -fno-strict-aliasing)
  target_link_libraries(OMsnd PRIVATE m ${CMAKE_DL_LIBS})
endif()
# the engine functions (Com_Printf, Cvar_Get ...) are looked up at run time in the executable, so on ELF
# systems the module links with --no-undefined against libc, libm and OpenAL only
om_elf_module_exports(OMsnd ${CMAKE_CURRENT_SOURCE_DIR}/OMsnd.def)

if(NOT WIN32)
  return()
endif()

# OpenAL32.dll (and the fmt.dll it needs, copied by vcpkg's applocal step) go next to OMsnd.dll;
# all of them go next to SoF.exe
add_custom_command(TARGET OMsnd POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:OpenAL::OpenAL> $<TARGET_FILE_DIR:OMsnd>)
