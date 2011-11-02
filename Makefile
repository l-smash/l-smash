# Makefile for L-SMASH

# note:
# Currently, this Makefile is not tested except GNU make.

include config.mak

vpath %.c $(SRCDIR)
vpath %.h $(SRCDIR)

OBJS = $(SRCS:%.c=%.o)

SRC_ALL = $(SRCS) $(SRC_TOOLS)

#### main rules ####

.PHONY: all lib install install-lib clean distclean dep depend

all: $(STATICLIB) $(SHAREDLIB) $(TOOLS)

lib: $(STATICLIB) $(SHAREDLIB)

$(STATICLIBNAME): $(OBJS)
	$(AR) rc $@ $^
	$(RANLIB) $@
	-@ $(if $(STRIP), $(STRIP) -x $@)

$(SHAREDLIBNAME): $(OBJS)
	$(LD) $(SO_LDFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)
	-@ $(if $(STRIP), $(STRIP) -s $@)

# $(TOOLS) is automatically generated as config.mak2 by configure.
# The reason for having config.mak2 is for makeing this Makefile easy to read.
include config.mak2

%.o: %.c .depend
	$(CC) -c $(CFLAGS) -o $@ $<

install: all install-lib
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TOOLS) $(DESTDIR)$(BINDIR)

install-lib: lib
	install -d $(DESTDIR)$(INCDIR)
	install -m 644 $(SRCDIR)/lsmash.h $(DESTDIR)$(INCDIR)
	install -d $(DESTDIR)$(LIBDIR)
ifneq ($(STATICLIB),)
	install -m644 $(STATICLIB) $(DESTDIR)$(LIBDIR)
endif
ifneq ($(SHAREDLIB),)
ifneq ($(IMPLIB),)
	install -m 644 $(IMPLIB) $(DESTDIR)$(LIBDIR)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(SHAREDLIB) $(DESTDIR)$(BINDIR)
else
	install -m 755 $(SHAREDLIB) $(DESTDIR)$(LIBDIR)
endif
endif

#All objects should be deleted regardless of configure when uninstall/clean/distclean.
uninstall:
	$(RM) $(DESTDIR)$(INCDIR)/lsmash.h
	$(RM) $(addprefix $(DESTDIR)$(LIBDIR)/, liblsmash.a liblsmash.dll.a liblsmash.so)
	$(RM) $(addprefix $(DESTDIR)$(BINDIR)/, $(TOOLS_ALL) $(TOOLS_ALL:%=%.exe) liblsmash.dll cyglsmash.dll)

clean:
	$(RM) *.o *.a *.so *.dll *.exe $(TOOLS_ALL) .depend

distclean: clean
	$(RM) config.mak*

dep: .depend

depend: .depend

ifneq ($(wildcard .depend),)
include .depend
endif

#The dependency of each source file is solved automatically by follows.
.depend: config.mak
	@$(RM) .depend
	@$(foreach SRC, $(SRC_ALL:%=$(SRCDIR)/%), $(CC) $(SRC) $(CFLAGS) -g0 -MT $(SRC:$(SRCDIR)/%.c=%.o) -MM >> .depend;)

config.mak2: config.mak

config.mak:
	./configure

