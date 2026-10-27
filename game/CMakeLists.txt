# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc game module: gamex86.dll, a drop-in replacement for the original 32-bit gamex86.dll 1.07f
# (first slice: world core, world entities, player/client, items, projectiles and equipment).
# Also builds g18probe.dll, the black-box interface probe used to measure unspecified layouts.
# Implements: SPEC-001, SPEC-006, SPEC-760, SPEC-761, SPEC-780, SPEC-781, SPEC-782
cmake_minimum_required(VERSION 3.20)
project(OMGame CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(WARNING "gamex86.dll must be built for 32-bit x86 to load into SoF.exe")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/../tools/build/OMModuleExports.cmake)

# static C runtime: the module imports only KERNEL32 like the original (SPEC-006)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

file(GLOB OM_GAME_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB OM_GAME_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h)
# Game subsystems of later packages live in their own subdirectories src/<package>/ (G2 ai, G3 damage, G5 nav and
# bots, G7 props, G9 modes ...). src/script is the separate om_script library (wired by G6b).
file(GLOB OM_GAME_PKG_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.cpp)
file(GLOB OM_GAME_PKG_H CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.h)
list(FILTER OM_GAME_PKG_CPP EXCLUDE REGEX "/src/script/")
list(FILTER OM_GAME_PKG_H EXCLUDE REGEX "/src/script/")
# Test builds only (G18 black-box runs while a sibling package does not compile): -DOM_GAME_SKIP="roster;ai"
# leaves those src/<package>/ directories out and links test/stubs/<package>.cpp (no-op hooks) instead.
# OM_GAME_SKIP_PACKAGES=ON skips every package. A DLL built this way is never shipped.
set(OM_GAME_SKIP "" CACHE STRING "src/<package> subdirectories to leave out of a test build")
option(OM_GAME_SKIP_PACKAGES "test build without any src/<package>/ subdirectory" OFF)
if(OM_GAME_SKIP_PACKAGES)
  file(GLOB OM_GAME_ALL_PKGS LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src/*)
  foreach(pkg ${OM_GAME_ALL_PKGS})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/${pkg} AND NOT pkg STREQUAL "script")
      list(APPEND OM_GAME_SKIP ${pkg})
    endif()
  endforeach()
endif()
foreach(pkg ${OM_GAME_SKIP})
  message(STATUS "OM_GAME_SKIP: test build without src/${pkg}/")
  list(FILTER OM_GAME_PKG_CPP EXCLUDE REGEX "/src/${pkg}/")
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs/${pkg}.cpp)
    list(APPEND OM_GAME_PKG_CPP ${CMAKE_CURRENT_SOURCE_DIR}/test/stubs/${pkg}.cpp)
  endif()
endforeach()
list(APPEND OM_GAME_CPP ${OM_GAME_PKG_CPP})
set(OM_GAME_SOURCES ${OM_GAME_CPP} ${OM_GAME_H} ${OM_GAME_PKG_H})

if(OM_GAME_CPP)
  add_library(gamex86 SHARED ${OM_GAME_SOURCES} gamex86.def)
  target_compile_definitions(gamex86 PRIVATE _CRT_SECURE_NO_WARNINGS GAME_INCLUDE)
  set_target_properties(gamex86 PROPERTIES PREFIX "" OUTPUT_NAME "gamex86")
  if(MSVC)
    target_compile_options(gamex86 PRIVATE /W3 /wd4244 /wd4305 /wd4996 /wd4018 /wd4267 /EHs-c- /GR- /fp:precise)
    target_compile_definitions(gamex86 PRIVATE _HAS_EXCEPTIONS=0)
    # linker map + PDB for symbolising g18crash.log offsets (src/g_crash.cpp); /OPT:REF keeps the size down
    target_compile_options(gamex86 PRIVATE /Zi)
    target_link_options(gamex86 PRIVATE /MAP /DEBUG /OPT:REF)
  endif()
  # G6b: the DesignerScript VM library (game/src/script, package G6a), wired by game/src/scriptglue
  set(OM_SCRIPT_NO_TESTS ON)
  add_subdirectory(src/script)
  target_link_libraries(gamex86 PRIVATE om_script)
  if(NOT MSVC)
    # the sources were written for MSVC's defaults: no strict aliasing, no exceptions or RTTI needed
    target_compile_options(gamex86 PRIVATE -fno-strict-aliasing -fno-exceptions -fno-rtti)
  endif()
  om_elf_module_exports(gamex86 ${CMAKE_CURRENT_SOURCE_DIR}/gamex86.def)
endif()

# G3 unit tests (SPEC-720..722) as a portable target; on Windows src/damage/test/build_tests.cmd stays the reference
if(WIN32)
  set(OM_G3_TESTS_DEFAULT OFF)
else()
  set(OM_G3_TESTS_DEFAULT ON)
endif()
option(OM_G3_TESTS "build the G3 unit tests (g3tests)" ${OM_G3_TESTS_DEFAULT})
if(OM_G3_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/damage/g3_damage.cpp)
  set(G3D ${CMAKE_CURRENT_SOURCE_DIR}/src/damage)
  add_executable(g3tests
    ${G3D}/test/g3_tests.cpp ${G3D}/test/g3_mock.cpp ${G3D}/test/g3_stubs.cpp
    ${G3D}/g3_damage.cpp ${G3D}/g3_shot.cpp ${G3D}/g3_gore.cpp ${G3D}/g3_body.cpp ${G3D}/g3_human.cpp
    ${G3D}/g3_animals.cpp ${G3D}/g3_fx.cpp ${G3D}/g3_ghoul.cpp ${G3D}/g3_save.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/effects/fx_emit.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/q_shared.cpp)
  target_compile_definitions(g3tests PRIVATE _CRT_SECURE_NO_WARNINGS GAME_INCLUDE)
  target_include_directories(g3tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  if(NOT MSVC)
    target_compile_options(g3tests PRIVATE -fno-strict-aliasing)
    target_link_libraries(g3tests PRIVATE m)
  endif()
endif()

# black-box probes of the original 32-bit SoF.exe interface: Windows x86 only
if(NOT WIN32)
  return()
endif()

add_library(g18probe SHARED probe/probe.cpp)
target_compile_definitions(g18probe PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(g18probe PROPERTIES PREFIX "" OUTPUT_NAME "g18probe")
if(MSVC)
  target_compile_options(g18probe PRIVATE /W3 /EHs-c- /GR-)
endif()

# RunFrame logging proxy (test tool): forwards to the module under test and logs the game time it returns
add_library(g18rfproxy SHARED probe/rfproxy.cpp)
target_compile_definitions(g18rfproxy PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(g18rfproxy PROPERTIES PREFIX "" OUTPUT_NAME "g18rfproxy")
