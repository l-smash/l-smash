# for future use
#include config.mak
#$(EXE)

UNAME_S:=$(shell uname -s)
UNAME_M:=$(shell uname -m)

CC=gcc
AR=ar
RANLIB=ranlib
STRIP=strip
ECHO=echo
EXE=

CFLAGS=-Wshadow -Wall -std=gnu99 -I.
#CFLAGS+=-Wsign-conversion
LDFLAGS=
EXTRALIBS=

ifeq ($(DEBUG),YES)
CFLAGS+=-g -O0
else
CFLAGS+=-O3
endif

ifneq ($(findstring i686, $(UNAME_M)),)
CFLAGS+=-march=i686 -mfpmath=sse -msse
endif

ifneq ($(findstring MINGW, $(UNAME_S)),)
LDFLAGS+=-Wl,--large-address-aware
EXE=.exe
endif
ifneq ($(findstring CYGWIN, $(UNAME_S)),)
LDFLAGS+=-Wl,--large-address-aware
EXE=.exe
endif

SRCS=isom.c isom_util.c mp4sys.c
OBJS=$(SRCS:%.c=%.o)
#TARGET=lsmash$(EXE)

TARGET_LIB=liblsmash.a

SRC_AUDIOMUXER=audiomuxer.c
OBJ_AUDIOMUXER=$(SRC_AUDIOMUXER:%.c=%.o)
TARGET_AUDIOMUXER=$(SRC_AUDIOMUXER:%.c=%$(EXE))

SRCS_ALL=$(SRCS) $(SRC_AUDIOMUXER)
OBJS_ALL=$(SRCS_ALL:%.c=%.o)

#### main rules ####

# should have distclean, install, uninstall in the future
.PHONY: all lib tools dep depend clean info

all: info tools

info:
	@echo "CFLAGS : $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"

lib: $(TARGET_LIB)

tools: $(TARGET_AUDIOMUXER)

$(TARGET_LIB): .depend $(OBJS)
	@$(ECHO) "AR: $@"
	@$(AR) rc $@ $(OBJS)
	@$(ECHO) "RANLIB: $@"
	@$(RANLIB) $@

$(TARGET_AUDIOMUXER): $(OBJ_AUDIOMUXER) $(TARGET_LIB)
	@$(ECHO) "LINK: $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $+ $(EXTRALIBS)
ifneq ($(DEBUG),YES)
	@$(ECHO) "STRIP: $@"
	@$(STRIP) $@
endif

#### type rules ####
%.o: %.c .depend
	@$(ECHO) "CC: $<"
	@$(CC) -c $(CFLAGS) -o $@ $<

#### dependency relative ####
dep: .depend
depend: .depend
ifneq ($(wildcard .depend),)
include .depend
endif

# when we have configure script, use ".depend: config.mak"
.depend:
	@rm -f .depend
	@$(foreach SRC, $(SRCS_ALL), $(CC) $(CFLAGS) $(SRC) -g0 -MT $(SRC:%.c=%.o) -MM >> .depend;)

# automagically create dependency of tools, but old style "make depend" is required
#	@$(foreach TOOL, $(SRCS_TOOLS), $(ECHO) -e '$(TOOL:%.c=%$(EXE)): $(TOOL:%.c=%.o) $(TARGET_LIB)\n\t$(CC) $(LDFLAGS) -o $$@ $$+ $(EXTRALIBS)' >> .depend;)

#### clean stuff ####
clean:
	rm -f $(OBJS_ALL) $(TARGET_LIB) $(TARGET_AUDIOMUXER) .depend
