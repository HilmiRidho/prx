# prx

User-space container / path-translation engine (glibc rootfs, no root needed).

`prx` runs a glibc-based Linux root filesystem as an unprivileged user, without root, namespaces or ptrace. Paths are translated by an `LD_PRELOAD` shim, and the guest's dynamic loader and libc are patched at run time.

Current version: **1.0.1** (see [Changelog](#changelog)).

## Requirements

- Linux on x86_64 or aarch64 (including Termux on Android).
- `bash` 4.3+, `coreutils`, `tar`, `findutils`, `sed`, `grep`.
- `gcc` or `clang` (compiles the shim).
- Optional:
  - `curl`: install from a URL.
  - `xz`, `zstd`, `bzip2`: extract rootfs archives in those formats.
  - `qemu-user`: run a rootfs of the other architecture (or i386), or use the built-in [pemu](#pemu-built-in-emulator-experimental).

## Installation

### Option 1: `.deb` package

Download the package from the releases page, or build it yourself:

    packaging/build-deb.sh debian    # -> prx_<ver>_all.deb        (Debian/Ubuntu, prefix /usr)
    packaging/build-deb.sh termux    # -> prx_<ver>_termux_all.deb (Termux)

Then install it:

    sudo apt install ./prx_<ver>_all.deb          # Debian / Ubuntu
    apt install ./prx_<ver>_termux_all.deb        # Termux

### Option 2: Manual (from source)

    make install                                  # PREFIX=/usr/local by default
    make install PREFIX=/usr DESTDIR=pkgroot      # for packagers
    make uninstall                                # same PREFIX / DESTDIR
    make check                                    # lint + smoke tests + emulator self-tests

The shim is compiled per user on the first `prx login` (or with `prx build`) and rebuilt automatically when its sources change.

## Quick start

    prx install debian rootfs.tar.gz --sha256 <HEX>   # or a URL instead of a file
    prx login debian                                  # shell as fake root
    prx login debian -- uname -a                      # run one command
    prx login debian --no-root-id -b /sdcard          # run as host user, bind a host path
    prx list                                          # installed containers

Always pass `--sha256`; without it the archive's integrity is not verified. See `man prx` or `prx help` for everything else.

## Commands

| Command | Purpose |
| --- | --- |
| `prx build` | Compile the shim, loader patcher and (if possible) the built-in emulator. |
| `prx install NAME FILE\|URL [--sha256 HEX]` | Extract a rootfs archive as container `NAME`. |
| `prx login NAME [options] [-- cmd]` | Start a shell, or run one command, in the container. |
| `prx list` | List installed containers. |
| `prx fixperm NAME` | Repair permissions and flush the library-path cache. |
| `prx remove NAME` | Delete a container. |
| `prx --version` / `prx help` | Print version / usage. |

`prx login` options:

| Option | Meaning |
| --- | --- |
| `-b, --bind PATH` / `HOST:GUEST` | Bind a host path into the guest (max 50; no `:` or `=` in paths). |
| `-w, --workdir DIR` | Initial working directory (default `/root`). |
| `-e, --env KEY=VAL` | Pass an environment variable. |
| `--no-root-id` | Run as the host user instead of fake root. |
| `--no-seccomp` | Skip the host-seccomp workarounds. |
| `--trace` | Debug syscall and seccomp tracing. |

## Environment

| Variable | Meaning |
| --- | --- |
| `PRX_HOME` | Data directory (default `~/.local/share/prx`). |
| `PRX_SRCDIR` | Directory with `prx.c`, `ldpatch.c` and `shim/`. |
| `PRX_SECCOMP` | `0` disables the seccomp workarounds (same as `--no-seccomp`). |
| `PRX_SHIM_CFLAGS` | Extra shim compiler flags; run `prx build` afterwards. |
| `PRX_DNS` | Guest nameservers (IPv4/IPv6), separated by `,`, `;` or blanks. |
| `PRX_QEMU` | Specific `qemu-user` binary; `PRX_QEMU=pemu` selects the built-in emulator. |
| `PRX_NO_ARGV0` | `1` never passes `--argv0` to the loader (auto-detected for glibc < 2.33). |
| `PRX_KEEP` | `1` leaves guest processes running after `prx login` exits. |
| `PEMU_JIT` | `0` disables the pemu JIT (pass with `-e PEMU_JIT=0`). |
| `CC` | C compiler used to build the shim. |

## Layout

    prx            shell front-end (pure bash)
    src/prx.c      shim entry point (includes src/shim/*.h)
    src/ldpatch.c  glibc loader/libc patcher
    src/pemu.c     built-in emulator: aarch64 guest on x86_64 hosts
    src/pemu_x64.c built-in emulator: x86_64 guest on aarch64 hosts
    man/prx.1      manual page
    tests/         smoke tests, shim tests, emulator self-tests and fuzzers
    packaging/     build-deb.sh [debian|termux]
    examples/      example Termux launcher script
    .github/       CI workflow (make check, .deb build, releases)
    Makefile       install / uninstall / lint / check / pemu targets

## pemu: built-in emulator (experimental)

A small user-mode emulator that can stand in for `qemu-user` when running a rootfs of the *other* architecture. qemu stays the default; pemu is used only when requested.

| Host | Guest rootfs | Source | Notes |
| --- | --- | --- | --- |
| x86_64 | aarch64 | `src/pemu.c` | JIT + interpreter; integer ISA, scalar FP, small NEON subset; no threads |
| aarch64 | x86_64 | `src/pemu_x64.c` | JIT + interpreter; integer ISA, MMX, SSE2 to SSE4.2 (no AVX), x87 subset; threads |

    prx build                       # also builds $PRX_HOME/lib/pemu
    PRX_QEMU=pemu prx login debian

- The aarch64-host build has not been run on real aarch64 hardware or Android/Termux.
- Without the JIT (`PEMU_JIT=0`) it is a plain interpreter, 50-1000x slower than native.
- Not implemented: threads in `pemu.c`, AVX/AVX2/FMA/BMI/AES, most NEON/SVE, 32-bit guests.
- Not a security boundary; use `qemu-user` for anything serious.

## Known limits

- Not a security boundary: LD_PRELOAD + loader patching can be bypassed by static or raw-syscall binaries.
- glibc rootfs only (no musl). A rootfs of the other architecture needs `qemu-user` (or pemu); i386 needs `qemu-i386`.
- Guest PAM is `pam_permit` and `sudo` is `NOPASSWD`. Do not run untrusted code in it.
- Loader patching depends on glibc machine code and may fail on some versions.

## Changelog

### 1.0.1

- **Added:** experimental emulator `pemu`; Debian and Termux `.deb` packaging with a CI workflow; a warning when `prx install` has no `--sha256`.
- **Safer:** writing into a rootfs no longer follows symlinks out of it; the patched loader is written atomically.
- **Fixed:** `--bind` path limits and validation; mistyped options are now errors; glibc < 2.33 rootfs; `sched_getaffinity` errors; URL installs with `#fragments`; library cache refresh; full reading of `/etc/passwd`, `/etc/group`, `/etc/hosts`; IPv6 nameservers in `PRX_DNS`.

## License

Apache License 2.0, see [LICENSE](LICENSE).
