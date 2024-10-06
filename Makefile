# Makefile :)
override NAME    := rvvm
override SRCDIR  := src
override VERSION := 0.7

#
# Determine build host features
#

ifdef WINDIR
# Set by a Windows host, rule out Cygwin via uname
override HOST_UNAME := $(firstword $(shell uname -o 2>/dev/null) Windows)
ifeq ($(OS),Windows_NT)
# Clean up garbage OS env passed on Windows by default
override OS :=
endif
else
# Assume a POSIX host
override HOST_UNAME := $(firstword $(shell uname -s 2>/dev/null) POSIX)
endif

ifeq ($(HOST_UNAME),Windows)
override HOST_WINDOWS := 1
override NULL_STDERR := 2>nul
override HOST_CPUS := $(firstword $(NUMBER_OF_PROCESSORS) 1)
ifeq ($(PROCESSOR_ARCHITECTURE),AMD64)
override HOST_ARCH := x86_64
else
ifeq ($(PROCESSOR_ARCHITECTURE),ARM64)
override HOST_ARCH := arm64
else
override HOST_ARCH := i386
endif
endif

else
override HOST_POSIX := 1
override NULL_STDERR := 2>/dev/null
override HOST_CPUS := $(shell nproc $(NULL_STDERR) || sysctl -n hw.ncpu $(NULL_STDERR) || echo 1)
override HOST_ARCH := $(firstword $(shell uname -m 2>/dev/null) Unknown)
endif

#
# Some eye-candy stuff
#

override SPACE :=
override RESET   := $(shell tput me   $(NULL_STDERR) || tput sgr0 $(NULL_STDERR)    || printf "\\e[0m" $(NULL_STDERR))
override BOLD    := $(shell tput md   $(NULL_STDERR) || tput bold $(NULL_STDERR)    || printf "\\e[1m" $(NULL_STDERR))
override RED     := $(shell tput AF 1 $(NULL_STDERR) || tput setaf 1 $(NULL_STDERR) || printf "\\e[31m" $(NULL_STDERR))$(BOLD)
override GREEN   := $(shell tput AF 2 $(NULL_STDERR) || tput setaf 2 $(NULL_STDERR) || printf "\\e[32m" $(NULL_STDERR))$(BOLD)
override YELLOW  := $(shell tput AF 3 $(NULL_STDERR) || tput setaf 3 $(NULL_STDERR) || printf "\\e[33m" $(NULL_STDERR))$(BOLD)
override WHITE   := $(RESET)$(BOLD)

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

# Message prefixes
override INFO_PREFIX := $(WHITE)[$(YELLOW)INFO$(WHITE)]
override WARN_PREFIX := $(WHITE)[$(RED)WARN$(WHITE)]

# Automatically parallelize build
JOBS ?= $(HOST_CPUS)
override MAKEFLAGS += -j $(JOBS)

#
# Determine build target features for cross-compilation
#

# Get compiler target triplet (arch-vendor-kernel-abi)
override CC_TRIPLET := $(firstword $(shell $(CC) $(CFLAGS) -print-multiarch $(NULL_STDERR))$(shell $(CC) $(CFLAGS) -dumpmachine $(NULL_STDERR)))
ifeq (,$(findstring -,$(CC_TRIPLET)))
override CC_TRIPLET :=
endif

# Try to detect target OS via target triplet
ifneq (,$(findstring android,$(CC_TRIPLET)))
override OS := Android
endif
ifneq (,$(findstring linux,$(CC_TRIPLET)))
override OS := Linux
endif
ifneq (,$(findstring mingw,$(CC_TRIPLET))$(findstring windows,$(CC_TRIPLET)))
override OS := Windows
endif
ifneq (,$(findstring darwin,$(CC_TRIPLET))$(findstring macos,$(CC_TRIPLET)))
override OS := Darwin
endif
ifneq (,$(findstring emscripten,$(CC_TRIPLET)))
override OS := Emscripten
endif

