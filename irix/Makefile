# The IRIX look for emwm: resources, menus and a desktop tile, installed
# where emwm-session looks for them. BSD or GNU make.

PREFIX ?= /usr/local
IRIXDIR ?= $(PREFIX)/lib/X11/irix

all:

install:
	install -d $(IRIXDIR)
	install -m 644 Emwm-irix.ad $(IRIXDIR)/Emwm-irix.ad
	install -m 644 irix.emwmrc $(IRIXDIR)/irix.emwmrc
	install -m 644 speckle.xpm $(IRIXDIR)/speckle.xpm

.PHONY: all install
