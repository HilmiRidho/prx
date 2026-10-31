# prx

User-space container / path-translation engine (glibc rootfs, no root needed).

`prx` runs a glibc-based Linux root filesystem as an unprivileged user, without root, namespaces or ptrace. Paths are translated by an `LD_PRELOAD` shim, and the guest's dynamic loader and libc are patched at run time.

## Requirements

- A host running Linux on x86_64 or aarch64 (including Termux on Android).
- `bash` 4.3 or newer, plus `coreutils`, `tar`, `findutils`, `sed` and `grep`.
- `gcc` or `clang`, used to compile the shim.
- Optional: `curl` (installing from a URL) and `qemu-user` (plus `lld` for cross-building the shim) to run a rootfs of the other architecture (or an i386 rootfs, which is qemu-only).

## Install

    make install                               # PREFIX=/usr/local by default
    make install PREFIX=/usr DESTDIR=pkgroot   # for packagers
    make check                                 # lint + smoke tests (needs a C compiler)

The shim is compiled per user on the first `prx login` (or explicitly with `prx build`). It is rebuilt automatically when `prx`, `src/prx.c` or `src/ldpatch.c` change.

Prebuilt packages:

    packaging/build-deb.sh debian    # -> prx_<ver>_all.deb      (prefix /usr)
    packaging/build-deb.sh termux    # -> prx_<ver>_termux_all.deb
                                     #    (prefix /data/data/com.termux/files/usr,
                                     #     shebang rewritten; Termux has no /usr/bin/env)

Termux install: `apt install ./prx_<ver>_termux_all.deb` (or `dpkg -i`, then `apt -f install`).

## Quick start

    prx install debian rootfs.tar.gz --sha256 <HEX>   # or a URL instead of a file
    prx login debian                                  # shell as fake root
    prx login debian -- uname -a                      # run one command
    prx login debian --no-root-id -b /sdcard          # run as the host user, bind a host path
    prx list                                          # installed containers

See `man prx` or `prx help` for all commands and options.

## Commands

| Command | Purpose |
| --- | --- |
| `prx build` | Compile the shim and the loader patcher. |
| `prx install NAME FILE\|URL [--sha256 HEX]` | Extract a rootfs archive as container `NAME`; the checksum is verified before extraction. |
| `prx login NAME [options] [-- cmd]` | Start a shell, or run one command, inside the container. |
| `prx list` | List installed containers. |
| `prx fixperm NAME` | Repair permissions and flush the library-path cache. |
| `prx remove NAME` | Delete a container and its derived files. |
| `prx source`, `prx ldsource` | Print the C sources of the shim / loader patcher. |
| `prx --version` | Print the version. |

`prx login` options:

| Option | Meaning |
| --- | --- |
| `-b, --bind PATH` / `HOST:GUEST` | Bind a host path into the guest (max 50 binds, each path < 1024 bytes, no `:` or `=`). |
| `-w, --workdir DIR` | Initial working directory inside the guest (default `/root`). |
| `-e, --env KEY=VAL` | Pass an environment variable. |
| `--no-root-id` | Run as the host user instead of fake root; `sudo` and `su` still work. |
| `--trace` | Debug syscall and seccomp tracing. |

## Environment

| Variable | Meaning |
| --- | --- |
| `PRX_HOME` | Data directory. Default: `~/.prx` if it exists, otherwise `$XDG_DATA_HOME/prx` (`~/.local/share/prx`). |
| `PRX_SRCDIR` | Directory holding `prx.c` and `ldpatch.c`. |
| `PRX_DNS` | IPv4 / IPv6 nameservers for the guest, separated by `,`, `;` or blanks. |
| `PRX_QEMU` | Path to a specific `qemu-user` binary for cross-architecture rootfs. |
| `PRX_NO_ARGV0` | Set to `1` to never pass `--argv0` to the loader (detected automatically; glibc < 2.33 lacks it). |
| `PRX_KEEP` | Set to `1` to leave guest processes running after `prx login` exits (by default they are cleaned up). |
| `CC` | C compiler used to build the shim. |