# Assume target OS matches host if triplet didn't match any known cross toolchains
ifndef OS
override OS := $(HOST_UNAME)
ifneq ($(CC),cc)
$(info $(INFO_PREFIX) Assuming target OS=$(OS), set explicitly if cross-compiling$(RESET))
endif
endif

override tolower = $(subst A,a,$(subst B,b,$(subst C,c,$(subst D,d,$(subst E,e,$(subst F,f,$(subst G,g,$(subst H,h,$(subst I,i,$(subst J,j,$(subst K,k,$(subst L,l,$(subst M,m,$(subst N,n,$(subst O,o,$(subst P,p,$(subst Q,q,$(subst R,r,$(subst S,s,$(subst T,t,$(subst U,u,$(subst V,v,$(subst W,w,$(subst X,x,$(subst Y,y,$(subst Z,z,$1))))))))))))))))))))))))))

override OS_PRETTY := $(OS)
override OS := $(call tolower,$(OS))

# Detect target arch
ifndef ARCH
ifneq (,$(findstring -,$(CC_TRIPLET)))
# Get target arch from target triplet
override ARCH := $(firstword $(subst -, ,$(CC_TRIPLET)))
else
# This may fail on older compilers, fallback to host arch then
override ARCH := $(HOST_ARCH)
ifneq ($(CC),cc)
$(info $(INFO_PREFIX) Assuming target ARCH=$(ARCH), set explicitly if cross-compiling$(RESET))
endif
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
override ARCH = i686
endif
endif

#
# Set OS-specific build options
#

# Windows-specific build options
ifeq ($(OS),windows)
override LDFLAGS += -static
override BIN_EXT := .exe
override LIB_EXT := .dll
else

# Emscripten-specific build options
ifeq ($(OS),emscripten)
override CFLAGS += -pthread
override LDFLAGS += -s TOTAL_MEMORY=512MB -s PROXY_TO_PTHREAD
override BIN_EXT := .html
override LIB_EXT := .so
USE_SDL ?= 1
USE_NET ?= 0
else

# POSIX build options
override BIN_EXT :=
ifeq ($(OS),darwin)
override LIB_EXT := .dylib
else
override LIB_EXT := .so
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

ifneq (,$(findstring linux,$(OS))$(findstring bsd,$(OS))$(findstring sunos,$(OS)))
# Enable X11 on Linux, *BSD, SunOS (Solaris) by default
USE_X11 ?= 1
endif

ifneq (,$(findstring darwin,$(OS))$(findstring serenity,$(OS)))
# Enable SDL2 on Darwin, Serenity by default
USE_SDL ?= 2
endif

ifneq (,$(findstring redox,$(OS)))
# Enable SDL1 and disable networking on Redox by default
USE_SDL ?= 1
USE_NET ?= 0
endif

ifeq ($(OS),openbsd)
override CFLAGS += -I/usr/X11R6/include -D_POSIX_C_SOURCE=200809L
override LDFLAGS += -L/usr/X11R6/lib
endif

endif
endif

#
# Default build configuration
#

# Debugging options
USE_DEBUG ?= 0
USE_DEBUG_FULL ?= 0

override BUILD_TYPE := release
ifeq ($(USE_DEBUG_FULL),1)
# Full debug with much less optimizations
override BUILD_TYPE := debug
endif

# Build output directory
BUILDDIR ?= $(BUILD_TYPE).$(OS).$(ARCH)

# Executable file name
BINARY ?= $(NAME)_$(ARCH)$(BIN_EXT)

# CPU features
USE_RV32 ?= 1
USE_RV64 ?= 1
USE_FPU ?= 1

# Infrastructure
USE_SPINLOCK_DEBUG ?= 1
USE_LIB ?= 0
USE_JNI ?= 1
USE_ISOLATION ?= 1

# Acceleration/accessibility
USE_JIT ?= 1
USE_GUI ?= 1
USE_SDL ?= 0
USE_NET ?= 1

