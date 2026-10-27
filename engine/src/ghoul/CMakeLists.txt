# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc Ghoul model system - static library (C++) and its test program.
# Implements: SPEC-010, SPEC-011, SPEC-012, SPEC-013, SPEC-014, SPEC-120, SPEC-121
#
# Built as part of engine/CMakeLists.txt (add_subdirectory) or standalone:
#   cmake -S engine/src/ghoul -B build-ghoul -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.16)
project(omghoul LANGUAGES CXX)

set(OMGHOUL_SOURCES
	ghoul_common.cpp
	ghoul_tables.cpp
	ghoul_codec.cpp
	ghb_file.cpp
	ghoul_text.cpp
	ghoul_system.cpp
	ghoul_object.cpp
	ghoul_inst.cpp
	ghoul_pose.cpp
	ghoul_bits.cpp
	ghoul_save.cpp
	ghoul_net.cpp
	ghoul_draw.cpp
	ghoul_texture.cpp
	ghoul_capi.cpp
)

add_library(omghoul STATIC ${OMGHOUL_SOURCES})
target_include_directories(omghoul PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(omghoul PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
	POSITION_INDEPENDENT_CODE ON)
if(MSVC)
	target_compile_options(omghoul PRIVATE /W3 /wd4244 /wd4305 /wd4018 /wd4996 /EHsc)
else()
	target_compile_options(omghoul PRIVATE -Wall -fno-strict-aliasing)
endif()

option(OMGHOUL_TESTS "Build the Ghoul test program" ON)
if(OMGHOUL_TESTS)
	add_executable(omghoul_test tests/ghoul_test.cpp tests/test_util.cpp tests/test_runtime.cpp tests/test_savenet.cpp
		tests/test_render.cpp tests/soft_gl.cpp tests/export.cpp)
	target_link_libraries(omghoul_test PRIVATE omghoul)
	set_target_properties(omghoul_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
	if(MSVC)
		target_compile_options(omghoul_test PRIVATE /W3 /wd4244 /wd4305 /wd4018 /wd4996 /EHsc)
	endif()
endif()
