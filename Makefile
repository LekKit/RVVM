# Makefile :)
NAME    := rvvm
SRCDIR  := src
VERSION := 0.5

SPACE   :=
ifneq (,$(TERM))
RESET   := $(shell tput sgr0; tput bold)
RED     := $(shell tput bold; tput setaf 1)
GREEN   := $(shell tput bold; tput setaf 2)
YELLOW  := $(shell tput bold; tput setaf 3)
BLUE    := $(shell tput bold; tput setaf 6)

$(info $(RESET))
$(info $(SPACE)  ██▀███   ██▒   █▓ ██▒   █▓ ███▄ ▄███▓)
$(info $(SPACE) ▓██ ▒ ██▒▓██░   █▒▓██░   █▒▓██▒▀█▀ ██▒)
$(info $(SPACE) ▓██ ░▄█ ▒ ▓██  █▒░ ▓██  █▒░▓██    ▓██░)
$(info $(SPACE) ▒██▀▀█▄    ▒██ █░░  ▒██ █░░▒██    ▒██ )
$(info $(SPACE) ░██▓ ▒██▒   ▒▀█░     ▒▀█░  ▒██▒   ░██▒)
$(info $(SPACE) ░ ▒▓ ░▒▓░   ░ ▐░     ░ ▐░  ░ ▒░   ░  ░)
$(info $(SPACE)   ░▒ ░ ▒░   ░ ░░     ░ ░░  ░  ░      ░)
$(info $(SPACE)   ░░   ░      ░░       ░░  ░      ░   )
$(info $(SPACE)    ░           ░        ░         ░   )
$(info $(SPACE)               ░        ░              )
$(info $(SPACE))
endif

# Detect host & target OS
ifeq ($(OS),Windows_NT)
# Passed by MinGW/Cygwin Make on Windows hosts
HOST_WINDOWS := 1
NULL_STDERR := 2>NUL
HOST_CPUS := $(NUMBER_OF_PROCESSORS)
OS := windows
$(info Detected OS: $(GREEN)Windows$(RESET))
else
# Assuming the host is POSIX
HOST_POSIX := 1
NULL_STDERR := 2>/dev/null
HOST_UNAME := $(shell uname -s)
HOST_CPUS := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

ifneq (,$(findstring mingw, $(shell $(CC) -v 2>&1)))
# Running MinGW
OS := windows
$(info Detected OS: $(GREEN)Windows$(RESET))
else
ifneq (,$(findstring Android, $(shell $(CC) -v 2>&1)))
# Running Android NDK
OS := android
$(info Detected OS: $(GREEN)Android$(RESET))
else
# Detect *nix OS by uname
ifeq ($(HOST_UNAME),Linux)
OS := linux
$(info Detected OS: $(GREEN)Linux$(RESET))
else
ifneq (,$(findstring BSD,$(HOST_UNAME)))
OS := bsd
$(info Detected OS: $(GREEN)BSD$(RESET))
else
ifeq ($(HOST_UNAME),Darwin)
OS := darwin
$(info Detected OS: $(GREEN)Darwin/MacOS$(RESET))
else
OS := unknown
$(info Detected OS: $(RED)Unknown$(RESET))
endif
endif
endif
endif
endif

endif

# Automatically parallelize build
JOBS ?= $(HOST_CPUS)
override MAKEFLAGS += -j $(JOBS) -l $(JOBS)

# Set up OS options
ifeq ($(OS),windows)
# -mwindows for GUI-only
override LDFLAGS += -static
BIN_EXT := .exe
LIB_EXT := .dll
else
# Check for lib presence before linking (there is no pthread on Android, etc)
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -lpthread 2>&1)))
override LDFLAGS += -lpthread
endif
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -lrt 2>&1)))
override LDFLAGS += -lrt
endif
LIB_EXT := .so
endif

