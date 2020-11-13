CC        = $(CROSS_COMPILE)gcc
PKG_CONFIG = $(CROSS_COMPILE)pkg-config
INSTALL    = install
STRIP     = $(CROSS_COMPILE)strip
CFLAGS    ?= -g 
CFLAGS    += -shared -fPIC -nostartfiles
CFLAGS    += $(shell $(PKG_CONFIG) --cflags lem)
CFLAGS    += $(shell $(PKG_CONFIG) --cflags opus)  # -I/usr/include/opus
LIBS      += $(shell $(PKG_CONFIG) --libs opus) # -lopus

lmoddir    = $(shell $(PKG_CONFIG) --variable=INSTALL_LMOD lem)
cmoddir    = $(shell $(PKG_CONFIG) --variable=INSTALL_CMOD lem)

llibs =
clibs = lem/opus/core.so

ifdef V
E=@\#
Q=
else
E=@echo
Q=@
endif

.PHONY: all

all: $(clibs)

%.so: %.c
	$E ' CCLD $@'
	$Q$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LIBS)

lem/opus/core.so:

%-strip: %
	$E '  STRIP $<'
	$Q$(STRIP) $<

strip: $(clibs:%=%-strip)

$(DESTDIR)$(lmoddir)/% $(DESTDIR)$(cmoddir)/%: %
	$E '  INSTALL $@'
	$Q$(INSTALL) -d $(dir $@)
	$Q$(INSTALL) -m 644 $< $@

install: strip\
	$(llibs:%=$(DESTDIR)$(lmoddir)/%) \
	$(clibs:%=$(DESTDIR)$(cmoddir)/%)

clean:
	rm -f $(clibs)
