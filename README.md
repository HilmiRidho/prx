# prx

User-space container / path-translation engine (glibc rootfs, no root needed).

`prx` runs a glibc-based Linux root filesystem as an unprivileged user, without root, namespaces or ptrace. Paths are translated by an `LD_PRELOAD` shim, and the guest's dynamic loader and libc are patched at run time.

Current version: **1.0.1** (see [Changelog](#changelog)).

## Requirements

- A host running Linux on x86_64 or aarch64 (including Termux on Android).
- `bash` 4.3 or newer, plus `coreutils`, `tar`, `findutils`, `sed` and `grep`.
- `gcc` or `clang`, used to compile the shim.
- Optional:
  - `curl`: installing from a URL.
  - `xz`, `zstd`, `bzip2`: extracting rootfs archives compressed with those formats.
  - `qemu-user`: running a rootfs of the other architecture (or an i386 rootfs, which is qemu-only). Alternatively use the built-in [pemu](#pemu-built-in-emulator-experimental) emulator.
  - A cross toolchain to build the shim for the guest architecture: `clang` + `lld`, or a cross gcc (`aarch64-linux-gnu-gcc` / `x86_64-linux-gnu-gcc`; for i386 also `i686-linux-gnu-gcc` or `gcc -m32`).

## Install

    make install                               # PREFIX=/usr/local by default
    make install PREFIX=/usr DESTDIR=pkgroot   # for packagers
    make uninstall                             # honours the same PREFIX / DESTDIR
    make check                                 # lint + smoke tests + emulator self-tests (needs a C compiler)

The shim is compiled per user on the first `prx login` (or explicitly with `prx build`). It is rebuilt automatically when `prx`, `src/prx.c`, `src/ldpatch.c` or any `src/shim/*.h` module change.

Prebuilt packages:

    packaging/build-deb.sh debian    # -> prx_<ver>_all.deb      (prefix /usr)
    packaging/build-deb.sh termux    # -> prx_<ver>_termux_all.deb
                                     #    (prefix /data/data/com.termux/files/usr,
                                     #     shebang rewritten; Termux has no /usr/bin/env)

Termux install: `apt install ./prx_<ver>_termux_all.deb` (or `dpkg -i`, then `apt -f install`).

The GitHub Actions workflow (`.github/workflows/build-deb.yml`) runs `make check` and builds both packages on every push and pull request. Pushing a tag `vX.Y.Z` that matches `PRX_VERSION` in `prx` publishes a release with the `.deb` files and `SHA256SUMS`.

### Make targets

| Target | Purpose |
| --- | --- |
| `install` / `uninstall` | Install or remove `prx`, the C sources, the man page (and `pemu` if it was built). |
| `lint` | `bash -n`, `shellcheck` (if installed) and `-fsyntax-only` builds of the C sources. |
| `check` | `lint`, the smoke tests, and the emulator self-tests and fuzzers for the host architecture. |
| `pemu` | Build the built-in emulator for this host (`src/pemu.c` on x86_64, `src/pemu_x64.c` on aarch64). |
| `pemu-x64` | Build the x86_64-guest emulator on any host (on x86_64 only useful for testing). |
| `pemu-x64-jit` | Test build of the x86_64-guest JIT on a non-aarch64 machine (see below). |
| `clean` | Remove test temporaries and built emulators. |

## Quick start

    prx install debian rootfs.tar.gz --sha256 <HEX>   # or a URL instead of a file
    prx login debian                                  # shell as fake root
    prx login debian -- uname -a                      # run one command
    prx login debian --no-root-id -b /sdcard          # run as the host user, bind a host path
    prx list                                          # installed containers

Always pass `--sha256`: without it `prx install` prints a warning that the archive's integrity and authenticity are not verified.

See `man prx` or `prx help` for all commands and options.

## Commands

| Command | Purpose |
| --- | --- |
| `prx build` | Compile the shim and the loader patcher (and the built-in emulator for your host, if it compiles). |
| `prx install NAME FILE\|URL [--sha256 HEX]` | Extract a rootfs archive as container `NAME`; the checksum is verified before extraction. |
| `prx login NAME [options] [-- cmd]` | Start a shell, or run one command, inside the container. |
| `prx list` | List installed containers. |
| `prx fixperm NAME` | Repair permissions and flush the library-path cache. |
| `prx remove NAME` | Delete a container and its derived files. |
| `prx source`, `prx ldsource` | Print the C sources of the shim / loader patcher. |
| `prx --version` (`-V`, `version`) | Print the version. |
| `prx help` (`-h`, `--help`) | Print usage. |

Container names may only contain letters, digits, `.`, `_` and `-`, and cannot start with `-`.

`prx login` options:

| Option | Meaning |
| --- | --- |
| `-b, --bind PATH` / `HOST:GUEST` | Bind a host path into the guest (max 50 binds, each path < 1024 bytes, no `:` or `=`; `GUEST` must be absolute). |
| `-w, --workdir DIR` | Initial working directory inside the guest (default `/root`). |
| `-e, --env KEY=VAL` | Pass an environment variable. |
| `--no-root-id` | Run as the host user instead of fake root; `sudo` and `su` still work. |
| `--trace` | Debug syscall and seccomp tracing. |

Unknown options are an error, not a command; put the command after `--`.

## Environment

| Variable | Meaning |
| --- | --- |
| `PRX_HOME` | Data directory. Default: `~/.prx` if it exists, otherwise `$XDG_DATA_HOME/prx` (`~/.local/share/prx`). |
| `PRX_SRCDIR` | Directory holding `prx.c`, `ldpatch.c` and `shim/`. |
| `PRX_DNS` | IPv4 / IPv6 nameservers for the guest, separated by `,`, `;` or blanks. Invalid entries and scoped addresses (`fe80::1%eth0`) are dropped with a warning. |
| `PRX_QEMU` | Path (or name) of a specific `qemu-user` binary for cross-architecture rootfs. `PRX_QEMU=pemu` selects the built-in emulator from `$PRX_HOME/lib`. |
| `QEMU_CPU`, `QEMU_STRACE`, `QEMU_LOG`, `QEMU_RESERVED_VA`, `QEMU_GUEST_BASE`, `QEMU_PAGESIZE` | Passed through to the emulator when one is used. |
| `PRX_NO_ARGV0` | Set to `1` to never pass `--argv0` to the loader (detected automatically; glibc < 2.33 lacks it). |
| `PRX_KEEP` | Set to `1` to leave guest processes running after `prx login` exits (by default they are cleaned up). |
| `GLIBC_TUNABLES` | Defaults to `glibc.pthread.rseq=0` inside the guest unless you set it yourself. |
| `PEMU_JIT` | Set to `0` to disable the JIT of the built-in emulator (same as `pemu -nojit`). |
| `PEMU_JIT_STATS` | Set to `1` to print JIT block counts and the most frequent interpreter fallbacks at exit (`pemu_x64`). |
| `CC` | C compiler used to build the shim. |

## Layout

    prx            shell front-end (pure bash, passes `bash -n` / shellcheck)
    src/prx.c      shim entry point: includes the modules below, in order
    src/shim/      shim modules (one concern each, see below)
    src/ldpatch.c  glibc loader/libc patcher
    src/pemu.c     built-in emulator, aarch64 guest on x86_64 hosts
    src/pemu_x64.c built-in emulator, x86_64 guest on aarch64 hosts
    man/prx.1      manual page
    tests/         smoke.sh (offline smoke tests), shim_unit.c and shim_i386.c (shim tests),
                   pemu_test.py / pemu_fuzz.py (aarch64 emulator), pemu_x64_test.sh, pemu_x64/ and
                   pemu_x64_fuzz.py (x86_64 emulator)
    packaging/     build-deb.sh [debian|termux]
    examples/      debian: example Termux launcher script (PulseAudio + Termux:X11 + `prx login debian`)
    .github/       workflows/build-deb.yml: CI (make check), .deb packages and releases
    Makefile       install / uninstall / lint / check / pemu targets
    LICENSE        Apache License 2.0

### Shim modules

The shim is compiled as a single translation unit (`src/prx.c` includes the
modules in dependency order). `prx source` prints the modules expanded into one file.

| Module | Concern |
| --- | --- |
| `types.h`, `sysnum.h`, `sys.h`, `libc.h` | Types and constants, per-arch syscall numbers, raw syscalls, libc imports |
| `state.h`, `init.h` | Global state, bind parsing, SIGSYS handler, early init |
| `path.h`, `procfs.h` | Guest/host path translation, fake `/proc` files |
| `fs_open.h`, `fs_stat.h`, `fs_ops.h`, `tty.h`, `glob_tmp.h` | File-system interposers |
| `net_unix.h`, `resolve.h` | AF_UNIX path translation, DNS and `/etc/hosts` |
| `proc.h`, `cred.h`, `pwdb.h`, `exec.h` | Capabilities and signals, virtual credentials, passwd/group, exec and spawn |

## pemu: built-in emulator (experimental)

A small, self-written user-mode emulator that can stand in for `qemu-user` when running a rootfs of
the *other* architecture. It maps the guest 1:1 into the host address space and translates guest
syscalls to the host kernel (stat/open-flag/epoll layouts, `brk`, `fork`/`execve`, signal frames).
qemu stays the default; pemu is only used when you ask for it.

| host | guest rootfs | source | notes |
|------|--------------|--------|-------|
| x86_64  | aarch64 | `src/pemu.c`     | JIT + interpreter; integer ISA, scalar FP, tiny NEON subset; no threads |
| aarch64 | x86_64  | `src/pemu_x64.c` | JIT + interpreter; integer ISA, SSE2, x87 subset; threads; runs real glibc programs |

    prx build                       # also builds $PRX_HOME/lib/pemu for your host
    PRX_QEMU=pemu prx login debian
    make check                      # runs the emulator self-tests too

If pemu does not compile, `prx build` retries without `-pthread` and then with `-DPEMU_NO_JIT`; if all fail it prints the compiler errors and carries on without it.

### x86_64 guest on aarch64 (`pemu_x64`)

- Validated against native runs of static and dynamic glibc programs, bash, python3, perl, gcc and common coreutils (on an x86_64 host). The aarch64-host build is the same code using the generic syscall ABI, but has not been executed on a real aarch64 machine or on Android/Termux.
- Threads (`CLONE_THREAD`: pthreads, futex, `pthread_join`) work; every guest thread runs on its own host pthread.
- Without the JIT (`PEMU_JIT=0`, `-nojit`, or when the JIT cannot be built/enabled) it is a plain interpreter: expect it to be 50-1000x slower than native.
- On aarch64 hosts x87 long double is computed in IEEE quad (slightly more precise than real x87).

**JIT** (on by default on aarch64 hosts):

- Guest basic blocks are compiled to AArch64 code and chained; indirect branches go through an inline cache. A compare followed by `jcc`/`setcc`/`cmov` uses the host flags directly (`cmp` + `b.cond`), and the lazy flag state of the interpreter is only written when the flags can still be observed.
- Translated natively: moves (all sizes, `fs:` and rip-relative operands, `movzx/movsx/movsxd`, `lea`), `push/pop`, `call/ret/jmp/jcc` (direct and indirect), add/or/and/sub/xor/cmp/test, `neg/not`, shifts and `imul` when their flags are not needed, `setcc/cmovcc`, the `cdq` family, SSE data moves, `pxor/por/pand/pandn`, `movd/movq`.
- Everything else (x87, most SSE, string instructions, `div`, `adc/sbb`, `inc/dec`, ...) runs through the interpreter from inside the translated block, so behaviour is identical.
- Faults inside translated code report the exact guest `rip` and flags; pending signals are polled on backward and indirect branches.
- The cache is flushed when translated pages are unmapped, re-protected or `MAP_FIXED`-overwritten. Pages that are writable *and* executable are never translated (they are interpreted).
- The JIT switches itself off when the guest creates a second thread (the cache is not shared safely between threads yet) and falls back to the interpreter.
- Disable with `PEMU_JIT=0` or `pemu -nojit`; `PEMU_JIT_STATS=1` prints statistics. Use `PEMU_JIT=0` as the workaround when reporting problems.

**Testing the JIT without an aarch64 machine.** `make pemu-x64-jit` (x86_64 host) builds `pemu_x64` with `-DPEMU_JIT_EMBED`, linked with `pemu.c` built as an *AArch64 interpreter*: the JIT emits real AArch64 machine code and that code is executed by `pemu.c`, so the actual back end is exercised (slowly). `make check` runs the x86 self-tests (`tests/pemu_x64_test.sh`), plus `tests/pemu_x64_fuzz.py`, which generates random x86-64 programs (ALU at all sizes, moves, shifts, `imul`, `setcc/cmov`, push/pop, loops, calls, SSE, `fs:`/rip-relative/SIB operands) and compares registers, flags and memory between the interpreter and the JIT. Not covered by that setup, and therefore **not yet verified on real hardware**: instruction-cache maintenance (`__builtin___clear_cache`), writable+executable anonymous mappings (SELinux/Android policy), and the real signal context (`uc_mcontext.pc`).

### aarch64 guest on x86_64 (`pemu`)

- Block-translating **JIT**, on by default: guest basic blocks are compiled to x86_64 code, direct branches are chained, indirect branches go through an inline cache, and x86 flags are used directly (a compare followed by a branch/`csel` costs one `cmp` + `jcc`).
- Translated natively: integer ALU, bitfield, multiply/divide, conditional select/compare, branches, and scalar and vector (`ldr q`, `ldp q`) loads/stores. Everything else (FP arithmetic, NEON ALU, exclusives, ...) runs through the interpreter from inside the translated block, so behaviour is identical. Roughly 20x faster than the interpreter on integer / memory loops.
- The code cache is flushed when the guest unmaps, re-protects or `MAP_FIXED`-overwrites translated pages, and on `ic` instructions, so self-modifying code and `dlopen` work.
- Disable with `PEMU_JIT=0` or `pemu -nojit` (the JIT also switches itself off if an executable mapping can't be created). `tests/pemu_test.py` and `tests/pemu_fuzz.py` run self-tests and random programs under both engines and compare all registers, NZCV and memory.

### Not implemented

Threads in `pemu.c` (aarch64 guest), AVX/SSE3+/BMI (cpuid reports a baseline x86-64 CPU), most NEON/SVE/LSE for `pemu.c`, 32-bit guests. Not a security boundary. Use `qemu-user` for anything serious.

## Known limits (by design)

- Not a security boundary (LD_PRELOAD + loader patching; static/raw-syscall binaries can bypass it). This cannot be fixed within an LD_PRELOAD design; it would need a ptrace / seccomp-unotify supervisor or user namespaces.
- glibc rootfs only (no musl), on x86_64 or aarch64 hosts. A rootfs of the other architecture runs through `qemu-user` (or experimental pemu). An i386 (32-bit) rootfs is supported too, but only through `qemu-i386`; there is no native i386 mode.
- Guest PAM is `pam_permit` and sudo is `NOPASSWD` because there is no real root to authenticate. Do not treat the guest as a sandbox for untrusted code.
- Loader patching depends on glibc machine code and may fail on some versions.
- Bind paths cannot contain `:` or `=` (the shim's `PRX_BIND` format uses them as separators).

## Changelog

### 1.0.1

**Added**

- Built-in experimental emulator `pemu` (`PRX_QEMU=pemu`) as a `qemu-user` alternative: aarch64 guest on x86_64 hosts, x86_64 guest (with threads) on aarch64 hosts, both with a JIT and self-tests / fuzzers in `make check`.
- Debian and Termux `.deb` packaging (`packaging/build-deb.sh`) and a CI workflow that checks, builds and publishes releases.
- `prx install` warns when no `--sha256` is given.
- The shim is rebuilt when any `src/shim/*.h` module changes.

**Safer**

- Writing into a rootfs (`install`, `login`) no longer follows symlinks out of it, so a link like `/var/lib/dbus/machine-id -> /etc/machine-id` can't make prx touch a file on your host. The same fix covers user, group and hosts lookups, and `login -w DIR`.
- The patched loader is written to a temporary file and renamed, so two logins at the same time can't run a half-written one.

**Fixed**

- `--bind` silently ignored paths longer than 159 bytes. It now accepts up to 1023 bytes per path and 50 binds, and rejects anything invalid with a clear error.
- A mistyped option such as `prx login NAME --typo` is now an error instead of being run as a command.
- Rootfs with glibc older than 2.33 now start correctly (set `PRX_NO_ARGV0=1` to force this mode).
- `sched_getaffinity` reports real errors instead of always succeeding.
- `prx install` from a URL copes with `#fragments` and URLs that have no file name.
- The library cache refreshes after the guest installs or removes packages, and the shim is rebuilt when only `src/ldpatch.c` changes.
- `/etc/passwd`, `/etc/group` and `/etc/hosts` are read in full, and groups can have any number of members.
- DNS: IPv6 nameservers now work and can be mixed with IPv4 in `PRX_DNS`; invalid entries are dropped with a warning.

## License

Apache License 2.0, see [LICENSE](LICENSE).