# Detect compiler type
CC_HELP := $(shell $(CC) --help $(NULL_STDERR))
ifneq (,$(findstring clang,$(CC_HELP)))
CC_TYPE := clang
$(info Detected CC: $(GREEN)LLVM Clang$(RESET))
else
ifneq (,$(findstring gcc,$(CC_HELP)))
CC_TYPE := gcc
$(info Detected CC: $(GREEN)GCC$(RESET))
else
CC_TYPE := unknown
$(info Detected CC: $(RED)Unknown$(RESET))
endif
endif

# Detect target arch
ifndef ARCH
# Ask a crosscompiler about it's actual target
ARCH := $(firstword $(subst -, ,$(shell $(CC) $(CFLAGS) -print-multiarch $(NULL_STDERR))))
ifndef ARCH
ARCH := $(firstword $(subst -, ,$(shell $(CC) $(CFLAGS) -dumpmachine $(NULL_STDERR))))
endif
# This may fail on older compilers, fallback to host arch then
ifndef ARCH
ifeq ($(HOST_POSIX),1)
ARCH := $(shell uname -m)
else
ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
ARCH := x86_64
else
ifeq ($(PROCESSOR_ARCHITECTURE),ARM64)
ARCH := arm64
else
ARCH := i386
endif
endif
endif
$(info [$(YELLOW)INFO$(RESET)] Picked arch from uname, specify ARCH manually if cross-compiling)
endif
# x86 compilers sometimes fail to report -m32 multiarch
ifneq (,$(findstring -m32, $(CFLAGS)))
ifneq (,$(findstring x86_64, $(ARCH)))
override ARCH = i386
endif
ifneq (,$(findstring amd64, $(ARCH)))
override ARCH = i386
endif
endif
endif

$(info Target arch: $(GREEN)$(ARCH)$(RESET))

# Detect presence of libatomic
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -latomic 2>&1)))
override LDFLAGS += -latomic
else
override CFLAGS += -DNO_LIBATOMIC
endif

# Set up compilation options
ifeq ($(DEBUG),1)
BUILD_TYPE := debug
override CFLAGS += -DDEBUG -Og -ggdb
else

BUILD_TYPE := release
override CFLAGS += -DNDEBUG
ifeq ($(CC_TYPE),gcc)
override CFLAGS := -O3 -flto=auto -pthread -fvisibility=hidden $(CFLAGS)
else
ifeq ($(CC_TYPE),clang)
override CFLAGS := -O3 -flto=thin -pthread -fvisibility=hidden $(CFLAGS)
else
# Whatever compiler that might be, lets not enable aggressive optimizations
override CFLAGS := -O2 $(CFLAGS)
endif
endif

endif

# Version string
ifndef VERSION
VERSION := git
endif
GIT_OUTPUT := $(firstword $(shell git rev-parse --short=7 HEAD $(NULL_STDERR)) unknown)
VERSION := $(VERSION)-$(GIT_OUTPUT)-$(BUILD_TYPE)

$(info Version:     $(GREEN)RVVM $(VERSION)$(RESET))
$(info $(SPACE))

# Conditionally compiled sources
SRC_cond := $(SRCDIR)/devices/x11window_xcb.c $(SRCDIR)/devices/x11window_xlib.c $(SRCDIR)/devices/win32window.c \
		$(SRCDIR)/devices/tap_linux.c $(SRCDIR)/devices/tap_user.c $(SRCDIR)/networking.c

# Default build configuration
USE_RV64 ?= 1
USE_FPU ?= 1
USE_JIT ?= 1
USE_FB ?= 1
USE_XCB ?= 0
USE_XSHM ?= 1
USE_NET ?= 0
USE_TAP_LINUX ?= 0
USE_FDT ?= 1
USE_RTC ?= 1
USE_PCI ?= 1
USE_SPINLOCK_DEBUG ?= 1

ifneq (,$(findstring lib,$(MAKECMDGOALS)))
override USE_LIB = 1
else
USE_LIB ?= 0
endif

ifeq ($(USE_RV64),1)
override CFLAGS += -DUSE_RV64
endif

