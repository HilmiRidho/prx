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
    src/prx.c      LD_PRELOAD path-translation shim
    src/ldpatch.c  glibc loader/libc patcher
    man/prx.1      manual page
    tests/         smoke.sh (offline smoke tests), shim_unit.c and shim_i386.c (shim tests)
    packaging/     build-deb.sh [debian|termux]
    examples/      debian: example Termux launcher script (PulseAudio + Termux:X11 + `prx login debian`)
    Makefile       install / uninstall / lint / check
    LICENSE        Apache License 2.0

## Known limits (by design)

- Not a security boundary (LD_PRELOAD + loader patching; static/raw-syscall binaries can bypass it). This cannot be fixed within an LD_PRELOAD design; it would need a ptrace / seccomp-unotify supervisor or user namespaces.
- glibc rootfs only (no musl), on x86_64 or aarch64 hosts. A rootfs of the other architecture runs through `qemu-user`. An i386 (32-bit) rootfs is supported too, but only through `qemu-i386`; there is no native i386 mode.
- Guest PAM is `pam_permit` and sudo is `NOPASSWD` because there is no real root to authenticate. Do not treat the guest as a sandbox for untrusted code.
- Loader patching depends on glibc machine code and may fail on some versions.
- Bind paths cannot contain `:` or `=` (the shim's `PRX_BIND` format uses them as separators).
- There are no CPU or RAM limits: the guest sees the host's real CPUs and memory.

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

**Faster**

- Cleaning up guest processes when `prx login` exits is about 5x faster on a busy host, and `prx list` is quicker too.

**Removed**

- CPU and RAM limits (`-c`, `-m`, `--hard-mem` and `prx set`).

## License

Apache License 2.0, see [LICENSE](LICENSE).
