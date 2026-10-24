#!/usr/bin/env bash
# Build a .deb for Debian/Ubuntu (default) or Termux.
#   packaging/build-deb.sh [debian|termux] [outdir]
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
target="${1:-debian}"; out="${2:-$HERE}"
ver="$(sed -n 's/^PRX_VERSION=//p' "$HERE/prx")"
[ -n "$ver" ] || { echo "cannot read PRX_VERSION" >&2; exit 1; }
command -v dpkg-deb >/dev/null || { echo "dpkg-deb not found" >&2; exit 1; }

case "$target" in
debian)
  PREFIX=/usr
  DEPENDS='bash (>= 4.3), coreutils, tar, findutils, sed, grep, gcc | clang, libc6-dev | libc-dev'
  RECOMMENDS='curl, util-linux'
  SUGGESTS='qemu-user-static, lld'
  FILE="prx_${ver}_all.deb" ;;
termux)
  PREFIX=/data/data/com.termux/files/usr
  DEPENDS='bash, coreutils, tar, findutils, sed, grep, clang'
  RECOMMENDS='curl, util-linux'
  SUGGESTS='qemu-user-x86-64, qemu-user-aarch64, lld'
  FILE="prx_${ver}_termux_all.deb" ;;
*) echo "usage: $0 [debian|termux] [outdir]" >&2; exit 1 ;;
esac

W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
P="$W/pkg"; mkdir -p "$P/DEBIAN"
make -s -C "$HERE" install DESTDIR="$P" PREFIX="$PREFIX" >/dev/null
# Termux has no /usr/bin/env: point the shebang at the real bash
[ "$target" = termux ] && sed -i "1s|.*|#!$PREFIX/bin/bash|" "$P$PREFIX/bin/prx"
gzip -9n "$P$PREFIX/share/man/man1/prx.1"
D="$P$PREFIX/share/doc/prx"; mkdir -p "$D"
install -m644 "$HERE/README.md" "$D/README.md"
cat > "$D/copyright" <<'C'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: prx

Files: *
Copyright: TODO year and author
License: TODO
 The license has not been chosen yet. Replace this stanza (and the SPDX
 tag in the prx script) before distributing the package.
C
printf 'prx (%s) unstable; urgency=low\n\n  * Packaged.\n\n -- prx maintainers <root@localhost>  %s\n' \
  "$ver" "$(date -R)" | gzip -9n > "$D/changelog.Debian.gz"
cat > "$P/DEBIAN/control" <<C
Package: prx
Version: $ver
Section: utils
Priority: optional
Architecture: all
Depends: $DEPENDS
Recommends: $RECOMMENDS
Suggests: $SUGGESTS
Maintainer: prx maintainers <root@localhost>
Description: user-space container and path-translation engine
 prx runs a glibc-based Linux root filesystem as an unprivileged user,
 without root, namespaces or ptrace. Paths are translated by an LD_PRELOAD
 shim that is compiled per user on first use.
 .
 It is a convenience tool, not a security boundary. Only glibc root
 filesystems on x86_64 and aarch64 hosts are supported; foreign-architecture
 root filesystems run through qemu-user.
C
find "$P" -type d -exec chmod 755 {} +
dpkg-deb --root-owner-group -Zxz --build "$P" "$out/$FILE" >/dev/null
echo "$out/$FILE"