ifeq ($(USE_FPU),1)
# Needed for floating-point functions like fetestexcept/feraiseexcept
override LDFLAGS += -lm
override CFLAGS += -DUSE_FPU
# Disable unsafe FPU optimizations
ifeq (,$(findstring rounding-math, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -frounding-math 2>&1)))
override CFLAGS += -frounding-math
endif
# Suppress Clang schizophrenia when frounding-math is misreported as supported
ifeq ($(CC_TYPE),clang)
override CFLAGS += -Wno-ignored-optimization-argument
endif
endif

ifeq ($(USE_JIT),1)
# Check if RVJIT supports the target
ifneq (,$(findstring 86, $(ARCH)))
else
ifneq (,$(findstring amd64, $(ARCH)))
else
ifneq (,$(findstring x64, $(ARCH)))
else
ifneq (,$(findstring arm, $(ARCH)))
else
ifneq (,$(findstring aarch64, $(ARCH)))
else
ifneq (,$(findstring riscv, $(ARCH)))
else
override USE_JIT = 0
$(info [$(YELLOW)INFO$(RESET)] No RVJIT support for current target)
endif
endif
endif
endif
endif
endif
ifeq ($(USE_JIT),1)
SRC_depbuild += $(SRCDIR)/rvjit/rvjit.c $(SRCDIR)/rvjit/rvjit_emit.c
override CFLAGS += -DUSE_JIT
endif
endif

ifeq ($(USE_FB),1)
# WinAPI Window
ifeq ($(OS),windows)
SRC_depbuild += $(SRCDIR)/devices/win32window.c
override CFLAGS += -DUSE_FB
override LDFLAGS += -lgdi32
else
# XCB Window
ifeq ($(USE_XCB),1)
ifeq ($(OS),darwin)
XCB_CFLAGS := $(shell pkg-config xcb xcb-icccm --cflags $(NULL_STDERR))
XCB_LDFLAGS := $(shell pkg-config xcb xcb-icccm --libs $(NULL_STDERR) || echo -lxcb-pkg-notfound)
else
XCB_LDFLAGS := -lxcb -lxcb-icccm
endif
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(XCB_CFLAGS) $(LDFLAGS) $(XCB_LDFLAGS) 2>&1)))
SRC_depbuild += $(SRCDIR)/devices/x11window_xcb.c
override CFLAGS += -DUSE_FB -DUSE_X11 -DUSE_XCB $(XCB_CFLAGS)
override LDFLAGS += $(XCB_LDFLAGS)
else
$(info [$(RED)WARN$(RESET)] libxcb not found, ignoring USE_FB)
endif
ifeq ($(USE_XSHM),1)
ifeq ($(OS),darwin)
XCB_SHM_CFLAGS := $(shell pkg-config xcb-shm --cflags $(NULL_STDERR))
XCB_SHM_LDFLAGS := $(shell pkg-config xcb-shm --libs $(NULL_STDERR) || echo -lxcb-shm-pkg-notfound)
else
XCB_SHM_LDFLAGS := -lxcb-shm
endif
ifneq (,$(findstring main, $(shell $(CC) $(CFLAGS) $(XCB_SHM_CFLAGS) $(LDFLAGS) $(XCB_SHM_LDFLAGS) 2>&1)))
override CFLAGS += -DUSE_XSHM $(XCB_SHM_CFLAGS)
override LDFLAGS += $(XCB_SHM_LDFLAGS)
else
$(info [$(RED)WARN$(RESET)] libxcb-shm not found, ignoring USE_XSHM)
endif
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
SRC_depbuild += $(SRCDIR)/devices/x11window_xlib.c
override CFLAGS += -DUSE_FB -DUSE_X11 $(XLIB_CFLAGS)
override LDFLAGS += $(XLIB_LDFLAGS)
else
$(info [$(RED)WARN$(RESET)] libX11 not found, ignoring USE_FB)
endif
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
endif
endif
endif

ifeq ($(USE_NET),1)
override CFLAGS += -DUSE_NET
ifneq ($(OS_UNAME),Linux)
override USE_TAP_LINUX = 0
endif
ifeq ($(USE_TAP_LINUX),1)
SRC_depbuild += $(SRCDIR)/devices/tap_linux.c
override CFLAGS += -DUSE_TAP_LINUX
else
SRC_depbuild += $(SRCDIR)/devices/tap_user.c $(SRCDIR)/networking.c
ifeq ($(OS),windows)
override LDFLAGS += -lws2_32
endif
endif
endif

