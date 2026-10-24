# prx 1.0.1

User-space container / path-translation engine (glibc rootfs, no root needed).

## Install

    make install                    # PREFIX=/usr/local by default
    make install PREFIX=/usr DESTDIR=pkgroot   # for packagers
    make check                      # lint + smoke tests

The shim is compiled per user on the first `prx login` (needs `gcc` or `clang`).

## Layout

    prx            shell front-end (pure bash, passes `bash -n` / shellcheck)
    src/prx.c      LD_PRELOAD path-translation shim
    src/ldpatch.c  glibc loader/libc patcher
    man/prx.1      manual page
    tests/smoke.sh offline smoke tests
    packaging/     build-deb.sh [debian|termux]

## Unreleased

- DNS: the shim now queries IPv6 nameservers as well. `PRX_DNS` and the guest `resolv.conf` may mix IPv4 and IPv6 (the list is comma-separated; a legacy colon-separated IPv4 list still works). Scoped addresses (`fe80::1%eth0`) are not supported and are dropped with a warning. Host-detected servers are ordered IPv4 first.
- passwd/group emulation: `/etc/passwd`, `/etc/group` and `/etc/hosts` are read completely (no 48 KiB / 16 KiB cut-off), groups may have any number of members, and the buffers behind `getpwnam`/`getgrnam`-style calls grow on demand. The virtual supplementary-group limit is raised from 64 to 256.

- Loader patching: `ldpatch -v` reports how many sites it patched per syscall (`set_robust_list`, `rseq`, `clone3`), and the "pattern not found" warning now includes that breakdown. After the loader/libc is (re)patched, `prx login` runs a trivial program (`true`) through it once and warns if it dies with SIGSYS, i.e. the patch did not cover a syscall this device blocks. `libc.so.6` is no longer re-scanned on every login when nothing could be patched.

## Changes in 1.0.1

- Writes into a rootfs (`login`, `install`) no longer follow symlinks out of it: an absolute link such as `/var/lib/dbus/machine-id -> /etc/machine-id` used to make `cp`/`>` hit the *host* file (dangerous when run as root). All such writes now go through `rpath`, which resolves links as if the rootfs were `/`.
- `--bind`: paths longer than 159 bytes were silently ignored by the shim. The limit is now 1023 bytes per path, 50 binds, 16000 bytes in total, and `prx` rejects anything beyond that (and paths containing `:` or `=`) with an error.
- Shim: `sched_getaffinity` returned success on `EINVAL`/`ESRCH`; real errors are now reported (a seccomp-blocked syscall is still emulated).
- `wfile` used `printf "$@"` with data from `PRX_DNS`/`resolv.conf`; it now uses `%b`. DNS servers are validated: only IPv4 is accepted, anything else is dropped with a warning.
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

## Before you publish

- Pick a licence: replace `TODO-choose-a-license` in `prx`, add a `COPYING` file.
- `prx` is no longer a standalone single file: it needs `src/` next to it or in `../share/prx/src`.

## Known limits (by design)

- Not a security boundary (LD_PRELOAD + loader patching; static/raw-syscall binaries can bypass it). This cannot be fixed within an LD_PRELOAD design; it would need a ptrace / seccomp-unotify supervisor or user namespaces.
- glibc rootfs only; x86_64 / aarch64 only.
- Guest PAM is `pam_permit` and sudo is `NOPASSWD` because there is no real root to authenticate.
- Loader patching depends on glibc machine code and may fail on some versions (native aarch64 only; a failed or incomplete patch is now reported at login, but not fixed).
- Bind paths cannot contain `:` or `=` (the shim's `PRX_BIND` format uses them as separators).

## Packages

    packaging/build-deb.sh debian    # -> prx_<ver>_all.deb      (prefix /usr)
    packaging/build-deb.sh termux    # -> prx_<ver>_termux_all.deb
                                     #    (prefix /data/data/com.termux/files/usr,
                                     #     shebang rewritten; Termux has no /usr/bin/env)

Termux install: `apt install ./prx_<ver>_termux_all.deb` (or `dpkg -i`, then `apt -f install`).