# Devices
USE_FDT ?= 1
USE_PCI ?= 1
USE_VFIO ?= 1

# Determine build commit id
GIT_COMMIT ?= $(firstword $(shell git describe --match=NeVeRmAtCh_TaG --always --dirty $(NULL_STDERR)))
ifneq (,$(GIT_COMMIT))
override VERSION := $(VERSION)-$(GIT_COMMIT)
endif

#
# Set up sources, useflags, CFLAGS & LDFLAGS
#

# Generic compiler flags
override CFLAGS := -I$(SRCDIR) -DRVVM_VERSION=\"$(VERSION)\" $(CFLAGS)

# Select sources to compile
override SRC := $(wildcard $(SRCDIR)/*.c $(SRCDIR)/devices/*.c)

# OS-specific useflag CFLAGS/LDFLAGS
ifeq ($(OS),windows)
USE_WIN32_GUI ?= 1
ifneq (,$(findstring main, $(subst WinMain,main,$(shell $(CC) $(CFLAGS) $(LDFLAGS) -lgdi32 2>&1))))
# On WinCE it's not expected to link gdi32
override LDFLAGS_USE_WIN32_GUI := -lgdi32
endif
ifneq (,$(findstring main, $(subst WinMain,main,$(shell $(CC) $(CFLAGS) $(LDFLAGS) -lws2 2>&1))))
# On WinCE there is no _32 suffix
override LDFLAGS_USE_NET := -lws2
else
override LDFLAGS_USE_NET := -lws2_32
endif
endif

ifeq ($(OS),haiku)
USE_HAIKU_GUI ?= 1
override LDFLAGS_USE_HAIKU_GUI := -lbe
override LDFLAGS_USE_NET := -lnetwork
endif

ifeq ($(OS),sunos)
override LDFLAGS_USE_NET := -lsocket
endif

ifeq ($(OS),emscripten)
override CFLAGS_USE_SDL := -s USE_SDL=$(USE_SDL)
endif

# Useflag sources
override SRC_USE_WIN32_GUI := $(SRCDIR)/devices/win32window.c
override SRC_CXX_USE_HAIKU_GUI := $(SRCDIR)/devices/haiku_window.cpp
override SRC_USE_X11 := $(SRCDIR)/devices/x11window_xlib.c
override SRC_USE_SDL := $(SRCDIR)/devices/sdl_window.c

override SRC_USE_TAP_LINUX := $(SRCDIR)/devices/tap_linux.c
override SRC_USE_NET := $(SRCDIR)/networking.c $(SRCDIR)/devices/tap_user.c
override SRC_USE_JIT := $(SRCDIR)/rvjit/rvjit.c $(SRCDIR)/rvjit/rvjit_emit.c
override SRC_USE_JNI := $(SRCDIR)/bindings/jni/rvvm_jni.c
override SRC_USE_RV64 := $(wildcard $(SRCDIR)/cpu/riscv64_*.c)
override SRC_USE_RV32 := $(wildcard $(SRCDIR)/cpu/riscv_*.c)

# Useflag CFLAGS
override CFLAGS_USE_DEBUG := -DDEBUG -g -fno-omit-frame-pointer
override CFLAGS_USE_DEBUG_FULL := -DDEBUG -Og -ggdb -fno-omit-frame-pointer
override CFLAGS_USE_LIB := -fPIC

# Useflag LDFLAGS
# Needed for floating-point functions like fetestexcept/feraiseexcept
override LDFLAGS_USE_FPU := -lm

# Useflag dependencies
override NEED_USE_X11 := USE_GUI
override NEED_USE_SDL := USE_GUI
override NEED_USE_JNI := USE_LIB

# Check if RVJIT supports the target architecture
ifeq ($(USE_JIT),1)
ifeq (,$(findstring 86,$(ARCH))$(findstring arm,$(ARCH))$(findstring riscv,$(ARCH)))
override USE_JIT = 0
$(info $(INFO_PREFIX) No RVJIT support for current target$(RESET))
endif
endif

ifeq ($(USE_TAP_LINUX),1)
$(info $(WARN_PREFIX) Linux TAP is deprecated in favor of USE_NET due to checksum issues)
endif

# Enable building the lib on lib or install target
ifneq (,$(findstring lib, $(MAKECMDGOALS))$(findstring install, $(MAKECMDGOALS)))
override USE_LIB := 1
endif

override USEFLAGS := $(sort $(filter USE_%,$(.VARIABLES)))
override SRC_CONDITIONAL := $(filter SRC_USE_%,$(.VARIABLES))

# Filter out all conditionally compiled C/C++ sources
override SRC := $(filter-out $(foreach cond_src,$(SRC_CONDITIONAL),$($(cond_src))),$(SRC))
override SRC_CXX := $(filter-out $(foreach cond_src,$(SRC_CONDITIONAL),$($(cond_src))),$(SRC_CXX))

# Disable all useflags which depend on another disabled useflags
override _ := $(foreach useflag,$(USEFLAGS),$(foreach need_useflag,$(NEED_$(useflag)),$(if $(filter 0,$($(need_useflag))),$(eval override $(useflag) := 0))))

# Include actually enabled C/C++ sources
override SRC += $(sort $(foreach useflag,$(USEFLAGS),$(if $(filter-out 0,$($(useflag))),$(SRC_$(useflag)))))
override SRC_CXX += $(strip $(foreach useflag,$(USEFLAGS),$(if $(filter-out 0,$($(useflag))),$(SRC_CXX_$(useflag)))))

# Set useflags CFLAGS
override CFLAGS += $(strip $(foreach useflag,$(USEFLAGS),$(if $(filter-out 0,$($(useflag))),$(CFLAGS_$(useflag)))))

# Set useflags LDFLAGS
override LDFLAGS += $(strip $(foreach useflag,$(USEFLAGS),$(if $(filter-out 0,$($(useflag))),$(LDFLAGS_$(useflag)))))

# Set useflags C definitions
override CFLAGS += $(strip $(foreach useflag, $(USEFLAGS),$(if $(filter-out 0,$($(useflag))),-D$(useflag)=$($(useflag)))))

# Output directories / files
override OBJDIR := $(BUILDDIR)/obj
override BINARY := $(BUILDDIR)/$(BINARY)
override SHARED := $(BUILDDIR)/lib$(NAME)$(LIB_EXT)
override STATIC := $(BUILDDIR)/lib$(NAME)_static.a

# Combine the object files
override OBJS := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o) $(SRC_CXX:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)
override LIB_OBJS := $(filter-out main.o,$(OBJS))
override DEPS := $(OBJS:.o=.d)
override DIRS := $(sort $(BUILDDIR) $(OBJDIR) $(dir $(OBJS)))

# Create directories for object files
ifeq ($(HOST_POSIX),1)
override _ := $(shell mkdir -p $(DIRS))
else
override _ := $(foreach directory,$(DIRS),$(shell mkdir $(subst /,\\,$(directory)) $(NULL_STDERR))$(shell mkdir $(directory) $(NULL_STDERR)))
endif

#
# Detect compiler brand & features, set up optimization/warning options
#

override CC_INFO := $(shell $(CC) -v 2>&1)
override CC_INFO_TMP := $(CC_INFO)
override CC_FULL_VERSION := $(strip $(foreach cc_word,$(CC_INFO),$(if $(filter version,$(word 2,$(CC_INFO_TMP))),$(wordlist 1,3,$(CC_INFO_TMP)))\
$(eval override CC_INFO_TMP := $(wordlist 2,$(words $(CC_INFO_TMP)),$(CC_INFO_TMP)))))
override CC_BRAND := $(firstword $(CC_FULL_VERSION))
override CC_VERSION := $(word 3,$(CC_FULL_VERSION))
ifeq (,$(findstring .,$(CC_VERSION)))
override CC_VERSION := $(shell $(CC) -dumpfullversion -dumpversion $(NULL_STDERR))
endif
ifeq ($(ARCH),e2k)
# It's not a real GCC, workaround issues by explicitly marking it as different compiler brand
override CC_BRAND := ПТН ПНХ
endif
ifeq (,$(CC_BRAND))
override CC_BRAND := Unknown
endif

# Compiler version checks
override CC_AT_LEAST_2_0 := $(filter-out 1.%,$(CC_VERSION))
override CC_AT_LEAST_3_0 := $(filter-out 2.%,$(CC_AT_LEAST_2_0))
override CC_AT_LEAST_4_0 := $(filter-out 3.%,$(CC_AT_LEAST_3_0))
override CC_AT_LEAST_5_0 := $(filter-out 4.%,$(CC_AT_LEAST_4_0))
override CC_AT_LEAST_6_0 := $(filter-out 5.%,$(CC_AT_LEAST_5_0))
override CC_AT_LEAST_7_0 := $(filter-out 6.%,$(CC_AT_LEAST_6_0))

# Check LTO support
override LTO_CHECK_OUT := $(OBJDIR)/lto_lest$(BIN_EXT)
override LTO_SUPPORTED := $(wildcard $(LTO_CHECK_OUT))
ifeq (,$(LTO_SUPPORTED))
override LTO_ERROR := $(shell echo "int main(){return 0;}" | $(CC) -flto -xc -o $(LTO_CHECK_OUT) - 2>&1)
ifeq (,$(findstring lto,$(LTO_ERROR))$(findstring LTO,$(LTO_ERROR)))
override LTO_SUPPORTED := 1
else
$(info $(INFO_PREFIX) LTO is not supported by this toolchain: $(wordlist 2,8,$(LTO_ERROR))$(RESET))
endif
endif

override CC_STD := -std=c99
override CXX_STD :=

# Warning options (Strict safety/portability, stack/object size limits)
# Need at least GCC 7.0 or Clang 7.0
# -Wbad-function-cast, -Wcast-align, need fixes in codebase
ifneq (,$(CC_AT_LEAST_7_0))
override WARN_OPTS := -Wall -Wextra -Wshadow -Wvla -Wpointer-arith -Walloca -Wduplicated-cond \
-Wtrampolines -Wlarger-than=1048576 -Wframe-larger-than=32768 -Wdouble-promotion -Werror=return-type
else
# Conservative warning options for older compilers
override WARN_OPTS := -Wall -Wextra
endif

# Set up optimization options based on the compiler brand
ifneq (,$(findstring clang,$(CC_INFO))$(findstring LLVM,$(CC_INFO)))
# LLVM Clang or derivatives (Zig CC, Emscripten)
override CC_PRETTY := LLVM Clang $(CC_VERSION)

override CC_STD := -std=gnu99
ifneq (,$(CC_AT_LEAST_4_0))
override CC_STD := -std=gnu11 -Wstrict-prototypes -Wold-style-definition
override CXX_STD := -std=gnu++11
endif

override CFLAGS := -O2 $(if $(LTO_SUPPORTED),-flto=thin) $(if $(CC_AT_LEAST_4_0),-frounding-math) -fvisibility=hidden -fno-math-errno \
$(WARN_OPTS) -Wno-unknown-warning-option -Wno-unsupported-floating-point-opt -Wno-ignored-optimization-argument \
-Wno-missing-braces -Wno-missing-field-initializers -Wno-ignored-pragmas -Wno-atomic-alignment $(CFLAGS)

else
ifeq ($(CC_BRAND),gcc)
# GNU GCC or derivatives (MinGW)
override CC_PRETTY := GCC $(CC_VERSION)

override CC_STD := -std=gnu99
ifneq (,$(CC_AT_LEAST_5_0))
override CC_STD := -std=gnu11 -Wstrict-prototypes -Wold-style-declaration -Wold-style-definition
override CXX_STD := -std=gnu++11
endif

override CFLAGS := -O2 $(if $(LTO_SUPPORTED),-flto=auto) -frounding-math $(if $(CC_AT_LEAST_4_0),-fvisibility=hidden -fno-math-errno) $(if $(CC_AT_LEAST_6_0),-fno-plt) \
$(WARN_OPTS) -Wno-missing-braces $(if $(CC_AT_LEAST_4_0),-Wno-missing-field-initializers) $(CFLAGS)

else
# Toy compiler (TCC, Chibicc, Cproc)
override CC_PRETTY := $(RED)$(CC_BRAND) $(CC_VERSION)

endif
endif

#
# Check previous build flags, force a rebuild if necessary
#

override CFLAGS_TXT := $(OBJDIR)/cflags.txt
override LDFLAGS_TXT := $(OBJDIR)/ldflags.txt
override CURR_CFLAGS := $(CC) $(CC_VERSION) $(CFLAGS)
override CURR_LDFLAGS := $(LD) $(CC_VERSION) $(LDFLAGS)
sinclude $(CFLAGS_TXT) $(LDFLAGS_TXT)

ifneq ($(CURR_CFLAGS),$(PREV_CFLAGS))
ifneq (,$(PREV_CFLAGS))
$(info $(INFO_PREFIX) CFLAGS changed, doing a full rebuild$(RESET))
endif
override MAKEFLAGS += -B
else
ifneq ($(CURR_LDFLAGS),$(PREV_LDFLAGS))
$(info $(INFO_PREFIX) LDFLAGS changed, relinking binaries$(RESET))
override _ := $(shell rm $(BINARY) $(SHARED) $(NULL_STDERR))
endif
endif

ifneq (,$(filter-out 3.%,$(MAKE_VERSION)))
override _ := $(file >$(CFLAGS_TXT),PREV_CFLAGS := $(CURR_CFLAGS))
override _ := $(file >$(LDFLAGS_TXT),PREV_LDFLAGS := $(CURR_LDFLAGS))
else
override _ := $(shell echo "PREV_CFLAGS := $(subst ",\\",$(CURR_CFLAGS))" > $(CFLAGS_TXT))
override _ := $(shell echo "PREV_LDFLAGS := $(subst ",\\",$(CURR_LDFLAGS))" > $(LDFLAGS_TXT))
endif

#
# Compiler invocation helpers
#

override DO_CC = $(CC) $(CC_STD) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -o $@ -c $<
override DO_CXX = $(CXX) $(CXX_STD) $(CFLAGS) -MMD -MF $(patsubst %.o, %.d, $@) -o $@ -c $<

# Link using CC or CXX if any C++ code is present
override CC_LD := $(CC)
ifneq (,$(strip $(SRC_CXX)))
override CC_LD := $(CXX)
endif

#
# Print build information
#

$(info $(WHITE)Detected OS: $(GREEN)$(OS_PRETTY)$(RESET))
$(info $(WHITE)Detected CC: $(GREEN)$(CC_PRETTY)$(RESET))
$(info $(WHITE)Target arch: $(GREEN)$(ARCH)$(RESET))
$(info $(WHITE)Version:     $(GREEN)RVVM $(VERSION)$(RESET))
$(info $(SPACE))

# Ignore deleted header files
%.h:
	@:

# C object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(info $(WHITE)[$(YELLOW)CC$(WHITE)] $< $(RESET))
	@$(DO_CC)

# C++ object files
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp Makefile
	$(info $(WHITE)[$(YELLOW)CXX$(WHITE)] $< $(RESET))
	@$(DO_CXX)

# Main binary
$(BINARY): $(OBJS)
	$(info $(WHITE)[$(GREEN)LD$(WHITE)] $@ $(RESET))
	@$(CC_LD) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

# Shared library
$(SHARED): $(LIB_OBJS)
	$(info $(WHITE)[$(GREEN)LD$(WHITE)] $@ $(RESET))
	@$(CC_LD) $(CFLAGS) $(LIB_OBJS) $(LDFLAGS) -shared -o $@

# Static library
$(STATIC): $(LIB_OBJS)
	$(info $(WHITE)[$(GREEN)AR$(WHITE)] $@ $(RESET))
	@$(AR) -rcs $@ $(LIB_OBJS)

.PHONY: all         # Build the main executable
all: $(BINARY)

.PHONY: lib         # Build shared & static libraries
lib: $(SHARED) $(STATIC)

.PHONY: codesign
codesign: all lib
ifeq ($(OS),darwin)
	@for file in "$(BINARY)" "$(SHARED)"; do \
		if codesign -s - --force --options=runtime --entitlements rvvm_debug.entitlements "$$file"; then \
		echo "$(WHITE)[$(YELLOW)CODESIGN$(WHITE)] $$file$(RESET)"; \
		else \
		echo "$(WHITE)[$(RED)FAIL CODESIGN$(WHITE)] $$file$(RESET)"; \
		exit -1; \
		fi; \
	done
	@echo "$(WHITE)[$(GREEN)CODESIGN$(WHITE)] Codesign complete!$(RESET)"; 
else
	@echo "$(WHITE)[$(RED)FAIL CODESIGN$(WHITE)] Codesign is not supported on this system!$(RESET)"
endif

.PHONY: codesign_isolement
codesign_isolement: all lib
ifeq ($(OS),darwin)
	@for file in "$(BINARY)" "$(SHARED)" "$(STATIC)"; do \
		if codesign -s - --force --options=runtime --entitlements rvvm_debug.entitlements "$$file"; then \
		echo "$(WHITE)[$(YELLOW)CODESIGN$(WHITE)] $$file$(RESET)"; \
		else \
		echo "$(WHITE)[$(RED)FAIL CODESIGN$(WHITE)] $$file$(RESET)"; \
		exit -1; \
		fi; \
	done
	@echo "$(WHITE)[$(GREEN)CODESIGN$(WHITE)] Codesign complete!$(RESET)"; 
else
	@echo "$(WHITE)[$(RED)FAIL CODESIGN$(WHITE)] Codesign is not supported on this system!$(RESET)"
endif

.PHONY: test        # Run RISC-V tests
test: all
	$(if $(wildcard $(BUILDDIR)/riscv-tests.tar.gz),,@cd "$(BUILDDIR)"; curl -LO "https://github.com/LekKit/riscv-tests/releases/download/rvvm-tests/riscv-tests.tar.gz")
	@tar xf "$(BUILDDIR)/riscv-tests.tar.gz" -C $(BUILDDIR)
	@echo
	@echo "$(INFO_PREFIX) Running RISC-V Tests (RV32)$(RESET)"
	@echo
	@for file in "$(BUILDDIR)/riscv-tests/rv32"*; do \
		result=$$($(BINARY) $$file -nogui -rv32 | tr -d '\0'); \
		result="$${result##* }"; \
		if [ "$$result" -eq "0" ]; then \
		echo "$(WHITE)[$(GREEN)PASS$(WHITE)] $$file$(RESET)"; \
		else \
		echo "$(WHITE)[$(RED)FAIL: $$result$(WHITE)] $$file$(RESET)"; \
		exit -1; \
		fi; \
	done
ifeq ($(USE_RV64),1)
	@echo
	@echo "$(INFO_PREFIX) Running RISC-V Tests (RV64)$(RESET)"
	@echo
	@for file in "$(BUILDDIR)/riscv-tests/rv64"*; do \
		result=$$($(BINARY) $$file -nogui -rv64 | tr -d '\0'); \
		result="$${result##* }"; \
		if [ "$$result" -eq "0" ]; then \
		echo "$(WHITE)[$(GREEN)PASS$(WHITE)] $$file$(RESET)"; \
		else \
		echo "$(WHITE)[$(RED)FAIL: $$result$(WHITE)] $$file$(RESET)"; \
		exit -1; \
		fi; \
	done
endif

override CPPCHECK_GENERIC_OPTIONS := -f -j$(JOBS) --inline-suppr --std=c99 -q -I $(SRCDIR)
override CPPCHECK_SUPPRESS_OPTIONS :=  --suppress=unmatchedSuppression --suppress=missingIncludeSystem \
--suppress=constParameterPointer --suppress=constVariablePointer --suppress=constParameterCallback \
--suppress=constVariable --suppress=variableScope --suppress=knownConditionTrueFalse \
--suppress=unusedStructMember --suppress=uselessAssignmentArg --suppress=unreadVariable --suppress=syntaxError
ifneq ($(CPPCHECK_FAST),1)
override CPPCHECK_GENERIC_OPTIONS += --check-level=exhaustive
else
override CPPCHECK_SUPPRESS_OPTIONS += --suppress=normalCheckLevelMaxBranches
endif

.PHONY: cppcheck    # Run cppcheck static analysis
cppcheck:
	$(info $(INFO_PREFIX) Running Cppcheck analysis$(RESET))
ifeq ($(CPPCHECK_ALL),1)
	@cppcheck $(CPPCHECK_GENERIC_OPTIONS) $(CPPCHECK_SUPPRESS_OPTIONS) --enable=all --inconclusive $(SRCDIR)
else
	@cppcheck $(CPPCHECK_GENERIC_OPTIONS) $(CPPCHECK_SUPPRESS_OPTIONS) --enable=warning,performance,portability $(SRCDIR)
endif

.PHONY: clean       # Clean the build directory
clean:
	$(info $(INFO_PREFIX) Cleaning up$(RESET))
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
PREFIX  ?= /usr
# Handle all the weird GNU-style variables
prefix      ?= $(PREFIX)
exec_prefix ?= $(prefix)
bindir      ?= $(exec_prefix)/bin
libdir      ?= $(exec_prefix)/lib
includedir  ?= $(prefix)/include
datarootdir ?= $(prefix)/share
datadir     ?= $(datarootdir)

.PHONY: install     # Install the package
ifeq ($(OS),darwin)
	install: all lib codesign
else
	install: all lib
endif
ifeq ($(HOST_POSIX),1)
	@echo "$(INFO_PREFIX) Installing to prefix $(DESTDIR)$(prefix)$(RESET)"
	@install -Dm755 $(BINARY)             $(DESTDIR)$(bindir)/rvvm
	@install -Dm755 $(SHARED)             $(DESTDIR)$(libdir)/librvvm$(LIB_EXT)
	@install -Dm644 $(STATIC)             $(DESTDIR)$(libdir)/librvvm_static.a
	@install -Dm644 $(SRCDIR)/rvvmlib.h   $(DESTDIR)$(includedir)/rvvm/rvvmlib.h
	@install -Dm644 $(SRCDIR)/fdtlib.h    $(DESTDIR)$(includedir)/rvvm/fdtlib.h
	@install -Dm644 $(SRCDIR)/devices/*.h $(DESTDIR)$(includedir)/rvvm/
	@install -d                           $(DESTDIR)$(datadir)/licenses/rvvm/
	@install -Dm644 LICENSE*              $(DESTDIR)$(datadir)/licenses/rvvm/
else
	@echo "$(WARN_PREFIX) Install target unsupported on non-POSIX!$(RESET)"
endif

.PHONY: help        # Show this help message
help:
	$(info $(INFO_PREFIX) Available make useflags:$(RESET))
	$(foreach useflag, $(USEFLAGS),$(info $(SPACE) $(useflag)=$($(useflag))))
	$(info $(INFO_PREFIX) Available make targets:$(RESET))
	@grep '^.PHONY:' Makefile | sed 's/\.PHONY://g'
	@echo $(NULL_STDERR)

.PHONY: info        # Show this help message
info: help

.PHONY: list        # Show this help message
list: help

sinclude $(DEPS)
