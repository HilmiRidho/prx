#!/usr/bin/env bash
# Self-test for pemu_x64 (x86_64 guest emulator).
# usage: tests/pemu_x64_test.sh [path/to/pemu]
# Needs a compiler that can build static x86_64 Linux programs: gcc on an x86_64 host, or
# x86_64-linux-gnu-gcc on others. Without one the test is skipped (exit 0).
HERE="$(cd "$(dirname "$0")" && pwd)"
PEMU="${1:-$HERE/../pemu}"
D="$HERE/pemu_x64"
if [ -n "${PEMU_X64_CC:-}" ]; then CC="$PEMU_X64_CC"
elif [ "$(uname -m)" = x86_64 ]; then CC=gcc
elif command -v x86_64-linux-gnu-gcc >/dev/null 2>&1; then CC=x86_64-linux-gnu-gcc
else echo "SKIP pemu_x64 tests: no x86_64 compiler (set PEMU_X64_CC)"; exit 0; fi
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
fail=0
run() {  # name, expected_rc, build-args...; program args via ARGS
    local n="$1" rc="$2"; shift 2
    if ! "$CC" "$@" -o "$T/$n" 2>"$T/$n.cc"; then echo "SKIP $n (does not build: $(head -1 "$T/$n.cc"))"; return; fi
    timeout 60 "$PEMU" "$T/$n" $ARGS >"$T/$n.out" 2>"$T/$n.err"; local got=$?
    if [ "$got" = 77 ]; then echo "SKIP $n (the host refuses what it tests)"; return; fi
    local exp="$D/$n.expect"
    # x87 is computed in IEEE quad on aarch64 hosts (see README), so a few last digits and the sign of a NaN differ
    [ -f "$exp.$(uname -m)" ] && exp="$exp.$(uname -m)"
    if [ "$got" = "$rc" ] && cmp -s "$T/$n.out" "$exp"; then echo "PASS $n"
    else echo "FAIL $n (rc=$got, want $rc)"; head -3 "$T/$n.err"; diff "$T/$n.out" "$exp" | head -5; fail=1; fi
}
ARGS="" run nolibc 0 -O0 -static -nostdlib -fno-stack-protector -fno-builtin "$D/nolibc.c"
ARGS="arg1" run hello 3 -O1 -static "$D/hello.c" -lm
ARGS="" run sig 0 -O1 -static "$D/sig.c" -lm
ARGS="" run x87 0 -O1 -static "$D/x87.c" -lm
ARGS="" run thread 0 -O1 -static -pthread "$D/thread.c"
ARGS="" run jit 0 -O1 -static "$D/jit.c"
ARGS="" run atomic 0 -O1 -static -pthread "$D/atomic.c"
ARGS="" run sigalt 0 -O1 -static -pthread "$D/sigalt.c"
ARGS="" run jitopt 0 -O1 -static -pthread "$D/jitopt.c"
ARGS="" run jitint 0 -O1 -static "$D/jitint.c"
ARGS="" run jitincdec 0 -O1 -static "$D/jitincdec.c"
ARGS="" run jitxlogps 0 -O1 -static "$D/jitxlogps.c"
ARGS="" run jitfp 0 -O1 -static "$D/jitfp.c" -lm
ARGS="" run mmx 0 -O1 -static "$D/mmx.c"
ARGS="" run sse42 0 -O1 -static -msse4.2 -mcx16 -pthread "$D/sse42.c"
ARGS="" run x87save 0 -O1 -static "$D/x87save.c"
ARGS="" run sys2 0 -O1 -static "$D/sys2.c"
ARGS="" run robust 0 -O1 -static -pthread "$D/robust.c"
ARGS="" run uring 0 -O1 -static "$D/uring.c"
ARGS="" run sse2jit 0 -O1 -static -msse2 "$D/sse2jit.c"
ARGS="" run cpuinfo 0 -O1 -static "$D/cpuinfo.c"
ARGS="" run winelike 0 -O1 -static "$D/winelike.c"
ARGS="" run sys3 0 -O1 -static "$D/sys3.c"
ARGS="" run clone3 0 -O1 -static "$D/clone3.c"
ARGS="" run execveat 0 -O1 -static "$D/execveat.c"
ARGS="" run seg 0 -O1 -static "$D/seg.c"
ARGS="" run iretq 0 -O1 -static "$D/iretq.c"
ARGS="" run trapno 0 -O0 -static "$D/trapno.c"
ARGS="" run accept 0 -O1 -static "$D/accept.c"
[ $fail = 0 ] && echo "RESULT PASS" || echo "RESULT FAIL"
exit $fail
