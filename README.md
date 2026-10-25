# prx

User-space container / path-translation engine (glibc rootfs, no root needed).

`prx` runs a glibc-based Linux root filesystem as an unprivileged user, without root, namespaces or ptrace. Paths are translated by an `LD_PRELOAD` shim, and the guest's dynamic loader and libc are patched at run time.

## Requirements

- A host running Linux on x86_64 or aarch64 (including Termux on Android).
- `bash` 4.3 or newer, plus `coreutils`, `tar`, `findutils`, `sed` and `grep`.
- `gcc` or `clang`, used to compile the shim.
- Optional: `curl` (installing from a URL), `taskset` (pins `--cpus` to host CPUs), and `qemu-user` (plus `lld` for cross-building the shim) to run a rootfs of the other architecture (or an i386 rootfs, which is qemu-only).

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
    prx login debian -c 2 -m 2G -- uname -a           # limited CPUs/RAM, run one command
    prx login debian --no-root-id -b /sdcard          # run as the host user, bind a host path
    prx list                                          # installed containers

See `man prx` or `prx help` for all commands and options.

## Commands

| Command | Purpose |
| --- | --- |
| `prx build` | Compile the shim and the loader patcher. |
| `prx install NAME FILE\|URL [--sha256 HEX]` | Extract a rootfs archive as container `NAME`; the checksum is verified before extraction. |
| `prx login NAME [options] [-- cmd]` | Start a shell, or run one command, inside the container. |
| `prx set NAME [options]` | Save default CPU / memory limits for a container. |
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
| `-c, --cpus N` | CPU cores seen by the guest (`0` = no limit); pinned with `taskset` when available. |
| `-m, --memory SIZE` | RAM seen by the guest, e.g. `512M`, `2G` (`0` = no limit, minimum 16M). |
| `--hard-mem` | With `--memory`, also enforce a hard cap with `ulimit -v`. |
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
    src/prx.c      LD_PRELOAD path-translation shim
    src/ldpatch.c  glibc loader/libc patcher
    man/prx.1      manual page
    tests/         smoke.sh (offline smoke tests), shim_unit.c (shim unit test)
    packaging/     build-deb.sh [debian|termux]
    examples/      debian: example Termux launcher script (PulseAudio + Termux:X11 + `prx login debian`)
    Makefile       install / uninstall / lint / check
    LICENSE        Apache License 2.0

## Known limits (by design)

- Not a security boundary (LD_PRELOAD + loader patching; static/raw-syscall binaries can bypass it). This cannot be fixed within an LD_PRELOAD design; it would need a ptrace / seccomp-unotify supervisor or user namespaces.
- glibc rootfs only (no musl); x86_64 / aarch64 hosts only. A rootfs of the other architecture runs through qemu-user. An i386 (32-bit x86) rootfs is supported on both host types but only through `qemu-i386`: there is no native i386 mode, not even on x86_64. Programs built with 64-bit `time_t` on i386 (`_TIME_BITS=64`) are covered too: `__stat64_time64`, `__lstat64_time64`, `__fstat64_time64`, `__fstatat64_time64` and `__utimensat64` are hooked and use `statx`, so timestamps past 2038 are preserved.
- Guest PAM is `pam_permit` and sudo is `NOPASSWD` because there is no real root to authenticate. Do not treat the guest as a sandbox for untrusted code.
- Loader patching depends on glibc machine code and may fail on some versions.
- Bind paths cannot contain `:` or `=` (the shim's `PRX_BIND` format uses them as separators).

## Changelog

### 1.0.1

- Writes into a rootfs (`login`, `install`) no longer follow symlinks out of it: an absolute link such as `/var/lib/dbus/machine-id -> /etc/machine-id` used to make `cp`/`>` hit the *host* file (dangerous when run as root). All such writes now go through `rpath`, which resolves links as if the rootfs were `/`.
- `--bind`: paths longer than 159 bytes were silently ignored by the shim. The limit is now 1023 bytes per path, 50 binds, 16000 bytes in total, and `prx` rejects anything beyond that (and paths containing `:` or `=`) with an error.
- Shim: `sched_getaffinity` returned success on `EINVAL`/`ESRCH`; real errors are now reported (a seccomp-blocked syscall is still emulated).
- `wfile` used `printf "$@"` with data from `PRX_DNS`/`resolv.conf`; it now uses `%b`. DNS servers are validated: invalid entries are dropped with a warning (in 1.0.1 only IPv4 was accepted; IPv6 support is listed under *Unreleased*).
- Rootfs with glibc < 2.33 (no `ld.so --argv0`) are detected and started without it (`PRX_NO_ARGV0=1` forces this).
- `prx login NAME --typo` is now an error instead of running `--typo` as a command.
- `prx set NAME --hard-mem` alone is now saved (and warns that it needs `-m`).
- Minor: per-install `tar.err` temp file, `execveat(NULL)` returns `EFAULT`, group file buffer raised to 64 KiB.
- Performance: cleaning up guest processes when `prx login` exits no longer forks `tr`/`grep` for every process in `/proc`; the check is pure bash and several times faster on busy hosts (about 5x with a few hundred processes).
- Performance: `prx list` no longer forks once per container, and the shim reads `/etc/passwd`, `/etc/group` and `/etc/hosts` with a smaller initial buffer.
- Fix: the shim's `getpw*`, `getgr*` and `/etc/hosts` lookups followed rootfs symlinks out of the rootfs (an absolute `/etc/passwd` link read the *host* file). They now resolve links as if the rootfs were `/`.
- Fix: `prx login -w DIR` resolves `DIR` inside the rootfs, so a symlink cannot start the session in a host directory.
- Fix: `ldpatch` writes the patched loader to a temporary file and renames it, so two concurrent logins cannot run a half-written loader and a failed write leaves nothing behind.
- Fix: the shim is also rebuilt when only `src/ldpatch.c` changed, and the library-path cache is refreshed when the guest's package database (dpkg, rpm, pacman, apk) or, without one, its library directories change.
- Fix: `prx install` from a URL strips a `#fragment` from the download file name and falls back to a default name when the URL has none.
- DNS: the shim now queries IPv6 nameservers as well. `PRX_DNS` and the guest `resolv.conf` may mix IPv4 and IPv6 (the list is comma-separated; a legacy colon-separated IPv4 list still works). Scoped addresses (`fe80::1%eth0`) are not supported and are dropped with a warning. Host-detected servers are ordered IPv4 first.
- passwd/group emulation: `/etc/passwd`, `/etc/group` and `/etc/hosts` are read completely (no 48 KiB / 16 KiB cut-off), groups may have any number of members, and the buffers behind `getpwnam`/`getgrnam`-style calls grow on demand. The virtual supplementary-group limit is raised from 64 to 256.
- Docs: README reorganised; `PRX_KEEP` documented.

## License

Apache License 2.0, see [LICENSE](LICENSE).
