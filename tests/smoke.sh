#!/usr/bin/env bash
# Smoke tests: no root, no network, no real rootfs needed.
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PRX="$HERE/prx"
T="$HERE/tests/.tmp"
rm -rf "$T"; mkdir -p "$T/home"
export HOME="$T/home"
unset PRX_HOME XDG_DATA_HOME
pass=0
ok()   { pass=$((pass+1)); echo "ok   - $1"; }
fail() { echo "FAIL - $1" >&2; exit 1; }

bash -n "$PRX" && ok "bash -n prx"
[ "$("$PRX" --version)" = "prx $(sed -n 's/^PRX_VERSION=//p' "$PRX")" ] && ok "--version"
"$PRX" help | grep -q 'prx install NAME' && ok "help"
# prx.c #includes src/prx_*.h; 'prx source' must print it as one self-contained file
# that the compiler sees exactly as it sees src/prx.c
"$PRX" source > "$T/prx-flat.c"
grep -q '^#include "prx_' "$T/prx-flat.c" && fail "prx source left a header include unexpanded"
[ "$("${CC:-cc}" -E -P -x c "$T/prx-flat.c" | md5sum)" = "$("${CC:-cc}" -E -P -x c "$HERE/src/prx.c" | md5sum)" ] \
  && ok "prx source expands src/prx.c with its headers" || fail "prx source differs from src/prx.c after preprocessing"
"$PRX" ldsource | cmp -s - "$HERE/src/ldpatch.c" && ok "prx ldsource matches src/ldpatch.c"
"$PRX" supsource | cmp -s - "$HERE/src/prxsup.c" && ok "prx supsource matches src/prxsup.c"

# XDG default location (no ~/.prx present)
"$PRX" build >/dev/null 2>&1 || fail "prx build"
[ -f "$HOME/.local/share/prx/lib/libprx.so" ] && [ -x "$HOME/.local/share/prx/lib/ldpatch" ] && ok "build -> XDG dir"

# legacy dir is honoured when it already exists
mkdir "$HOME/.prx"
"$PRX" build >/dev/null 2>&1 || fail "prx build (legacy)"
[ -f "$HOME/.prx/lib/libprx.so" ] && ok "legacy ~/.prx used when it exists"
rm -rf "$HOME/.prx"

# installed layout: bin/prx finds ../share/prx/src
D="$T/dest"
make -s -C "$HERE" install DESTDIR="$D" PREFIX=/usr >/dev/null
"$D/usr/bin/prx" source | cmp -s - "$T/prx-flat.c" && ok "installed layout finds sources"
for h in "$HERE"/src/prx_*.h; do [ -f "$D/usr/share/prx/src/${h##*/}" ] || fail "make install did not install ${h##*/}"; done
ok "make install installs every src/prx_*.h header"
[ -f "$D/usr/share/man/man1/prx.1" ] && ok "manpage installed"

# plain http:// is refused without --sha256 (before any download is attempted)
if "$PRX" install h1 http://example.invalid/rootfs.tar.gz 2>"$T/err"; then fail "http:// without --sha256 accepted"; fi
grep -q 'plain http' "$T/err" && ok "refuses http:// without --sha256"
[ ! -e "$HOME/.local/share/prx/dl/rootfs.tar.gz" ] && ok "no download attempted for refused http://"

# remove: unknown option is an error; -y is accepted
if "$PRX" remove --bogus x 2>"$T/err"; then fail "remove accepted an unknown option"; fi
grep -q "Unknown option for 'remove'" "$T/err" && ok "remove rejects unknown options"

# name validation
if "$PRX" login ../evil 2>"$T/err"; then fail "path traversal accepted"; fi
grep -q 'Invalid container name' "$T/err" && ok "rejects invalid container name"

