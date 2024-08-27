# Makefile :)
NAME    := rvvm
SRCDIR  := src
VERSION := 0.7

# Detect build host features
ifeq ($(OS),Windows_NT)
# Passed by MinGW/Cygwin Make on Windows build hosts
override OS := $(CROSS_OS)
HOST_WINDOWS := 1
NULL_STDERR := 2>NUL
HOST_UNAME := Windows
HOST_CPUS := $(firstword $(NUMBER_OF_PROCESSORS) 1)
ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
HOST_ARCH := x86_64
else
ifeq ($(PROCESSOR_ARCHITECTURE),ARM64)
HOST_ARCH := arm64
else
HOST_ARCH := i386
endif
endif
else
# Assuming the build host is POSIX
HOST_POSIX := 1
NULL_STDERR := 2>/dev/null
HOST_UNAME := $(firstword $(shell uname -s 2>/dev/null) POSIX)
HOST_CPUS := $(firstword $(shell nproc 2>/dev/null) $(shell sysctl -n hw.ncpu 2>/dev/null) 1)
HOST_ARCH := $(firstword $(shell uname -m 2>/dev/null) Unknown)
endif

# Some eye-candy stuff
SPACE   :=
ifneq (,$(TERM))
BOLD    := $(shell tput md   $(NULL_STDERR) || tput bold $(NULL_STDERR)    || printf "\\e[1m" $(NULL_STDERR))
RESET   := $(shell tput me   $(NULL_STDERR) || tput sgr0 $(NULL_STDERR)    || printf "\\e[0m" $(NULL_STDERR))$(BOLD)
RED     := $(shell tput AF 1 $(NULL_STDERR) || tput setaf 1 $(NULL_STDERR) || printf "\\e[31m" $(NULL_STDERR))$(BOLD)
GREEN   := $(shell tput AF 2 $(NULL_STDERR) || tput setaf 2 $(NULL_STDERR) || printf "\\e[32m" $(NULL_STDERR))$(BOLD)
YELLOW  := $(shell tput AF 3 $(NULL_STDERR) || tput setaf 3 $(NULL_STDERR) || printf "\\e[33m" $(NULL_STDERR))$(BOLD)

$(info $(RESET))
ifneq (,$(findstring UTF, $(LANG)))
$(info $(SPACE)  ██▀███   ██▒   █▓ ██▒   █▓ ███▄ ▄███▓)
$(info $(SPACE) ▓██ ▒ ██▒▓██░   █▒▓██░   █▒▓██▒▀█▀ ██▒)
$(info $(SPACE) ▓██ ░▄█ ▒ ▓██  █▒░ ▓██  █▒░▓██    ▓██░)
$(info $(SPACE) ▒██▀▀█▄    ▒██ █░░  ▒██ █░░▒██    ▒██ )
$(info $(SPACE) ░██▓ ▒██▒   ▒▀█░     ▒▀█░  ▒██▒   ░██▒)
$(info $(SPACE) ░ ▒▓ ░▒▓░   ░ █░     ░ █░  ░ ▒░   ░  ░)
$(info $(SPACE)   ░▒ ░ ▒░   ░ ░░     ░ ░░  ░  ░      ░)
$(info $(SPACE)   ░░   ░      ░░       ░░  ░      ░   )
$(info $(SPACE)    ░           ░        ░         ░   )
$(info $(SPACE)               ░        ░              )
$(info $(SPACE))
endif
endif

# Automatically parallelize build
JOBS ?= $(HOST_CPUS)
override MAKEFLAGS += -j $(JOBS)

# Get compiler target triplet (arch-vendor-kernel-abi)
CC_TRIPLET := $(firstword $(shell $(CC) $(CFLAGS) -print-multiarch $(NULL_STDERR)) $(shell $(CC) $(CFLAGS) -dumpmachine $(NULL_STDERR)))
ifeq (,$(findstring -,$(CC_TRIPLET)))
CC_TRIPLET :=
$(info [$(YELLOW)INFO$(RESET)] Invalid triplet, specify OS/ARCH manually if cross-compiling)
endif

