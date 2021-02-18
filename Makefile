# Makefile :)
OS_UNAME =  $(shell uname -s)

ifeq ($(OS_UNAME), Linux)
OS=linux
PROGRAMEXT=
else
OS=windows
PROGRAMEXT=.exe
endif

ifeq ($(OS_UNAME), FreeBSD)
OS=freebsd
PROGRAMEXT=
endif

SRCDIR=src

NAME=rvvm

CC?=gcc
CXX?=g++

ifeq ($(CLANG), 1)
CC=clang
CXX=clang++
endif

ifeq ($(TCC), 1)
CC=tcc
endif

OPT_CFLAGS = -O2 -flto

BASE_CFLAGS = -std=gnu11 -DVERSION=\"$(VERSION)\" -DARCH=\"$(ARCH)\" -Wall -Wextra

ARCH=$(shell uname -m)

COMMIT := $(firstword $(shell git rev-parse --short=6 HEAD) unknown)

ifeq ($(DEBUG),1)
BUILD_TYPE = debug
BUILD_TYPE_CFLAGS = -g -DDEBUG
#-fsanitize=undefined -fsanitize=address -fsanitize=thread
else
BUILD_TYPE = release
BUILD_TYPE_CFLAGS = -DNDEBUG
DEBUG=0
endif

VERSION := $(COMMIT)-$(BUILD_TYPE)

OBJDIR=$(BUILD_TYPE).$(OS).$(ARCH)

CFLAGS = $(BUILD_TYPE_CFLAGS) $(BASE_CFLAGS) $(OPT_CFLAGS) $(ARCH_CFLAGS)

INCLUDE=-I. -I$(SRCDIR)

LDFLAGS += -flto

DO_CC=$(CC) $(CFLAGS) $(INCLUDE) -o $@ -c $<

DO_CXX=$(CXX) $(CFLAGS) $(INCLUDE) -o $@ -c $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(DO_CC)

SRC = $(wildcard src/*.c)

OBJ := $(SRC:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

$(NAME)_$(ARCH)$(PROGRAMEXT): neat depend $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) $(LDFLAGS) -o $(OBJDIR)/$@

neat:
	@mkdir -p $(OBJDIR)

$(OBJDIR): neat

clean: depend
	-rm -f $(OBJDIR)/*.so
	-rm -f $(OBJ)
	-rm -f $(OBJDIR)/$(NAME)_$(ARCH)$(PROGRAMEXT)
	-rm -f $(OBJDIR)/Rules.depend

depend: $(OBJDIR)/Rules.depend

$(OBJDIR)/Rules.depend: $(SRCFILES) $(OBJDIR)
	$(CC) -MM $(INCLUDE) $(SRC) $(CFLAGS) | sed "s;\(^[^         ]*\):\(.*\);$(OBJDIR)/\1:\2;" > $@

include $(OBJDIR)/Rules.depend