# install with checksum
mkdir -p "$T/fs/etc"; echo test > "$T/fs/etc/os-release"
tar -C "$T/fs" -czf "$T/fs.tar.gz" .
good="$(sha256sum "$T/fs.tar.gz" | cut -d' ' -f1)"
if "$PRX" install bad "$T/fs.tar.gz" --sha256 "$(printf '0%.0s' $(seq 64))" 2>"$T/err"; then fail "wrong checksum accepted"; fi
grep -q 'mismatch' "$T/err" && ok "wrong --sha256 is rejected"
[ ! -e "$HOME/.local/share/prx/rootfs/bad" ] && ok "nothing extracted on mismatch"
"$PRX" install t1 "$T/fs.tar.gz" --sha256 "$good" 2>&1 | grep -q 'SHA-256 verified' && ok "correct --sha256 accepted"
"$PRX" install t2 "$T/fs.tar.gz" 2>&1 | grep -q 'not verified' && ok "warns when no checksum given"
"$PRX" list | grep -qx t1 && ok "list"

# CPU/RAM limits were removed: the options and the 'set' command must be gone
if "$PRX" set t1 -c 1 2>/dev/null; then fail "'prx set' still exists"; fi
ok "'prx set' command removed"
for o in -c --cpus -m --memory --hard-mem; do
  if "$PRX" login t1 "$o" 1 2>/dev/null; then fail "login still accepts $o"; fi
done
ok "login rejects -c/--cpus, -m/--memory, --hard-mem"
# ---- 1.0.1 regression tests -------------------------------------------
# unknown option must not be run as a command
if "$PRX" login t1 --bogus 2>"$T/err"; then fail "unknown login option accepted"; fi
grep -q "Unknown option for 'login'" "$T/err" && ok "login rejects unknown option" || fail "login rejects unknown option"

# bind validation: too long / ambiguous ':'
long="/$(printf 'x%.0s' $(seq 1 1100))"
if "$PRX" login t1 -b "$T:$long" 2>"$T/err"; then fail "overlong bind accepted"; fi
grep -q 'too long' "$T/err" && ok "overlong --bind is rejected, not silently dropped" || fail "overlong --bind is rejected, not silently dropped"
if "$PRX" login t1 -b "$T:/a:/b" 2>"$T/err"; then fail "ambiguous bind accepted"; fi
grep -q "must not contain ':'" "$T/err" && ok "ambiguous ':' in --bind is rejected" || fail "ambiguous ':' in --bind is rejected"
# DNS list: valid IPv4/IPv6 survive, junk is dropped, '%' is never used as a printf format
PRX_DNS='1.2.3.4%s,5.6.7.8,2001:db8::1,999.1.1.1,fe80::1%eth0,[2606:4700:4700::1111],::ffff:1.2.3.4' "$PRX" install t4 "$T/fs.tar.gz" --sha256 "$good" >/dev/null 2>"$T/err"
rc4="$HOME/.local/share/prx/rootfs/t4/etc/resolv.conf"
want4="$(printf 'nameserver 5.6.7.8\nnameserver 2001:db8::1\nnameserver 2606:4700:4700::1111\nnameserver ::ffff:1.2.3.4')"
[ "$(cat "$rc4")" = "$want4" ] && ok "PRX_DNS keeps valid IPv4 and IPv6" || fail "PRX_DNS keeps valid IPv4 and IPv6"
grep -q "ignoring invalid DNS server '1.2.3.4%s'" "$T/err" && grep -q "ignoring invalid DNS server '999.1.1.1'" "$T/err" && ok "invalid DNS entries are reported" || fail "invalid DNS entries are reported"
grep -q 'zone addresses are not supported' "$T/err" && ok "scoped IPv6 DNS entry is reported" || fail "scoped IPv6 DNS entry is reported"
"$PRX" remove t4 >/dev/null 2>&1
# legacy ':'-separated IPv4 list still works
PRX_DNS='9.9.9.9:149.112.112.112' "$PRX" install t5 "$T/fs.tar.gz" --sha256 "$good" >/dev/null 2>&1
want5="$(printf 'nameserver 9.9.9.9\nnameserver 149.112.112.112')"
[ "$(cat "$HOME/.local/share/prx/rootfs/t5/etc/resolv.conf")" = "$want5" ] && ok "legacy colon-separated IPv4 list" || fail "legacy colon-separated IPv4 list"
"$PRX" remove t5 >/dev/null 2>&1

