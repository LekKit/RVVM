# CMake configuration file for those who don't like Make (like I do)

cmake_minimum_required(VERSION 3.9)

set(RVVM_VERSION 0.2)
project(RVVM VERSION ${RVVM_VERSION}
	DESCRIPTION "RISC-V Virtual Machine"
	HOMEPAGE_URL "https://github.com/LekKit/RVVM"
	LANGUAGES C)

option(RVVM_USE_FB "Use framebuffer output" ON)
option(RVVM_USE_XCB "Use XCB framebuffer backend - Xlib is used otherwise" OFF)
option(RVVM_USE_XSHM "Use XSHM shared memory extension for X11" ON)
option(RVVM_USE_RV64 "Use RV64 CPU" OFF)
option(RVVM_USE_JIT "Use RVJIT Just-in-time compiler" OFF)
option(RVVM_USE_NET "Use networking" OFF)
option(RVVM_USE_FPU "Use floating-point instructions" ON)
option(RVVM_USE_VMSWAP "Use swap file for RAM" OFF)
option(RVVM_USE_VMSWAP_SPLIT "Use swap splitting - one file per page" OFF)

set(RVVM_SRC_DIR "${RVVM_SOURCE_DIR}/src")

# Common pseudo-library to pass parameters to other targets
add_library(rvvm_common INTERFACE)
target_include_directories(rvvm_common INTERFACE "${RVVM_SRC_DIR}")
if (MSVC)
	# Suppress warnings
	target_compile_definitions(rvvm_common INTERFACE _CRT_SECURE_NO_WARNINGS)
	target_compile_options(rvvm_common INTERFACE /wd4146 /wd4090 /wd4996 /wd4267 /wd4244)
endif()

# Enable LTO
include(CheckIPOSupported)
check_ipo_supported(RESULT RVVM_LTO)
if (RVVM_LTO)
	# CMAKE_INTERPROCEDURAL_OPTIMIZATION sets the default value of IPO
	# flag on new targets, so we set it and restore it at the end.
	set(RVVM_OLD_IPO ${CMAKE_INTERPROCEDURAL_OPTIMIZATION})
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if (RVVM_IS_TOP_LEVEL)
	# Add some cool fancy stuff if we're running as top level project

	# Warning checks
	if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
		add_compile_options(-Wall -Wextra)
	endif()
endif()

