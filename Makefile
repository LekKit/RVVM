# Makefile :)
NAME    := rvvm
SRCDIR  := src
VERSION := 0.6

# Detect host features
ifeq ($(OS),Windows_NT)
# Passed by MinGW/Cygwin Make on Windows hosts
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
# Assuming the host is POSIX
HOST_POSIX := 1
NULL_STDERR := 2>/dev/null
HOST_UNAME := $(firstword $(shell uname -s 2>/dev/null) POSIX)
HOST_CPUS := $(firstword $(shell nproc 2>/dev/null) $(shell sysctl -n hw.ncpu 2>/dev/null) 1)
HOST_ARCH := $(firstword $(shell uname -m 2>/dev/null) Unknown)
endif

# Some eye-candy stuff
SPACE   :=
ifneq (,$(TERM))
BOLD    := $(shell tput md   $(NULL_STDERR) || tput bold $(NULL_STDERR) || echo -e "\033[1m" $(NULL_STDERR))
RESET   := $(shell tput me   $(NULL_STDERR) || tput sgr0 $(NULL_STDERR) || echo -e "\033[0m" $(NULL_STDERR))$(BOLD)
RED     := $(shell tput AF 1 $(NULL_STDERR) || tput setaf 1 $(NULL_STDERR) || echo -e "\033[31m" $(NULL_STDERR))$(BOLD)
GREEN   := $(shell tput AF 2 $(NULL_STDERR) || tput setaf 2 $(NULL_STDERR) || echo -e "\033[32m" $(NULL_STDERR))$(BOLD)
YELLOW  := $(shell tput AF 3 $(NULL_STDERR) || tput setaf 3 $(NULL_STDERR) || echo -e "\033[33m" $(NULL_STDERR))$(BOLD)

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
ifneq (,$(findstring cygwin, $(CC_TRIPLET)))
# Technically, Cygwin != Windows, since it defines both _WIN32 & __unix__,
# which may lead to funny API mixing, but let's ignore that for now
override OS := Windows
else
ifneq (,$(findstring windows, $(CC_TRIPLET)))
override OS := Windows
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

# Set up OS options
ifeq ($(OS),emscripten)
override CFLAGS += -pthread
override LDFLAGS += -s TOTAL_MEMORY=512MB -s PROXY_TO_PTHREAD
BIN_EXT := .html
LIB_EXT := .so
USE_SDL ?= 1
USE_NET ?= 0
else

ifeq ($(OS),windows)
# Use LDFLAG -mwindows for GUI-only
override LDFLAGS += -static
BIN_EXT := .exe
LIB_EXT := .dll
# Link to winmm / WinCE mmtimer for timeBeginPeriod()
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -lwinmm 2>&1)))
override LDFLAGS += -lwinmm
else
override LDFLAGS += -lmmtimer
endif
else

LIB_EXT := .so
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

ifeq ($(OS),darwin)
USE_SDL ?= 2
endif
ifeq ($(OS),serenityos)
USE_SDL ?= 1
endif
ifeq ($(OS),openbsd)
override CFLAGS += -I/usr/X11R6/include -D_POSIX_C_SOURCE=200809L
override LDFLAGS += -L/usr/X11R6/lib
endif

endif

ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -latomic 2>&1)))
override LDFLAGS += -latomic
else
override CFLAGS += -DNO_LIBATOMIC
endif

endif

# Detect compiler type, version
CC_HELP := $(shell $(CC) --help $(NULL_STDERR))
CC_VERSION := $(shell $(CC) -dumpversion $(NULL_STDERR))
ifneq (,$(findstring Emscripten,$(CC_HELP)))
CC_VERSION := (EMCC $(CC_VERSION))
endif
ifneq (,$(findstring clang,$(CC_HELP)))
CC_TYPE := clang
$(info Detected CC: $(GREEN)LLVM Clang $(CC_VERSION)$(RESET))
else
ifneq (,$(findstring gcc,$(CC_HELP)))
CC_TYPE := gcc
$(info Detected CC: $(GREEN)GCC $(CC_VERSION)$(RESET))
else
CC_TYPE := unknown
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
DEBUG_OPTS := -DDEBUG -Og -ggdb
else
BUILD_TYPE := release
ifeq ($(USE_DEBUG),1)
# Release with debug info
DEBUG_OPTS := -DNDEBUG -g
else
DEBUG_OPTS := -DNDEBUG
endif
endif

# Warning options (Strict safety/portability)
WARN_OPTS := -Wall -Wextra -Wshadow -Wvla -Wpointer-arith -Wframe-larger-than=32768

# Compiler-specific options
ifeq ($(CC_TYPE),gcc)
CC_STD := -std=gnu11
CXX_STD := -std=gnu++11
override CFLAGS := -O3 -flto=auto -fvisibility=hidden -fno-math-errno $(WARN_OPTS) $(DEBUG_OPTS) $(CFLAGS)
else
ifeq ($(CC_TYPE),clang)
CC_STD := -std=gnu11
CXX_STD := -std=gnu++11
override CFLAGS := -O3 -flto=thin -fvisibility=hidden -fno-math-errno $(WARN_OPTS) $(DEBUG_OPTS) $(CFLAGS)
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
STATIC := $(BUILDDIR)/lib$(NAME).a