# shim: groups with >32 members, >48 KiB passwd/group, IPv6 literal parsing
mkdir -p "$T/shimroot/etc"
awk 'BEGIN {
  print "root:x:0:"
  for (i = 0; i < 3000; i++) printf "filler%d:x:%d:a,b\n", i, 2000 + i
  printf "big:x:5000:"
  for (i = 0; i < 300; i++) printf "%suser%d", (i ? "," : ""), i
  print ""
  print "last:x:5001:u1"
}' > "$T/shimroot/etc/group"
awk 'BEGIN {
  g = sprintf("%40s", ""); gsub(/ /, "G", g)
  for (i = 0; i < 2500; i++) printf "u%d:x:%d:%d:%s:/home/u%d:/bin/sh\n", i, 10000 + i, 10000 + i, g, i
  print "lastuser:x:99999:99999:Last:/home/last:/bin/bash"
}' > "$T/shimroot/etc/passwd"
if "${CC:-cc}" -w -o "$T/shim_unit" "$HERE/tests/shim_unit.c" 2>"$T/err" && "$T/shim_unit" "$T/shimroot" >/dev/null; then
  ok "shim: >32 group members, >48 KiB passwd/group, IPv6 literals"
else
  cat "$T/err" >&2; fail "shim: >32 group members, >48 KiB passwd/group, IPv6 literals"
fi

# rootfs symlinks must not redirect writes to the host
mkdir -p "$T/fs2/etc/pam.d" "$T/fs2/var/lib/dbus" "$T/decoy"
echo keep > "$T/decoy/machine-id"; echo keep > "$T/decoy/pam-sudo"
ln -s "$T/decoy/machine-id" "$T/fs2/var/lib/dbus/machine-id"
ln -s "$T/decoy/pam-sudo"   "$T/fs2/etc/pam.d/sudo"
tar -C "$T/fs2" -czf "$T/fs2.tar.gz" .
"$PRX" install t3 "$T/fs2.tar.gz" >/dev/null 2>&1 || fail "install t3"
"$PRX" login t3 >/dev/null 2>&1 || true      # stops at 'loader not found', after the writes

# editing any src/prx_*.h header must rebuild the shim; an untouched tree must not
S="$D/usr/share/prx/src"
PRX_SRCDIR="$S" "$PRX" login t3 >/dev/null 2>&1 || true        # bring the shim up to date
if PRX_SRCDIR="$S" "$PRX" login t3 2>&1 | grep -q 'Compiling path-translation shim'; then fail "shim rebuilt although no source changed"; fi
sleep 1; touch "$S/prx_dns.h"
PRX_SRCDIR="$S" "$PRX" login t3 2>&1 | grep -q 'Compiling path-translation shim' \
  && ok "editing a prx_*.h header triggers a shim rebuild" || fail "editing a prx_*.h header did not trigger a shim rebuild"
[ "$(cat "$T/decoy/machine-id")" = keep ] && [ "$(cat "$T/decoy/pam-sudo")" = keep ] && ok "rootfs symlinks cannot redirect writes to the host" || fail "rootfs symlinks cannot redirect writes to the host"
grep -q pam_permit "$HOME/.local/share/prx/rootfs/t3$T/decoy/pam-sudo" && ok "write landed inside the rootfs instead" || fail "write landed inside the rootfs instead"
"$PRX" remove t3 >/dev/null 2>&1