tolower = $(subst A,a,$(subst B,b,$(subst C,c,$(subst D,d,$(subst E,e,$(subst F,f,$(subst G,g,$(subst H,h,$(subst I,i,$(subst J,j,$(subst K,k,$(subst L,l,$(subst M,m,$(subst N,n,$(subst O,o,$(subst P,p,$(subst Q,q,$(subst R,r,$(subst S,s,$(subst T,t,$(subst U,u,$(subst V,v,$(subst W,w,$(subst X,x,$(subst Y,y,$(subst Z,z,$1))))))))))))))))))))))))))

# Detect target OS
ifneq (,$(findstring android, $(CC_TRIPLET)))
override OS := Android
else
ifneq (,$(findstring linux, $(CC_TRIPLET)))
override OS := Linux
else
ifneq (,$(findstring mingw, $(CC_TRIPLET)))
override OS := Windows
else
ifneq (,$(findstring windows, $(CC_TRIPLET)))
override OS := Windows
else
ifneq (,$(findstring cygwin, $(CC_TRIPLET)))
override OS := Cygwin
else
ifneq (,$(findstring darwin, $(CC_TRIPLET)))
override OS := Darwin
else
ifneq (,$(findstring macos, $(CC_TRIPLET)))
override OS := Darwin
else
ifneq (,$(findstring emscripten, $(CC_TRIPLET)))
# Running Emscripten
override OS := Emscripten
else
# Failed to determine target toolchain OS
ifndef OS
# Use host OS as a target
override OS := $(HOST_UNAME)
endif
endif
endif
endif
endif
endif
endif
endif
endif

$(info Detected OS: $(GREEN)$(OS)$(RESET))
override OS := $(call tolower,$(OS))

# Windows-specific build options
ifeq ($(OS),windows)
# Use LDFLAG -mwindows for GUI-only
override LDFLAGS += -static
BIN_EXT := .exe
LIB_EXT := .dll
else

# Emscripten-specific build options
ifeq ($(OS),emscripten)
override CFLAGS += -pthread
override LDFLAGS += -s TOTAL_MEMORY=512MB -s PROXY_TO_PTHREAD
BIN_EXT := .html
LIB_EXT := .so
USE_SDL ?= 1
USE_NET ?= 0
else

# POSIX build options
BIN_EXT :=
ifeq ($(OS),darwin)
LIB_EXT := .dylib
else
LIB_EXT := .so
endif

# Check for lib presence before linking (there is no pthread on Android, etc)
ifneq (,$(findstring main, $(shell $(CC) -pthread $(CFLAGS) $(LDFLAGS) -lpthread 2>&1)))
override CFLAGS += -pthread
endif
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -lpthread 2>&1)))
override LDFLAGS += -lpthread
endif
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -lrt 2>&1)))
override LDFLAGS += -lrt
endif
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -ldl 2>&1)))
override LDFLAGS += -ldl
endif
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -latomic 2>&1)))
override LDFLAGS += -latomic
else
override CFLAGS += -DNO_LIBATOMIC
endif

# Set some addiional options based on POSIX flavor

ifneq (,$(findstring darwin, $(OS))$(findstring serenity, $(OS)))
# Enable SDL2 on Darwin, Serenity
USE_SDL ?= 2
endif

ifeq (,$(findstring darwin, $(OS))$(findstring haiku, $(OS))$(findstring serenity, $(OS))$(findstring android, $(OS)))
# Not Darwin, Haiku, Serenity or Android - enable X11
USE_X11 ?= 1
endif

ifeq ($(OS),openbsd)
override CFLAGS += -I/usr/X11R6/include -D_POSIX_C_SOURCE=200809L
override LDFLAGS += -L/usr/X11R6/lib
endif

endif
endif

# Detect compiler type, version
CC_HELP := $(shell $(CC) --help $(NULL_STDERR))
CC_VERSION := $(shell $(CC) -dumpversion $(NULL_STDERR))

