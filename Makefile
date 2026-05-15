CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPDIR ?= $(DATADIR)/applications
ICONDIR ?= $(DATADIR)/icons/hicolor/scalable/apps

CFLAGS ?= -O3 -pipe
CPPFLAGS += -DG_DISABLE_CAST_CHECKS
LDFLAGS ?= -Wl,-O1,--as-needed
WARNFLAGS ?= -Wall -Wextra -Wpedantic
PKGS := gtk+-3.0 gio-unix-2.0 gstreamer-1.0 gstreamer-video-1.0 gstreamer-pbutils-1.0 gstreamer-app-1.0
FM_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(PKGS))
FM_LIBS := $(shell $(PKG_CONFIG) --libs $(PKGS))

FM_TARGET := build/filemanager
FM_SRC := src/filemanager.c src/desktop_integration.c

.PHONY: all clean install uninstall

all: $(FM_TARGET)

$(FM_TARGET): $(FM_SRC)
	install -d "$(dir $@)"
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNFLAGS) $(FM_CFLAGS) -o $@ $(FM_SRC) $(LDFLAGS) $(FM_LIBS)

install: all
	install -d "$(DESTDIR)$(BINDIR)"
	install -d "$(DESTDIR)$(APPDIR)"
	install -d "$(DESTDIR)$(ICONDIR)"
	install -m 755 "$(FM_TARGET)" "$(DESTDIR)$(BINDIR)/filemanager"
	sed "s|^Exec=.*|Exec=$(BINDIR)/filemanager %U|" "src/filemanager.desktop" > "$(DESTDIR)$(APPDIR)/filemanager.desktop"
	chmod 644 "$(DESTDIR)$(APPDIR)/filemanager.desktop"
	install -m 644 "src/filemanager.svg" "$(DESTDIR)$(ICONDIR)/filemanager.svg"
	-command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$(DESTDIR)$(APPDIR)"
	-command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f "$(DESTDIR)$(DATADIR)/icons/hicolor"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/filemanager"
	rm -f "$(DESTDIR)$(APPDIR)/filemanager.desktop"
	rm -f "$(DESTDIR)$(ICONDIR)/filemanager.svg"

clean:
	rm -rf build
