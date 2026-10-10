# CMake configuration file for those who don't like Make
# Prev-maintainer: cerg2010cerg2010

cmake_minimum_required(VERSION 3.9...3.30)

set(RVVM_VERSION 0.7)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)

# Export compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(RVVM VERSION ${RVVM_VERSION}
	DESCRIPTION "RISC-V Virtual Machine"
	HOMEPAGE_URL "https://github.com/LekKit/RVVM"
	LANGUAGES C CXX)

#
# Default build configuration
#

# CPU Features
option(USE_RV32 "Enable riscv32imacb CPU support" ON)
option(USE_RV64 "Enable riscv64imacb CPU support" ON)
option(USE_FPU  "Enable FPU extensions support" ON)
option(USE_RVV  "Enable Vector extensions support" OFF)

# Infrastructure
option(USE_DEBUG "Enable debug logging / sanity checks" OFF)
option(USE_SPINLOCK_DEBUG "Enable deadlock debugging" ON)
option(USE_ISOLATION "Enable seccomp/pledge isolation measures" ON)
option(USE_JNI "Enable JNI bindings in librvvm (Very tiny size impact)" OFF)

# Acceleration, accessibility
option(USE_JIT "Enable RVJIT Just-in-time compiler" ON)
option(USE_GUI "Enable VM display GUI" ON)
option(USE_NET "Enable userland networking stack" ON)
option(USE_GDBSTUB "Enable GDB server stub" ON)

# Devices
option(USE_FDT "Enable Flattened Device Tree generation" ON)
option(USE_VFIO "Enable VFIO PCIe passthrough on Linux hosts" ON)

# Deprecated features
option(USE_TAP_LINUX "Use Linux TAP (deprecated)" OFF)

# Libraries to build
option(BUILD_SHARED_LIBS "Build shared librvvm library" ON)
option(BUILD_STATIC_LIBS "Build static librvvm library" OFF)

# Libretro core
option(BUILD_LIBRETRO "Build libretro core" OFF)
option(LIBRETRO_STATIC "Statically link the libretro core" OFF)

# GUI backends
if (APPLE OR SERENITY)
	option(USE_SDL "Enable SDL GUI backend" 2)
else()
	option(USE_SDL "Enable SDL GUI backend" OFF)
endif()

if (WIN32)
	option(USE_WIN32_GUI "Enable Win32 GUI backend" ON)
endif()

if (LINUX OR BSD OR CMAKE_SYSTEM_NAME STREQUAL "SunOS")
	option(USE_X11 "Enable X11 GUI backend" ON)
endif()

if (LINUX)
	option(USE_WAYLAND "Enable Wayland GUI backend" ON)
endif()

if (HAIKU)
	option(USE_HAIKU_GUI "Enable Haiku GUI backend" ON)
endif()

#
# Set up source & binary dirs, compiler & target specific build options
#