ifneq (,$(findstring clang,$(CC_HELP)))
# LLVM Clang or derivatives (Zig CC, Emscripten)
CC_TYPE := clang
ifneq (,$(findstring Emscripten,$(CC_HELP)))
$(info Detected CC: $(GREEN)LLVM Clang (EMCC $(CC_VERSION))$(RESET))
else
$(info Detected CC: $(GREEN)LLVM Clang $(CC_VERSION)$(RESET))
endif

else
ifneq (,$(findstring gcc,$(CC_HELP)))
# GNU GCC or derivatives (MinGW)
CC_TYPE := gcc
$(info Detected CC: $(GREEN)GCC $(CC_VERSION)$(RESET))

else
# Toy compiler (TCC, Chibicc, Cproc)
CC_TYPE ?= generic
$(info Detected CC: $(RED)Unknown$(RESET))

endif
endif

# Detect target arch
ifndef ARCH
ifneq (,$(findstring -,$(CC_TRIPLET)))
# Get target arch from target triplet
ARCH := $(firstword $(subst -, ,$(CC_TRIPLET)))
else
# This may fail on older compilers, fallback to host arch then
ARCH := $(HOST_ARCH)
endif
endif

# Use common arch names (x86_64, arm64)
ifneq (,$(findstring amd64, $(ARCH)))
override ARCH = x86_64
endif
ifneq (,$(findstring aarch64, $(ARCH)))
override ARCH = arm64
endif
# x86 compilers sometimes fail to report -m32 multiarch
ifneq (,$(findstring -m32, $(CFLAGS)))
ifneq (,$(findstring x86_64, $(ARCH)))
override ARCH = i386
endif
endif

$(info Target arch: $(GREEN)$(ARCH)$(RESET))

# Debugging options
ifeq ($(USE_DEBUG_FULL),1)
BUILD_TYPE := debug
DEBUG_OPTS := -DDEBUG -Og -ggdb -fno-omit-frame-pointer
else
BUILD_TYPE := release
ifeq ($(USE_DEBUG),1)
# Release with debug info
DEBUG_OPTS := -DNDEBUG -g -fno-omit-frame-pointer
else
DEBUG_OPTS := -DNDEBUG
endif
endif

# Warning options (Strict safety/portability, stack/object size limits)
# Need at least GCC 7.0 or Clang 7.0
# -Wbad-function-cast, -Wcast-align, need fixes in codebase
WARN_OPTS := -Wall -Wextra -Wshadow -Wvla -Wpointer-arith -Walloca -Wduplicated-cond \
-Wtrampolines -Wlarger-than=1048576 -Wframe-larger-than=32768 -Wdouble-promotion -Werror=return-type

# Compiler-specific options
ifeq ($(CC_TYPE),gcc)
CC_STD := -std=gnu11
CXX_STD := -std=gnu++11
override CFLAGS := -O2 -flto=auto -fvisibility=hidden -fno-math-errno $(WARN_OPTS) $(DEBUG_OPTS) $(CFLAGS)
else
ifeq ($(CC_TYPE),clang)
CC_STD := -std=gnu11
CXX_STD := -std=gnu++11
override CFLAGS := -O2 -flto=thin -fvisibility=hidden -fno-math-errno -Wno-unknown-warning-option $(WARN_OPTS) $(DEBUG_OPTS) $(CFLAGS)
else
# Whatever compiler that might be, use conservative options
CC_STD := -std=gnu99
CXX_STD :=
override CFLAGS := -O2 $(DEBUG_OPTS) $(CFLAGS)
endif
endif

# Version string
GIT_COMMIT := $(firstword $(shell git describe --match=NeVeRmAtCh_TaG --always --dirty $(NULL_STDERR)) unknown)
VERSION := $(VERSION)-$(GIT_COMMIT)

$(info Version:     $(GREEN)RVVM $(VERSION)$(RESET))
$(info $(SPACE))

