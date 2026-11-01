# prx - packaging-friendly Makefile (honours PREFIX, DESTDIR, CC)
PREFIX  ?= /usr/local
DESTDIR ?=
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share/prx
MANDIR  ?= $(PREFIX)/share/man/man1
CC      ?= cc

.PHONY: all install uninstall check lint clean pemu pemu-x64 pemu-x64-jit

all:
	@echo "Nothing to compile at install time: the shim is built per-user on first 'prx login' (or 'prx build')."
	@echo "Targets: install, uninstall, check"

# Optional: experimental user-mode emulator, a qemu-user alternative.
#   x86_64 host  -> src/pemu.c     (aarch64 guest)
#   aarch64 host -> src/pemu_x64.c (x86_64 guest)
HOSTARCH := $(shell uname -m)
ifeq ($(HOSTARCH),aarch64)
PEMU_SRC = src/pemu_x64.c
else
PEMU_SRC = src/pemu.c
endif

pemu: $(PEMU_SRC)
	$(CC) -O2 -Wall -o pemu $(PEMU_SRC) -lm -pthread

# x86_64 guest emulator on any host (on x86_64 hosts it is only useful for testing).
pemu-x64: src/pemu_x64.c
	$(CC) -O2 -Wall -o pemu-x64 src/pemu_x64.c -lm -pthread

# Test build of the x86_64-guest JIT on a NON-aarch64 machine: the AArch64 code the JIT generates is executed by the
# interpreter in src/pemu.c (built with -DPEMU_EMBED) instead of by a CPU. Slow, but it tests the real back end.
pemu-x64-jit: src/pemu_x64.c src/pemu.c
	$(CC) -O2 -Wall -DPEMU_JIT_EMBED -c -o pemu-x64-jit.o src/pemu_x64.c
	$(CC) -O2 -DPEMU_EMBED -Dmain=a64_unused_main -c -o pemu-a64-embed.o src/pemu.c
	$(CC) -o pemu-x64-jit pemu-x64-jit.o pemu-a64-embed.o -lm -pthread
	@rm -f pemu-x64-jit.o pemu-a64-embed.o

install:
	install -Dm755 prx          $(DESTDIR)$(BINDIR)/prx
	install -Dm644 src/prx.c     $(DESTDIR)$(DATADIR)/src/prx.c
	install -Dm644 src/ldpatch.c $(DESTDIR)$(DATADIR)/src/ldpatch.c
	install -Dm644 src/pemu.c     $(DESTDIR)$(DATADIR)/src/pemu.c
	install -Dm644 src/pemu_x64.c $(DESTDIR)$(DATADIR)/src/pemu_x64.c
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
	$(CC) -fsyntax-only -Wall src/pemu_x64.c
	$(CC) -fsyntax-only -Wall -DPEMU_NO_JIT src/pemu_x64.c
	@if [ "$$(uname -m)" != aarch64 ]; then $(CC) -fsyntax-only -Wall -DPEMU_JIT_EMBED src/pemu_x64.c; fi
	@if [ "$$(uname -m)" = x86_64 ]; then $(CC) -fsyntax-only -Wall src/pemu.c; fi

check: lint
	bash tests/smoke.sh
	@if [ "$$(uname -m)" = x86_64 ]; then \
		if command -v python3 >/dev/null 2>&1; then $(MAKE) --no-print-directory pemu && python3 tests/pemu_test.py ./pemu && python3 tests/pemu_fuzz.py ./pemu 200 1; fi; \
		$(MAKE) --no-print-directory pemu-x64 && bash tests/pemu_x64_test.sh ./pemu-x64; \
		if command -v python3 >/dev/null 2>&1; then $(MAKE) --no-print-directory pemu-x64-jit && bash tests/pemu_x64_test.sh ./pemu-x64-jit && python3 tests/pemu_x64_fuzz.py ./pemu-x64-jit 60 1; fi; \
	elif [ "$$(uname -m)" = aarch64 ]; then \
		$(MAKE) --no-print-directory pemu && bash tests/pemu_x64_test.sh ./pemu && if command -v python3 >/dev/null 2>&1; then python3 tests/pemu_x64_fuzz.py ./pemu 200 1; fi; \
	fi

clean:
	rm -rf tests/.tmp pemu pemu-x64 pemu-x64-jit pemu-x64-jit.o pemu-a64-embed.o
