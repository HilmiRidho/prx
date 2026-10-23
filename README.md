# prx

**A single-file, rootless container and path-translation engine for Linux.**

`prx` lets you run a full glibc-based Linux root filesystem (Debian, Ubuntu, etc.) as a regular user, with no root, no `chroot`, no kernel namespaces, and no daemon. It works in user space by preloading a small C shim that translates filesystem paths and fakes the bits of the system a guest expects to see. It is similar in spirit to `proot`, and is aimed at environments where privileged containers are not an option (for example Termux on Android, shared hosts, or locked-down CI).

Everything lives in one Bash script. The C sources are embedded in the script itself and compiled on first use, so there is nothing else to download or install.

## Features

- **Rootless.** Runs entirely as an unprivileged user.
- **Single file.** One script, with the shim and loader-patch tool embedded and built locally.
- **Path translation.** The guest sees the rootfs as `/`, with correct handling of `..`, symlinks and `/proc/self/exe`.
- **Bind mounts.** Expose host paths inside the guest, at the same path or at a custom one.
- **Fake root identity.** The guest runs as `root` by default; `sudo` and `su` work out of the box. Use `--no-root-id` to keep your host user name, UID and GID instead.
- **Resource limits (guest view).** Limit the CPU count and RAM the guest sees. CPU limits are pinned with `taskset` when available; `--hard-mem` also enforces a hard memory cap with `ulimit -v`. Limits can be saved per container.
- **Cross-architecture.** x86_64 rootfs on aarch64 hosts, and aarch64 rootfs on x86_64 hosts, run automatically through `qemu-user`.
- **Housekeeping built in.** Automatic DNS detection, cleanup of leftover guest processes on exit, and permission repair.

## Requirements

- Linux on `x86_64` or `aarch64`
- Bash, `tar`, and a C compiler (`gcc` or `clang`)
- `curl`, only if you install a rootfs from a URL
- `taskset`, optional, for pinning CPUs
- `qemu-user`, only for cross-architecture rootfs (plus `clang` + `lld`, or a cross `gcc`, to build the guest shim)

Install hints:

```sh
# Termux
pkg install qemu-user-x86-64 qemu-user-aarch64 clang

# Debian / Ubuntu
sudo apt install qemu-user-static clang lld
```

## Quick start

```sh
chmod +x prx

# 1. Build the runtime shim and tools (also done automatically on first login)
./prx build

# 2. Install a glibc rootfs archive (local file or URL)
./prx install ubuntu ubuntu-rootfs.tar.gz

# 3. Enter the container
./prx login ubuntu
```

Run a single command instead of a shell:

```sh
./prx login ubuntu -- apt --version
```

## Usage

```
prx build                          Compile the C runtime shim and tools
prx install NAME FILE|URL          Install a glibc rootfs archive
prx login NAME [options] [-- cmd]  Run a shell or command inside the rootfs
prx set NAME [options]             Save default CPU/memory limits for a container
prx list                           List installed containers
prx fixperm NAME                   Fix file permissions and clear the library cache
prx remove NAME                    Remove a container completely
prx source | prx ldsource          Dump the embedded C sources
```

### `login` options

| Option | Description |
| --- | --- |
| `-b, --bind PATH` | Bind a host path at the identical path in the guest |
| `-b, --bind HOST:GUEST` | Bind a host path at a custom guest path |
| `-w, --workdir DIR` | Initial working directory (default: `/root`) |
| `-e, --env KEY=VAL` | Pass an environment variable |
| `-c, --cpus N` | Limit the CPU cores the guest sees (`0` = no limit) |
| `-m, --memory SIZE` | Limit the RAM the guest sees, e.g. `512M`, `2G` (`0` = no limit) |
| `--hard-mem` | With `--memory`, also enforce a hard cap (`ulimit -v`) |
| `--no-root-id` | Run as the host user instead of faking root |
| `--trace` | Enable debug syscall and seccomp tracing |

### Examples

```sh
# Share a project directory with the guest
./prx login ubuntu -b ~/project:/work -w /work

# Same path on host and guest
./prx login ubuntu -b /tmp/shared

# Limit to 2 CPUs and 1 GB RAM, with a hard memory cap
./prx login ubuntu -c 2 -m 1G --hard-mem

# Save those limits as the defaults for this container
./prx set ubuntu -c 2 -m 1G --hard-mem
./prx set ubuntu --show
```

## Configuration

| Variable | Purpose |
| --- | --- |
| `PRX_HOME` | Data directory (default: `~/.prx`) |
| `PRX_QEMU` | Path to a specific `qemu-user` binary |
| `QEMU_CPU` | CPU model passed to QEMU, e.g. `max` (other `QEMU_*` variables are passed through) |
| `CC` | Compiler used to build the shim |
| `PRX_DNS` | Colon-separated DNS servers to write into the guest |
| `PRX_KEEP` | Set to `1` to skip killing leftover guest processes on exit |

Containers live in `$PRX_HOME/rootfs/NAME`.

## How it works

`prx login` starts the guest's own dynamic loader and shell with `LD_PRELOAD` pointing at a small freestanding C library (`libprx.so`). The shim intercepts the libc calls that touch the filesystem, credentials, `/proc`, networking and process execution, rewrites paths so that the rootfs appears as `/`, and presents a consistent view of users, CPUs and memory to the guest. On aarch64 the loader and libc are additionally patched so that blocked syscalls fail cleanly.

## Limitations

- **Not a security sandbox.** `prx` runs with your own UID and provides path translation, not isolation.
- **glibc rootfs only.** musl-based distributions (e.g. Alpine) are not supported.
- **User-space interception.** Because it relies on `LD_PRELOAD`, programs that bypass libc (such as statically linked binaries) may not be translated.
- **No real mounts or namespaces.** `mount`, `chroot` and similar calls are emulated or refused, not performed.
- **Emulation is slow.** Cross-architecture rootfs run under QEMU user-mode emulation.
