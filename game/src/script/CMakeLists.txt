# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc DesignerScript VM (static library om_script) and its test tool omscript_test.
# Built standalone for now (game/src/script/build.cmd); G18 links om_script into gamex86 later with
# add_subdirectory(src/script).
# Implements: SPEC-140, SPEC-141, SPEC-750, SPEC-751, SPEC-752
cmake_minimum_required(VERSION 3.20)
project(OMScript CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# same runtime and code generation as gamex86 (game/CMakeLists.txt)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(om_script STATIC
  script_host.h script_vm.h script_internal.h
  script_value.cpp script_vm.cpp script_save.cpp script_ai_table.inc)
target_include_directories(om_script PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(om_script PRIVATE /W3 /wd4244 /wd4305 /wd4996 /wd4018 /wd4267 /EHs-c- /GR- /fp:precise)
  target_compile_definitions(om_script PRIVATE _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
endif()

if(NOT OM_SCRIPT_NO_TESTS)
  add_executable(omscript_test test/mock_host.h test/mock_host.cpp test/asm.h test/omscript_test.cpp)
  target_link_libraries(omscript_test PRIVATE om_script)
  if(MSVC)
    target_compile_options(omscript_test PRIVATE /W3 /wd4244 /wd4305 /wd4996 /wd4267 /EHs-c- /GR-)
    target_compile_definitions(omscript_test PRIVATE _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
  endif()
endif()