ifeq ($(USE_FDT),1)
override CFLAGS += -DUSE_FDT
endif

ifeq ($(USE_RTC),1)
override CFLAGS += -DUSE_RTC
endif

ifeq ($(USE_PCI),1)
override CFLAGS += -DUSE_PCI
endif

ifeq ($(USE_SPINLOCK_DEBUG),1)
override CFLAGS += -DUSE_SPINLOCK_DEBUG
endif

ifeq ($(USE_LIB),1)
override CFLAGS += -DUSE_LIB -fPIC
endif

# Generic compiler flags
override CFLAGS := -std=gnu11 -DVERSION=\"$(VERSION)\" -DARCH=\"$(ARCH)\" -Wall -Wextra -I$(SRCDIR) $(CFLAGS)

BUILDDIR := $(BUILD_TYPE).$(OS).$(ARCH)
OBJDIR := $(BUILDDIR)/obj

# Select sources to compile
SRC := $(wildcard $(SRCDIR)/*.c $(SRCDIR)/devices/*.c)
# Wipe all platform/config-dependant sources
SRC := $(filter-out $(SRC_cond),$(SRC))
# Put only needed sources to list
SRC += $(SRC_depbuild)
OBJ := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Rules to build CPU object files for different architectures
SRC_CPU   := $(wildcard $(SRCDIR)/cpu/*.c)
OBJ_CPU32 := $(SRC_CPU:$(SRCDIR)/%.c=$(OBJDIR)/%.32.o)
ifeq ($(USE_RV64),1)
OBJ_CPU64 := $(SRC_CPU:$(SRCDIR)/%.c=$(OBJDIR)/%.64.o)
endif

OBJS := $(OBJ) $(OBJ_CPU32) $(OBJ_CPU64)
LIB_OBJS := $(filter-out main.c,$(OBJS))
DEPS := $(OBJS:.o=.d)
DIRS := $(sort $(BUILDDIR) $(OBJDIR) $(dir $(OBJS)))

BINARY := $(BUILDDIR)/$(NAME)_$(ARCH)$(BIN_EXT)
SHARED := $(BUILDDIR)/lib$(NAME)$(LIB_EXT)

ifeq ($(USE_LIB),1)
TARGET := $(BINARY) $(SHARED)
else
TARGET := $(BINARY)
endif

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
ifeq (,$(findstring MMD, $(shell $(CC) $(CFLAGS) $(LDFLAGS) -MMD 2>&1)))
DO_CC = @$(CC) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -o $@ -c $<
else
$(info [$(RED)WARN$(RESET)] No compiler support for header dependencies, forcing rebuild)
override MAKEFLAGS += -B
DO_CC = @$(CC) $(CFLAGS) -o $@ -c $<
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

# Main binary
$(BINARY): $(OBJS)
	$(info [$(GREEN)LD$(RESET)] $@)
	@$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

# Library
$(SHARED): $(LIB_OBJS)
	$(info [$(GREEN)LD$(RESET)] $@)
	@$(CC) $(CFLAGS) $(LIB_OBJS) $(LDFLAGS) -shared -o $@

.PHONY: all
all: $(TARGET)

.PHONY: lib
lib: $(SHARED)

.PHONY: test
test: $(TARGET)
#@curl url -o $(BUILDDIR)/riscv-tests.tar.gz
	@tar xf $(BUILDDIR)/riscv-tests.tar.gz -C $(BUILDDIR)
	@echo
	@echo "[$(YELLOW)INFO$(RESET)] Running RISC-V Tests (RV32)"
	@echo
	@for file in $(BUILDDIR)/riscv-tests/rv32*.bin; do \
		result=$$($(TARGET) $$file -nogui | tr -d '\0'); \
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
		result=$$($(TARGET) $$file -nogui -rv64 | tr -d '\0'); \
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

sinclude $(DEPS)
