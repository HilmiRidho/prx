# prx - packaging-friendly Makefile (honours PREFIX, DESTDIR, CC)
PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/prx
MANDIR  ?= $(PREFIX)/share/man/man1
CC      ?= cc

.PHONY: all install uninstall check lint clean

all:
	@echo "Nothing to compile at install time: the shim is built per-user on first 'prx login' (or 'prx build')."
	@echo "Targets: install, uninstall, check"

install:
	install -Dm755 prx          $(DESTDIR)$(BINDIR)/prx
	install -Dm644 src/prx.c     $(DESTDIR)$(DATADIR)/src/prx.c
	install -Dm644 src/ldpatch.c $(DESTDIR)$(DATADIR)/src/ldpatch.c
	install -d $(DESTDIR)$(DATADIR)/src/shim
	install -m644 src/shim/*.h   $(DESTDIR)$(DATADIR)/src/shim/
	install -Dm644 man/prx.1     $(DESTDIR)$(MANDIR)/prx.1

uninstall:
	rm -f  $(DESTDIR)$(BINDIR)/prx $(DESTDIR)$(MANDIR)/prx.1
	rm -rf $(DESTDIR)$(DATADIR)

lint:
	bash -n prx
	@if command -v shellcheck >/dev/null 2>&1; then shellcheck -S warning -s bash prx tests/smoke.sh; else echo "shellcheck not installed, skipped"; fi
	$(CC) -fsyntax-only -Wall -Wno-unused-function src/ldpatch.c

check: lint
	bash tests/smoke.sh

clean:
	rm -rf tests/.tmp
