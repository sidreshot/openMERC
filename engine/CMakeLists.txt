cmake_minimum_required(VERSION 3.31 FATAL_ERROR)

# Print a message that using the Makefiles is recommended.
message(NOTICE: " The CMakeLists.txt is unmaintained. Use the Makefile if possible.")

# Enforce "Debug" as standard build type.
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

# CMake project configuration.
project(openMerc C CXX) # OpenMerc I1b: CXX for server/sv_ghoul.cpp

# OpenMerc: the release version shown everywhere ("openMERC V<version>": menus, window title,
# console stamp); `git describe` of the build goes only into the build info (OM_BUILD:
# start-up banner, --help, cvar om_build)
set(OM_VERSION "0.2.2")
set(OM_BUILD "${OM_VERSION}")
find_package(Git QUIET)
if(GIT_FOUND)
	execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --always
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE OM_GIT_DESCRIBE OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE OM_GIT_RESULT ERROR_QUIET)
	if(OM_GIT_RESULT EQUAL 0 AND OM_GIT_DESCRIBE)
		string(REGEX REPLACE "^v" "" OM_BUILD "${OM_GIT_DESCRIBE}")
	endif()
	# re-run this step when HEAD moves, so a rebuild after a commit
	# does not keep the old version string
	foreach(OM_GIT_REF HEAD packed-refs)
		execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --git-path ${OM_GIT_REF}
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			OUTPUT_VARIABLE OM_GIT_FILE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
		list(APPEND OM_GIT_FILES ${OM_GIT_FILE})
	endforeach()
	execute_process(COMMAND ${GIT_EXECUTABLE} symbolic-ref -q HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		OUTPUT_VARIABLE OM_GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if(OM_GIT_BRANCH)
		execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --git-path ${OM_GIT_BRANCH}
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			OUTPUT_VARIABLE OM_GIT_FILE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
		list(APPEND OM_GIT_FILES ${OM_GIT_FILE})
	endif()
	foreach(OM_GIT_FILE ${OM_GIT_FILES})
		if(NOT IS_ABSOLUTE "${OM_GIT_FILE}")
			set(OM_GIT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${OM_GIT_FILE}")
		endif()
		if(EXISTS "${OM_GIT_FILE}")
			set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${OM_GIT_FILE}")
		endif()
	endforeach()
endif()
# the version goes into a generated header (src/common/header/om_version.h makes the shown text
# "openMERC V<OM_VERSION>" of it; OM_BUILD = git describe, the build id of the log and crash reports),
# rewritten only when it changes, not into the compile flags, so a new commit rebuilds a few files, not all
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/om_version_gen.h
	CONTENT "#define OM_VERSION \"@OM_VERSION@\"\n#define OM_BUILD \"@OM_BUILD@\"\n" @ONLY)
include_directories(${CMAKE_BINARY_DIR})
add_compile_definitions(OM_HAVE_VERSION_H)

# Cmake module search path.
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/stuff/cmake/modules ${CMAKE_MODULE_PATH})
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)

if(YQUAKE2LIBS)
	if(CMAKE_CROSSCOMPILING)
		set(CMAKE_FIND_ROOT_PATH ${YQUAKE2LIBS})
	else()
		set(ENV{CMAKE_PREFIX_PATH} ${YQUAKE2LIBS})
	endif()

	set(ENV{SDL2DIR} ${YQUAKE2LIBS})
endif()

# Add extended path for FreeBSD and Homebrew on OS X.
list(APPEND CMAKE_PREFIX_PATH /usr/local)

if (MSVC)
	add_compile_options(/MP) # parallel build (use all cores, or as many as configured in VS)

	# ignore some compiler warnings
	add_compile_options(/wd4244 /wd4305) # possible loss of data/truncation (double to float etc; ignore)
	add_compile_options(/wd4018) # signed/unsigned mismatch
	add_compile_options(/wd4996) # 'function': was declared deprecated (like all that secure CRT stuff)
	# don't show me warnings for system headers, why the fuck isn't this default
	add_compile_options(/experimental:external /external:W0)
else() # GCC/clang/mingw
# Enforce compiler flags:
#  -Wall                -> More warnings
#  -fno-strict-aliasing -> Quake 2 is far away from strict aliasing
#  -fwrapv              -> Make signed integer overflows defined
#  -fvisibility=hidden	-> Force defaultsymbol visibility to hidden
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fno-strict-aliasing -fwrapv -fvisibility=hidden")