# shim: sched_getaffinity errors and long bind paths
SHIMSO="$HOME/.local/share/prx/lib/libprx.so"
CCB="$(command -v "${CC:-cc}" || command -v cc || command -v gcc || command -v clang || true)"
if [ -n "$CCB" ] && [ -f "$SHIMSO" ]; then
cat > "$T/aff.c" <<'CEOF'
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <errno.h>
int main(void) {
    cpu_set_t s; unsigned char b[1];
    int r = sched_getaffinity(999999, sizeof s, &s);
    printf("pid invalid: r=%d errno=%d\n", r, errno);
    errno = 0;
    r = sched_getaffinity(0, 1, (cpu_set_t *) b);
    printf("buf 1 byte: r=%d errno=%d\n", r, errno);
    return 0;
}
CEOF
"$CCB" -o "$T/aff" "$T/aff.c"
out="$(env -i PRX_ROOTFS="$T/fs" PRX_SHIM="$SHIMSO" LD_PRELOAD="$SHIMSO" "$T/aff")"
echo "$out" | grep -q 'pid invalid: r=-1 errno=3' && ok "sched_getaffinity reports ESRCH" || fail "sched_getaffinity reports ESRCH"
echo "$out" | grep -q 'buf 1 byte: r=-1 errno=22' && ok "sched_getaffinity reports EINVAL" || fail "sched_getaffinity reports EINVAL"
bd="$T/lb"; while [ "${#bd}" -lt 400 ]; do bd="$bd/$(printf 'b%.0s' $(seq 1 60))"; done
mkdir -p "$bd"; echo bound > "$bd/f"
[ "$(env -i PRX_ROOTFS="$T/fs" PRX_SHIM="$SHIMSO" LD_PRELOAD="$SHIMSO" PRX_BIND="$bd" "$(command -v cat)" "$bd/f" 2>/dev/null)" = bound ] && ok "bind path > 159 bytes works" || fail "bind path > 159 bytes works"
# passwd lookups follow rootfs symlinks inside the rootfs, never out of it
cat > "$T/pw.c" <<'CEOF'
#include <pwd.h>
#include <stdio.h>
int main(int c, char **v) {
    struct passwd *p = c > 1 ? getpwnam(v[1]) : 0;
    puts(p ? p->pw_name : "(null)");
    return 0;
}
CEOF
"$CCB" -o "$T/pw" "$T/pw.c"
mkdir -p "$T/fsl/etc" "$T/fsl2/etc" "$T/fsl2/data"
echo 'hostonly:x:4242:4242::/h:/bin/sh' > "$T/hostpw"
ln -s "$T/hostpw" "$T/fsl/etc/passwd"
echo 'insider:x:4343:4343::/h:/bin/sh' > "$T/fsl2/data/pw"
ln -s ../data/pw "$T/fsl2/etc/passwd"
[ "$(env -i PRX_ROOTFS="$T/fsl" PRX_SHIM="$SHIMSO" LD_PRELOAD="$SHIMSO" "$T/pw" hostonly)" = "(null)" ] && ok "absolute /etc/passwd symlink cannot reach host files" || fail "absolute /etc/passwd symlink cannot reach host files"
[ "$(env -i PRX_ROOTFS="$T/fsl2" PRX_SHIM="$SHIMSO" LD_PRELOAD="$SHIMSO" "$T/pw" insider)" = "insider" ] && ok "relative /etc/passwd symlink inside the rootfs works" || fail "relative /etc/passwd symlink inside the rootfs works"
fi
# ldpatch writes atomically: no temp file is left behind, also on failure
LDP="$HOME/.local/share/prx/lib/ldpatch"
if [ -x "$LDP" ]; then
cp "$(command -v cat)" "$T/ldin"
"$LDP" "$T/ldin" "$T/ldout" >/dev/null && [ -x "$T/ldout" ] && cmp -s "$T/ldin" "$T/ldout" && ok "ldpatch output written and executable" || fail "ldpatch output written and executable"
if "$LDP" "$T/ldin" "$T/nodir/out" 2>/dev/null; then fail "ldpatch ignored a write error"; fi
found=0
for f in "$T"/ldout.tmp.* "$T"/nodir; do
    if [ -e "$f" ]; then found=1; break; fi
done
if [ "$found" -eq 1 ]; then fail "ldpatch left a temp file"; else ok "ldpatch leaves no temp file behind"; fi
fi
# ------------------------------------------------------------------------

