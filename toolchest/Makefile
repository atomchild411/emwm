# Toolchest Makefile (BSD or GNU make). The defaults suit NetBSD with Motif
# from pkgsrc; override on the command line elsewhere, e.g.
#	make X11INC=/usr/include X11LIB=/usr/lib MOTIFINC=/usr/include MOTIFLIB=/usr/lib

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/man
RCDIR ?= $(PREFIX)/lib/X11
APPLRESDIR ?= $(PREFIX)/lib/X11/app-defaults

X11INC ?= /usr/X11R7/include
X11LIB ?= /usr/X11R7/lib
MOTIFINC ?= /usr/pkg/include
MOTIFLIB ?= /usr/pkg/lib

CFLAGS += -O2 -Wall -I$(X11INC) -I$(MOTIFINC) -DSYSTEM_CHESTRC='"$(RCDIR)/system.chestrc"'
LDFLAGS += -L$(X11LIB) -L$(MOTIFLIB) -Wl,-R$(X11LIB),-R$(MOTIFLIB)
LIBS = -lXm -lXt -lX11

all: toolchest

toolchest: toolchest.c
	$(CC) $(CFLAGS) -o toolchest toolchest.c $(LDFLAGS) $(LIBS)

install: toolchest
	install -d $(BINDIR) $(RCDIR) $(APPLRESDIR) $(MANDIR)/man1
	install -m 755 toolchest $(BINDIR)/toolchest
	install -m 755 emwm-session $(BINDIR)/emwm-session
	install -m 644 system.chestrc $(RCDIR)/system.chestrc
	install -m 644 Toolchest.ad $(APPLRESDIR)/Toolchest
	install -m 644 toolchest.1 $(MANDIR)/man1/toolchest.1

clean:
	rm -f toolchest

.PHONY: all install clean
