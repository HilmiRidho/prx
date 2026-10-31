# prx - packaging-friendly Makefile (honours PREFIX, DESTDIR, CC)
PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/prx
MANDIR  ?= $(PREFIX)/share/man/man1
CC      ?= cc

.PHONY: all install uninstall check lint clean pemu

all:
	@echo "Nothing to compile at install time: the shim is built per-user on first 'prx login' (or 'prx build')."
	@echo "Targets: install, uninstall, check"

# Optional: experimental aarch64 user-mode emulator (x86_64 hosts only).
pemu: src/pemu.c
	$(CC) -O2 -Wall -o pemu src/pemu.c -lm

install:
	install -Dm755 prx          $(DESTDIR)$(BINDIR)/prx
	install -Dm644 src/prx.c     $(DESTDIR)$(DATADIR)/src/prx.c
	install -Dm644 src/ldpatch.c $(DESTDIR)$(DATADIR)/src/ldpatch.c
	install -Dm644 src/pemu.c    $(DESTDIR)$(DATADIR)/src/pemu.c
	@if [ -x pemu ]; then install -Dm755 pemu $(DESTDIR)$(BINDIR)/pemu; fi
	install -d $(DESTDIR)$(DATADIR)/src/shim
	install -m644 src/shim/*.h   $(DESTDIR)$(DATADIR)/src/shim/
	install -Dm644 man/prx.1     $(DESTDIR)$(MANDIR)/prx.1

uninstall:
	rm -f  $(DESTDIR)$(BINDIR)/prx $(DESTDIR)$(BINDIR)/pemu $(DESTDIR)$(MANDIR)/prx.1
	rm -rf $(DESTDIR)$(DATADIR)

lint:
	bash -n prx
	@if command -v shellcheck >/dev/null 2>&1; then shellcheck -S warning -s bash prx tests/smoke.sh; else echo "shellcheck not installed, skipped"; fi
	$(CC) -fsyntax-only -Wall -Wno-unused-function src/ldpatch.c
	@if [ "$$(uname -m)" = x86_64 ]; then $(CC) -fsyntax-only -Wall src/pemu.c; fi

check: lint
	bash tests/smoke.sh
	@if [ "$$(uname -m)" = x86_64 ] && command -v python3 >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory pemu && python3 tests/pemu_test.py ./pemu; \
	fi

clean:
	rm -rf tests/.tmp pemu