# Select sources to compile
SRC := $(wildcard $(SRCDIR)/*.c $(SRCDIR)/devices/*.c)
# Wipe all platform/config-dependant sources
SRC := $(filter-out $(SRC_cond),$(SRC))

# Default build configuration
USE_RV64 ?= 1
USE_FPU ?= 1
USE_JIT ?= 1
USE_FB ?= 1
USE_SDL ?= 0
USE_XSHM ?= 1
USE_NET ?= 1
USE_TAP_LINUX ?= 0
USE_FDT ?= 1
USE_PCI ?= 1
USE_SPINLOCK_DEBUG ?= 1

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
override CFLAGS += -Wno-unsupported-floating-point-opt -Wno-unknown-warning-option -Wno-ignored-optimization-argument
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

ifeq ($(USE_FB),1)
# SDL Window
ifeq ($(USE_SDL),2)
ifeq ($(OS),emscripten)
SDL_CFLAGS := -DUSE_SDL=2 -s USE_SDL=2
else
SDL_LDFLAGS := -lSDL2
SDL_CFLAGS := -DUSE_SDL=2
endif
else
SDL_LDFLAGS := -lSDL
SDL_CFLAGS := -DUSE_SDL
endif

ifneq ($(USE_SDL),0)
ifneq ($(OS),emscripten)
ifeq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) $(SDL_LDFLAGS) 2>&1)))
$(info [$(RED)WARN$(RESET)] SDL not found, ignoring USE_FB)
override USE_SDL = 0
endif
endif
ifneq ($(USE_SDL),0)
SRC += $(SRCDIR)/devices/sdl_window.c
override CFLAGS += -DUSE_FB $(SDL_CFLAGS)
override LDFLAGS += $(SDL_LDFLAGS)
endif
else

# Haiku Window
ifeq ($(OS),haiku)
SRC_CXX += $(SRCDIR)/devices/haiku_window.cpp
override CFLAGS += -DUSE_FB
override LDFLAGS += -lbe
else

# WinAPI Window
ifeq ($(OS),windows)
SRC += $(SRCDIR)/devices/win32window.c
override CFLAGS += -DUSE_FB
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -lgdi32 2>&1)))
override LDFLAGS += -lgdi32
endif
else

# Xlib Window
ifeq ($(OS),darwin)
XLIB_CFLAGS := $(shell pkg-config x11 --cflags $(NULL_STDERR))
XLIB_LDFLAGS := $(shell pkg-config x11 --libs $(NULL_STDERR) || echo -lx11-pkg-notfound)
else
XLIB_LDFLAGS := -lX11
endif
# Detect presence of libX11
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(XLIB_CFLAGS) $(LDFLAGS) $(XLIB_LDFLAGS) 2>&1)))
SRC += $(SRCDIR)/devices/x11window_xlib.c
override CFLAGS += -DUSE_FB -DUSE_X11 $(XLIB_CFLAGS)
override LDFLAGS += $(XLIB_LDFLAGS)
ifeq ($(USE_XSHM),1)
ifeq ($(OS),darwin)
XEXT_CFLAGS := $(shell pkg-config xext --cflags $(NULL_STDERR))
XEXT_LDFLAGS := $(shell pkg-config xext --libs $(NULL_STDERR) || echo -lxext-pkg-notfound)
else
XEXT_LDFLAGS := -lXext
endif
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(XEXT_CFLAGS) $(LDFLAGS) $(XEXT_LDFLAGS) 2>&1)))
override CFLAGS += -DUSE_XSHM $(XEXT_CFLAGS)
override LDFLAGS += $(XEXT_LDFLAGS)
else
$(info [$(RED)WARN$(RESET)] libXext not found, ignoring USE_XSHM)
endif
endif
else
$(info [$(RED)WARN$(RESET)] libX11 not found, ignoring USE_FB)
endif
endif
endif
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

ifeq ($(USE_SPINLOCK_DEBUG),1)
override CFLAGS += -DUSE_SPINLOCK_DEBUG
endif

# Rules for object files from sources
OBJ := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
OBJ_CXX := $(SRC_CXX:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

# Rules to build CPU object files for RV32/RV64
SRC_CPU   := $(wildcard $(SRCDIR)/cpu/*.c)
OBJ_CPU32 := $(SRC_CPU:$(SRCDIR)/%.c=$(OBJDIR)/%.32.o)
ifeq ($(USE_RV64),1)
OBJ_CPU64 := $(SRC_CPU:$(SRCDIR)/%.c=$(OBJDIR)/%.64.o)
endif

# Combine the object files
OBJS := $(OBJ) $(OBJ_CXX) $(OBJ_CPU32) $(OBJ_CPU64)
LIB_OBJS := $(filter-out main.o,$(OBJS))
DEPS := $(OBJS:.o=.d)
DIRS := $(sort $(BUILDDIR) $(OBJDIR) $(dir $(OBJS)))

# Create directories for object files
ifeq ($(HOST_POSIX),1)
$(shell mkdir -p $(DIRS))
else
$(shell mkdir $(subst /,\\, $(DIRS)) $(NULL_STDERR))
endif

# Do not pass lib-related flags for dev builds (Faster)
ifneq (,$(MAKECMDGOALS))
override CFLAGS += -DUSE_LIB -fPIC -ffat-lto-objects
endif

# Check previous buildflags
CFLAGS_TXT := $(OBJDIR)/cflags.txt
LDFLAGS_TXT := $(OBJDIR)/ldflags.txt
CURR_CFLAGS := $(CC) $(CFLAGS)
CURR_LDFLAGS := $(LD) $(LDFLAGS)
sinclude $(CFLAGS_TXT) $(LDFLAGS_TXT)
ifneq ($(CURR_CFLAGS), $(PREV_CFLAGS))
ifneq ($(PREV_CFLAGS),)
$(info [$(YELLOW)INFO$(RESET)] CFLAGS changed, doing a full rebuild)
endif
override MAKEFLAGS += -B
else
ifneq ($(CURR_LDFLAGS), $(PREV_LDFLAGS))
$(info [$(YELLOW)INFO$(RESET)] LDFLAGS changed, relinking binaries)
$(shell rm -f $(TARGET))
endif
endif
ifeq (3.82,$(firstword $(sort $(MAKE_VERSION) 3.82)))
$(file >$(CFLAGS_TXT),PREV_CFLAGS = $(CC) $(CFLAGS))
$(file >$(LDFLAGS_TXT),PREV_LDFLAGS = $(LD) $(LDFLAGS))
else
$(shell echo PREV_CFLAGS = $(subst \,\\\, $(CC) $(CFLAGS)) > $(CFLAGS_TXT))
$(shell echo PREV_LDFLAGS = $(subst \,\\\, $(LD) $(LDFLAGS)) > $(LDFLAGS_TXT))
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

# RV64 CPU
$(OBJDIR)/%.64.o: $(SRCDIR)/%.c Makefile
	$(info [$(YELLOW)CC$(RESET)] $<)
	$(DO_CC) -DRV64

# RV32 CPU
$(OBJDIR)/%.32.o: $(SRCDIR)/%.c Makefile
	$(info [$(YELLOW)CC$(RESET)] $<)
	$(DO_CC)

# Any normal code
$(OBJDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(info [$(YELLOW)CC$(RESET)] $<)
	$(DO_CC)

# Any C++ code
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
#@curl url -o $(BUILDDIR)/riscv-tests.tar.gz
	@tar xf $(BUILDDIR)/riscv-tests.tar.gz -C $(BUILDDIR)
	@echo
	@echo "[$(YELLOW)INFO$(RESET)] Running RISC-V Tests (RV32)"
	@echo
	@for file in $(BUILDDIR)/riscv-tests/rv32*.bin; do \
		result=$$($(BINARY) $$file -nogui -rv32 | tr -d '\0'); \
		result="$${result##* }"; \
		if [[ "$$result" == "0" ]]; then \
		echo "[$(GREEN)PASS$(RESET)] $$file"; \
		else \
		echo "[$(RED)FAIL: $$result$(RESET)] $$file"; \
		fi; \
	done
ifeq ($(USE_RV64),1)
	@echo
	@echo "[$(YELLOW)INFO$(RESET)] Running RISC-V Tests (RV64)"
	@echo
	@for file in $(BUILDDIR)/riscv-tests/rv64*.bin; do \
		result=$$($(BINARY) $$file -nogui -rv64 | tr -d '\0'); \
		result="$${result##* }"; \
		if [[ "$$result" == "0" ]]; then \
		echo "[$(GREEN)PASS$(RESET)] $$file"; \
		else \
		echo "[$(RED)FAIL: $$result$(RESET)] $$file"; \
		fi; \
	done
endif

.PHONY: cppcheck
cppcheck:
ifeq ($(CHECK_ALL),1)
	@cppcheck -f -j$(JOBS) --enable=all --language=c --std=c11 $(SRCDIR)
else
	@cppcheck -f -j$(JOBS) --enable=warning,performance,portability --language=c --std=c11 $(SRCDIR)
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
	@install -Dm644 $(STATIC)             $(DESTDIR)$(libdir)/librvvm.a
	@install -Dm644 $(SRCDIR)/rvvmlib.h   $(DESTDIR)$(includedir)/rvvm/rvvmlib.h
	@install -Dm644 $(SRCDIR)/fdtlib.h    $(DESTDIR)$(includedir)/rvvm/fdtlib.h
	@install -Dm644 $(SRCDIR)/devices/*.h $(DESTDIR)$(includedir)/rvvm/
	@install -d                           $(DESTDIR)$(datadir)/licenses/rvvm/
	@install -Dm644 LICENSE*              $(DESTDIR)$(datadir)/licenses/rvvm/
else
	@echo "[$(RED)WARN$(RESET)] Unsupported on non-POSIX!"
endif

sinclude $(DEPS)
