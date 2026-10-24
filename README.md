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

- Not a security boundary (LD_PRELOAD + loader patching; static/raw-syscall binaries can bypass it).
- glibc rootfs only; x86_64 / aarch64 only.
- Guest PAM is `pam_permit` and sudo is `NOPASSWD` because there is no real root to authenticate.
- Loader patching depends on glibc machine code and may fail on some versions.
- Bind paths cannot contain `:` or `=` (the shim's `PRX_BIND` format uses them as separators).
- Only IPv4 DNS servers are supported for the guest.
- Groups with more than 32 members and `/etc/passwd` files larger than 48 KiB are truncated by the shim's passwd/group emulation.

## Packages

    packaging/build-deb.sh debian    # -> prx_<ver>_all.deb      (prefix /usr)
    packaging/build-deb.sh termux    # -> prx_<ver>_termux_all.deb
                                     #    (prefix /data/data/com.termux/files/usr,
                                     #     shebang rewritten; Termux has no /usr/bin/env)

Termux install: `apt install ./prx_<ver>_termux_all.deb` (or `dpkg -i`, then `apt -f install`).
