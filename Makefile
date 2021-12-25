# Makefile :)
NAME     := rvvm
SRCDIR   := src
VERSION  := 0.4

# Passed by mingw make
ifeq ($(OS),Windows_NT)
WIN_SET := 1
NULL_STDERR := 2>nul
OS := windows
$(info Detected OS: Windows)

# On windows there is a special way of detecting arch
ifndef ARCH
ifeq ($(PROCESSOR_ARCHITEW6432),AMD64)
ARCH := x86_64
else
ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
ARCH := x86_64
endif
endif

ifeq ($(PROCESSOR_ARCHITECTURE),x86)
ARCH := i386
endif
endif

endif


# Detect unix-like OS by uname
ifndef OS

NULL_STDERR := 2>/dev/null
OS_UNAME := $(shell uname -s)
ifeq ($(OS_UNAME),Linux)
OS := linux
$(info Detected OS: Linux)
else
ifneq (,$(findstring BSD,$(OS_UNAME)))
OS := bsd
$(info Detected OS: BSD)
else
ifeq ($(OS_UNAME),Darwin)
OS := darwin
$(info Detected OS: Darwin/MacOS)
else
OS := unknown
$(warning [WARN] Unknown OS)
endif
endif
endif

else
ifneq ($(WIN_SET),1)
$(info Chosen OS: $(OS))
endif

endif

# Set up OS options
ifeq ($(OS),windows)
override CFLAGS += -mwindows -static
PROGRAMEXT := .exe
else
override LDFLAGS += -lpthread
endif


# Detect compiler type
CC_HELP := $(shell $(CC) --help $(NULL_STDERR))
ifneq (,$(findstring clang,$(CC_HELP)))
CC_TYPE := clang
$(info Detected compiler: LLVM Clang)
else
ifneq (,$(findstring gcc,$(CC_HELP)))
CC_TYPE := gcc
$(info Detected compiler: GCC)
else
CC_TYPE := unknown
$(warning [WARN] Unknown compiler)
endif
endif


# Detect target arch
ifndef ARCH
ifeq ($(CC_TYPE),gcc)
ARCH := $(firstword $(subst -, ,$(shell $(CC) $(CFLAGS) -print-multiarch $(NULL_STDERR))))
else
ifeq ($(CC_TYPE),clang)
ARCH := $(firstword $(subst -, ,$(shell $(CC) $(CFLAGS) -dumpmachine $(NULL_STDERR))))
endif
endif


# This may fail on older compilers, fallback to host arch then
ifndef ARCH
ARCH := $(shell uname -m)
endif
$(info Target arch: $(ARCH))
else
endif


# Set up compilation options
ifeq ($(DEBUG),1)

BUILD_TYPE := debug
override CFLAGS += -DDEBUG -Og -ggdb

else

BUILD_TYPE := release
override CFLAGS += -DNDEBUG
ifeq ($(CC_TYPE),gcc)
override CFLAGS += -O3 -flto -pthread -frounding-math -fvisibility=hidden
else
ifeq ($(CC_TYPE),clang)
# Many clang versions lack -frounding-math, and it doesn't need it in fact
override CFLAGS += -O3 -flto -pthread -fvisibility=hidden
else
# Whatever compiler that might be, lets not enable aggressive optimizations
override CFLAGS += -O2
endif
endif

endif

# Version string
ifndef VERSION
VERSION := git
endif
GIT_OUTPUT := $(firstword $(shell git rev-parse --short=7 HEAD $(NULL_STDERR)) unknown)
VERSION := $(VERSION)-$(GIT_OUTPUT)-$(BUILD_TYPE)


$(info RVVM $(VERSION))

# Target-dependant sources
SRC_deplist := $(SRCDIR)/devices/x11window_xcb.c $(SRCDIR)/devices/x11window_xlib.c $(SRCDIR)/devices/win32window.c $(SRCDIR)/devices/rtc-goldfish.c $(SRCDIR)/devices/tap_linux.c $(SRCDIR)/devices/tap_user.c $(SRCDIR)/networking.c

# Default build configuration
USE_FB ?= 1
USE_XCB ?= 0
USE_XSHM ?= 1
USE_RV64 ?= 1
USE_JIT ?= 0
USE_NET ?= 0
USE_TAP_LINUX ?= 0
USE_FPU ?= 1
USE_FDT ?= 1
USE_RTC ?= 1
USE_SPINLOCK_DEBUG ?= 1
USE_PCI ?= 1

# Need fixes
USE_VMSWAP ?= 0
USE_VMSWAP_SPLIT ?= 0

ifeq ($(OS),linux)
override LDFLAGS += -lrt
endif
# Needed for floating-point functions like fetestexcept/feraiseexcept
ifeq ($(USE_FPU),1)
override LDFLAGS += -lm
override CFLAGS += -DUSE_FPU
endif

ifeq ($(USE_FB),1)
override CFLAGS += -DUSE_FB
ifeq ($(OS),windows)
SRC_depbuild += $(SRCDIR)/devices/win32window.c
override LDFLAGS += -lgdi32
else
ifeq ($(USE_XCB),1)
SRC_depbuild += $(SRCDIR)/devices/x11window_xcb.c
override CFLAGS += -DUSE_X11 -DUSE_XCB
ifeq ($(OS),darwin)
PKGCFG_LIST += xcb
else
override LDFLAGS += -lxcb
endif
ifeq ($(USE_XSHM),1)
override CFLAGS += -DUSE_XSHM
ifeq ($(OS),darwin)
PKGCFG_LIST += xcb-shm
else
override LDFLAGS += -lxcb-shm
endif
endif
else
SRC_depbuild += $(SRCDIR)/devices/x11window_xlib.c
override CFLAGS += -DUSE_X11
ifeq ($(OS),darwin)
PKGCFG_LIST += x11
else
override LDFLAGS += -lX11
endif
ifeq ($(USE_XSHM),1)
override CFLAGS += -DUSE_XSHM
ifeq ($(OS),darwin)
PKGCFG_LIST += xext
else
override LDFLAGS += -lXext
endif
endif
endif
endif
endif

