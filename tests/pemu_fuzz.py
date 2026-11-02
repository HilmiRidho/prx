#!/usr/bin/env python3
"""Differential fuzzer: random AArch64 programs run under the interpreter (PEMU_JIT=0) and the
JIT; registers, NZCV and a memory window are dumped and compared.
usage: pemu_fuzz.py [pemu] [count] [seed]"""
import os, random, struct, subprocess, sys, tempfile, importlib.util
here = os.path.dirname(os.path.abspath(__file__))
PEMU = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'pemu')
COUNT = int(sys.argv[2]) if len(sys.argv) > 2 else 300
SEED = int(sys.argv[3]) if len(sys.argv) > 3 else 1
sys.argv = [sys.argv[0], PEMU]
spec = importlib.util.spec_from_file_location('pt', os.path.join(here, 'pemu_test.py'))
pt = importlib.util.module_from_spec(spec); spec.loader.exec_module(pt)

R = random.Random()
GP = list(range(0, 26))          # freely used registers (x26 loop counter, x27 index, x28/x29 bases)

def reg(z=True): return R.choice(GP + ([31] if z else []))
def dreg(): return R.choice(GP)

def logimm(sf):
    while True:
        es = R.choice([2, 4, 8, 16, 32, 64] if sf else [2, 4, 8, 16, 32])
        ones = R.randrange(1, es); rot = R.randrange(es)
        N = 1 if es == 64 else 0
        imms = ((-(es << 1)) | (ones - 1)) & 0x3f
        return N, rot, imms

def gen_one():
    k = R.randrange(100)
    sf = R.randrange(2)
    rd, rn, rm = dreg(), reg(), reg()
    if k < 8:                                                    # add/sub imm
        S = R.randrange(2); op = R.randrange(2)
        if not S and R.random() < .5: rd = dreg()
        rd = rd if (S or rd != 31) else 0
        return sf << 31 | op << 30 | S << 29 | 0x11000000 | R.randrange(2) << 22 | R.randrange(4096) << 10 | rn << 5 | rd
    if k < 14:                                                   # logical imm
        N, r_, s_ = logimm(sf); opc = R.randrange(4)
        return sf << 31 | opc << 29 | 0x12000000 | N << 22 | r_ << 16 | s_ << 10 | rn << 5 | (rd if opc == 3 else dreg())
    if k < 18:                                                   # move wide
        opc = R.choice([0, 2, 3, 3]); hw = R.randrange(4 if sf else 2)
        return sf << 31 | opc << 29 | 0x12800000 | hw << 21 | R.randrange(65536) << 5 | dreg()
    if k < 21:                                                   # adr/adrp
        return R.randrange(2) << 31 | R.randrange(4) << 29 | 0x10000000 | R.randrange(1 << 19) << 5 | dreg()
    if k < 30:                                                   # logical shifted
        amt = R.randrange(32 if not sf else 64)
        return sf << 31 | R.randrange(4) << 29 | 0x0A000000 | R.randrange(4) << 22 | R.randrange(2) << 21 | rm << 16 | amt << 10 | rn << 5 | rd
    if k < 38:                                                   # add/sub shifted
        amt = R.randrange(32 if not sf else 64)
        return sf << 31 | R.randrange(2) << 30 | R.randrange(2) << 29 | 0x0B000000 | R.randrange(3) << 22 | rm << 16 | amt << 10 | rn << 5 | rd
    if k < 42:                                                   # add/sub extended
        S = R.randrange(2); r2 = rd if S else dreg()
        return sf << 31 | R.randrange(2) << 30 | S << 29 | 0x0B200000 | rm << 16 | R.randrange(8) << 13 | R.randrange(5) << 10 | rn << 5 | r2
    if k < 45:                                                   # adc/sbc
        return sf << 31 | R.randrange(2) << 30 | R.randrange(2) << 29 | 0x1A000000 | rm << 16 | rn << 5 | rd
    if k < 49:                                                   # ccmp/ccmn
        imm = R.randrange(2)
        return sf << 31 | R.randrange(2) << 30 | 1 << 29 | 0x1A400000 | (R.randrange(32) if imm else rm) << 16 | R.randrange(16) << 12 | imm << 11 | rn << 5 | R.randrange(16)
    if k < 56:                                                   # csel family
        return sf << 31 | R.randrange(2) << 30 | 0x1A800000 | rm << 16 | R.randrange(16) << 12 | R.randrange(2) << 10 | rn << 5 | rd
    if k < 60:                                                   # 2-source
        return sf << 31 | 0x1AC00000 | rm << 16 | R.choice([2, 3, 8, 9, 10, 11]) << 10 | rn << 5 | rd
    if k < 64:                                                   # 1-source
        op = R.choice([0, 1, 2, 4, 5] + ([3] if sf else []))
        return sf << 31 | 0x5AC00000 | op << 10 | rn << 5 | rd
    if k < 68:                                                   # 3-source
        op31 = R.choice([0, 0, 1, 2, 5, 6]); o0 = R.randrange(2); ra = reg()
        if op31 in (2, 6): o0 = 0; ra = 31
        s = 1 if op31 else sf
        return s << 31 | 0x1B000000 | op31 << 21 | rm << 16 | o0 << 15 | ra << 10 | rn << 5 | rd
    if k < 76:                                                   # bitfield
        n = 64 if sf else 32
        return sf << 31 | R.choice([0, 1, 2]) << 29 | 0x13000000 | sf << 22 | R.randrange(n) << 16 | R.randrange(n) << 10 | rn << 5 | rd
    if k < 78:                                                   # extr
        return sf << 31 | 0x13800000 | sf << 22 | rm << 16 | R.randrange(64 if sf else 32) << 10 | rn << 5 | rd
    return gen_mem()

def gen_mem():
    k = R.randrange(100)
    base = R.choice([29, 29, 28])
    rt = R.choice(GP + [31]) if R.random() < .2 else dreg()
    if k < 30:                                                   # unsigned imm
        size = R.randrange(4)
        opc = R.choice([0, 1, 1] + ([2] if size < 3 else []) + ([3] if size < 2 else []))
        if size == 3 and opc > 1: opc = 1
        return size << 30 | 0x39000000 | opc << 22 | R.randrange(0, 0x100 >> size) << 10 | base << 5 | (rt if opc != 0 or True else 0)
    if k < 45:                                                   # pre/post/unscaled
        size = R.randrange(4)
        opc = R.choice([0, 1, 1] + ([2] if size < 3 else []) + ([3] if size < 2 else []))
        m = R.choice([0, 1, 3]); imm9 = R.randrange(-64, 64) & 0x1ff
        b = 28 if m else base
        r = rt
        if m and r == b: r = 0
        return size << 30 | 0x38000000 | opc << 22 | imm9 << 12 | m << 10 | b << 5 | r
    if k < 58:                                                   # register offset
        size = R.randrange(4)
        opc = R.choice([0, 1, 1] + ([2] if size < 3 else []))
        return size << 30 | 0x38200800 | opc << 22 | 27 << 16 | R.choice([2, 3, 6, 7]) << 13 | R.randrange(2) << 12 | base << 5 | rt
    if k < 74:                                                   # GPR pairs
        opc = R.choice([0, 2, 2]); L = R.randrange(2); idx = R.choice([1, 2, 2, 3])
        if L and R.random() < .15: opc = 1
        b = 28 if idx != 2 else base
        r1, r2 = dreg(), dreg()
        if idx != 2 and L and b in (r1, r2): r1, r2 = 0, 1
        imm = R.randrange(-8, 8) & 0x7f
        if opc == 1 and not L: opc = 2
        return opc << 30 | 0x28000000 | idx << 23 | L << 22 | imm << 15 | r2 << 10 | b << 5 | r1
    if k < 84:                                                   # vector unsigned imm
        size = R.randrange(4); q = R.random() < .4
        if q: opc, size = R.choice([2, 3]), 0
        else: opc = R.randrange(2)
        sc = 4 if q else size
        return size << 30 | 0x3D000000 | opc << 22 | R.randrange(0, 0x100 >> sc) << 10 | base << 5 | R.randrange(8)
    if k < 94:                                                   # vector pairs
        opc = R.randrange(3); L = R.randrange(2); idx = R.choice([1, 2, 2, 3])
        b = 28 if idx != 2 else base
        return opc << 30 | 0x2C000000 | idx << 23 | L << 22 | (R.randrange(-4, 4) & 0x7f) << 15 | R.randrange(8) << 10 | b << 5 | R.randrange(8)
    # literal load (reads code words)
    opc = R.randrange(3)
    return opc << 30 | 0x18000000 | R.randrange(1, 12) << 5 | dreg()

def gen_prog():
    ins = []          # list of words / ('br', kind) items resolved after
    # initial register state
    for r in GP:
        v = R.choice([0, 1, 0xffffffffffffffff, 0x8000000000000000, 0x7fffffffffffffff, 0xffffffff, 0x80000000, R.getrandbits(64), R.getrandbits(64), R.getrandbits(8), R.getrandbits(32)])
        ins.append(0xD2800000 | (v & 0xffff) << 5 | r)
        for hw in (1, 2, 3):
            if (v >> 16 * hw) & 0xffff: ins.append(0xF2800000 | hw << 21 | ((v >> 16 * hw) & 0xffff) << 5 | r)
    ins.append(0xD1200000 | 0 << 10 | 31 << 5 | 29)                  # placeholder, replaced below
    ins.pop()
    # x29 = sp - 0x20000 (bss-like region? use stack), x28 = x29 + 0x400, x27 = 8
    ins += [0xD1400000 | 8 << 10 | 31 << 5 | 29]                     # sub x29, sp, #8, lsl 12 (-0x8000)
    ins += [0x91000000 | 0 << 22 | 0x100 << 10 | 29 << 5 | 28]       # add x28, x29, #0x100
    ins += [0xD2800000 | 8 << 5 | 27]
    for k in range(12):                                              # seed memory
        ins.append(0xF9000000 | k << 10 | 29 << 5 | R.randrange(26))
    body = []
    n = R.randrange(20, 90)
    i = 0
    while i < n:
        t = R.random()
        if t < .10:                                                  # forward branch
            kind = R.randrange(5); skip = R.randrange(0, 5)
            body.append(('fwd', kind, skip, R.randrange(2), dreg(), R.randrange(16), R.randrange(64)))
        elif t < .13:                                                # indirect forward branch via adr/br
            body.append(('ind', R.randrange(0, 4)))
        elif t < .19:                                                # counted loop
            cnt = R.randrange(2, 40)
            body.append(('loop', cnt, [gen_one() for _ in range(R.randrange(1, 6))]))
        else:
            body.append(gen_one())
        i += 1
    # flatten with offsets
    flat = []     # items: ('w', word) or ('fwd', ...) with positions
    pos = len(ins)
    out = list(ins)
    sizes = []
    for b in body:
        if isinstance(b, tuple) and b[0] == 'ind': sizes.append(2)
        elif isinstance(b, tuple) and b[0] == 'loop': sizes.append(len(b[2]) + 3)
        else: sizes.append(1)
    starts = []
    p = pos
    for s in sizes: starts.append(p); p += s
    end = p
    def target_idx(bi, skip):
        j = min(bi + 1 + skip, len(body))
        return starts[j] if j < len(body) else end
    for bi, b in enumerate(body):
        if isinstance(b, int): out.append(b); continue
        if b[0] == 'fwd':
            _, kind, skip, sf, rt, cc, bit = b
            off = target_idx(bi, skip) - starts[bi]
            if off <= 0: off = 1
            if kind == 0: out.append(0x54000000 | off << 5 | cc)
            elif kind == 1: out.append(sf << 31 | 0x34000000 | off << 5 | rt)
            elif kind == 2: out.append(sf << 31 | 0x35000000 | off << 5 | rt)
            elif kind == 3: out.append((bit >> 5) << 31 | 0x36000000 | ((bit >> 0) & 31) << 19 | off << 5 | rt)
            else: out.append(0x14000000 | off)
        elif b[0] == 'ind':
            tgt = target_idx(bi, b[1]); off = (tgt - starts[bi]) * 4
            if tgt - starts[bi] < 2: tgt = starts[bi] + 2; off = 8
            out.append(((off & 3) << 29) | 0x10000000 | ((off >> 2) & 0x7ffff) << 5 | 25)       # adr x25, target
            out.append(0xD61F0000 | 25 << 5)                                                    # br x25
        else:
            _, cnt, lb = b
            out.append(0xD2800000 | cnt << 5 | 26)
            out += lb
            out.append(0xF1000400 | 26 << 5 | 26)                                              # subs x26,x26,#1
            out.append(0x54000000 | ((-(len(lb) + 1)) & 0x7ffff) << 5 | 1)                      # b.ne loop
    # epilogue: save regs and write window
    out.append(0xD53B4200 | 27)                                                                 # mrs x27, nzcv
    for r in range(0, 31, 1):
        out.append(0xF9000000 | (0x900 // 8 + r) << 10 | 29 << 5 | r)                           # str xr,[x29,#0x900+8r]
    for q in range(8):
        out.append(0x3D800000 | (0x980 // 16 + q) << 10 | 29 << 5 | q)                           # str qN
    out += [0xD2800000 | 1 << 5 | 0, 0xAA0003E0 | 29 << 16 | 1]                                 # x0=1, x1=x29
    out += [0xD2800000 | 0xa80 << 5 | 2, 0xD2800000 | 64 << 5 | 8, 0xD4000001]
    out += [0xD2800000 | 0, 0xD2800000 | 93 << 5 | 8, 0xD4000001]
    out[-3] = 0xD2800000 | 0
    return out

def run(words, jit):
    img, _ = pt.elf(b''.join(struct.pack('<I', w & 0xffffffff) for w in words), b'')
    f = tempfile.NamedTemporaryFile(delete=False, suffix='.elf'); f.write(img); f.close(); os.chmod(f.name, 0o755)
    try:
        p = subprocess.run(['setarch', os.uname().machine, '-R', PEMU, f.name], capture_output=True, timeout=5, env=dict(os.environ, PEMU_JIT='1' if jit else '0'))
    except subprocess.TimeoutExpired:
        return -999, b'timeout', b''
    finally:
        os.unlink(f.name)
    return p.returncode, p.stdout, p.stderr

def differs(w):
    a = run(w, False); b = run(w, True)
    return (a[0] != b[0] or a[1] != b[1]) and a[0] != -999 and b[0] != -999

def minimize(w):
    NOP = 0xD503201F
    w = list(w)
    for i in range(len(w)):
        if w[i] == NOP: continue
        keep = w[i]; w[i] = NOP
        if not differs(w): w[i] = keep
    return [(i, '%08x' % x) for i, x in enumerate(w) if x != NOP]

bad = 0
for n in range(COUNT):
    R.seed(SEED * 1000003 + n)
    w = gen_prog()
    a = run(w, False); b = run(w, True)
    if a[0] != b[0] or a[1] != b[1]:
        bad += 1
        print(f'MISMATCH seed={SEED} n={n} rc {a[0]} vs {b[0]} len(out) {len(a[1])} vs {len(b[1])} err={b[2][:120]!r}')
        if a[1] and b[1]:
            for off in range(0, min(len(a[1]), len(b[1])), 8):
                if a[1][off:off + 8] != b[1][off:off + 8]: print('   first diff at window offset 0x%x: int=%s jit=%s' % (off, a[1][off:off+8].hex(), b[1][off:off+8].hex())); break
        if os.environ.get('FUZZ_MIN'): print('   minimal:', minimize(w))
        if bad >= 5: break
print(f'fuzz: {COUNT if bad < 5 else n + 1} programs, {bad} mismatches')
sys.exit(1 if bad else 0)