set(RVVM_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(RVVM_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Fancy coloring
if (NOT WIN32)
	string(ASCII 27 VT_ESC)
	set(COLOR_RESET   "${VT_ESC}[m")
	set(COLOR_BOLD    "${VT_ESC}[1m")
	set(COLOR_RED     "${VT_ESC}[1;31m")
	set(COLOR_GREEN   "${VT_ESC}[1;32m")
	set(COLLOR_YELLOW "${VT_ESC}[1;33m")
	set(COLOR_BLUE    "${VT_ESC}[1;34m")
endif()

# Check for in-source build
if (${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
	message(FATAL_ERROR "CMake in-source build is not allowed, see README")
endif()

# Set build type
if (NOT CMAKE_BUILD_TYPE OR NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
	if (USE_DEBUG)
		set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
	else()
		set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
	endif()
	set(CMAKE_CONFIGURATION_TYPES "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
	message(STATUS "${COLLOR_YELLOW}Setting build type to ${CMAKE_BUILD_TYPE} as none was specified${COLOR_RESET}")
endif()

# Get git commit hash, append to version
find_package(Git)
execute_process(COMMAND
	"${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh_TaG --always --dirty
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	RESULT_VARIABLE RESULT
	OUTPUT_VARIABLE RVVM_COMMIT
	ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT RESULT)
	set(RVVM_VERSION ${RVVM_VERSION}-${RVVM_COMMIT})
endif()

# Common pseudo-library to pass parameters to other targets
add_library(rvvm_common INTERFACE)
target_include_directories(rvvm_common INTERFACE "${RVVM_INC_DIR}" "${RVVM_SRC_DIR}" "${RVVM_SRC_DIR}/util")
target_compile_definitions(rvvm_common INTERFACE "RVVM_VERSION=\"${RVVM_VERSION}\"")

# Check and enable LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT lto_error)

if (lto_supported)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
	message(STATUS "${COLLOR_YELLOW}LTO is not supported by this toolchain: ${lto_error}")
endif()

if (MSVC)
	# Suppress warnings: casts between int sizes, unsigned minus, cast after shift
	target_compile_definitions(rvvm_common INTERFACE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(rvvm_common INTERFACE /wd4267 /wd4244 /wd4146 /wd4334)
	# Use C11 atomics
	target_compile_options(rvvm_common INTERFACE /experimental:c11atomics)
	# Use static runtime
	set(buildflags CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
			CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
	foreach(buildflag ${buildflags})
		if(${buildflag} MATCHES "/MD")
			string(REGEX REPLACE "/MD" "/MT" ${buildflag} "${${buildflag}}")
		endif()
	endforeach()
endif()

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	# Use -O2 optimization level
	set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
	# Disable unsafe FPU optimizations, hide internal symbols
	target_compile_options(rvvm_common INTERFACE -frounding-math -fvisibility=hidden -fno-math-errno)
	# Warning options (Strict safety/portability, stack/object size limits)
	# TODO: -Wbad-function-cast, -Wcast-align, -Wdouble-promotion need fixes in codebase
	target_compile_options(rvvm_common INTERFACE -Wall -Wextra -Wshadow -Wvla -Wpointer-arith -Walloca -Wduplicated-cond)
	target_compile_options(rvvm_common INTERFACE -Wtrampolines -Wlarger-than=1048576 -Wframe-larger-than=32768 -Wdouble-promotion -Werror=return-type)
	# Shut off bogus warnings
	if (CMAKE_C_COMPILER_ID MATCHES "GNU")
		target_compile_options(rvvm_common INTERFACE -Wno-missing-braces -Wno-missing-field-initializers)
	elseif (CMAKE_C_COMPILER_ID MATCHES "Clang")
		target_compile_options(rvvm_common INTERFACE -Wno-unknown-warning-option -Wno-unsupported-floating-point-opt -Wno-ignored-optimization-argument)
		target_compile_options(rvvm_common INTERFACE -Wno-missing-braces -Wno-missing-field-initializers -Wno-ignored-pragmas -Wno-atomic-alignment)
	endif()
endif()

# Check JIT support for target
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" cpu_lowercase)
if (NOT cpu_lowercase MATCHES "^(x86|amd64|i386|aarch64|arm|riscv)")
	message(WARNING "Unsupported RVJIT target! RVJIT won't be built")
	set(USE_JIT OFF)
endif()

# Link libm, librt, libdl, libatomic if we have them
if (UNIX)
	find_package(Threads REQUIRED)
	target_link_libraries(rvvm_common INTERFACE Threads::Threads)
	find_library(RVVM_M_LIB m)
	if (RVVM_M_LIB)
		target_link_libraries(rvvm_common INTERFACE m)
	endif()
	find_library(RVVM_RT_LIB rt)
	if (RVVM_RT_LIB)
		target_link_libraries(rvvm_common INTERFACE rt)
	endif()
	find_library(RVVM_DL_LIB dl)
	if (RVVM_DL_LIB)
		target_link_libraries(rvvm_common INTERFACE dl)
	endif()
	# Linking to libatomic on x86_64 is redundant and may cause issues
	if (NOT cpu_lowercase MATCHES "x86_64")
		find_library(RVVM_ATOMIC_LIB atomic)
		if (RVVM_ATOMIC_LIB)
			target_link_libraries(rvvm_common INTERFACE atomic)
		endif()
	endif()
endif()

if (BUILD_SHARED_LIBS)
	set(USE_LIB ON)
endif()

#
# Set up sources, useflags
#

# General sources
file(GLOB_RECURSE RVVM_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_INC_DIR}/rvvm/*.h"
	"${RVVM_SRC_DIR}/*.h"
	"${RVVM_SRC_DIR}/util/*.h"
	"${RVVM_SRC_DIR}/*.c"
)

list(REMOVE_ITEM RVVM_SRC "${RVVM_SRC_DIR}/bindings/libretro/libretro.c")

# Conditionally compiled sources
set(SRC_USE_WIN32_GUI "${RVVM_SRC_DIR}/gui/win32_window.c")
set(SRC_USE_HAIKU_GUI "${RVVM_SRC_DIR}/gui/haiku_window.cpp")
set(SRC_USE_WAYLAND "${RVVM_SRC_DIR}/gui/wayland_window.c")
set(SRC_USE_X11 "${RVVM_SRC_DIR}/gui/x11_window.c")
set(SRC_USE_SDL "${RVVM_SRC_DIR}/gui/sdl_window.c")

set(SRC_USE_TAP_LINUX "${RVVM_SRC_DIR}/devices/tap_linux.c")
set(SRC_USE_NET "${RVVM_SRC_DIR}/util/networking.c" "${RVVM_SRC_DIR}/devices/tap_user.c")
set(SRC_USE_JIT "${RVVM_SRC_DIR}/rvjit/rvjit.c" "${RVVM_SRC_DIR}/rvjit/rvjit_emit.c")
set(SRC_USE_JNI "${RVVM_SRC_DIR}/bindings/jni/rvvm_jni.c")

file(GLOB SRC_USE_RV32 LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS "${RVVM_SRC_DIR}/cpu/riscv32_*.c")
file(GLOB SRC_USE_RV64 LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS "${RVVM_SRC_DIR}/cpu/riscv64_*.c")

# Libraries needed for specific feature
if (WIN32)
	set(LIBS_USE_NET "ws2_32")
elseif (HAIKU)
	set(LIBS_USE_NET "network")
elseif(CMAKE_SYSTEM_NAME STREQUAL "SunOS") # Solaris, Illumos, etc
	set(LIBS_USE_NET "socket")
endif()

set(LIBS_USE_WIN32_GUI "gdi32")
set(LIBS_USE_HAIKU_GUI "be")

if (NOT USE_LIBS_PROBE)
	if (USE_SDL EQUAL 1)
		set(LIBS_USE_SDL "SDL")
	elseif (USE_SDL EQUAL 2)
		set(LIBS_USE_SDL "SDL2")
	elseif (USE_SDL EQUAL 3)
		set(LIBS_USE_SDL "SDL3")
	endif()
	set(LIBS_USE_X11 "X11" "Xext")
	set(LIBS_USE_WAYLAND "wayland-client" "xkbcommon")
endif()

# Useflag dependencies
set(NEED_USE_X11 "USE_GUI")
set(NEED_USE_SDL "USE_GUI")
set(NEED_USE_WAYLAND "USE_GUI")
set(NEED_USE_WIN32_GUI "USE_GUI")
set(NEED_USE_HAIKU_GUI "USE_GUI")
set(NEED_USE_JNI "USE_LIB")
set(NEED_USE_GDBSTUB "USE_NET")

# Get all variables as a list
get_cmake_property(RVVM_CMAKE_VARIABLES VARIABLES)
list (SORT RVVM_CMAKE_VARIABLES)
list (REMOVE_DUPLICATES RVVM_CMAKE_VARIABLES)

# Filter out all conditionally compiled C/C++ sources
foreach (SRC_CONDITIONAL ${RVVM_CMAKE_VARIABLES})
	if (SRC_CONDITIONAL MATCHES "^SRC_USE_*")
		list(REMOVE_ITEM RVVM_SRC ${${SRC_CONDITIONAL}})
	endif()
endforeach()

foreach (USEFLAG ${RVVM_CMAKE_VARIABLES})
	if (USEFLAG MATCHES "^USE_*" AND ${USEFLAG} AND NOT (DEFINED NEED_${USEFLAG} AND NOT ${NEED_${USEFLAG}}))
		# Set useflags C definitions
		if (${USEFLAG} MATCHES ON)
			target_compile_definitions(rvvm_common INTERFACE ${USEFLAG}=1)
		elseif (NOT ${USEFLAG} MATCHES 0)
			target_compile_definitions(rvvm_common INTERFACE ${USEFLAG}=${${USEFLAG}})
		endif()

		if (DEFINED SRC_${USEFLAG})
			# Include sources enabled by useflag
			list(APPEND RVVM_SRC ${SRC_${USEFLAG}})
		endif()

		if (DEFINED LIBS_${USEFLAG})
			# Link libraries enabled by useflag
			target_link_libraries(rvvm_common INTERFACE ${LIBS_${USEFLAG}})
		endif()
	endif()
endforeach()

set(RVVM_MAIN_SRC "${RVVM_SRC_DIR}/main.c")
list(REMOVE_ITEM RVVM_SRC ${RVVM_MAIN_SRC})

set(RVVM_USER_MAIN_SRC "${RVVM_SRC_DIR}/rvvm_user_main.c")
list(REMOVE_ITEM RVVM_SRC ${RVVM_USER_MAIN_SRC})

#
# Set up binaries to compile
#

# Compile all object files once
add_library(rvvm_objlib OBJECT ${RVVM_SRC})
target_link_libraries(rvvm_objlib PRIVATE rvvm_common)

# Static library target
if (BUILD_STATIC_LIBS)
	add_library(rvvm_static STATIC $<TARGET_OBJECTS:rvvm_objlib>)
	target_link_libraries(rvvm_static PRIVATE rvvm_common)
	set_target_properties(rvvm_static PROPERTIES PREFIX "lib")
endif()

# Shared library target
if (BUILD_SHARED_LIBS)
	set_property(TARGET rvvm_objlib PROPERTY POSITION_INDEPENDENT_CODE 1)
	add_library(rvvm SHARED $<TARGET_OBJECTS:rvvm_objlib>)
	target_link_libraries(rvvm PRIVATE rvvm_common)
	set_target_properties(rvvm PROPERTIES PREFIX "lib")
endif()

# Libretro core
if (BUILD_LIBRETRO)
	set(RVVM_LIBRETRO_SRC "${RVVM_SRC_DIR}/bindings/libretro/libretro.c")
	if (LIBRETRO_STATIC)
		add_library(rvvm_libretro STATIC ${RVVM_LIBRETRO_SRC} $<TARGET_OBJECTS:rvvm_objlib>)
	else()
		set_property(TARGET rvvm_objlib PROPERTY POSITION_INDEPENDENT_CODE 1)
		add_library(rvvm_libretro SHARED ${RVVM_LIBRETRO_SRC} $<TARGET_OBJECTS:rvvm_objlib>)
	endif()
	target_link_libraries(rvvm_libretro PRIVATE rvvm_common)

	# Follow naming conventions for libretro cores
	set_target_properties(rvvm_libretro PROPERTIES PREFIX "")
	if (ANDROID)
		set_target_properties(rvvm_libretro PROPERTIES SUFFIX "_android.so")
	elseif (EMSCRIPTEN)
		set_target_properties(rvvm_libretro PROPERTIES SUFFIX "${LIBRETRO_SUFFIX}.bc")
	elseif (LIBRETRO_STATIC)
		set_target_properties(rvvm_libretro PROPERTIES SUFFIX "${LIBRETRO_SUFFIX}.a")
	endif ()
endif()

# Main executable
add_executable(rvvm_cli ${RVVM_MAIN_SRC} $<TARGET_OBJECTS:rvvm_objlib>)
target_link_libraries(rvvm_cli PRIVATE rvvm_common)
set_target_properties(rvvm_cli PROPERTIES OUTPUT_NAME rvvm)

# Main executable
add_executable(rvvm_user ${RVVM_USER_MAIN_SRC} $<TARGET_OBJECTS:rvvm_objlib>)
target_link_libraries(rvvm_user PRIVATE rvvm_common)