# ---- i386 guest: qemu-user only, never native ---------------------------
mkdir -p "$T/fs386/lib" "$T/fs386/bin" "$T/fs386/etc"
: > "$T/fs386/lib/ld-linux.so.2"; chmod +x "$T/fs386/lib/ld-linux.so.2"
printf '#!/bin/sh\n' > "$T/fs386/bin/sh"; chmod +x "$T/fs386/bin/sh"
tar -C "$T/fs386" -czf "$T/fs386.tar.gz" .
"$PRX" install i3 "$T/fs386.tar.gz" >/dev/null 2>&1
if PRX_QEMU=/nonexistent "$PRX" login i3 -- true 2>"$T/err"; then fail "i386 rootfs ran without qemu"; fi
grep -q 'is i386 but this host is .*qemu-i386 was not found' "$T/err" && ok "i386 rootfs requires qemu-i386 (no native mode)" || fail "i386 rootfs requires qemu-i386 (no native mode)"
cat > "$T/qstub" <<'QEOF'
#!/bin/sh
case "$*" in *--help*) echo "  --argv0 STRING"; exit 0;; esac
echo "ARGS: $*"
echo "SETENV: $QEMU_SET_ENV"
QEOF
chmod +x "$T/qstub"
if PRX_QEMU="$T/qstub" "$PRX" login i3 -- true >"$T/o386" 2>"$T/e386"; then
if grep -q 'Guest shim built' "$T/e386"; then
grep -q 'ld-linux.so.2 --library-path' "$T/o386" && grep -q 'i386-linux-gnu' "$T/o386" && ok "i386 guest launched through qemu with its own loader" || fail "i386 guest launched through qemu with its own loader"
grep -q 'SETENV: LD_PRELOAD=.*libprx-i386.so' "$T/o386" && ok "i386 shim preloaded via QEMU_SET_ENV" || fail "i386 shim preloaded via QEMU_SET_ENV"
if command -v file >/dev/null 2>&1; then file -L "$HOME/.local/share/prx/lib/libprx-i386.so" | grep -q 'ELF 32-bit.*80386' && ok "i386 shim is an ELF32 Intel 80386 object" || fail "i386 shim is an ELF32 Intel 80386 object"; fi
else ok "i386 shim build skipped (no 32-bit capable compiler)"; fi
else grep -q 'Cross-compiling the shim for i386 needs' "$T/e386" && ok "i386 shim build skipped (no 32-bit capable compiler)" || fail "i386 login with stub qemu"; fi
"$PRX" remove i3 >/dev/null 2>&1
# raw i386 syscall layer, run natively (needs gcc -m32 and a kernel that runs 32-bit binaries)
r386=skip
if gcc -m32 -O1 -static -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -no-pie -Wl,--unresolved-symbols=ignore-all -o "$T/shim_i386" "$HERE/tests/shim_i386.c" 2>/dev/null; then
r386="$("$T/shim_i386" 2>&1 </dev/null || true)"
fi
case "$r386" in
*"RESULT PASS"*) ok "i386 shim: syscalls, stat64/time64 layout, pread64, 64-bit div" ;;
*"RESULT FAIL"*|*FAIL*) echo "$r386" >&2; fail "i386 shim native test" ;;
*) ok "i386 shim native test skipped (no gcc -m32 or no 32-bit execution)" ;;
esac

"$PRX" remove t1 >/dev/null 2>&1 && "$PRX" remove t2 >/dev/null 2>&1
[ "$("$PRX" list)" = "(no rootfs installed yet)" ] || [ -z "$("$PRX" list | grep -E '^t[12]$')" ] && ok "remove"

# ---- --ptrace: path translation for static programs (tests/ptrace.sh) ------
if out="$(bash "$HERE/tests/ptrace.sh" 2>&1)"; then
while IFS= read -r l; do echo "$l"; pass=$((pass+1)); done <<<"$out"
else
echo "$out" >&2; fail "tests/ptrace.sh"
fi

rm -rf "$T"
echo "all $pass checks passed"
