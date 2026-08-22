override NAME    := RVVM
override DESC    := The RVVM Project
override URL     := https://github.com/LekKit/RVVM
override VERSION := v0.7-git

override define LOGO
$(BLUE) 🭥█████🭐 ██  ██ ██  ██🭢█🭌🬿  🭊🭁█🭚
$(BLUE)  ██  🭨█🭬██  ██ ██  ██ ███🭏🭄███
$(ORANGE)  █████🭪 $(BLUE)██  ██ ██  ██$(ORANGE) ██🭥🭒🭝🭚██
$(ORANGE)  ██  🭖█🭀🭕█🭏🭄█🭠 🭕█🭏🭄█🭠 ██ 🭢🭗 ██
$(ORANGE)  ██  🭦█🭛 🭥🭒🭝🭚   🭥🭒🭝🭚  █🭠    ██
$(ORANGE)  █🭠   🭠🭗  🭢🭗     🭢🭗   🭠🭗    🭕█
$(ORANGE)  🭠🭗                         🭢🭕
endef

#
# Platform-dependent default project configuration
#

ifneq (,$(filter linux,$(OS)))
# Enable Wayland on Linux by default
USE_WAYLAND ?= 1
endif
ifneq (,$(filter linux %bsd sunos,$(OS)))
# Enable X11 on Linux, *BSD, Solaris by default
USE_X11 ?= 1
endif
ifneq (,$(filter windows,$(OS)))
USE_WIN32_GUI ?= 1
endif
ifneq (,$(filter haiku,$(OS)))
USE_HAIKU_GUI ?= 1
endif
ifneq (,$(filter darwin,$(OS)))
# Enable the native Cocoa GUI backend on macOS by default
USE_COCOA_GUI ?= 1
endif
ifneq (,$(filter serenity,$(OS)))
# Enable SDL2 on Serenity by default
USE_SDL ?= 2
endif
ifneq (,$(filter redox,$(OS)))
# Enable SDL1 and disable networking on Redox by default
USE_SDL ?= 1
USE_NET ?= 0
USE_LIB ?= 0
endif
ifneq (,$(filter emscripten,$(OS)))
# Enable SDL2 on Emscripten by default
USE_SDL ?= 2
endif
ifneq (,$(filter dos,$(OS)))
# Disable JIT/network/sound, emulate threads/atomics in DOS
USE_JIT        ?= 0
USE_NET        ?= 0
USE_SOUND      ?= 0
USE_ATOMIC_EMU ?= 1
USE_THREAD_EMU ?= 1
endif

# Only allow dynamic linking to librvvm in released versions
ifneq (,$(findstring -,$(VERSION)))
USE_LIB_SHARING ?= 0
endif

#
# Default project build configuration
#

# CPU features
USE_RV32 ?= 1 # Support riscv32imacb guests
USE_RV64 ?= 1 # Support riscv64imacb guests
USE_FPU  ?= 1 # Support FPU extensions
USE_RVV  ?= 0 # Support Vector extension

# Usability features
USE_GUI     ?= 1          # Enable guest display GUI
USE_SDL     ?= 0          # Enable SDL as GUI backend - usually picked on per-platform basis
USE_NET     ?= 1          # Enable networking support
USE_SOUND   ?= 0          # Enable sound support
USE_GDBSTUB ?= $(USE_NET) # Support debugging the guest via GDB remote protocol

# Board features
USE_FDT  ?= 1 # Enable Flattened Device Tree automatic generation
USE_VFIO ?= 1 # Support PCIe VFIO pass-through on Linux hosts

# Infrastructure
USE_INFRA_TESTS ?= 0 # Build infrastructure tests
USE_LIBS_PROBE  ?= 0 # Probe libraries in runtime instead of linking to them
USE_LOCK_DEBUG  ?= 1 # Runtime lock debugging & locking debug info
USE_ISOLATION   ?= 1 # Process isolation via seccomp/pledge
USE_JNI         ?= 0 # Enable JNI support in librvvm