# CPU interpreter sources
file(GLOB RVVM_CPU_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_SRC_DIR}/cpu/*.h"
	"${RVVM_SRC_DIR}/cpu/*.c"
)
add_library(rvvm_cpu32 STATIC EXCLUDE_FROM_ALL ${RVVM_CPU_SRC})
target_link_libraries(rvvm_cpu32 PRIVATE rvvm_common)
add_library(rvvm_cpu64 STATIC EXCLUDE_FROM_ALL ${RVVM_CPU_SRC})
target_link_libraries(rvvm_cpu64 PRIVATE rvvm_common)
target_compile_definitions(rvvm_cpu64 PRIVATE RV64)
target_compile_definitions(rvvm_cpu64 PUBLIC USE_RV64)
if (RVVM_USE_FPU)
	# Link libm library if we have it
	find_library(RVVM_M_LIB m)
	if (RVVM_M_LIB)
		target_link_libraries(rvvm_cpu32 PRIVATE m)
		target_link_libraries(rvvm_cpu64 PRIVATE m)
	endif()

	# Enforce correct floating-point behavior
	if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"
	    AND NOT MSVC) # clang-cl
		target_compile_options(rvvm_cpu32 PRIVATE -frounding-math)
		target_compile_options(rvvm_cpu64 PRIVATE -frounding-math)
	endif()
endif()

# General sources
file(GLOB RVVM_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_SRC_DIR}/*.h"
	"${RVVM_SRC_DIR}/*.c"
)
set(RVVM_MAIN_SRC "${RVVM_SRC_DIR}/main.c")
list(REMOVE_ITEM RVVM_SRC ${RVVM_MAIN_SRC})
add_library(rvvm ${RVVM_SRC})
target_link_libraries(rvvm PRIVATE rvvm_common)
target_link_libraries(rvvm PUBLIC rvvm_cpu32)
target_compile_definitions(rvvm PRIVATE "VERSION=${RVVM_VERSION}")
if (UNIX OR WIN32)
	find_package(Threads REQUIRED)
	target_link_libraries(rvvm PRIVATE Threads::Threads)
endif()
if (UNIX)
	# Link librt if we have it
	find_library(RVVM_RT_LIB rt)
	if (RVVM_RT_LIB)
		target_link_libraries(rvvm PRIVATE rt)
	endif()
endif()
if (RVVM_USE_RV64)
	target_link_libraries(rvvm PUBLIC rvvm_cpu64)
endif()
if (RVVM_USE_JIT)
	target_link_libraries(rvvm PUBLIC rvjit)
endif()
if (RVVM_USE_VMSWAP)
	target_compile_definitions(rvvm PUBLIC USE_VMSWAP)
	if (RVVM_USE_VMSWAP_SPLIT)
		target_compile_definitions(rvvm PUBLIC USE_VMSWAP_SPLIT)
	endif()
endif()

# Device sources
file(GLOB RVVM_DEVICES_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_SRC_DIR}/devices/*.h"
	"${RVVM_SRC_DIR}/devices/*.c"
)
set(RVVM_XCB_SRC "${RVVM_SRC_DIR}/devices/x11window_xcb.c")
set(RVVM_XLIB_SRC "${RVVM_SRC_DIR}/devices/x11window_xlib.c")
set(RVVM_WIN32_SRC "${RVVM_SRC_DIR}/devices/win32window.c")
list(REMOVE_ITEM RVVM_DEVICES_SRC "${RVVM_XCB_SRC}" "${RVVM_XLIB_SRC}" "${RVVM_WIN32_SRC}")
target_sources(rvvm PRIVATE ${RVVM_DEVICES_SRC})
if (RVVM_USE_FB)
	target_compile_definitions(rvvm PRIVATE USE_FB)
	if (UNIX)
		find_package(X11 REQUIRED)
		target_compile_definitions(rvvm PRIVATE USE_X11)
		if (RVVM_USE_XCB)
			if (NOT TARGET X11::xcb)
				message(FATAL_ERROR "XCB library not found")
			endif()
			target_sources(rvvm PRIVATE ${RVVM_XCB_SRC})
			target_link_libraries(rvvm PRIVATE X11::xcb)
			if (RVVM_USE_XSHM)
				# FindX11 doesn't search for xcb-shm, so do this manually
				find_library(RVVM_XCB_SHM_LIB xcb-shm REQUIRED)
				target_link_libraries(rvvm PRIVATE ${RVVM_XCB_SHM_LIB})
				target_compile_definitions(rvvm PRIVATE USE_XSHM)
			endif()
		else()
			if (NOT TARGET X11::X11)
				message(FATAL_ERROR "X11 library not found")
			endif()
			target_sources(rvvm PRIVATE ${RVVM_XLIB_SRC})
			target_link_libraries(rvvm PRIVATE X11::X11)
			if (RVVM_USE_XSHM)
				if (NOT TARGET X11::Xext)
					message(FATAL_ERROR "Xext library not found")
				endif()
				target_link_libraries(rvvm PRIVATE X11::Xext)
				target_compile_definitions(rvvm PRIVATE USE_XSHM)
			endif()
		endif()
	elseif (WIN32)
		find_library(RVVM_GDI32_LIB gdi32 REQUIRED)
		target_sources(rvvm PRIVATE ${RVVM_WIN32_SRC})
		target_link_libraries(rvvm PRIVATE ${RVVM_GDI32_LIB})
	endif()
endif()
if (RVVM_USE_NET)
	if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
		target_compile_definitions(rvvm PUBLIC USE_NET)
	else()
		message(FATAL_ERROR "Networking is supported only on Linux for now")
	endif()
endif()

# RVJIT library
file(GLOB RVVM_RVJIT_SRC LIST_DIRECTORIES FALSE CONFIGURE_DEPENDS
	"${RVVM_SRC_DIR}/rvjit/*.h"
	"${RVVM_SRC_DIR}/rvjit/*.c"
)
add_library(rvjit EXCLUDE_FROM_ALL ${RVVM_RVJIT_SRC})
target_link_libraries(rvjit PRIVATE rvvm_common)
target_compile_definitions(rvjit PUBLIC USE_RVJIT)

# Main executable
add_executable(rvvm_bin ${RVVM_MAIN_SRC})
target_link_libraries(rvvm_bin PUBLIC rvvm)
target_link_libraries(rvvm_bin PRIVATE rvvm_common)
set_target_properties(rvvm_bin PROPERTIES OUTPUT_NAME rvvm)

# Restore IPO setting
if (RVVM_LTO)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${RVVM_OLD_IPO})
endif()
