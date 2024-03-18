# CMake configuration file for those who don't like Make (like I do)
# Prev-maintainer: cerg2010cerg2010

cmake_minimum_required(VERSION 3.9)

set(RVVM_VERSION 0.7)
project(RVVM VERSION ${RVVM_VERSION}
	DESCRIPTION "RISC-V Virtual Machine"
	HOMEPAGE_URL "https://github.com/LekKit/RVVM"
	LANGUAGES C CXX)

option(RVVM_USE_RV64 "Use RV64 CPU" ON)
option(RVVM_USE_FPU "Use floating-point instructions" ON)
option(RVVM_USE_JIT "Use RVJIT Just-in-time compiler" ON)
option(RVVM_USE_FB "Use framebuffer window" ON)
option(RVVM_USE_SDL "Use SDL instead of native windowing APIs" OFF)
option(RVVM_USE_XSHM "Use XSHM shared memory extension for X11" ON)
option(RVVM_USE_NET "Use networking" ON)
option(RVVM_USE_TAP_LINUX "Use Linux TAP implementation" OFF)
option(RVVM_USE_FDT "Use Flattened Device Tree library for DTB generation" ON)
option(RVVM_USE_PCI "Use ATA over PCI, PIO mode is used otherwise" ON)
option(RVVM_USE_SPINLOCK_DEBUG "Use spinlock debugging" ON)
option(RVVM_USE_PRECISE_FS "Use precise floating-point status tracking" OFF)
option(RVVM_USE_LIB "Build shared librvvm library" ON)
option(RVVM_USE_JNI "Enable JNI bindings in librvvm (Very tiny size impact)" ON)
option(BUILD_LIBRETRO "Build a libretro core" OFF)
option(LIBRETRO_STATIC "Statically link the libretro core" OFF)

if(NOT WIN32)
	# Fancy coloring
	string(ASCII 27 VT_ESC)
	set(COLOR_RESET   "${VT_ESC}[m")
	set(COLOR_BOLD    "${VT_ESC}[1m")
	set(COLOR_RED     "${VT_ESC}[1;31m")
	set(COLOR_GREEN   "${VT_ESC}[1;32m")
	set(COLLOR_YELLOW "${VT_ESC}[1;33m")
	set(COLOR_BLUE    "${VT_ESC}[1;34m")
endif()

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
	message(FATAL_ERROR "CMake in-source build is not allowed, see README")
endif()

