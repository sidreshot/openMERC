# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc player module: player.dll, a drop-in replacement for the original 32-bit player.dll 1.07f
# (weapons, ammunition, armor, items, inventory, in-view weapon models, weapon commands, weapon prediction).
# Also builds omptrace.dll, the black-box tracing proxy used to compare it with the original.
# Implements: SPEC-003, SPEC-600, SPEC-601, SPEC-602
cmake_minimum_required(VERSION 3.20)
project(OMPlayer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(WARNING "player.dll must be built for 32-bit x86 to load into SoF.exe / gamex86.dll")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/../tools/build/OMModuleExports.cmake)

# static C runtime: the module imports only KERNEL32 like the original (SPEC-003)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(OM_PLAYER_SOURCES
  src/om_ghoul.h
  src/om_player_api.h
  src/pm_local.h
  src/pm_main.cpp
  src/pm_tables.cpp
  src/pm_viewmodel.cpp
  src/pm_inventory.cpp
  src/pm_weapons.cpp
  src/pm_weaponrules.cpp
  src/pm_items.cpp
  src/pm_persist.cpp
  src/pm_predict.cpp)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/pm_main.cpp)
  add_library(player SHARED ${OM_PLAYER_SOURCES} player.def)
  # Ghoul interfaces from the engine's library (I3); without the define src/om_ghoul.h uses its own copy
  target_compile_definitions(player PRIVATE _CRT_SECURE_NO_WARNINGS OM_PLAYER_USE_ENGINE_GHOUL)
  set_target_properties(player PROPERTIES PREFIX "" OUTPUT_NAME "player")
  if(MSVC)
    # no C++ exceptions across the module boundary: build without exception handling
    target_compile_options(player PRIVATE /W4 /wd4100 /wd4201 /wd4996 /EHs-c- /GR-)
    target_compile_definitions(player PRIVATE _HAS_EXCEPTIONS=0)
  else()
    target_compile_options(player PRIVATE -fno-strict-aliasing -fno-exceptions -fno-rtti)
  endif()
  om_elf_module_exports(player ${CMAKE_CURRENT_SOURCE_DIR}/player.def)

  # host unit tests of the engine-independent rules (same sources): build-player-x86-*/pm_unit.exe
  add_executable(pm_unit test/unit/pm_unit.cpp ${OM_PLAYER_SOURCES})
  target_compile_definitions(pm_unit PRIVATE _CRT_SECURE_NO_WARNINGS OM_PLAYER_USE_ENGINE_GHOUL)
  if(MSVC)
    target_compile_options(pm_unit PRIVATE /W3 /EHs-c- /GR-)
    target_compile_definitions(pm_unit PRIVATE _HAS_EXCEPTIONS=0)
  endif()
endif()

# tracing proxy of the original 32-bit player.dll (x86 inline assembly): Windows x86 only
if(NOT WIN32)
  return()
endif()
add_library(omptrace SHARED test/trace/omptrace.cpp)
target_compile_definitions(omptrace PRIVATE _CRT_SECURE_NO_WARNINGS OM_PLAYER_USE_ENGINE_GHOUL)
set_target_properties(omptrace PROPERTIES PREFIX "" OUTPUT_NAME "omptrace")
if(MSVC)
  target_compile_options(omptrace PRIVATE /W3 /EHs-c- /GR-)
  target_link_options(omptrace PRIVATE /SAFESEH:NO)
endif()