ifeq ($(USE_RV64),1)
override CFLAGS += -DUSE_RV64

# GCC builtins need libatomic on 32-bit targets
# Clang builtins are in compiler-rt, libatomic should be omitted
ifeq ($(CC_TYPE),gcc)
ifeq (,$(findstring 64,$(ARCH)))
override LDFLAGS += -latomic
endif
endif

endif

ifeq ($(USE_JIT),1)
SRC_depbuild += $(SRCDIR)/rvjit/rvjit.c $(SRCDIR)/rvjit/rvjit_emit.c
override CFLAGS += -DUSE_JIT
endif

ifeq ($(USE_NET),1)
override CFLAGS += -DUSE_NET
ifneq ($(OS_UNAME),Linux)
USE_TAP_LINUX := 0
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

ifeq ($(USE_VMSWAP_SPLIT),1)
override CFLAGS += -DUSE_VMSWAP_SPLIT
USE_VMSWAP := 1
endif

ifeq ($(USE_VMSWAP),1)
override CFLAGS += -DUSE_VMSWAP
endif

ifeq ($(USE_FDT),1)
override CFLAGS += -DUSE_FDT
endif

ifeq ($(USE_RTC),1)
SRC_depbuild += $(SRCDIR)/devices/rtc-goldfish.c
override CFLAGS += -DUSE_RTC
endif

ifeq ($(USE_SPINLOCK_DEBUG),1)
override CFLAGS += -DUSE_SPINLOCK_DEBUG
endif

ifeq ($(USE_PCI),1)
override CFLAGS += -DUSE_PCI
endif

ifeq ($(OS),darwin)
override CFLAGS += $(shell pkg-config $(PKGCFG_LIST) --cflags)
override LDFLAGS += $(shell pkg-config $(PKGCFG_LIST) --libs)
endif

# Generic compiler flags
override CFLAGS += -std=gnu11 -DVERSION=\"$(VERSION)\" -DARCH=\"$(ARCH)\" -Wall -Wextra -I$(SRCDIR)

DO_CC = @$(CC) $(CFLAGS) -o $@ -c $<

OBJDIR := $(BUILD_TYPE).$(OS).$(ARCH)

# Select sources to compile
SRC := $(wildcard $(SRCDIR)/*.c $(SRCDIR)/devices/*.c)
# Wipe all platform/config-dependant sources
SRC := $(filter-out $(SRC_deplist),$(SRC))
# Put only needed sources to list
SRC += $(SRC_depbuild)
OBJ := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Rules to build CPU object files for different architectures
SRC_CPU   := $(wildcard $(SRCDIR)/cpu/*.c)
OBJ_CPU32 := $(SRC_CPU:$(SRCDIR)/%.c=$(OBJDIR)/%.32.o)
ifeq ($(USE_RV64),1)
OBJ_CPU64 := $(SRC_CPU:$(SRCDIR)/%.c=$(OBJDIR)/%.64.o)
endif

# Make directory if we need to.
# This is really ugly, the proper solution would be a dependency on
# an object file directory, but unfortunately we can't do that :(
MKDIR_FOR_TARGET = @mkdir -p $(@D)

# RV64 CPU
$(OBJDIR)/%.64.o: $(SRCDIR)/%.c Makefile
	$(MKDIR_FOR_TARGET)
	$(info CC $<)
	$(DO_CC) -DRV64

# RV32 CPU
$(OBJDIR)/%.32.o: $(SRCDIR)/%.c Makefile
	$(MKDIR_FOR_TARGET)
	$(info CC $<)
	$(DO_CC)

# Any normal code
$(OBJDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(MKDIR_FOR_TARGET)
	$(info CC $<)
	$(DO_CC)

DEPEND   := $(OBJDIR)/Rules.depend
TARGET   := $(OBJDIR)/$(NAME)_$(ARCH)$(PROGRAMEXT)

.PHONY: all
all: $(TARGET)

$(TARGET): $(DEPEND) $(OBJ) $(OBJ_CPU32) $(OBJ_CPU64)
	$(info LD $@)
	@$(CC) $(CFLAGS) $(OBJ) $(OBJ_CPU32) $(OBJ_CPU64) $(LDFLAGS) -o $@

.PHONY: neat
neat: $(OBJDIR)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

.PHONY: clean
clean: depend
	@-rm -f $(OBJDIR)/*.so
	@-rm -f $(OBJ)
	@-rm -f $(OBJ_CPU32)
	@-rm -f $(OBJ_CPU64)
	@-rm -f $(TARGET)
	@-rm -f $(OBJDIR)/Rules.depend
#	@-find $(OBJDIR)/ -depth -type d -exec rmdir {} +

.PHONY: depend
depend: $(DEPEND)

# If it fails, changing headers would not result in rebuilt sources.
# Currently it's not properly working for CPU sources,
# because rule "riscv_cpu.o" is not working on "riscv_cpu.64.o" target.
# Ironically, was completely broken before makefile rework
# Also may break on systems without GNU sed...
$(OBJDIR)/Rules.depend: $(SRC) | $(OBJDIR)
	@$(CC) -MM $(SRC) $(CFLAGS) $(NULL_STDERR) | sed "s;\(^[^         ]*\):\(.*\);$(OBJDIR)/\1:\2;" > $@


sinclude $(OBJDIR)/Rules.depend