# Acceleration
# Enable JIT by default on x86_64, arm64, riscv64
USE_JIT ?= $(if $(filter x86_64 arm64 riscv64 loongarch64,$(ARCH)),1,0)
USE_KVM ?= 0

# Misc toggles for debugging host platform/compiler issues
USE_NO_STACKTRACE ?= 0 # Disable post-mortem crash stacktraces
USE_NO_DLIB       ?= 0 # Disable dynamic library/symbol probing via dlsym()/GetProcAddress()
USE_STDIO         ?= 0 # Use non-threaded stdio fallback IO backend (Instead of Win32/POSIX)
USE_SELECT        ?= 0 # Use select() event interface fallback for networking (Instead of epoll/kqueue)

USE_SOFT_FPU_WRAP ?= 0 # Wrap native floating-point types into bitcasted representation (Fixes 8087 FPU)
USE_SOFT_FPU_FENV ?= 0 # Emulate FPU exceptions (Note this isn't soft-fp, and still fairly fast)

USE_FUTEX_EMU  ?= 0 # Emulate futexes via pthread_cond / Win32 Event / etc
USE_ATOMIC_EMU ?= 0 # Emulate atomics via host mutex
USE_THREAD_EMU ?= 0 # Emulate threads via guest CPU preemption

USE_NO_THREAD_LOCAL ?= 0 # Disable and undefine THREAD_LOCAL attribute
USE_NO_BUILD_ASSERT ?= 0 # Disable build-time assertions
USE_NO_RANDSTRUCT   ?= 0 # Disable struct randomization via randomize_layout
USE_NO_ALIGN_TYPE   ?= 0 # Disable type alignment where it's optional
USE_NO_SOURCE_OPT   ?= 0 # Disable per-source manual optimization level
USE_NO_PREFETCH     ?= 0 # Disable use of __builtin_prefetch()
USE_NO_LIKELY       ?= 0 # Disable use of __builtin_expect()
USE_NO_FORCEINLINE  ?= 0 # Disable force inlining
USE_NO_NOINLINE     ?= 0 # Disable force un-inlining
USE_NO_SLOW_PATH    ?= 0 # Disable slow_path attribute
USE_NO_FLATTEN      ?= 0 # Disable flatten_calls attribute

ifneq (,$(call var_use,USE_TAP_LINUX))
$(call log_warn,Linux TAP is deprecated in favor of USE_NET due to checksum issues)
endif

#
# Useflag handling
#

# Useflag conditional sources
override SRC_USE_WIN32_GUI := $(SRCDIR)/gui/win32_window.c
override SRC_USE_HAIKU_GUI := $(SRCDIR)/gui/haiku_window.cpp
override SRC_USE_COCOA_GUI := $(SRCDIR)/gui/cocoa_window.c
override SRC_USE_X11       := $(SRCDIR)/gui/x11_window.c
override SRC_USE_SDL       := $(SRCDIR)/gui/sdl_window.c
override SRC_USE_WAYLAND   := $(SRCDIR)/gui/wayland_window.c

override SRC_USE_TAP_LINUX := $(SRCDIR)/devices/tap_linux.c
override SRC_USE_NET       := $(SRCDIR)/util/networking.c $(SRCDIR)/devices/tap_user.c
override SRC_USE_JIT       := $(SRCDIR)/rvjit/rvjit.c $(SRCDIR)/rvjit/rvjit_emit.c
override SRC_USE_RV32      := $(SRCDIR)/cpu/riscv32_interpreter.c
override SRC_USE_RV64      := $(SRCDIR)/cpu/riscv64_interpreter.c
override SRC_USE_LIBRETRO  := $(SRCDIR)/bindings/libretro/libretro.c
override SRC_USE_JNI       := $(SRCDIR)/bindings/jni/rvvm_jni.c

# Useflag dependencies
override RVJIT_SUPPORTS_ARCH := $(if $(filter i386 x86_64 arm% riscv% loongarch64,$(ARCH)),1)
override DEPS_USE_JIT        := RVJIT_SUPPORTS_ARCH

