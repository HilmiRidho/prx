#!/usr/bin/env bash
# Tests for `prx login --ptrace` (src/prxsup.c). Needs no root, no network and no
# real rootfs: a tiny glibc rootfs is assembled from the host's own binaries.
# Prints "ok   - ..." per check; every check that cannot run here says so.
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PRX="$HERE/prx"
T="$HERE/tests/.tmp-ptrace"
rm -rf "$T"; mkdir -p "$T/home"
trap 'rm -rf "$T"' EXIT
export HOME="$T/home"
unset PRX_HOME XDG_DATA_HOME PRX_PTRACE PRX_TRACE
ok()   { echo "ok   - $1"; }
skip() { echo "ok   - $1 (skipped: $2)"; exit 0; }
fail() { echo "FAIL - $1" >&2; [ $# -lt 2 ] || printf '%s\n' "$2" >&2; exit 1; }
CCB="$(command -v "${CC:-cc}" || command -v cc || command -v gcc || command -v clang || true)"
[ -n "$CCB" ] || skip "ptrace tests" "no C compiler"

# ---- 1. the shim and the supervisor translate paths identically ----------------
P="$T/par"
mkdir -p "$P/root/etc" "$P/root/usr/bin" "$P/root/bin" "$P/hostdata/sub" "$P/hostid"
echo pw > "$P/root/etc/passwd"; echo f > "$P/hostdata/f"; echo f > "$P/hostid/f"
ln -s /etc "$P/root/lnk-abs"; ln -s etc "$P/root/lnk-rel"
ln -s loop2 "$P/root/loop1"; ln -s loop1 "$P/root/loop2"; ln -s ../../bin/bash "$P/root/usr/bin/sh"
ln -s sub "$P/hostdata/lnk"; ln -s "$P/root/etc" "$P/hostdata/hostlnk"
if "$CCB" -w -DSHIM -o "$T/xlate_shim" "$HERE/tests/xlate_cmp.c" 2>"$T/err" \
   && "$CCB" -w -DSUP -o "$T/xlate_sup" "$HERE/tests/xlate_cmp.c" 2>>"$T/err"; then
  env T="$P" PRX_ROOTFS="$P/root" PRX_BIND="/data=$P/hostdata:$P/hostid" "$T/xlate_shim" > "$T/xl_shim"
  env T="$P" PRX_ROOTFS="$P/root" PRX_BIND="/data=$P/hostdata:$P/hostid" "$T/xlate_sup"  > "$T/xl_sup"
  [ "$(wc -l < "$T/xl_shim")" -gt 400 ] || fail "path translation parity: corpus is too small"
  cmp -s "$T/xl_shim" "$T/xl_sup" && ok "prxsup translates $(wc -l < "$T/xl_shim") paths exactly like the shim" \
    || fail "prxsup and the shim translate paths differently" "$(diff "$T/xl_shim" "$T/xl_sup" | head -20)"
else
  fail "cannot build the parity test" "$(cat "$T/err")"
fi

# ---- 2. end to end ---------------------------------------------------------------
case "$(uname -m)" in x86_64|amd64|aarch64|arm64) ;; *) skip "ptrace end-to-end tests" "unsupported architecture" ;; esac
command -v ldd >/dev/null 2>&1 || skip "ptrace end-to-end tests" "no ldd"
"$PRX" build >/dev/null 2>&1 || fail "prx build"
SUP="$HOME/.local/share/prx/lib/prxsup"
[ -x "$SUP" ] && ok "prx build also builds prxsup" || fail "prxsup was not built"
"$SUP" probe 2>"$T/err" || skip "ptrace end-to-end tests" "ptrace/seccomp is not usable here ($(tr '\n' ' ' < "$T/err"))"
ok "prxsup probe: ptrace and seccomp work on this host"
"$CCB" -O1 -static -o "$T/static_probe" "$HERE/tests/static_probe.c" -pthread 2>/dev/null \
  || skip "ptrace end-to-end tests" "no static libc (cc -static failed)"
# an ldd that reports glibc paths is required to assemble a rootfs
ldd "$(command -v bash)" 2>/dev/null | grep -q 'libc\.so' || skip "ptrace end-to-end tests" "host is not glibc based"

R="$T/rootfs"
mkdir -p "$R/usr/bin" "$R/etc" "$R/tmp" "$R/root" "$R/proc" "$R/dev" "$R/sys" "$R/var" "$R/home"
ln -s usr/bin "$R/bin"
for b in bash cat sleep; do
  src="$(type -P "$b")"
  cp -L "$src" "$R/usr/bin/$b"
  for l in $(ldd "$src" 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i}'); do
    mkdir -p "$R$(dirname "$l")"; cp -L "$l" "$R$l" 2>/dev/null || true
  done
done
ln -s bash "$R/usr/bin/sh"
cp "$T/static_probe" "$R/usr/bin/static_probe"
if [ "$(uname -m)" = x86_64 ] && "$CCB" -O1 -static -nostdlib -fno-stack-protector -o "$R/usr/bin/rawsys" "$HERE/tests/rawsys.c" 2>/dev/null; then HAVE_RAW=1; else HAVE_RAW=0; fi
echo GUEST-SIDE > "$R/etc/os-release"
printf '#!/bin/sh\necho "script: $(cat /etc/os-release) args: $*"\n' > "$R/usr/bin/myscript"; chmod +x "$R/usr/bin/myscript"
echo 'plain text' > "$R/usr/bin/noexec"; chmod 644 "$R/usr/bin/noexec"
tar -C "$R" -czf "$T/rootfs.tar.gz" .
"$PRX" install pt "$T/rootfs.tar.gz" >/dev/null 2>&1 || fail "prx install"

g()  { "$PRX" login pt --ptrace -- "$@" 2>&1; }
want() { # NAME EXPECTED ACTUAL
  [ "$3" = "$2" ] && ok "$1" || fail "$1" "expected: $2
actual:   $3"
}

# the limit this feature removes: without --ptrace a static program sees the host
nop="$("$PRX" login pt -- static_probe cat /etc/os-release 2>&1 || true)"
[ "$nop" != GUEST-SIDE ] && ok "without --ptrace a static program bypasses the shim (documented limit)" \
  || fail "without --ptrace a static program was translated"
want "static program reads the guest's /etc/os-release" GUEST-SIDE "$(g static_probe cat /etc/os-release)"
[ "$HAVE_RAW" = 1 ] && want "program without libc (raw syscalls, like Go)" GUEST-SIDE "$(g rawsys /etc/os-release)"
want "relative paths, chdir and getcwd" "cwd=/root
GUEST-SIDE" "$(g static_probe cwd)"
out="$(g static_probe fs)"
want "create, rename, symlink, readlink, stat, unlink stay in the rootfs" "link=/tmp/sp/b
size=6
nope=No such file or directory" "$out"
[ ! -e /tmp/sp ] && ok "nothing was created on the host" || fail "a directory leaked to the host /tmp"
g static_probe list /etc | grep -qx os-release && ok "readdir sees the guest's /etc" || fail "readdir sees the guest's /etc"
want "fork" "GUEST-SIDE
child exit=7" "$(g static_probe fork /etc/os-release)"
want "8 threads translate in parallel" "8 GUEST-SIDE" "$(g static_probe threads /etc/os-release | sort | uniq -c | sed 's/^ *//')"
g static_probe exit 42 >/dev/null || rc=$?; want "exit status is passed on" 42 "${rc:-0}"

want "static -> dynamic (raw execve)" GUEST-SIDE "$(g static_probe exec /usr/bin/cat /etc/os-release)"
want "static -> shebang script" "script: GUEST-SIDE args: a b" "$(g static_probe exec /usr/bin/myscript a b)"
want "static -> static" GUEST-SIDE "$(g static_probe exec /usr/bin/static_probe cat /etc/os-release)"
want "exec of a missing file reports ENOENT" "execve /usr/bin/nope: No such file or directory" "$(g static_probe exec /usr/bin/nope)"
want "exec of a non-executable file reports EACCES" "execve /usr/bin/noexec: Permission denied" "$(g static_probe exec /usr/bin/noexec)"
want "fork + execve of a dynamic program" "GUEST-SIDE
spawned exit=0" "$(g static_probe spawn /usr/bin/cat /etc/os-release)"
want "vfork + execve of a dynamic program (like Go's os/exec)" "GUEST-SIDE
vspawned exit=0" "$(g static_probe vspawn /usr/bin/cat /etc/os-release)"
want "execve from a non-leader thread (static target)" GUEST-SIDE "$(g static_probe texec /usr/bin/static_probe cat /etc/os-release)"
want "execve from a non-leader thread (dynamic target)" GUEST-SIDE "$(g static_probe texec /usr/bin/cat /etc/os-release)"
want "dynamic shell -> static program" "GUEST-SIDE" "$(g bash -c 'static_probe cat /etc/os-release')"
want "static -> shell -> static" GUEST-SIDE "$(g static_probe exec /usr/bin/bash -c 'static_probe cat /etc/os-release')"

# signals and exit status reach the traced program; the pid is the one the shell knows
"$PRX" login pt --ptrace -- static_probe pause > "$T/pause.out" 2>&1 &
bgpid=$!
pid=""; for _ in $(seq 1 50); do pid="$(sed -n 's/^pid=//p' "$T/pause.out")"; [ -n "$pid" ] && break; sleep 0.1; done
[ -n "$pid" ] || fail "the traced program did not start" "$(cat "$T/pause.out")"
kill -USR1 "$pid"; sleep 0.3; kill -USR1 "$pid"; sleep 0.3
kill -TERM "$pid"; wait "$bgpid" || rc=$?
grep -q 'usr1 x2' "$T/pause.out" && ok "signals are delivered to the traced program" || fail "signals are delivered" "$(cat "$T/pause.out")"
want "death by SIGTERM is reported as 143" 143 "${rc:-0}"

g static_probe exec /usr/bin/myscript >/dev/null
"$PRX" login pt --ptrace --trace -- static_probe cat /etc/os-release 2>&1 | grep -q 'prxsup.*exec done, static' \
  && ok "--trace makes the supervisor log what it does" || fail "--trace output"
sleep 0.5
n="$(ps -eo stat=,comm= | awk '$2 == "prxsup" && $1 !~ /^Z/ {n++} END {print n + 0}')"
[ "$n" = 0 ] && ok "no tracer is left behind" || fail "$n prxsup processes are still running"
"$PRX" remove pt >/dev/null 2>&1