if (NOT CMAKE_BUILD_TYPE AND NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
	message(STATUS "${COLLOR_YELLOW}Setting build type to Release as none was specified${COLOR_RESET}")
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
	set(CMAKE_CONFIGURATION_TYPES "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(RVVM_SRC_DIR "${RVVM_SOURCE_DIR}/src")

# Get commit id
find_package(Git)
execute_process(COMMAND
  "${GIT_EXECUTABLE}" describe --match=NeVeRmAtCh_TaG --always --dirty
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  RESULT_VARIABLE RESULT
  OUTPUT_VARIABLE RVVM_COMMIT
  ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
if (RESULT)
	message(WARNING "Couldn't determine upstream git commit!")
	set(RVVM_COMMIT "unknown")
endif()

# Common pseudo-library to pass parameters to other targets
add_library(rvvm_common INTERFACE)
target_include_directories(rvvm_common INTERFACE "${RVVM_SRC_DIR}")
target_compile_definitions(rvvm_common INTERFACE "RVVM_VERSION=\"${RVVM_VERSION}-${RVVM_COMMIT}\"")
if (MSVC)
	# Suppress warnings: casts between int sizes, unsigned minus, cast after shift
	target_compile_definitions(rvvm_common INTERFACE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(rvvm_common INTERFACE /wd4267 /wd4244 /wd4146 /wd4334)
	# Use static runtime
	set(buildflags CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO
			CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
	foreach(buildflag ${buildflags})
		if(${buildflag} MATCHES "/MD")
			string(REGEX REPLACE "/MD" "/MT" ${buildflag} "${${buildflag}}")
		endif()
	endforeach()
endif()

# Enable LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT RVVM_LTO)
if (RVVM_LTO)
	# CMAKE_INTERPROCEDURAL_OPTIMIZATION sets the default value of IPO
	# flag on new targets, so we set it and restore it at the end.
	set(RVVM_OLD_IPO ${CMAKE_INTERPROCEDURAL_OPTIMIZATION})
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
	# Enable parallel LTO
	if (CMAKE_C_COMPILER_ID MATCHES "Clang")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=thin")
	elseif (CMAKE_C_COMPILER_ID MATCHES "GNU")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=auto")
	endif()
endif()

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
	# Warning options (Strict safety/portability, stack/object size limits)
	# -Wbad-function-cast, -Wcast-align, -Wdouble-promotion need fixes in codebase
	add_compile_options(-Wall -Wextra -Wshadow -Wvla -Wpointer-arith -Walloca -Wduplicated-cond)
	add_compile_options(-Wtrampolines -Wlarger-than=1048576 -Wframe-larger-than=32768 -Werror=return-type)
	if (NOT CMAKE_BUILD_TYPE MATCHES "Debug")
		# Optimization options
		add_compile_options(-O3 -DNDEBUG)
	endif()
	# Disable unsafe FPU optimizations, hide internal symbols
	add_compile_options(-fvisibility=hidden -fno-math-errno -frounding-math)
	if (CMAKE_C_COMPILER_ID MATCHES "Clang")
		# Shut off bogus warnings on older Clang
		add_compile_options(-Wno-unsupported-floating-point-opt -Wno-unknown-warning-option -Wno-ignored-optimization-argument)
	endif()
endif()

if (UNIX)
	find_package(Threads REQUIRED)
	target_link_libraries(rvvm_common INTERFACE Threads::Threads)
	# Link librt, libatomic if we have them
	find_library(RVVM_RT_LIB rt)
	if (RVVM_RT_LIB)
		target_link_libraries(rvvm_common INTERFACE rt)
	endif()
	find_library(RVVM_ATOMIC_LIB atomic)
	if (RVVM_ATOMIC_LIB)
		target_link_libraries(rvvm_common INTERFACE ${RVVM_ATOMIC_LIB})
	endif()
endif()

# General sources
file(GLOB RVVM_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_SRC_DIR}/*.h"
	"${RVVM_SRC_DIR}/*.c"
	"${RVVM_SRC_DIR}/cpu/riscv_*.c"
)
set(RVVM_MAIN_SRC "${RVVM_SRC_DIR}/main.c")
set(RVVM_NETWORKING_SRC "${RVVM_SRC_DIR}/networking.c")
list(REMOVE_ITEM RVVM_SRC ${RVVM_MAIN_SRC} ${RVVM_NETWORKING_SRC})

if (RVVM_USE_RV64)
	file(GLOB RVVM_RV64_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
		"${RVVM_SRC_DIR}/cpu/riscv64_*.c"
	)
	target_compile_definitions(rvvm_common INTERFACE USE_RV64)
	list(APPEND RVVM_SRC ${RVVM_RV64_SRC})
endif()

if (RVVM_USE_FPU)
	target_compile_definitions(rvvm_common INTERFACE USE_FPU)

	# Link libm library if we have it
	find_library(RVVM_M_LIB m)
	if (RVVM_M_LIB)
		target_link_libraries(rvvm_common INTERFACE ${RVVM_M_LIB})
	endif()
endif()

# Check JIT support for target
if (RVVM_USE_JIT AND NOT MSVC)
	execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine RESULT_VARIABLE RESULT OUTPUT_VARIABLE TRIPLET ERROR_QUIET)
	if (RESULT)
		message(WARNING "Couldn't determine target triplet! If build fails, disable USE_JIT")
	else()
		if (NOT TRIPLET MATCHES "^(x86|amd64|i386|aarch64|arm|riscv)")
			message(WARNING "Unsupported RVJIT target! RVJIT won't be built")
			set(RVVM_USE_JIT OFF)
		endif()
	endif()
endif()

# RVJIT sources
if (RVVM_USE_JIT)
	file(GLOB RVVM_RVJIT_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
		"${RVVM_SRC_DIR}/rvjit/*.h"
		"${RVVM_SRC_DIR}/rvjit/*.c"
	)
	list(APPEND RVVM_SRC ${RVVM_RVJIT_SRC})
	target_compile_definitions(rvvm_common INTERFACE USE_JIT)
endif()

# Device sources
file(GLOB RVVM_DEVICES_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_SRC_DIR}/devices/*.h"
	"${RVVM_SRC_DIR}/devices/*.c"
)
set(RVVM_XCB_SRC "${RVVM_SRC_DIR}/devices/x11window_xcb.c")
set(RVVM_XLIB_SRC "${RVVM_SRC_DIR}/devices/x11window_xlib.c")
set(RVVM_WIN32_SRC "${RVVM_SRC_DIR}/devices/win32window.c")
set(RVVM_HAIKU_SRC "${RVVM_SRC_DIR}/devices/haiku_window.cpp")
set(RVVM_SDL_SRC "${RVVM_SRC_DIR}/devices/sdl_window.c")
set(RVVM_TAP_LINUX_SRC "${RVVM_SRC_DIR}/devices/tap_linux.c")
set(RVVM_TAP_USER_SRC "${RVVM_SRC_DIR}/devices/tap_user.c")
list(REMOVE_ITEM RVVM_DEVICES_SRC
	${RVVM_XLIB_SRC}
	${RVVM_WIN32_SRC}
	${RVVM_HAIKU_SRC}
	${RVVM_SDL_SRC}
	${RVVM_TAP_LINUX_SRC}
	${RVVM_TAP_USER_SRC})

if (RVVM_USE_FB AND (NOT BUILD_LIBRETRO))
	if (RVVM_USE_SDL OR APPLE)
		find_library(RVVM_SDL_LIB SDL REQUIRED)
		list(APPEND RVVM_DEVICES_SRC ${RVVM_SDL_SRC})
		target_link_libraries(rvvm_common INTERFACE ${RVVM_SDL_LIB})
		target_compile_definitions(rvvm_common INTERFACE USE_FB USE_SDL)
	elseif (HAIKU)
		find_library(RVVM_BE_LIB be REQUIRED)
		list(APPEND RVVM_DEVICES_SRC ${RVVM_HAIKU_SRC})
		target_link_libraries(rvvm_common INTERFACE ${RVVM_BE_LIB})
		target_compile_definitions(rvvm_common INTERFACE USE_FB)
	elseif (UNIX)
		find_package(X11)
		if (NOT TARGET X11::X11)
			message(WARNING "X11 library not found, ignoring USE_FB")
		else()
			list(APPEND RVVM_DEVICES_SRC ${RVVM_XLIB_SRC})
			target_link_libraries(rvvm_common INTERFACE X11::X11)
			target_compile_definitions(rvvm_common INTERFACE USE_FB USE_X11)
			if (RVVM_USE_XSHM)
				if (NOT TARGET X11::Xext)
					message(WARNING "Xext library not found, ignoring USE_XSHM")
				else()
					target_link_libraries(rvvm_common INTERFACE X11::Xext)
					target_compile_definitions(rvvm_common INTERFACE USE_XSHM)
				endif()
			endif()
		endif()
	elseif (WIN32)
		list(APPEND RVVM_DEVICES_SRC ${RVVM_WIN32_SRC})
		target_link_libraries(rvvm_common INTERFACE gdi32)
		target_compile_definitions(rvvm_common INTERFACE USE_FB)
	endif()
endif()

if (RVVM_USE_NET)
	list(APPEND RVVM_SRC ${RVVM_NETWORKING_SRC})
	target_compile_definitions(rvvm_common INTERFACE USE_NET)
	if (WIN32)
		target_link_libraries(rvvm_common INTERFACE ws2_32)
	elseif (HAIKU)
		target_link_libraries(rvvm_common INTERFACE network)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "SunOS") # Solaris, Illumos, etc
		target_link_libraries(rvvm_common INTERFACE socket)
	endif()

	if (RVVM_USE_TAP_LINUX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
		list(APPEND RVVM_DEVICES_SRC ${RVVM_TAP_LINUX_SRC})
		target_compile_definitions(rvvm_common INTERFACE USE_TAP_LINUX)
	else()
		list(APPEND RVVM_DEVICES_SRC ${RVVM_TAP_USER_SRC})
		if (RVVM_USE_TAP_LINUX)
			message(WARNING "TAP is supported only on Linux, building userland networking")
		endif()
	endif()
endif()

if (RVVM_USE_LIB AND RVVM_USE_JNI)
	list(APPEND RVVM_SRC "${RVVM_SRC_DIR}/bindings/jni/rvvm_jni.c")
endif()

if (RVVM_USE_FDT)
	target_compile_definitions(rvvm_common INTERFACE USE_FDT)
endif()

if (RVVM_USE_PCI)
	target_compile_definitions(rvvm_common INTERFACE USE_PCI)
endif()

if (RVVM_USE_SPINLOCK_DEBUG)
	target_compile_definitions(rvvm_common INTERFACE USE_SPINLOCK_DEBUG)
endif()

if (RVVM_USE_PRECISE_FS)
	target_compile_definitions(rvvm_common INTERFACE USE_PRECISE_FS)
endif()

# Compile all object files once
add_library(rvvm_objlib OBJECT ${RVVM_SRC} ${RVVM_DEVICES_SRC})
target_link_libraries(rvvm_objlib PRIVATE rvvm_common)

# Static library target
add_library(rvvm_static STATIC $<TARGET_OBJECTS:rvvm_objlib>)
target_link_libraries(rvvm_static PRIVATE rvvm_common)
set_target_properties(rvvm_static PROPERTIES PREFIX "lib")

if (NOT BUILD_LIBRETRO)
	# Shared library target
	if (RVVM_USE_LIB)
		target_compile_definitions(rvvm_common INTERFACE USE_LIB)
		set_property(TARGET rvvm_objlib PROPERTY POSITION_INDEPENDENT_CODE 1)
		target_compile_options(rvvm_objlib INTERFACE -ffat-lto-objects)
		add_library(rvvm SHARED $<TARGET_OBJECTS:rvvm_objlib>)
		target_link_libraries(rvvm PRIVATE rvvm_common)
		set_target_properties(rvvm PROPERTIES PREFIX "lib")
	endif()

	# Main executable
	add_executable(rvvm_cli ${RVVM_MAIN_SRC})
	target_link_libraries(rvvm_cli PUBLIC rvvm_static)
	target_link_libraries(rvvm_cli PRIVATE rvvm_common)
	set_target_properties(rvvm_cli PROPERTIES OUTPUT_NAME rvvm)
else()
	# libretro core
	set(RVVM_LIBRETRO_SRC "${RVVM_SRC_DIR}/bindings/libretro/libretro.c")
	if (LIBRETRO_STATIC)
		add_library(rvvm_libretro STATIC ${RVVM_LIBRETRO_SRC})
	else()
		add_library(rvvm_libretro SHARED ${RVVM_LIBRETRO_SRC})
	endif()
	target_link_libraries(rvvm_libretro PRIVATE rvvm_static rvvm_common)
	if (RVVM_USE_NET)
		target_compile_definitions(rvvm_libretro PRIVATE USE_NET)
	endif()

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

# Restore IPO setting
if (RVVM_LTO)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${RVVM_OLD_IPO})
endif()