override DEPS_USE_X11       := USE_GUI
override DEPS_USE_SDL       := USE_GUI
override DEPS_USE_WAYLAND   := USE_GUI
override DEPS_USE_WIN32_GUI := USE_GUI
override DEPS_USE_HAIKU_GUI := USE_GUI
override DEPS_USE_COCOA_GUI := USE_GUI
override DEPS_USE_ALSA      := USE_SOUND
override DEPS_USE_GDBSTUB   := USE_NET
override DEPS_USE_JNI       := USE_LIB USE_NET
override DEPS_USE_LIBRETRO  := USE_LIB USE_NET

# Libraries
override LIBS_USE_SDL     := sdl$(filter-out 1,$(USE_SDL))
override LIBS_USE_X11     := x11 xext
override LIBS_USE_WAYLAND := wayland-client xkbcommon

#
# Additional headers
#

override CPPFLAGS := $(CPPFLAGS) -I$(SRCDIR)/util

#
# Prepare build targets
#

override BIN_TARGETS := rvvm
override LIB_TARGETS := rvvm # TODO: rvvm_libretro FTBFS

override bin_src_rvvm          := $(SRCDIR)/main.c
override lib_src_rvvm_libretro := $(SRCDIR)/bindings/libretro/libretro.c

# The userland emulator is Linux-only, enable it solely on Linux targets
ifneq (,$(filter linux,$(OS)))
override BIN_TARGETS          := $(BIN_TARGETS) rvvm_user
override CPPFLAGS             := $(CPPFLAGS) -DRVVM_USER_TEST
override bin_src_rvvm_user    := $(SRCDIR)/rvvm_user_main.c
override bin_libs_rvvm_user   := rvvm
endif

override lib_src_rvvm          := $(filter-out $(bin_src_rvvm) $(bin_src_rvvm_user) $(lib_src_rvvm_libretro),$(call recursive_match,$(SRCDIR),*.c *.cpp *.cc *.cxx))

override bin_libs_rvvm := rvvm
override lib_libs_rvvm := $(if $(call var_use,USE_LIBS_PROBE),,$(LIBS_USE_SDL) $(LIBS_USE_X11) $(LIBS_USE_WAYLAND))

#
# Tests
#

override RVVM := $(call bin_target,rvvm)

override TEST_DATA_TAR_LINK := https://github.com/LekKit/riscv-tests/releases/download/rvvm-tests/riscv-tests.tar.gz
override TEST_DATA_TAR_FILE := $(lastword $(subst /,$(SPACE),$(TEST_DATA_TAR_LINK)))

override test_result = $(call println,$(TEXT)[$(if $(filter 0,$1),$(GREEN)PASS,$(RED)FAIL: $1)$(TEXT)] $2)$(if $(filter 0,$1),,fail)
override invoke_rvvm = $(call test_result,$(lastword $(call shell_ex,$(RVVM) $1 -nonet -nogui -nosound $(NULL_STDERR))),$(firstword $1))
override filter_test = $(filter-out $(foreach isa,$2,$(BUILDDIR)/riscv-tests/$(isa)%),$(call recursive_wildcard,$(BUILDDIR)/riscv-tests/$1*))

test:
	$(if $(call paths_exist,$(BUILDDIR)/riscv-tests),,$(call shell_ex,cd $(BUILDDIR) && curl -LO $(TEST_DATA_TAR_LINK) && tar xzf $(TEST_DATA_TAR_FILE)))
ifneq (,$(call var_use,USE_RV32))
	$(call println,)
	$(call log_info,Running RISC-V Tests (riscv32))
	$(call println,)
	@$(if $(strip $(foreach test,$(call filter_test,rv32,$(if $(call var_use,USE_FPU),,rv32uf rv32ud rv32uzfh)),$(call invoke_rvvm,$(test) -rv32))),exit 1)
endif
ifneq (,$(call var_use,USE_RV64))
	$(call println,)
	$(call log_info,Running RISC-V Tests (riscv64))
	$(call println,)
	@$(if $(strip $(foreach test,$(call filter_test,rv64,$(if $(call var_use,USE_FPU),,rv64uf rv64ud rv64uzfh)),$(call invoke_rvvm,$(test) -rv64))),exit 1)
endif
	@:
