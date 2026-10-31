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
    if [ "$got" = "$rc" ] && cmp -s "$T/$n.out" "$D/$n.expect"; then echo "PASS $n"
    else echo "FAIL $n (rc=$got, want $rc)"; head -3 "$T/$n.err"; diff "$T/$n.out" "$D/$n.expect" | head -5; fail=1; fi
}
ARGS="" run nolibc 0 -O0 -static -nostdlib -fno-stack-protector -fno-builtin "$D/nolibc.c"
ARGS="arg1" run hello 3 -O1 -static "$D/hello.c" -lm
ARGS="" run sig 0 -O1 -static "$D/sig.c" -lm
ARGS="" run x87 0 -O1 -static "$D/x87.c" -lm
ARGS="" run thread 0 -O1 -static -pthread "$D/thread.c"
[ $fail = 0 ] && echo "RESULT PASS" || echo "RESULT FAIL"
exit $fail