ifeq ($(GIT_COMMIT),unknown)
$(info [$(RED)WARN$(RESET)] Unknown upstream git commit!)
endif

# Generic compiler flags
override CFLAGS := -I$(SRCDIR) -DRVVM_VERSION=\"$(VERSION)\" $(CFLAGS)

# Conditionally compiled sources, do not build by default
SRC_cond := $(SRCDIR)/devices/x11window_xlib.c $(SRCDIR)/devices/win32window.c \
		$(SRCDIR)/devices/sdl_window.c $(SRCDIR)/devices/haiku_window.cpp \
		$(SRCDIR)/devices/tap_linux.c $(SRCDIR)/devices/tap_user.c $(SRCDIR)/networking.c

# Output directories / files
BUILDDIR ?= $(BUILD_TYPE).$(OS).$(ARCH)
OBJDIR := $(BUILDDIR)/obj
ifndef BINARY
BINARY := $(BUILDDIR)/$(NAME)_$(ARCH)$(BIN_EXT)
else
override BINARY := $(BUILDDIR)/$(BINARY)
endif
SHARED := $(BUILDDIR)/lib$(NAME)$(LIB_EXT)
STATIC := $(BUILDDIR)/lib$(NAME)_static.a

# Select sources to compile
SRC := $(wildcard $(SRCDIR)/*.c $(SRCDIR)/devices/*.c)
# Wipe all platform/config-dependant sources
SRC := $(filter-out $(SRC_cond),$(SRC))

# Default build configuration
USE_RV64 ?= 1
USE_FPU ?= 1
USE_JIT ?= 1
USE_GUI ?= 1
USE_SDL ?= 0
USE_NET ?= 1
USE_TAP_LINUX ?= 0
USE_FDT ?= 1
USE_PCI ?= 1
USE_VFIO ?= 1
USE_SPINLOCK_DEBUG ?= 1
USE_JNI ?= 1
USE_ISOLATION ?= 1

ifeq ($(USE_RV64),1)
override CFLAGS += -DUSE_RV64
endif

ifeq ($(USE_FPU),1)
# Needed for floating-point functions like fetestexcept/feraiseexcept
override LDFLAGS += -lm
override CFLAGS += -DUSE_FPU
# Disable unsafe FPU optimizations
ifeq (,$(findstring rounding-math, $(shell $(CC) -frounding-math 2>&1)))
override CFLAGS += -frounding-math
ifeq ($(CC_TYPE),clang)
override CFLAGS += -Wno-unsupported-floating-point-opt -Wno-ignored-optimization-argument
endif
endif
endif

ifeq ($(USE_JIT),1)
# Check if RVJIT supports the target
ifneq (,$(findstring 86, $(ARCH)))
else
ifneq (,$(findstring arm, $(ARCH)))
else
ifneq (,$(findstring riscv, $(ARCH)))
else
override USE_JIT = 0
$(info [$(YELLOW)INFO$(RESET)] No RVJIT support for current target)
endif
endif
endif
ifeq ($(USE_JIT),1)
SRC += $(SRCDIR)/rvjit/rvjit.c $(SRCDIR)/rvjit/rvjit_emit.c
override CFLAGS += -DUSE_JIT
endif
endif

# Framebuffer GUI
ifeq ($(USE_GUI),1)
override CFLAGS += -DUSE_GUI

# SDL Window
ifneq ($(USE_SDL),0)

# Beware: SDL2 on Emscripten is VERY BROKEN at the moment
ifeq ($(USE_SDL),2)
ifeq ($(OS),emscripten)
SDL_CFLAGS := -s USE_SDL=2
endif
endif

SRC += $(SRCDIR)/devices/sdl_window.c
override CFLAGS += -DUSE_SDL=$(USE_SDL) $(SDL_CFLAGS)
endif

# Haiku Window
ifeq ($(OS),haiku)
SRC_CXX += $(SRCDIR)/devices/haiku_window.cpp
override LDFLAGS += -lbe
endif

# WinAPI Window
ifeq ($(OS),windows)
SRC += $(SRCDIR)/devices/win32window.c
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -lgdi32 2>&1)))
override LDFLAGS += -lgdi32
endif
endif

# Xlib Window
ifeq ($(USE_X11),1)
SRC += $(SRCDIR)/devices/x11window_xlib.c
override CFLAGS += -DUSE_X11
endif

endif

ifeq ($(USE_NET),1)
override CFLAGS += -DUSE_NET
ifneq ($(OS),linux)
override USE_TAP_LINUX = 0
endif

# Networking over Linux TAP
ifeq ($(USE_TAP_LINUX),1)
SRC += $(SRCDIR)/devices/tap_linux.c
override CFLAGS += -DUSE_TAP_LINUX
else

# Userspace networking
SRC += $(SRCDIR)/devices/tap_user.c $(SRCDIR)/networking.c

# Link WinSock on Win32
ifeq ($(OS),windows)
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -lws2_32 2>&1)))
override LDFLAGS += -lws2_32
else
# On WinCE there is no _32 suffix
override LDFLAGS += -lws2
endif
else

# Link libnetwork on Haiku
ifeq ($(OS),haiku)
override LDFLAGS += -lnetwork
else

# Link libsocket on SunOS and derivatives (Solaris)
ifeq ($(OS),sunos)
override LDFLAGS += -lsocket
endif
endif
endif
endif
endif

ifeq ($(USE_FDT),1)
override CFLAGS += -DUSE_FDT
endif

ifeq ($(USE_PCI),1)
override CFLAGS += -DUSE_PCI
endif

ifeq ($(USE_VFIO),1)
override CFLAGS += -DUSE_VFIO
endif

ifeq ($(USE_SPINLOCK_DEBUG),1)
override CFLAGS += -DUSE_SPINLOCK_DEBUG
endif

ifeq ($(USE_ISOLATION),1)
override CFLAGS += -DUSE_ISOLATION
endif

# Do not pass lib-related flags for dev/cli/test builds (Faster)
ifneq (,$(findstring lib, $(MAKECMDGOALS))$(findstring install, $(MAKECMDGOALS)))
override CFLAGS += -DUSE_LIB -fPIC -ffat-lto-objects
# Build JNI bindings inside librvvm dynlib
ifeq ($(USE_JNI),1)
SRC += $(SRCDIR)/bindings/jni/rvvm_jni.c
endif
endif

# CPU interpreter sources
SRC += $(wildcard $(SRCDIR)/cpu/riscv_*.c)
ifeq ($(USE_RV64),1)
SRC += $(wildcard $(SRCDIR)/cpu/riscv64_*.c)
endif

# Rules for object files from sources
OBJ := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
OBJ_CXX := $(SRC_CXX:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

# Combine the object files
OBJS := $(OBJ) $(OBJ_CXX)
LIB_OBJS := $(filter-out main.o,$(OBJS))
DEPS := $(OBJS:.o=.d)
DIRS := $(sort $(BUILDDIR) $(OBJDIR) $(dir $(OBJS)))

# Create directories for object files
ifeq ($(HOST_POSIX),1)
$(shell mkdir -p $(DIRS))
else
$(shell mkdir $(subst /,\\, $(DIRS)) $(NULL_STDERR))
endif

# Check previous buildflags
CFLAGS_TXT := $(OBJDIR)/cflags.txt
LDFLAGS_TXT := $(OBJDIR)/ldflags.txt
CURR_CFLAGS := $(CC) $(CFLAGS)
CURR_LDFLAGS := $(LD) $(LDFLAGS)
sinclude $(CFLAGS_TXT) $(LDFLAGS_TXT)
ifneq ($(CURR_CFLAGS),$(PREV_CFLAGS))
ifneq ($(PREV_CFLAGS),)
$(info [$(YELLOW)INFO$(RESET)] CFLAGS changed, doing a full rebuild)
endif
override MAKEFLAGS += -B
else
ifneq ($(CURR_LDFLAGS),$(PREV_LDFLAGS))
$(info [$(YELLOW)INFO$(RESET)] LDFLAGS changed, relinking binaries)
$(shell rm $(BINARY) $(SHARED) $(NULL_STDERR))
endif
endif
ifeq (3.82,$(firstword $(sort $(MAKE_VERSION) 3.82)))
$(file >$(CFLAGS_TXT),PREV_CFLAGS := $(CURR_CFLAGS))
$(file >$(LDFLAGS_TXT),PREV_LDFLAGS := $(CURR_LDFLAGS))
else
$(shell echo "PREV_CFLAGS := $(subst ",\\",$(CURR_CFLAGS))" > $(CFLAGS_TXT))
$(shell echo "PREV_LDFLAGS := $(subst ",\\",$(CURR_LDFLAGS))" > $(LDFLAGS_TXT))
endif

# Check compiler ability to generate header dependencies
ifeq (,$(findstring MMD, $(shell $(CC) -MMD 2>&1)))
DO_CC = @$(CC) $(CC_STD) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -o $@ -c $<
DO_CXX = @$(CXX) $(CXX_STD) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -o $@ -c $<
else
$(info [$(RED)WARN$(RESET)] No compiler support for header dependencies, forcing rebuild)
override MAKEFLAGS += -B
DO_CC = @$(CC) $(CC_STD) $(CFLAGS) -o $@ -c $<
DO_CXX = @$(CXX) $(CXX_STD) $(CFLAGS) -o $@ -c $<
endif

# Link using CC or CXX if any C++ code is present
ifndef SRC_CXX
CC_LD = $(CC)
else
CC_LD = $(CXX)
endif

# Ignore deleted header files
%.h:
	@:

# C object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(info [$(YELLOW)CC$(RESET)] $<)
	$(DO_CC)

# C++ object files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp Makefile
	$(info [$(YELLOW)CXX$(RESET)] $<)
	$(DO_CXX)

# Main binary
$(BINARY): $(OBJS)
	$(info [$(GREEN)LD$(RESET)] $@)
	@$(CC_LD) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

# Shared library
$(SHARED): $(LIB_OBJS)
	$(info [$(GREEN)LD$(RESET)] $@)
	@$(CC_LD) $(CFLAGS) $(LIB_OBJS) $(LDFLAGS) -shared -o $@

# Static library
$(STATIC): $(LIB_OBJS)
	$(info [$(GREEN)AR$(RESET)] $@)
	@$(AR) -rcs $@ $(LIB_OBJS)

.PHONY: all
all: $(BINARY)

.PHONY: lib
lib: $(SHARED) $(STATIC)

.PHONY: test
test: $(BINARY)
	@curl -LO "https://github.com/LekKit/riscv-tests/releases/download/rvvm-tests/riscv-tests.tar.gz" --output-dir "$(BUILDDIR)"
	@tar xf "$(BUILDDIR)/riscv-tests.tar.gz" -C $(BUILDDIR)
	@echo
	@echo "[$(YELLOW)INFO$(RESET)] Running RISC-V Tests (RV32)"
	@echo
	@for file in "$(BUILDDIR)/riscv-tests/rv32"*; do \
		result=$$($(BINARY) $$file -nogui -rv32 | tr -d '\0'); \
		result="$${result##* }"; \
		if [ "$$result" -eq "0" ]; then \
		echo "[$(GREEN)PASS$(RESET)] $$file"; \
		else \
		echo "[$(RED)FAIL: $$result$(RESET)] $$file"; \
		fi; \
	done
ifeq ($(USE_RV64),1)
	@echo
	@echo "[$(YELLOW)INFO$(RESET)] Running RISC-V Tests (RV64)"
	@echo
	@for file in "$(BUILDDIR)/riscv-tests/rv64"*; do \
		result=$$($(BINARY) $$file -nogui -rv64 | tr -d '\0'); \
		result="$${result##* }"; \
		if [ "$$result" -eq "0" ]; then \
		echo "[$(GREEN)PASS$(RESET)] $$file"; \
		else \
		echo "[$(RED)FAIL: $$result$(RESET)] $$file"; \
		fi; \
	done
endif

CPPCHECK_GENERIC_OPTIONS := -f -j$(JOBS) --inline-suppr --std=c99 -q -I $(SRCDIR)
CPPCHECK_SUPPRESS_OPTIONS :=  --suppress=missingIncludeSystem \
--suppress=constParameterPointer --suppress=constVariablePointer --suppress=constParameterCallback \
--suppress=constVariable --suppress=variableScope --suppress=knownConditionTrueFalse \
--suppress=unusedStructMember --suppress=uselessAssignmentArg --suppress=unreadVariable --suppress=syntaxError
ifneq ($(CPPCHECK_FAST),1)
CPPCHECK_GENERIC_OPTIONS += --check-level=exhaustive
else
CPPCHECK_SUPPRESS_OPTIONS += --suppress=normalCheckLevelMaxBranches
endif

.PHONY: cppcheck
cppcheck:
	$(info [$(YELLOW)INFO$(RESET)] Running Cppcheck analysis)
ifeq ($(CPPCHECK_ALL),1)
	@cppcheck $(CPPCHECK_GENERIC_OPTIONS) $(CPPCHECK_SUPPRESS_OPTIONS) --enable=all --inconclusive $(SRCDIR)
else
	@cppcheck $(CPPCHECK_GENERIC_OPTIONS) $(CPPCHECK_SUPPRESS_OPTIONS) --enable=warning,performance,portability $(SRCDIR)
endif

.PHONY: clean
clean:
	$(info [$(YELLOW)INFO$(RESET)] Cleaning up)
ifeq ($(HOST_POSIX),1)
	@-rm -f $(BINARY) $(SHARED)
	@-rm -r $(OBJDIR)
else
	@-rm -f $(BINARY) $(SHARED) $(NULL_STDERR) ||:
	@-rm -r $(OBJDIR) $(NULL_STDERR) ||:
	@-del $(subst /,\\, $(BINARY) $(SHARED)) $(NULL_STDERR) ||:
	@-rmdir /S /Q $(subst /,\\, $(OBJDIR)) $(NULL_STDERR) ||:
endif

# System-wide install
DESTDIR ?=
PREFIX  ?= /usr/local
# Handle all the weird GNU-style variables
prefix      ?= $(PREFIX)
exec_prefix ?= $(prefix)
bindir      ?= $(exec_prefix)/bin
libdir      ?= $(exec_prefix)/lib
includedir  ?= $(prefix)/include
datarootdir ?= $(prefix)/share
datadir     ?= $(datarootdir)

.PHONY: install
install: all lib
ifeq ($(HOST_POSIX),1)
	@echo "[$(YELLOW)INFO$(RESET)] Installing to prefix $(DESTDIR)$(prefix)"
	@install -Dm755 $(BINARY)             $(DESTDIR)$(bindir)/rvvm
	@install -Dm755 $(SHARED)             $(DESTDIR)$(libdir)/librvvm$(LIB_EXT)
	@install -Dm644 $(STATIC)             $(DESTDIR)$(libdir)/librvvm_static.a
	@install -Dm644 $(SRCDIR)/rvvmlib.h   $(DESTDIR)$(includedir)/rvvm/rvvmlib.h
	@install -Dm644 $(SRCDIR)/fdtlib.h    $(DESTDIR)$(includedir)/rvvm/fdtlib.h
	@install -Dm644 $(SRCDIR)/devices/*.h $(DESTDIR)$(includedir)/rvvm/
	@install -d                           $(DESTDIR)$(datadir)/licenses/rvvm/
	@install -Dm644 LICENSE*              $(DESTDIR)$(datadir)/licenses/rvvm/
else
	@echo "[$(RED)WARN$(RESET)] Unsupported on non-POSIX!"
endif

sinclude $(DEPS)
