# Makefile :)
OS_UNAME =  $(shell uname -s)

ifeq ($(OS_UNAME),Linux)
OS = linux
PROGRAMEXT =
else
OS = windows
PROGRAMEXT = .exe
endif

ifeq ($(OS_UNAME),FreeBSD)
OS = freebsd
PROGRAMEXT =
endif

SRCDIR = src

NAME = rvvm

ifeq ($(CLANG),1)
CC = clang
CXX = clang++
else ifeq ($(TCC),1)
CC= tcc
else
CC = cc
CXX = c++
endif

OPT_CFLAGS = -O2 -flto -pthread

BASE_CFLAGS = -std=gnu11 -DVERSION=\"$(VERSION)\" -DARCH=\"$(ARCH)\" -Wall -Wextra

ARCH=$(shell uname -m)

COMMIT := $(firstword $(shell git rev-parse --short=6 HEAD) unknown)

ifeq ($(DEBUG),1)
BUILD_TYPE = debug
BUILD_TYPE_CFLAGS = -Og -ggdb -DDEBUG
#-fsanitize=undefined -fsanitize=address -fsanitize=thread
else
BUILD_TYPE = release
BUILD_TYPE_CFLAGS = $(OPT_CFLAGS) -DNDEBUG
DEBUG = 0
endif

VERSION := $(COMMIT)-$(BUILD_TYPE)

OBJDIR = $(BUILD_TYPE).$(OS).$(ARCH)

CFLAGS = $(BUILD_TYPE_CFLAGS) $(BASE_CFLAGS) $(ARCH_CFLAGS)

INCLUDE = -I. -I$(SRCDIR)

LDFLAGS += $(OPT_CFLAGS)

DO_CC = $(CC) $(CFLAGS) $(INCLUDE) -o $@ -c $<
DO_CXX = $(CXX) $(CFLAGS) $(INCLUDE) -o $@ -c $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c Makefile
	$(DO_CC)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp Makefile
	$(DO_CXX)

SRC := $(wildcard $(SRCDIR)/*.c $(SRCDIR)/*.cpp)
OBJ := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
DEPEND := $(OBJDIR)/Rules.depend

TARGET := $(OBJDIR)/$(NAME)_$(ARCH)$(PROGRAMEXT)

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJDIR) $(DEPEND) $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $@

.PHONY: neat
neat: $(OBJDIR)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

.PHONY: clean
clean: depend
	-rm -f $(OBJDIR)/*.so
	-rm -f $(OBJ)
	-rm -f $(TARGET)
	-rm -f $(OBJDIR)/Rules.depend
	-rmdir $(OBJDIR)

.PHONY: depend
depend: $(DEPEND)

$(OBJDIR)/Rules.depend: $(SRC) | $(OBJDIR)
	$(CC) -MM $(INCLUDE) $(SRC) $(CFLAGS) | sed "s;\(^[^         ]*\):\(.*\);$(OBJDIR)/\1:\2;" > $@

include $(OBJDIR)/Rules.depend
