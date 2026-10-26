# prx

User-space container / path-translation engine (glibc rootfs, no root needed).

`prx` runs a glibc-based Linux root filesystem as an unprivileged user, without root or namespaces, and by default without ptrace. Paths are translated by an `LD_PRELOAD` shim, and the guest's dynamic loader and libc are patched at run time. Static binaries, which the shim cannot see, can optionally be handled by a small ptrace helper (`--ptrace`, see below).

## Requirements

- A host running Linux on x86_64 or aarch64 (including Termux on Android).
- `bash` 4.3 or newer, plus `coreutils`, `tar`, `findutils`, `sed` and `grep`.
- `gcc` or `clang`, used to compile the shim.
- Optional: ptrace and seccomp-bpf permitted on the host, for `prx login --ptrace` (Linux 3.8+; `kernel.yama.ptrace_scope` 0 or 1).
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
| `prx build` | Compile the shim, the loader patcher and the ptrace helper. |
| `prx install NAME FILE\|URL [--sha256 HEX]` | Extract a rootfs archive as container `NAME`; the checksum is verified before extraction. |
| `prx login NAME [options] [-- cmd]` | Start a shell, or run one command, inside the container. |
| `prx list` | List installed containers. |
| `prx fixperm NAME` | Repair permissions and flush the library-path cache. |
| `prx remove NAME` | Delete a container and its derived files. |
| `prx source`, `prx ldsource`, `prx supsource` | Print the C sources of the shim / loader patcher / ptrace helper. |
| `prx --version` | Print the version. |

`prx login` options:

| Option | Meaning |
| --- | --- |
| `-b, --bind PATH` / `HOST:GUEST` | Bind a host path into the guest (max 50 binds, each path < 1024 bytes, no `:` or `=`). |
| `-w, --workdir DIR` | Initial working directory inside the guest (default `/root`). |
| `-e, --env KEY=VAL` | Pass an environment variable. |
| `--no-root-id` | Run as the host user instead of fake root; `sudo` and `su` still work. |
| `--trace` | Debug syscall and seccomp tracing. |
| `--ptrace` | Also translate paths for static and raw-syscall programs (see below). Default off; `PRX_PTRACE=1` turns it on for every login, `--no-ptrace` turns it off again. |

## Environment

| Variable | Meaning |
| --- | --- |
| `PRX_HOME` | Data directory. Default: `~/.prx` if it exists, otherwise `$XDG_DATA_HOME/prx` (`~/.local/share/prx`). |
| `PRX_SRCDIR` | Directory holding `prx.c`, `ldpatch.c` and `prxsup.c`. |
| `PRX_PTRACE` | Set to `1` to behave as if `--ptrace` were given. |
| `PRX_DNS` | IPv4 / IPv6 nameservers for the guest, separated by `,`, `;` or blanks. |
| `PRX_QEMU` | Path to a specific `qemu-user` binary for cross-architecture rootfs. |
| `PRX_NO_ARGV0` | Set to `1` to never pass `--argv0` to the loader (detected automatically; glibc < 2.33 lacks it). |
| `PRX_KEEP` | Set to `1` to leave guest processes running after `prx login` exits (by default they are cleaned up). |
| `CC` | C compiler used to build the shim. |

## Layout

    prx            shell front-end (pure bash, passes `bash -n` / shellcheck)
    src/prx.c      LD_PRELOAD path-translation shim
    src/ldpatch.c  glibc loader/libc patcher
    src/prxsup.c   optional ptrace helper for static programs (`--ptrace`)
    man/prx.1      manual page
    tests/         smoke.sh (offline smoke tests), ptrace.sh (the `--ptrace` tests), shim_unit.c, shim_i386.c, xlate_cmp.c, static_probe.c, rawsys.c
    packaging/     build-deb.sh [debian|termux]
    examples/      debian: example Termux launcher script (PulseAudio + Termux:X11 + `prx login debian`)
    Makefile       install / uninstall / lint / check
    LICENSE        Apache License 2.0

## Static programs (`--ptrace`)

The shim lives inside the guest's libc, so a program that never loads it (a static binary such as a Go program or busybox-static, or any raw `syscall` instruction) reaches the host filesystem directly. `prx login NAME --ptrace` closes that gap for programs that run on the host's architecture:

1. When the shim is about to exec a static ELF, it starts it through `prxsup run`, which keeps the pid. `prxsup` forks a tracer, lets it `PTRACE_SEIZE` the process, installs a seccomp-bpf filter that stops only the path-taking syscalls (`openat`, `stat*`, `readlink*`, `chdir`, `mkdir*`, `rename*`, `unlink*`, `execve`, ... about 50 in all) and execs the program.
2. The tracer follows the whole process tree below it and rewrites the path arguments with the same rules as the shim (same root, binds and symlink handling; `tests/ptrace.sh` checks 500+ paths for equality). `getcwd` and `readlink` results are mapped back to guest paths.
3. Everything else runs at native speed. A dynamic program, or a static one started by a dynamic program, is not slowed down unless static code runs above it: a dynamic program exec'ed by a traced static one is handed back to the shim (the tracer runs the guest shell under the loader with `PRX_DELEGATE=1`, and the shim's constructor performs the real `execve`), so shebangs, the loader and credentials work as before.

Measured on x86_64: about 17 µs per trapped syscall and 40 ms to start a static program; dynamic programs pay nothing.

What it does **not** do: it is not a security boundary (a tracee's own threads can still race a path between the check and the use); static programs get no fake uid/gid, no `/proc` fakes and no `AF_UNIX` path translation; a program that uses ptrace itself (gdb, strace, delve) cannot run inside the traced tree; `qemu-user` (cross-architecture and i386) rootfs are not supported; 32-bit compat syscalls are not intercepted. If ptrace or seccomp is unavailable, `prx login --ptrace` says so once and carries on without it. The aarch64 code path follows the kernel's documented ABI but has so far only been compiled and exercised on x86_64.

## Known limits (by design)

- Not a security boundary (LD_PRELOAD + loader patching). Without `--ptrace`, static and raw-syscall binaries bypass the translation; with it they are translated, but ptrace cannot make a process boundary out of path rewriting (see above). Real isolation needs user namespaces.
- glibc rootfs only (no musl), on x86_64 or aarch64 hosts. A rootfs of the other architecture runs through `qemu-user`. An i386 (32-bit) rootfs is supported too, but only through `qemu-i386`; there is no native i386 mode.
- Guest PAM is `pam_permit` and sudo is `NOPASSWD` because there is no real root to authenticate. Do not treat the guest as a sandbox for untrusted code.
- Loader patching depends on glibc machine code and may fail on some versions.
- Bind paths cannot contain `:` or `=` (the shim's `PRX_BIND` format uses them as separators).

## Changelog

### 1.0.1

**New**

- `prx login --ptrace` (or `PRX_PTRACE=1`): static and raw-syscall programs are path-translated by an optional ptrace + seccomp helper, `prxsup` (`src/prxsup.c`, built by `prx build`, `prx supsource` prints it). Off by default; dynamic programs are never slowed down. See "Static programs".
- `tests/ptrace.sh`: end-to-end tests on a rootfs assembled from the host's own binaries, plus a shim/supervisor path-translation parity test.

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
