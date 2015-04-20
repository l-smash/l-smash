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
	-@ $(if $(STRIP), $(STRIP) -x $@)
ifeq ($(SHAREDLIBNAME), liblsmash.so.$(MAJVER))
	ln -sf $(SHAREDLIBNAME) liblsmash.so
endif

# $(TOOLS) is automatically generated as config.mak2 by configure.
# The reason for having config.mak2 is for making this Makefile easy to read.
include config.mak2

%.o: %.c .depend config.h
	$(CC) -c $(CFLAGS) -o $@ $<

install: all install-lib
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(TOOLS) $(DESTDIR)$(bindir)

install-lib: liblsmash.pc lib
	install -d $(DESTDIR)$(includedir)
	install -m 644 $(SRCDIR)/lsmash.h $(DESTDIR)$(includedir)
	install -d $(DESTDIR)$(libdir)/pkgconfig
	install -m 644 liblsmash.pc $(DESTDIR)$(libdir)/pkgconfig
ifneq ($(STATICLIB),)
	install -m 644 $(STATICLIB) $(DESTDIR)$(libdir)
endif
ifneq ($(SHAREDLIB),)
ifneq ($(IMPLIB),)
	install -m 644 $(IMPLIB) $(DESTDIR)$(libdir)
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(SHAREDLIB) $(DESTDIR)$(bindir)
else
	install -m 644 $(SHAREDLIB) $(DESTDIR)$(libdir)
ifeq ($(SHAREDLIB), liblsmash.so.$(MAJVER))
	ln -sf $(SHAREDLIB) $(DESTDIR)$(libdir)/liblsmash.so
endif
endif
endif

#All objects should be deleted regardless of configure when uninstall/clean/distclean.
uninstall:
	$(RM) $(DESTDIR)$(includedir)/lsmash.h
	$(RM) $(addprefix $(DESTDIR)$(libdir)/, liblsmash.a liblsmash.dll.a liblsmash.so* liblsmash.dylib pkgconfig/liblsmash.pc)
	$(RM) $(addprefix $(DESTDIR)$(bindir)/, $(TOOLS_ALL) $(TOOLS_ALL:%=%.exe) liblsmash.dll cyglsmash.dll)

clean:
	$(RM) */*.o *.a *.so* *.dll *.dylib $(addprefix cli/, *.exe $(TOOLS_ALL)) .depend

distclean: clean
	$(RM) config.* *.pc

dep: .depend

depend: .depend

ifneq ($(wildcard .depend),)
include .depend
endif

#The dependency of each source file is solved automatically by follows.
.depend: config.mak
	@$(RM) .depend
	@$(foreach SRC, $(SRC_ALL:%=$(SRCDIR)/%), $(CC) $(SRC) $(CFLAGS) -g0 -MT $(SRC:$(SRCDIR)/%.c=%.o) -MM >> .depend;)

liblsmash.pc:
	./configure

config.h:
	./configure

config.mak2:
	./configure

config.mak:
	./configure
