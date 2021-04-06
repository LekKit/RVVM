# Makefile :)
NAME     := rvvm
SRCDIR   := src

# OS-specific configuration
OS_UNAME := $(shell uname -s)
ARCH     := $(shell uname -m)

linux_PROGRAMEXT   :=
linux_CFLAGS       := -pthread -DUSE_X11
linux_LDFLAGS      := -pthread -lX11
freebsd_PROGRAMEXT :=
freebsd_CFLAGS     := -pthread -DUSE_X11
freebsd_LDFLAGS    := -pthread -lX11
windows_PROGRAMEXT := .exe
windows_CFLAGS     :=
windows_LDFLAGS    := -pthread

ifeq ($(OS_UNAME),Linux)
OS := linux
else ifeq ($(OS_UNAME),FreeBSD)
OS := freebsd
else
OS := windows
endif

# Optimization/debug configuration
debug_CFLAGS   := -DDEBUG
release_CFLAGS := -DNDEBUG

ifeq ($(DEBUG),1)
BUILD_TYPE := debug
else
BUILD_TYPE := release
endif

# Version string
COMMIT  := $(firstword $(shell git rev-parse --short=6 HEAD) unknown)
VERSION := $(COMMIT)-$(BUILD_TYPE)

# Choose compiler
# (but actually you just need to pass CC/CXX variable to make for this)
clang_debug_CFLAGS       := -Og -ggdb
#-fsanitize=undefined -fsanitize=address -fsanitize=thread
clang_release_CFLAGS     := -O2 -flto
clang++_debug_CXXFLAGS   := $(clang_debug_CFLAGS)
clang++_release_CXXFLAGS := $(clang_release_CFLAGS)

tcc_debug_CFLAGS         := -Og -ggdb
tcc_release_CFLAGS       := -O2
# no tcc for C++ :(

cc_debug_CFLAGS          := -Og -ggdb
cc_release_CFLAGS        := -O2 -flto -fwhole-program
c++_debug_CXXFLAGS       := $(cc_debug_CFLAGS)
c++_release_CXXFLAGS     := $(cc_release_CFLAGS)

ifeq ($(CLANG),1)
CC  := clang
CXX := clang++
else ifeq ($(TCC),1)
CC  := tcc
else
CC  := cc
CXX := c++
endif

# Generic compiler flags
BASE_CFLAGS := -DVERSION=\"$(VERSION)\" -DARCH=\"$(ARCH)\" -Wall -Wextra -I. -I$(SRCDIR)
CFLAGS   += -std=gnu11 $(BASE_CFLAGS) $($(OS)_CFLAGS) $($(BUILD_TYPE)_CFLAGS) $($(CC)_$(BUILD_TYPE)_CFLAGS)
CXXFLAGS += -std=gnu++17 $(BASE_CFLAGS) $($(OS)_CFLAGS) $($(BUILD_TYPE)_CFLAGS) $($(CXX)_$(BUILD_TYPE)_CXXFLAGS)
LDFLAGS  += $($(OS)_LDFLAGS)

DO_CC  = $(CC) $(CFLAGS) $(TARGET_ARCH) -o $@ -c $<
DO_CXX = $(CXX) $(CXXFLAGS) $(TARGET_ARCH) -o $@ -c $<

OBJDIR := $(BUILD_TYPE).$(OS).$(ARCH)

# Generic sources
SRC           := $(wildcard $(SRCDIR)/*.c $(SRCDIR)/*.cpp)
OBJ_GENERIC32 := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.32.o)
OBJ_GENERIC32 := $(OBJ_GENERIC32:$(SRCDIR)/%.cpp=$(OBJDIR)/%.32.o)
OBJ_GENERIC64 := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.64.o)
OBJ_GENERIC64 := $(OBJ_GENERIC64:$(SRCDIR)/%.cpp=$(OBJDIR)/%.64.o)

# Rules to build CPU object files for different architectures
SRC_CPU   := $(wildcard $(SRCDIR)/cpu/*.c $(SRCDIR)/cpu/*.cpp)
OBJ_CPU32 := $(SRC_CPU:$(SRCDIR)/%.c=$(OBJDIR)/%.32.o)
OBJ_CPU32 := $(OBJ_CPU32:$(SRCDIR)/%.cpp=$(OBJDIR)/%.32.o)
OBJ_CPU64 := $(SRC_CPU:$(SRCDIR)/%.c=$(OBJDIR)/%.64.o)
OBJ_CPU64 := $(OBJ_CPU64:$(SRCDIR)/%.cpp=$(OBJDIR)/%.64.o)
SRC += $(SRC_CPU)

# Make directory if we need to.
# This is really ugly, the proper solution would be a dependency on
# an object file directory, but unfortunately we can't do that :(
MKDIR_FOR_TARGET = @mkdir -p $(@D)

# RV64
$(OBJDIR)/%.64.o: $(SRCDIR)/%.c Makefile
	$(MKDIR_FOR_TARGET)
	$(DO_CC) -DRV64

$(OBJDIR)/%.64.o: $(SRCDIR)/%.cpp Makefile
	$(MKDIR_FOR_TARGET)
	$(DO_CXX) -DRV64

# RV32 (no defines)
$(OBJDIR)/%.32.o: $(SRCDIR)/%.c Makefile
	$(MKDIR_FOR_TARGET)
	$(DO_CC)

$(OBJDIR)/%.32.o: $(SRCDIR)/%.cpp Makefile
	$(MKDIR_FOR_TARGET)
	$(DO_CXX)

# Default rules for generic code (should not be used, left just in case)
$(OBJDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(MKDIR_FOR_TARGET)
	$(DO_CC)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp Makefile
	$(MKDIR_FOR_TARGET)
	$(DO_CXX)

DEPEND   := $(OBJDIR)/Rules.depend
TARGET   := $(OBJDIR)/$(NAME)_$(ARCH)$($(OS)_PROGRAMEXT)
TARGET64 := $(OBJDIR)/$(NAME)64_$(ARCH)$($(OS)_PROGRAMEXT)

.PHONY: all
all: $(TARGET) #$(TARGET64)

$(TARGET): $(DEPEND) $(OBJ_GENERIC32) $(OBJ_CPU32)
	$(CC) $(CFLAGS) $(OBJ_GENERIC32) $(OBJ_CPU32) $(LDFLAGS) -o $@

$(TARGET64): $(DEPEND) $(OBJ_GENERIC64) $(OBJ_CPU32) $(OBJ_CPU64)
	$(CC) $(CFLAGS) $(OBJ_GENERIC64) $(OBJ_CPU32) $(OBJ_CPU64) $(LDFLAGS) -o $@

.PHONY: neat
neat: $(OBJDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

.PHONY: clean
clean: depend
	-rm -f $(OBJDIR)/*.so
	-rm -f $(OBJ_GENERIC32)
	-rm -f $(OBJ_GENERIC64)
	-rm -f $(OBJ_CPU32)
	-rm -f $(OBJ_CPU64)
	-rm -f $(TARGET) $(TARGET64)
	-rm -f $(OBJDIR)/Rules.depend
	-find $(OBJDIR)/ -depth -type d -exec rmdir {} +

.PHONY: depend
depend: $(DEPEND)

$(OBJDIR)/Rules.depend: $(SRC) | $(OBJDIR)
	$(CC) -MM $(INCLUDE) $(SRC) $(CFLAGS) | sed "s;\(^[^         ]*\):\(.*\);$(OBJDIR)/\1:\2;" > $@

include $(OBJDIR)/Rules.depend
