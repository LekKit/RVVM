# CMake configuration file for those who don't like Make (like I do)

cmake_minimum_required(VERSION 3.9)

set(RVVM_VERSION 0.4)
project(RVVM VERSION ${RVVM_VERSION}
	DESCRIPTION "RISC-V Virtual Machine"
	HOMEPAGE_URL "https://github.com/LekKit/RVVM"
	LANGUAGES C)

option(RVVM_USE_FB "Use framebuffer output" ON)
option(RVVM_USE_XCB "Use XCB framebuffer backend - Xlib is used otherwise" OFF)
option(RVVM_USE_XSHM "Use XSHM shared memory extension for X11" ON)
option(RVVM_USE_RV64 "Use RV64 CPU" ON)
option(RVVM_USE_JIT "Use RVJIT Just-in-time compiler" OFF)
option(RVVM_USE_NET "Use networking" OFF)
option(RVVM_USE_FPU "Use floating-point instructions" ON)
option(RVVM_USE_VMSWAP "Use swap file for RAM" OFF)
option(RVVM_USE_VMSWAP_SPLIT "Use swap splitting - one file per page" OFF)
option(RVVM_USE_SPINLOCK_DEBUG "Use spinlock debugging" ON)
option(RVVM_USE_FDT "Use Flattened Device Tree library for DTB generation" ON)
option(RVVM_USE_PRECISE_FS "Use precise floating-point status tracking - makes context switches cheaper" ON)
option(RVVM_USE_PCI "Use PCI bus emulation" ON)
option(RVVM_USE_RTC "Use Real Time Clock" ON)
option(RVVM_USE_TAP_LINUX "Use Linux TAP implementation" OFF)

if (RVVM_USE_JIT AND RVVM_USE_PRECISE_FS)
	message(FATAL_ERROR "Precise FS tracking cannot be used with JIT.")
endif()

set(RVVM_SRC_DIR "${RVVM_SOURCE_DIR}/src")

# Common pseudo-library to pass parameters to other targets
add_library(rvvm_common INTERFACE)
target_include_directories(rvvm_common INTERFACE "${RVVM_SRC_DIR}")
target_compile_definitions(rvvm_common INTERFACE "VERSION=\"${RVVM_VERSION}\"")
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

if (RVVM_USE_SPINLOCK_DEBUG)
	target_compile_definitions(rvvm_common INTERFACE USE_SPINLOCK_DEBUG)
endif()

if (RVVM_USE_FDT)
	target_compile_definitions(rvvm_common INTERFACE USE_FDT)
endif()

if (RVVM_USE_PRECISE_FS)
	target_compile_definitions(rvvm_common INTERFACE USE_PRECISE_FS)
endif()

if (RVVM_USE_PCI)
	target_compile_definitions(rvvm_common INTERFACE USE_PCI)
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
if (RVVM_USE_RV64)
	target_compile_definitions(rvvm_cpu32 PUBLIC USE_RV64)
endif()
if (RVVM_USE_FPU)
	target_compile_definitions(rvvm_cpu32 PUBLIC USE_FPU)
	target_compile_definitions(rvvm_cpu64 PUBLIC USE_FPU)

	# Link libm library if we have it
	find_library(RVVM_M_LIB m)
	if (RVVM_M_LIB)
		target_link_libraries(rvvm_cpu32 PRIVATE ${RVVM_M_LIB})
		target_link_libraries(rvvm_cpu64 PRIVATE ${RVVM_M_LIB})
	endif()

	# Enforce correct floating-point behavior
	if (CMAKE_C_COMPILER_ID MATCHES "GNU"
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
if (UNIX OR WIN32)
	find_package(Threads REQUIRED)
	target_link_libraries(rvvm PRIVATE Threads::Threads)
endif()
if (UNIX)
	# Link librt if we have it
	find_library(RVVM_RT_LIB rt)
	if (RVVM_RT_LIB)
		target_link_libraries(rvvm PRIVATE ${RVVM_RT_LIB})
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
set(RVVM_RTC_SRC "${RVVM_SRC_DIR}/devices/rtc-goldfish.c")
set(RVVM_TAP_LINUX_SRC "${RVVM_SRC_DIR}/devices/tap_linux.c")
set(RVVM_TAP_USER_SRC "${RVVM_SRC_DIR}/devices/tap_user.c")
list(REMOVE_ITEM RVVM_DEVICES_SRC
	${RVVM_XCB_SRC}
	${RVVM_XLIB_SRC}
	${RVVM_WIN32_SRC}
	${RVVM_RTC_SRC}
	${RVVM_TAP_LINUX_SRC}
	${RVVM_TAP_USER_SRC})
target_sources(rvvm PRIVATE ${RVVM_DEVICES_SRC})
if (RVVM_USE_FB)
	target_compile_definitions(rvvm PUBLIC USE_FB)
	if (UNIX)
		find_package(X11 REQUIRED)
		target_compile_definitions(rvvm PRIVATE USE_X11)
		if (RVVM_USE_XCB)
			if (NOT TARGET X11::xcb)
				message(FATAL_ERROR "XCB library not found")
			endif()
			target_sources(rvvm PRIVATE "${RVVM_XCB_SRC}")
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
			target_sources(rvvm PRIVATE "${RVVM_XLIB_SRC}")
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
		target_sources(rvvm PRIVATE "${RVVM_WIN32_SRC}")
		find_library(RVVM_GDI32_LIB gdi32 REQUIRED)
		target_link_libraries(rvvm PRIVATE ${RVVM_GDI32_LIB})
	endif()
endif()
if (RVVM_USE_NET)
	target_compile_definitions(rvvm PUBLIC USE_NET)
	if (RVVM_USE_TAP_LINUX)
		if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
			target_sources(rvvm PRIVATE "${RVVM_TAP_LINUX_SRC}")
			target_compile_definitions(rvvm PUBLIC USE_TAP_LINUX)
		else()
			message(FATAL_ERROR "Networking is supported only on Linux for now")
		endif()
	else()
		target_sources(rvvm PRIVATE "${RVVM_TAP_USER_SRC}")
	endif()
endif()

if (RVVM_USE_RTC)
  target_compile_definitions(rvvm PUBLIC USE_RTC)
  target_sources(rvvm PRIVATE ${RVVM_RTC_SRC})
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