# Use -O2 as maximum optimization level. -O3 has its problems with yquake2.
string(REPLACE "-O3" "-O2" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
endif() # MSVC'S else-case

# Switch off some annoying warnings
if (${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-braces")
elseif (${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
	if (CMAKE_C_COMPILER_VERSION GREATER 7.99)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-format-truncation -Wno-format-overflow")
	endif()
endif()

# Compilation time options.
option(CURL_SUPPORT "cURL support" ON)
option(SYSTEMWIDE_SUPPORT "Enable systemwide installation of game assets" OFF)
option(SDL3_SUPPORT "Build against SDL 3 instead of SDL2" ON)
option(GL3_RENDERER "Build the GL3 renderer" ON)
option(GLES3_RENDERER "Build the GLES3 renderer" ON)
option(XDG_SUPPORT "Use XDG Directories to store user data" ON)

set(SYSTEMDIR "" CACHE STRING "Override the system default directory")

# OpenMerc: client/cl_sof.c (+ header/sof_tables.h, generated by
# tools/net/gen_sof_tables.py) speaks the SoF 1.07f protocol 33.
# OpenMerc: name of the game data directory below basedir/datadir.
# Soldier of Fortune keeps its paks in "base" (Quake II: "baseq2").
# The game module is written to release/<OPENMERC_BASEDIR> so the engine finds it.
set(OPENMERC_BASEDIR "base" CACHE STRING "Name of the game data directory")
add_definitions(-DBASEDIRNAME="${OPENMERC_BASEDIR}")

# These variables will act as our list of include folders and linker flags.
set(yquake2IncludeDirectories)
set(yquake2LinkerDirectories)
set(yquake2LinkerFlags)
set(yquake2ClientLinkerFlags)
set(yquake2ServerLinkerFlags)
set(yquake2VulkanLinkerFlags)
set(yquake2SDLLinkerFlags)
set(yquake2ZLibLinkerFlags)

# Set directory locations (allowing us to move directories easily)
set(SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(BACKENDS_SRC_DIR ${SOURCE_DIR}/backends)
set(COMMON_SRC_DIR ${SOURCE_DIR}/common)
set(SERVER_SRC_DIR ${SOURCE_DIR}/server)
set(CLIENT_SRC_DIR ${SOURCE_DIR}/client)
set(REF_SRC_DIR ${SOURCE_DIR}/client/refresh)

# Operating system.
set(YQ2OSTYPE "${CMAKE_SYSTEM_NAME}" CACHE STRING "Override operation system type")
add_definitions(-DYQ2OSTYPE="${YQ2OSTYPE}")

# Architecture string
# work around CMake's useless/broken CMAKE_SYSTEM_PROCESSOR (taken from dhewm3)

set(cpu ${CMAKE_SYSTEM_PROCESSOR})

# Originally, ${CMAKE_SYSTEM_PROCESSOR} was supposed to contain the *target* CPU, according to CMake's documentation.
# As far as I can tell this has always been broken (always returns host CPU) at least on Windows
# (see e.g. https://cmake.org/pipermail/cmake-developers/2014-September/011405.html) and wasn't reliable on
# other systems either, for example on Linux with 32bit userland but 64bit kernel it returned the kernel CPU type
# (e.g. x86_64 instead of i686). Instead of fixing this, CMake eventually updated their documentation in 3.20,
# now it's officially the same as CMAKE_HOST_SYSTEM_PROCESSOR except when cross-compiling (where it's explicitly set)
# So we gotta figure out the actual target CPU type ourselves..
if(NOT (CMAKE_SYSTEM_PROCESSOR STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR))
	# special case: cross-compiling, here CMAKE_SYSTEM_PROCESSOR should be correct, hopefully
	# (just leave cpu at ${CMAKE_SYSTEM_PROCESSOR})
elseif(MSVC)
	# because all this wasn't ugly enough, it turned out that, unlike standalone CMake, Visual Studio's
	# integrated CMake doesn't set CMAKE_GENERATOR_PLATFORM, so I gave up on guessing the CPU arch here
	# and moved the CPU detection to MSVC-specific code in neo/sys/platform.h
else() # not MSVC and not cross-compiling, assume GCC or clang (-compatible), seems to work for MinGW as well
	execute_process(COMMAND ${CMAKE_C_COMPILER} "-dumpmachine"
	                RESULT_VARIABLE cc_dumpmachine_res
	                OUTPUT_VARIABLE cc_dumpmachine_out)
	if(cc_dumpmachine_res EQUAL 0)
		string(STRIP ${cc_dumpmachine_out} cc_dumpmachine_out) # get rid of trailing newline
		message(DEBUG "`${CMAKE_C_COMPILER} -dumpmachine` says: \"${cc_dumpmachine_out}\"")
		# gcc -dumpmachine and clang -dumpmachine seem to print something like "x86_64-linux-gnu" (gcc)
		# or "x64_64-pc-linux-gnu" (clang) or "i686-w64-mingw32" (32bit mingw-w64) i.e. starting with the CPU,
		# then "-" and then OS or whatever - so use everything up to first "-"
		string(REGEX MATCH "^[^-]+" cpu ${cc_dumpmachine_out})
		message(DEBUG "  => CPU architecture extracted from that: \"${cpu}\"")
	else()
		message(WARNING "${CMAKE_C_COMPILER} -dumpmachine failed with error (code) ${cc_dumpmachine_res}")
		message(WARNING "will use the (sometimes incorrect) CMAKE_SYSTEM_PROCESSOR (${cpu}) to determine YQ2ARCH")
	endif()
endif()

if(cpu STREQUAL "powerpc")
	set(cpu "ppc")
elseif(cpu STREQUAL "aarch64")
	# "arm64" is more obvious, and some operating systems (like macOS) use it instead of "aarch64"
	set(cpu "arm64")
elseif(cpu MATCHES "[aA][mM][dD]64" OR cpu MATCHES "[xX].*64")
	# a 64-bit compiler driven with -m32 (tools/build/build-linux.sh i386) still reports x86_64
	if(CMAKE_SIZEOF_VOID_P EQUAL 4)
		set(cpu "i386")
	else()
		set(cpu "x86_64")
	endif()
elseif(cpu MATCHES "i.86" OR cpu MATCHES "[xX]86")
	set(cpu "i386")
elseif(cpu MATCHES "[aA][rR][mM].*") # some kind of arm..
	# On 32bit Raspbian gcc -dumpmachine returns sth starting with "arm-",
	# while clang -dumpmachine says "arm6k-..." - try to unify that to "arm"
	if(CMAKE_SIZEOF_VOID_P EQUAL 8) # sizeof(void*) == 8 => must be arm64
		set(cpu "arm64")
	else() # should be 32bit arm then (probably "armv7l" "armv6k" or sth like that)
		set(cpu "arm")
	endif()
endif()

if(MSVC)
	# for MSVC YQ2ARCH is set in code (in src/common/header/common.h)
	message(STATUS "Setting YQ2OSTYPE to \"${YQ2OSTYPE}\" - NOT setting YQ2ARCH, because we're targeting MSVC (VisualC++)")
else()
	set(ARCH "${cpu}")
	add_definitions(-DYQ2ARCH="${ARCH}")
	message(STATUS "Setting YQ2OSTYPE to \"${YQ2OSTYPE}\" and YQ2ARCH to \"${ARCH}\".")
endif()
# make sure that ${cpu} isn't used below - if at all use ${ARCH}, but not when compiling with MSVC!
unset(cpu)

# END OF workarounds for CMake's poor choices regarding CPU architecture detection


# Systemwide installation of game assets.
if(${SYSTEMWIDE_SUPPORT})
    add_definitions(-DSYSTEMWIDE)
    if(NOT ${SYSTEMDIR} STREQUAL "")
      add_definitions(-DSYSTEMDIR="${SYSTEMDIR}")
    endif()
endif()

# We need to pass some options to minizip / unzip.
add_definitions(-DNOUNCRYPT)

if(NOT (CMAKE_SYSTEM_NAME MATCHES "Linux") AND NOT (CMAKE_SYSTEM_NAME MATCHES "Windows"))
	add_definitions(-DIOAPI_NO_64)
endif()

# Required libraries to build the different components of the binaries. Find
# them and add the include/linker directories and flags (in case the package
# manager find it in a weird place).
if (SDL3_SUPPORT)
	find_package(SDL3 REQUIRED)
	add_definitions(-DUSE_SDL3)
else()
	find_package(SDL2 REQUIRED)
	list(APPEND yquake2IncludeDirectories "${SDL2_INCLUDE_DIR}/..")
	list(APPEND yquake2SDLLinkerFlags ${SDL2_LIBRARY})
endif()

# XDG Support for user data
if(XDG_SUPPORT)
	add_definitions(-DUSE_XDG)
endif()

# backtrace lookup
# Some systems like Linux has it within the libc some like the BSD, Haiku ...
# into an external libexecinfo library
include(CheckFunctionExists)
include(CheckLibraryExists)
check_function_exists(backtrace HAVE_EXECINFO_SYS)
IF (NOT HAVE_EXECINFO_SYS)
	check_library_exists(execinfo backtrace "" HAVE_EXECINFO_LIB)
	if (HAVE_EXECINFO_LIB)
		list(APPEND yquake2ClientLinkerFlags execinfo)
		list(APPEND yquake2ServerLinkerFlags execinfo)
		add_definitions(-DHAVE_EXECINFO)
	endif()
else()
	add_definitions(-DHAVE_EXECINFO)
endif()

# cURL support.
if (${CURL_SUPPORT})
	find_package(CURL REQUIRED)
	include_directories(${CURL_INCLUDE_DIR})
	add_definitions(-DUSE_CURL)
endif()

# General linker flags.
if(NOT MSVC)
	list(APPEND yquake2LinkerFlags m)
endif()
list(APPEND yquake2LinkerFlags ${CMAKE_DL_LIBS})

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	if(!MSVC)
		list(APPEND yquake2LinkerFlags "-static-libgcc")
	endif()
else()
	if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Haiku")
		list(APPEND yquake2LinkerFlags "-rdynamic")
	else()
		list(APPEND yquake2LinkerFlags "-lnetwork")
                set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	endif()
	if (${CMAKE_SYSTEM_NAME} MATCHES "SunOS")
		list(APPEND yquake2LinkerFlags "-lsocket -lnsl")
	endif()
endif()

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND NOT ${CMAKE_SYSTEM_NAME} MATCHES "OpenBSD" AND NOT WIN32)
	list(APPEND yquake2LinkerFlags "-Wl,--no-undefined")
endif()

# With all of those libraries and user defined paths
# added, lets give them to the compiler and linker.
include_directories(${yquake2IncludeDirectories})
link_directories(${yquake2LinkerDirectories})

# these settings only work for GCC and clang
if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
	# If we're building with gcc for i386 let's define -ffloat-store.
	# This helps the old and crappy x87 FPU to produce correct values.
	# Would be nice if Clang had something comparable.
	if ("${ARCH}" STREQUAL "i386" AND ${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffloat-store")
	endif()

	# Force SSE math on x86_64. All sane compilers should do this
	# anyway, just to protect us from broken Linux distros.
	if ("${ARCH}" STREQUAL "x86_64")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mfpmath=sse")
	endif()

	if ("${ARCH}" STREQUAL "arm")
		if (CMAKE_SIZEOF_VOID_P EQUAL 4)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv6k")
		endif()
	endif()
endif()

set(Backends-Generic-Source
	${BACKENDS_SRC_DIR}/generic/misc.c
	)

set(Backends-Unix-Source
	${BACKENDS_SRC_DIR}/unix/main.c
	${BACKENDS_SRC_DIR}/unix/network.c
	${BACKENDS_SRC_DIR}/unix/signalhandler.c
	${BACKENDS_SRC_DIR}/unix/system.c
	${BACKENDS_SRC_DIR}/unix/shared/hunk.c
	)

set(Backends-Windows-Source
	${BACKENDS_SRC_DIR}/windows/icon.rc
	${BACKENDS_SRC_DIR}/windows/crash.c
	${BACKENDS_SRC_DIR}/windows/main.c
	${BACKENDS_SRC_DIR}/windows/network.c
	${BACKENDS_SRC_DIR}/windows/system.c
	${BACKENDS_SRC_DIR}/windows/shared/hunk.c
	)

set(Backends-Windows-Header
	${BACKENDS_SRC_DIR}/windows/header/resource.h
	)

set(REF-Windows-Source
	${BACKENDS_SRC_DIR}/windows/shared/hunk.c
	)

set(REF-Unix-Source
	${BACKENDS_SRC_DIR}/unix/shared/hunk.c
	)

# Set the nessesary platform specific source
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set(Platform-Specific-Source ${Backends-Windows-Source} ${Backends-Windows-Header})
	set(REF-Platform-Specific-Source ${REF-Windows-Source})
else()
	set(Platform-Specific-Source ${Backends-Unix-Source})
	set(REF-Platform-Specific-Source ${REF-Unix-Source})
endif()

# OpenMerc (I5): client effects system, engine/src/client/fx/*.c and *.cpp (bolt queries)
file(GLOB OM_CLIENT_FX_SOURCES ${CLIENT_SRC_DIR}/fx/*.c ${CLIENT_SRC_DIR}/fx/*.cpp)

set(Client-Source
	${CLIENT_SRC_DIR}/cl_cin.c
	${CLIENT_SRC_DIR}/cl_console.c
	${CLIENT_SRC_DIR}/cl_download.c
	${CLIENT_SRC_DIR}/cl_effects.c
	${CLIENT_SRC_DIR}/cl_entities.c
	${CLIENT_SRC_DIR}/cl_input.c
	${CLIENT_SRC_DIR}/cl_image.c
	${CLIENT_SRC_DIR}/cl_inventory.c
	${CLIENT_SRC_DIR}/cl_keyboard.c
	${CLIENT_SRC_DIR}/cl_lights.c
	${CLIENT_SRC_DIR}/cl_main.c
	${CLIENT_SRC_DIR}/cl_network.c
	${CLIENT_SRC_DIR}/cl_parse.c
	${CLIENT_SRC_DIR}/cl_particles.c
	${CLIENT_SRC_DIR}/cl_prediction.c
	${CLIENT_SRC_DIR}/cl_screen.c
	${CLIENT_SRC_DIR}/cl_serverlist.c
	${CLIENT_SRC_DIR}/cl_sof.c
	${CLIENT_SRC_DIR}/cl_tempentities.c
	${CLIENT_SRC_DIR}/cl_view.c
	# OpenMerc (I6h): SoF view, HUD, screen texts, demos, string stand-in, Windows keys
	${CLIENT_SRC_DIR}/cl_demo.c
	${CLIENT_SRC_DIR}/cl_omrec.c
	${CLIENT_SRC_DIR}/cl_hud.c
	${CLIENT_SRC_DIR}/cl_sofscreen.c
	${CLIENT_SRC_DIR}/cl_sofview.c
	${CLIENT_SRC_DIR}/cl_strings.c
	${CLIENT_SRC_DIR}/input/winkeys.c
	# OpenMerc (I1c): the Ghoul client system (svc 26/27, renderer hand-off, view weapon, menu models)
	${CLIENT_SRC_DIR}/cl_ghoul.c
	${CLIENT_SRC_DIR}/cl_ghoulsys.cpp
	# OpenMerc (I1d): client side of the player module (SPEC-003)
	${CLIENT_SRC_DIR}/cl_player.c
	${CLIENT_SRC_DIR}/cl_playerghl.cpp
	${OM_CLIENT_FX_SOURCES}
	${CLIENT_SRC_DIR}/curl/download.c
	${CLIENT_SRC_DIR}/curl/qcurl.c
	${CLIENT_SRC_DIR}/input/gyro.c
	${CLIENT_SRC_DIR}/menu/menu.c
	${CLIENT_SRC_DIR}/menu/m_data.c
	${CLIENT_SRC_DIR}/menu/m_draw.c
	${CLIENT_SRC_DIR}/menu/m_font.c
	${CLIENT_SRC_DIR}/menu/m_ghoul.cpp
	${CLIENT_SRC_DIR}/menu/m_layout.c
	${CLIENT_SRC_DIR}/menu/m_model.c
	${CLIENT_SRC_DIR}/menu/m_parse.c
	${CLIENT_SRC_DIR}/menu/m_strings.c
	${CLIENT_SRC_DIR}/menu/m_test.c
	${CLIENT_SRC_DIR}/menu/m_widgets.c
	${CLIENT_SRC_DIR}/menu/m_wide.c
	# OpenMerc (I1a, SND2): the sound system is the OMsnd module (SPEC-004,
	# modules/sound); Yamagi's OpenAL/SDL/OGG sound is removed
	${CLIENT_SRC_DIR}/snd_host.c
	${CLIENT_SRC_DIR}/vid/vid.c
	${COMMON_SRC_DIR}/argproc.c
	${COMMON_SRC_DIR}/clientserver.c
	${COMMON_SRC_DIR}/collision.c
	${COMMON_SRC_DIR}/crc.c
	${COMMON_SRC_DIR}/cmdparser.c
	${COMMON_SRC_DIR}/cvar.c
	${COMMON_SRC_DIR}/filesystem.c
	${COMMON_SRC_DIR}/glob.c
	${COMMON_SRC_DIR}/md4.c
	${COMMON_SRC_DIR}/movemsg.c
	${COMMON_SRC_DIR}/frame.c
	${COMMON_SRC_DIR}/gamespy.c
	${COMMON_SRC_DIR}/gamespy_chat.c
	${COMMON_SRC_DIR}/netchan.c
	${COMMON_SRC_DIR}/pmove.c
	${COMMON_SRC_DIR}/sof_pmove.c # OpenMerc ENG2: SoF player movement (SPEC-314), server and client
	${COMMON_SRC_DIR}/szone.c
	${COMMON_SRC_DIR}/zone.c
	${COMMON_SRC_DIR}/sof_admin.c
	${COMMON_SRC_DIR}/sof_cvarreg.c
	${COMMON_SRC_DIR}/sof_engine.c
	${COMMON_SRC_DIR}/sof_exports.c
	${COMMON_SRC_DIR}/sof_locks.c
	${COMMON_SRC_DIR}/sof_save.c
	${COMMON_SRC_DIR}/sof_strings.c
	${COMMON_SRC_DIR}/shared/flash.c
	${COMMON_SRC_DIR}/shared/rand.c
	${COMMON_SRC_DIR}/shared/shared.c
	${COMMON_SRC_DIR}/unzip/ioapi.c
	${COMMON_SRC_DIR}/unzip/unzip.c
	${COMMON_SRC_DIR}/unzip/miniz/miniz.c
	${COMMON_SRC_DIR}/unzip/miniz/miniz_tdef.c
	${COMMON_SRC_DIR}/unzip/miniz/miniz_tinfl.c
	${SERVER_SRC_DIR}/sv_cmd.c
	${SERVER_SRC_DIR}/sv_conless.c
	${SERVER_SRC_DIR}/sv_entities.c
	${SERVER_SRC_DIR}/sv_game.c
	${SERVER_SRC_DIR}/sv_gamespy.c
	${SERVER_SRC_DIR}/sv_init.c
	${SERVER_SRC_DIR}/sv_main.c
	${SERVER_SRC_DIR}/sv_save.c
	${SERVER_SRC_DIR}/sv_send.c
	${SERVER_SRC_DIR}/sv_user.c
	${SERVER_SRC_DIR}/sv_world.c
	${SERVER_SRC_DIR}/sv_ghoul.cpp
	${SERVER_SRC_DIR}/sv_msg.c
	${SERVER_SRC_DIR}/sv_player.c
	${SERVER_SRC_DIR}/sv_pmove.c
	${SERVER_SRC_DIR}/sv_strings.c
	${SERVER_SRC_DIR}/sv_watchdog.c
	${SERVER_SRC_DIR}/sv_imptrace.c
	)

if(SDL3_SUPPORT)
	set(Client-SDL-Source
		${CLIENT_SRC_DIR}/input/sdl3.c
		${CLIENT_SRC_DIR}/vid/glimp_sdl3.c
	)
else()
	set(Client-SDL-Source
		${CLIENT_SRC_DIR}/input/sdl2.c
		${CLIENT_SRC_DIR}/vid/glimp_sdl2.c
	)
endif()

set(Client-Header
	${CLIENT_SRC_DIR}/header/client.h
	${CLIENT_SRC_DIR}/header/cl_ghoul.h
	${CLIENT_SRC_DIR}/header/cl_player.h
	${CLIENT_SRC_DIR}/header/console.h
	${CLIENT_SRC_DIR}/header/keyboard.h
	${CLIENT_SRC_DIR}/header/screen.h
	${CLIENT_SRC_DIR}/header/serverlist.h
	${CLIENT_SRC_DIR}/header/sof_tables.h
	${CLIENT_SRC_DIR}/curl/header/download.h
	${CLIENT_SRC_DIR}/curl/header/qcurl.h
	${CLIENT_SRC_DIR}/input/header/gyro.h
	${CLIENT_SRC_DIR}/input/header/input.h
	${CLIENT_SRC_DIR}/menu/header/menu.h
	${CLIENT_SRC_DIR}/menu/header/m_draw.h
	${CLIENT_SRC_DIR}/menu/header/m_ghoul.h
	${CLIENT_SRC_DIR}/header/sound.h
	${CLIENT_SRC_DIR}/vid/header/ref.h
	${CLIENT_SRC_DIR}/vid/header/stb_image_write.h
	${CLIENT_SRC_DIR}/vid/header/vid.h
	${COMMON_SRC_DIR}/header/common.h
	${COMMON_SRC_DIR}/header/crc.h
	${COMMON_SRC_DIR}/header/files.h
	${COMMON_SRC_DIR}/header/gamespy.h
	${COMMON_SRC_DIR}/header/gamespy_chat.h
	${COMMON_SRC_DIR}/header/glob.h
	${COMMON_SRC_DIR}/header/shared.h
	${COMMON_SRC_DIR}/header/zone.h
	${COMMON_SRC_DIR}/unzip/ioapi.h
	${COMMON_SRC_DIR}/unzip/unzip.h
	${COMMON_SRC_DIR}/unzip/miniz/miniz.h
	${COMMON_SRC_DIR}/unzip/miniz/miniz_tdef.h
	${COMMON_SRC_DIR}/unzip/miniz/miniz_tinfl.h
	${COMMON_SRC_DIR}/unzip/miniz/minizconf.h
	${SERVER_SRC_DIR}/header/server.h
	)

set(Server-Source
	${COMMON_SRC_DIR}/argproc.c
	${COMMON_SRC_DIR}/clientserver.c
	${COMMON_SRC_DIR}/collision.c
	${COMMON_SRC_DIR}/crc.c
	${COMMON_SRC_DIR}/cmdparser.c
	${COMMON_SRC_DIR}/cvar.c
	${COMMON_SRC_DIR}/filesystem.c
	${COMMON_SRC_DIR}/glob.c
	${COMMON_SRC_DIR}/md4.c
	${COMMON_SRC_DIR}/frame.c
	${COMMON_SRC_DIR}/gamespy.c
	${COMMON_SRC_DIR}/gamespy_chat.c
	${COMMON_SRC_DIR}/movemsg.c
	${COMMON_SRC_DIR}/netchan.c
	${COMMON_SRC_DIR}/pmove.c
	${COMMON_SRC_DIR}/sof_pmove.c # OpenMerc ENG2: SoF player movement (SPEC-314), server and client
	${COMMON_SRC_DIR}/szone.c
	${COMMON_SRC_DIR}/zone.c
	${COMMON_SRC_DIR}/sof_admin.c
	${COMMON_SRC_DIR}/sof_cvarreg.c
	${COMMON_SRC_DIR}/sof_engine.c
	${COMMON_SRC_DIR}/sof_exports.c
	${COMMON_SRC_DIR}/sof_locks.c
	${COMMON_SRC_DIR}/sof_save.c
	${COMMON_SRC_DIR}/sof_strings.c
	${COMMON_SRC_DIR}/shared/rand.c
	${COMMON_SRC_DIR}/shared/shared.c
	${COMMON_SRC_DIR}/unzip/ioapi.c
	${COMMON_SRC_DIR}/unzip/unzip.c
	${COMMON_SRC_DIR}/unzip/miniz/miniz.c
	${COMMON_SRC_DIR}/unzip/miniz/miniz_tdef.c
	${COMMON_SRC_DIR}/unzip/miniz/miniz_tinfl.c
	${SERVER_SRC_DIR}/sv_cmd.c
	${SERVER_SRC_DIR}/sv_conless.c
	${SERVER_SRC_DIR}/sv_entities.c
	${SERVER_SRC_DIR}/sv_game.c
	${SERVER_SRC_DIR}/sv_gamespy.c
	${SERVER_SRC_DIR}/sv_init.c
	${SERVER_SRC_DIR}/sv_main.c
	${SERVER_SRC_DIR}/sv_save.c
	${SERVER_SRC_DIR}/sv_send.c
	${SERVER_SRC_DIR}/sv_user.c
	${SERVER_SRC_DIR}/sv_world.c
	${SERVER_SRC_DIR}/sv_ghoul.cpp
	${SERVER_SRC_DIR}/sv_msg.c
	${SERVER_SRC_DIR}/sv_player.c
	${SERVER_SRC_DIR}/sv_pmove.c
	${SERVER_SRC_DIR}/sv_strings.c
	${SERVER_SRC_DIR}/sv_watchdog.c
	${SERVER_SRC_DIR}/sv_imptrace.c
	)

set(Server-Header
	${COMMON_SRC_DIR}/header/common.h
	${COMMON_SRC_DIR}/header/crc.h
	${COMMON_SRC_DIR}/header/files.h
	${COMMON_SRC_DIR}/header/gamespy.h
	${COMMON_SRC_DIR}/header/gamespy_chat.h
	${COMMON_SRC_DIR}/header/glob.h
	${COMMON_SRC_DIR}/header/shared.h
	${COMMON_SRC_DIR}/header/zone.h
	${COMMON_SRC_DIR}/unzip/ioapi.h
	${COMMON_SRC_DIR}/unzip/unzip.h
	${COMMON_SRC_DIR}/unzip/miniz/miniz.h
	${COMMON_SRC_DIR}/unzip/miniz/miniz_tdef.h
	${COMMON_SRC_DIR}/unzip/miniz/miniz_tinfl.h
	${COMMON_SRC_DIR}/unzip/miniz/minizconf.h
	${SERVER_SRC_DIR}/header/server.h
	)

set(GL3-Source
	${REF_SRC_DIR}/gl3/gl3_draw.c
	${REF_SRC_DIR}/gl3/gl3_image.c
	${REF_SRC_DIR}/gl3/gl3_light.c
	${REF_SRC_DIR}/gl3/gl3_lightmap.c
	${REF_SRC_DIR}/gl3/gl3_main.c
	${REF_SRC_DIR}/gl3/gl3_mesh.c
	${REF_SRC_DIR}/gl3/gl3_misc.c
	${REF_SRC_DIR}/gl3/gl3_model.c
	${REF_SRC_DIR}/gl3/gl3_sdl.c
	${REF_SRC_DIR}/gl3/gl3_surf.c
	${REF_SRC_DIR}/gl3/gl3_warp.c
	${REF_SRC_DIR}/gl3/gl3_shaders.c
	${REF_SRC_DIR}/gl3/gl3_sof2d.c
	${REF_SRC_DIR}/gl3/gl3_fx.c
	${REF_SRC_DIR}/gl3/gl3_fog.c
	${REF_SRC_DIR}/gl3/gl3_detail.c
	${REF_SRC_DIR}/gl3/gl3_sofcvar.c
	${REF_SRC_DIR}/gl3/gl3_terrain.c
	${REF_SRC_DIR}/gl3/gl3_liquid.c
	${REF_SRC_DIR}/gl3/gl3_avi.c
	${REF_SRC_DIR}/gl3/gl3_dynlight.c
	${REF_SRC_DIR}/gl3/gl3_sofapi.c
	${REF_SRC_DIR}/gl3/gl3_ghoul.c
	${REF_SRC_DIR}/gl3/gl3_ghoulemu.c
	${REF_SRC_DIR}/gl3/gl3_post.c
	${REF_SRC_DIR}/gl3/gl3_csm.c
	${REF_SRC_DIR}/files/common.c
	${REF_SRC_DIR}/files/models.c
	${REF_SRC_DIR}/files/pcx.c
	${REF_SRC_DIR}/files/stb.c
	${REF_SRC_DIR}/files/surf.c
	${REF_SRC_DIR}/files/wal.c
	${REF_SRC_DIR}/files/pvs.c
	${COMMON_SRC_DIR}/shared/shared.c
	${COMMON_SRC_DIR}/md4.c
	)

set(Glad-GL3-Source ${REF_SRC_DIR}/gl3/glad/src/glad.c)
set(Glad-GLES3-Source ${REF_SRC_DIR}/gl3/glad-gles3/src/glad.c)

set(GL3-Header
	${REF_SRC_DIR}/ref_shared.h
	${REF_SRC_DIR}/constants/anorms.h
	${REF_SRC_DIR}/constants/anormtab.h
	${REF_SRC_DIR}/constants/warpsin.h
	${REF_SRC_DIR}/files/stb_image.h
	${REF_SRC_DIR}/gl3/header/DG_dynarr.h
	${REF_SRC_DIR}/gl3/header/HandmadeMath.h
	${REF_SRC_DIR}/gl3/header/local.h
	${REF_SRC_DIR}/gl3/header/model.h
	${COMMON_SRC_DIR}/header/shared.h
	)

set(Glad-GL3-Header
	${REF_SRC_DIR}/gl3/glad/include/glad/glad.h
	${REF_SRC_DIR}/gl3/glad/include/KHR/khrplatform.h
	)

set(Glad-GLES3-Header
	${REF_SRC_DIR}/gl3/glad-gles3/include/glad/glad.h
	${REF_SRC_DIR}/gl3/glad-gles3/include/KHR/khrplatform.h
	)

# Main executable: openMerc (openMerc.exe on Windows), client + listen server

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	add_executable(openMerc WIN32 ${Client-Source} ${Client-SDL-Source} ${Client-Header}
			${Platform-Specific-Source} ${Backends-Generic-Source})
	target_link_libraries(openMerc ${yquake2LinkerFlags} ${yquake2ClientLinkerFlags}
			${yquake2SDLLinkerFlags} ${yquake2ZLibLinkerFlags} ws2_32 winmm)
	if(MSVC AND CMAKE_MAJOR_VERSION GREATER 3 OR ( CMAKE_MAJOR_VERSION EQUAL 3 AND CMAKE_MINOR_VERSION GREATER_EQUAL 6 ))
		# CMake >= 3.6 supports setting the default project started for debugging (instead of trying to launch ALL_BUILD ...)
		set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT openMerc)
		set_target_properties(openMerc PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/release)
	endif()
else()
	add_executable(openMerc ${Client-Source} ${Client-SDL-Source} ${Client-Header}
			${Platform-Specific-Source} ${Backends-Generic-Source})
	target_link_libraries(openMerc ${yquake2LinkerFlags} ${yquake2ClientLinkerFlags}
		${yquake2SDLLinkerFlags} ${yquake2ZLibLinkerFlags})
	if(NOT APPLE)
		# OpenMerc: the SoF engine functions OMsnd looks up by name (src/common/sof_exports.dynlist)
		target_link_options(openMerc PRIVATE "-Wl,--dynamic-list=${CMAKE_CURRENT_SOURCE_DIR}/src/common/sof_exports.dynlist")
		target_compile_definitions(openMerc PRIVATE OM_SOF_ELF_EXPORTS)
		set_target_properties(openMerc PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/common/sof_exports.dynlist)
	endif()
endif()
set_target_properties(openMerc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release)
target_link_libraries(openMerc omghoul) # OpenMerc I1b: Ghoul services of the server
if(SDL3_SUPPORT)
	target_link_libraries(openMerc SDL3::SDL3)
endif()

# Dedicated server: openMerc-ded
add_executable(openMerc-ded ${Server-Source} ${Server-Header} ${Platform-Specific-Source}
		${Backends-Generic-Source})
set_target_properties(openMerc-ded PROPERTIES
	COMPILE_DEFINITIONS "DEDICATED_ONLY"
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
	)
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	target_link_libraries(openMerc-ded ${yquake2LinkerFlags} ${yquake2ZLibLinkerFlags} ws2_32 winmm)
	target_link_libraries(openMerc-ded omghoul) # OpenMerc I1b: Ghoul services of the server
else()
	target_link_libraries(openMerc-ded ${yquake2LinkerFlags} ${yquake2ServerLinkerFlags} ${yquake2ZLibLinkerFlags})
	target_link_libraries(openMerc-ded omghoul) # OpenMerc I1b: Ghoul services of the server
endif()

if(${GL3_RENDERER})

# Build the GL3 dynamic library
add_library(ref_gl3 MODULE ${GL3-Source} ${Glad-GL3-Source} ${GL3-Header} ${Glad-GL3-Header} ${REF-Platform-Specific-Source})
set_target_properties(ref_gl3 PROPERTIES
		PREFIX ""
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
		SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
		)
target_include_directories(ref_gl3 PRIVATE ${CMAKE_SOURCE_DIR}/src/client/refresh/gl3/glad/include)
target_link_libraries(ref_gl3 ${yquake2LinkerFlags} ${yquake2SDLLinkerFlags})
if(SDL3_SUPPORT)
	target_link_libraries(ref_gl3 SDL3::SDL3)
endif()

# OpenMerc (I4a): the GL3 renderer as a Soldier of Fortune renderer module
# (SPEC-002 GetRefAPI, own Win32 window and context; loadable by SoF.exe as
# vid_ref omgl, or as ref_gl.dll). 32-bit Windows only.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(OMGL-Source ${GL3-Source})
	list(REMOVE_ITEM OMGL-Source ${REF_SRC_DIR}/gl3/gl3_sdl.c)
	list(APPEND OMGL-Source ${REF_SRC_DIR}/gl3/gl3_sof.c ${REF_SRC_DIR}/gl3/gl3_win32.c
		${REF_SRC_DIR}/gl3/gl3_ghoulgl.c)
	add_library(ref_omgl MODULE ${OMGL-Source} ${Glad-GL3-Source} ${GL3-Header} ${Glad-GL3-Header} ${REF-Platform-Specific-Source})
	set_target_properties(ref_omgl PROPERTIES
			PREFIX ""
			LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
			RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
			SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
			MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
			)
	target_include_directories(ref_omgl PRIVATE ${CMAKE_SOURCE_DIR}/src/client/refresh/gl3/glad/include)
	target_compile_definitions(ref_omgl PRIVATE OM_SOFREF=1)
	target_link_libraries(ref_omgl opengl32 user32 gdi32)
	target_link_options(ref_omgl PRIVATE /MAP)
endif()

endif()

if(${GLES3_RENDERER})

# Build the GLES3 dynamic library
add_library(ref_gles3 MODULE ${GL3-Source} ${Glad-GLES3-Source} ${GL3-Header} ${Glad-GLES3-Header} ${REF-Platform-Specific-Source})
set_target_properties(ref_gles3 PROPERTIES
		PREFIX ""
		#COMPILE_DEFINITIONS "YQ2_GL3_GLES3=1;YQ2_GL3_GLES=1"
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/release
		SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
		)
target_include_directories(ref_gles3 PRIVATE ${CMAKE_SOURCE_DIR}/src/client/refresh/gl3/glad-gles3/include)
target_compile_definitions(ref_gles3 PRIVATE YQ2_GL3_GLES3=1 YQ2_GL3_GLES=1)
target_link_libraries(ref_gles3 ${yquake2LinkerFlags} ${yquake2SDLLinkerFlags})
if(SDL3_SUPPORT)
	target_link_libraries(ref_gles3 SDL3::SDL3)
endif()

endif()

add_subdirectory(src/ghoul) # OpenMerc I3: Ghoul model system library (C++, SPEC-010..014, 120, 121)

# OpenMerc ENG2: SPEC-314 player-movement test vectors (sofpm_test, exit code = failures)
option(OM_SOFPM_TESTS "Build the SPEC-314 player-movement test program" ON)
if(OM_SOFPM_TESTS)
	add_executable(sofpm_test ${COMMON_SRC_DIR}/test_sof_pmove.c ${COMMON_SRC_DIR}/sof_pmove.c)
	if(NOT MSVC)
		target_link_libraries(sofpm_test m)
	endif()
	# OpenMerc ICE2: always-run 90 degree snap / rapid strafe measurement (exit code 1 = airborne or > 300 u/s)
	add_executable(sofpm_ice ${COMMON_SRC_DIR}/test_sof_ice.c ${COMMON_SRC_DIR}/sof_pmove.c)
	if(NOT MSVC)
		target_link_libraries(sofpm_ice m)
	endif()
endif()
