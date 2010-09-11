# should have distclean, install, uninstall in the future
.PHONY: all lib audiomuxer dep depend clean

# for future use
#include config.mak
#$(EXE)

CC=gcc
AR=ar
RANLIB=ranlib
STRIP=strip
ECHO=echo
EXE=.exe

CFLAGS=-Wshadow -Wall -std=gnu99 -I.
#CFLAGS+=-Wsign-conversion
CFLAGS+=-g -O0
#CFLAGS+=-O3
CFLAGS+=-march=i686 -mfpmath=sse -msse
LDFLAGS=-Wl,--large-address-aware
EXTRALIBS=

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
all: audiomuxer

lib: $(TARGET_LIB)

audiomuxer: $(TARGET_AUDIOMUXER)

$(TARGET_LIB): .depend $(OBJS)
	@$(ECHO) "AR: $@"
	@$(AR) rc $@ $(OBJS)
	@$(ECHO) "RANLIB: $@"
	@$(RANLIB) $@

$(TARGET_AUDIOMUXER): $(OBJ_AUDIOMUXER) $(TARGET_LIB)
	@$(ECHO) "LINK: $@"
	@$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $+ $(EXTRALIBS)

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
