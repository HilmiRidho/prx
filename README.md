# prx

User-space container / path-translation engine (glibc rootfs, no root needed).

`prx` runs a glibc-based Linux root filesystem as an unprivileged user, without root, namespaces or ptrace. Paths are translated by an `LD_PRELOAD` shim, and the guest's dynamic loader and libc are patched at run time.

## Requirements

- A host running Linux on x86_64 or aarch64 (including Termux on Android).
- `bash` 4.3 or newer, plus `coreutils`, `tar`, `findutils`, `sed` and `grep`.
- `gcc` or `clang`, used to compile the shim.
- Optional: `curl` (installing from a URL), and `qemu-user` (plus `lld` for cross-building the shim) to run a rootfs of the other architecture (or an i386 rootfs, which is qemu-only).

## Install

    make install                    # PREFIX=/usr/local by default
    make install PREFIX=/usr DESTDIR=pkgroot   # for packagers
    make check                      # lint + smoke tests (needs a C compiler)

The shim is compiled per user on the first `prx login` (or explicitly with `prx build`).

## Quick start

    prx install debian rootfs.tar.gz --sha256 <HEX>   # or a URL instead of a file
    prx login debian                                  # shell as fake root
    prx login debian -c 2 -m 2G -- uname -a           # limited CPUs/RAM, run one command
    prx list                                          # installed containers

See `man prx` or `prx help` for all commands and options.

## Layout

    prx            shell front-end (pure bash, passes `bash -n` / shellcheck)
    src/prx.c      LD_PRELOAD path-translation shim
    src/ldpatch.c  glibc loader/libc patcher
    man/prx.1      manual page
    tests/         smoke.sh (offline smoke tests), shim_unit.c (shim unit test)
    packaging/     build-deb.sh [debian|termux]
    Makefile       install / uninstall / lint / check
    debian         example Termux launcher script (PulseAudio + Termux:X11 + `prx login debian`)
    prx-1.0.1-fixes.patch   patch that produced 1.0.1 from 1.0.0
    LICENSE        Apache License 2.0

## Unreleased

- DNS: the shim now queries IPv6 nameservers as well. `PRX_DNS` and the guest `resolv.conf` may mix IPv4 and IPv6 (the list is comma-separated; a legacy colon-separated IPv4 list still works). Scoped addresses (`fe80::1%eth0`) are not supported and are dropped with a warning. Host-detected servers are ordered IPv4 first.
- passwd/group emulation: `/etc/passwd`, `/etc/group` and `/etc/hosts` are read completely (no 48 KiB / 16 KiB cut-off), groups may have any number of members, and the buffers behind `getpwnam`/`getgrnam`-style calls grow on demand. The virtual supplementary-group limit is raised from 64 to 256.

## Changes in 1.0.1

- Writes into a rootfs (`login`, `install`) no longer follow symlinks out of it: an absolute link such as `/var/lib/dbus/machine-id -> /etc/machine-id` used to make `cp`/`>` hit the *host* file (dangerous when run as root). All such writes now go through `rpath`, which resolves links as if the rootfs were `/`.
- `--bind`: paths longer than 159 bytes were silently ignored by the shim. The limit is now 1023 bytes per path, 50 binds, 16000 bytes in total, and `prx` rejects anything beyond that (and paths containing `:` or `=`) with an error.
- Shim: `sched_getaffinity` returned success on `EINVAL`/`ESRCH`; real errors are now reported (a seccomp-blocked syscall is still emulated).
- `wfile` used `printf "$@"` with data from `PRX_DNS`/`resolv.conf`; it now uses `%b`. DNS servers are validated: invalid entries are dropped with a warning (in 1.0.1 only IPv4 was accepted; IPv6 support is listed under *Unreleased*).
- Rootfs with glibc < 2.33 (no `ld.so --argv0`) are detected and started without it (`PRX_NO_ARGV0=1` forces this).
- `prx login NAME --typo` is now an error instead of running `--typo` as a command.
- `prx set NAME --hard-mem` alone is now saved (and warns that it needs `-m`).
- Minor: per-install `tar.err` temp file, `execveat(NULL)` returns `EFAULT`, group file buffer raised to 64 KiB.

## Changes from the single-file version

- C sources moved out of the script into `src/` (`prx source` / `prx ldsource` still print them).
- Data dir follows XDG (`~/.local/share/prx`); an existing `~/.prx` is still used.
- `prx install NAME URL --sha256 HEX` verifies the archive before extracting; a warning is shown without it.
- Shim is rebuilt when `src/prx.c` changes, not only when the script changes.
- Added `--version`, manpage, Makefile (PREFIX/DESTDIR), tests.
- Runtime behaviour of `login`, `set`, `list`, `fixperm`, `remove` is unchanged.

## Known limits (by design)

- Not a security boundary (LD_PRELOAD + loader patching; static/raw-syscall binaries can bypass it). This cannot be fixed within an LD_PRELOAD design; it would need a ptrace / seccomp-unotify supervisor or user namespaces.
- glibc rootfs only (no musl); x86_64 / aarch64 hosts only. A rootfs of the other architecture runs through qemu-user. An i386 (32-bit x86) rootfs is supported on both host types but only through `qemu-i386`: there is no native i386 mode, not even on x86_64. Programs built with 64-bit `time_t` on i386 (`_TIME_BITS=64`) are covered too: `__stat64_time64`, `__lstat64_time64`, `__fstat64_time64`, `__fstatat64_time64` and `__utimensat64` are hooked and use `statx`, so timestamps past 2038 are preserved.
- Guest PAM is `pam_permit` and sudo is `NOPASSWD` because there is no real root to authenticate. Do not treat the guest as a sandbox for untrusted code.
- Loader patching depends on glibc machine code and may fail on some versions.
- Bind paths cannot contain `:` or `=` (the shim's `PRX_BIND` format uses them as separators).

## Packages

    packaging/build-deb.sh debian    # -> prx_<ver>_all.deb      (prefix /usr)
    packaging/build-deb.sh termux    # -> prx_<ver>_termux_all.deb
                                     #    (prefix /data/data/com.termux/files/usr,
                                     #     shebang rewritten; Termux has no /usr/bin/env)

Termux install: `apt install ./prx_<ver>_termux_all.deb` (or `dpkg -i`, then `apt -f install`).

## License

Apache License 2.0, see [LICENSE](LICENSE).
