# CMake configuration file for those who don't like Make (like I do)

cmake_minimum_required(VERSION 3.9)

set(RVVM_VERSION 0.5)
project(RVVM VERSION ${RVVM_VERSION}
	DESCRIPTION "RISC-V Virtual Machine"
	HOMEPAGE_URL "https://github.com/LekKit/RVVM"
	LANGUAGES C CXX)

option(RVVM_USE_RV64 "Use RV64 CPU" ON)
option(RVVM_USE_FPU "Use floating-point instructions" ON)
option(RVVM_USE_JIT "Use RVJIT Just-in-time compiler" ON)
option(RVVM_USE_FB "Use framebuffer window" ON)
option(RVVM_USE_XSHM "Use XSHM shared memory extension for X11" ON)
option(RVVM_USE_NET "Use networking" OFF)
option(RVVM_USE_TAP_LINUX "Use Linux TAP implementation" OFF)
option(RVVM_USE_FDT "Use Flattened Device Tree library for DTB generation" ON)
option(RVVM_USE_PCI "Use PCI bus emulation" ON)
option(RVVM_USE_SPINLOCK_DEBUG "Use spinlock debugging" ON)
option(RVVM_USE_PRECISE_FS "Use precise floating-point status tracking" OFF)

if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
	message(FATAL_ERROR "CMake in-source build is not allowed")
endif()

if (NOT CMAKE_BUILD_TYPE OR NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
	message(STATUS "Setting build type to Release as none was specified.")
	set(CMAKE_BUILD_TYPE "Release" CACHE
		STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

set(RVVM_SRC_DIR "${RVVM_SOURCE_DIR}/src")

# Common pseudo-library to pass parameters to other targets
add_library(rvvm_common INTERFACE)
target_include_directories(rvvm_common INTERFACE "${RVVM_SRC_DIR}")
target_compile_definitions(rvvm_common INTERFACE "VERSION=\"${RVVM_VERSION}\"")
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

if (RVVM_IS_TOP_LEVEL)
	# Add some cool fancy stuff if we're running as top level project

	# Warning checks
	if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		add_compile_options(-Wall -Wextra)
	endif()
endif()

if (UNIX OR WIN32)
	find_package(Threads REQUIRED)
	target_link_libraries(rvvm_common INTERFACE Threads::Threads)
endif()
if (UNIX)
	# Link librt, libatomic if we have them
	find_library(RVVM_RT_LIB rt)
	if (RVVM_RT_LIB)
		target_link_libraries(rvvm_common INTERFACE ${RVVM_RT_LIB})
	endif()
	find_library(RVVM_ATOMIC_LIB atomic)
	if (RVVM_ATOMIC_LIB)
		target_link_libraries(rvvm_common INTERFACE ${RVVM_ATOMIC_LIB})
	endif()
endif()

# CPU interpreter sources
file(GLOB RVVM_CPU_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_SRC_DIR}/cpu/*.h"
	"${RVVM_SRC_DIR}/cpu/*.c"
)
add_library(rvvm_cpu32 STATIC EXCLUDE_FROM_ALL ${RVVM_CPU_SRC})
target_link_libraries(rvvm_cpu32 PRIVATE rvvm_common)
target_link_libraries(rvvm_common INTERFACE rvvm_cpu32)
if (RVVM_USE_RV64)
	target_compile_definitions(rvvm_common INTERFACE USE_RV64)
	add_library(rvvm_cpu64 STATIC EXCLUDE_FROM_ALL ${RVVM_CPU_SRC})
	target_link_libraries(rvvm_cpu64 PRIVATE rvvm_common)
	target_link_libraries(rvvm_common INTERFACE rvvm_cpu64)
	target_compile_definitions(rvvm_cpu64 PRIVATE RV64)
endif()

if (RVVM_USE_FPU)
	target_compile_definitions(rvvm_common INTERFACE USE_FPU)

	# Link libm library if we have it
	find_library(RVVM_M_LIB m)
	if (RVVM_M_LIB)
		target_link_libraries(rvvm_common INTERFACE ${RVVM_M_LIB})
	endif()

	# Enforce correct floating-point behavior
	if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		include(CheckCCompilerFlag)
		check_c_compiler_flag(-frounding-math FROUNDING_MATH)
		if (FROUNDING_MATH)
			target_compile_options(rvvm_common INTERFACE -frounding-math)
		endif()
	endif()
endif()

# RVJIT library
if (RVVM_USE_JIT)
	file(GLOB RVVM_RVJIT_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
		"${RVVM_SRC_DIR}/rvjit/*.h"
		"${RVVM_SRC_DIR}/rvjit/*.c"
	)
	add_library(rvjit EXCLUDE_FROM_ALL ${RVVM_RVJIT_SRC})
	target_link_libraries(rvjit PRIVATE rvvm_common)
	target_link_libraries(rvvm_common INTERFACE rvjit)
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

if (RVVM_USE_FB)
	if (HAIKU)
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
	target_compile_definitions(rvvm_common INTERFACE USE_NET)
	if (RVVM_USE_TAP_LINUX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
		list(APPEND RVVM_DEVICES_SRC ${RVVM_TAP_LINUX_SRC})
		target_compile_definitions(rvvm_common INTERFACE USE_TAP_LINUX)
	else()
		if (RVVM_USE_TAP_LINUX)
			message(STATUS "TAP is supported only on Linux")
		endif()
		list(APPEND RVVM_DEVICES_SRC ${RVVM_TAP_USER_SRC})
		if (WIN32)
			target_link_libraries(rvvm_common INTERFACE ws2_32)
		endif()
	endif()
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

# General sources
file(GLOB RVVM_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_SRC_DIR}/*.h"
	"${RVVM_SRC_DIR}/*.c"
)
set(RVVM_MAIN_SRC "${RVVM_SRC_DIR}/main.c")
list(REMOVE_ITEM RVVM_SRC ${RVVM_MAIN_SRC})
add_library(rvvm ${RVVM_SRC} ${RVVM_DEVICES_SRC})
target_link_libraries(rvvm PRIVATE rvvm_common)

# Main executable
add_executable(rvvm_bin ${RVVM_MAIN_SRC})
target_link_libraries(rvvm_bin PUBLIC rvvm)
target_link_libraries(rvvm_bin PRIVATE rvvm_common)
set_target_properties(rvvm_bin PROPERTIES OUTPUT_NAME rvvm)

# Restore IPO setting
if (RVVM_LTO)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${RVVM_OLD_IPO})
endif()