## Layout

    prx            shell front-end (pure bash, passes `bash -n` / shellcheck)
    src/prx.c      shim entry point: includes the modules below, in order
    src/shim/      shim modules (one concern each, see below)
    src/ldpatch.c  glibc loader/libc patcher
    man/prx.1      manual page
    tests/         smoke.sh (offline smoke tests), shim_unit.c and shim_i386.c (shim tests)
    packaging/     build-deb.sh [debian|termux]
    examples/      debian: example Termux launcher script (PulseAudio + Termux:X11 + `prx login debian`)
    Makefile       install / uninstall / lint / check
    LICENSE        Apache License 2.0

### Shim modules

The shim is still compiled as a single translation unit (`src/prx.c` includes the
modules in dependency order), so the build, the static helpers and the tests work
as before. `prx source` prints the modules expanded into one file.

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
| x86_64  | aarch64 | `src/pemu.c`     | JIT + interpreter; integer ISA, scalar FP, tiny NEON subset |
| aarch64 | x86_64  | `src/pemu_x64.c` | integer ISA, SSE2, x87 subset; runs real glibc programs |

    prx build                       # also builds $PRX_HOME/lib/pemu for your host
    PRX_QEMU=pemu prx login debian
    make check                      # runs the emulator self-tests too

`pemu_x64` was validated against native runs of static and dynamic glibc programs, bash, python3,
perl, gcc and common coreutils (on an x86_64 host); the aarch64-host build is the same code using the
generic syscall ABI, but has not been executed on a real aarch64 machine or on Android/Termux.
`pemu_x64` is an interpreter (no JIT): expect it to be 50-1000x slower than native. Arithmetic flags are evaluated lazily, which makes it a little faster, but it is still far slower than `qemu-user` (a JIT).

`pemu` (aarch64 guest on an x86_64 host) has a block-translating **JIT** that is on by default: guest basic blocks are compiled to x86_64 code, direct branches are chained, indirect branches go through an inline cache, and x86 flags are used directly (a compare followed by a branch/`csel` costs one `cmp` + `jcc`). Integer ALU, bitfield, multiply/divide, conditional select/compare, branches, scalar and vector (`ldr q`, `ldp q`) loads/stores are translated natively; everything else (FP arithmetic, NEON ALU, exclusives, ...) runs through the interpreter from inside the translated block, so behaviour is identical. Roughly 20x faster than the interpreter on integer / memory loops. The code cache is flushed when the guest unmaps, re-protects or `MAP_FIXED`-overwrites translated pages, and on `ic` instructions, so self-modifying code and `dlopen` work. Disable it with `PEMU_JIT=0` or `pemu -nojit` (the JIT also switches itself off if an executable mapping can't be created). `tests/pemu_fuzz.py` runs random programs under both engines and compares all registers, NZCV and memory.

Threads (`CLONE_THREAD`, so pthreads / futex / `pthread_join`) work in `pemu_x64`: every guest thread runs on its own host pthread.

Not implemented: threads in `pemu.c` (aarch64 guest), AVX/SSE3+/BMI (cpuid reports a baseline x86-64 CPU), most
NEON/SVE/LSE for `pemu.c`, 32-bit guests. On aarch64 hosts x87 long double is computed in IEEE quad
(slightly more precise than real x87). Not a security boundary. Use `qemu-user` for anything serious.

## Known limits (by design)

- Not a security boundary (LD_PRELOAD + loader patching; static/raw-syscall binaries can bypass it). This cannot be fixed within an LD_PRELOAD design; it would need a ptrace / seccomp-unotify supervisor or user namespaces.
- glibc rootfs only (no musl), on x86_64 or aarch64 hosts. A rootfs of the other architecture runs through `qemu-user`. An i386 (32-bit) rootfs is supported too, but only through `qemu-i386`; there is no native i386 mode.
- Guest PAM is `pam_permit` and sudo is `NOPASSWD` because there is no real root to authenticate. Do not treat the guest as a sandbox for untrusted code.
- Loader patching depends on glibc machine code and may fail on some versions.
- Bind paths cannot contain `:` or `=` (the shim's `PRX_BIND` format uses them as separators).

## Changelog

### 1.0.1

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
