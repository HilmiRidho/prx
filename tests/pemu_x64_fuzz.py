#!/usr/bin/env python3
"""Differential fuzzer for the pemu_x64 JIT: random x86-64 programs are run once with the interpreter
(PEMU_JIT=0) and once with the JIT; all registers, the flags and the whole data segment are dumped and compared.

usage: pemu_x64_fuzz.py [pemu] [count] [seed]
A program is a static ELF built here (no x86 toolchain needed). r13 = index register, r14 = data base, r15 = loop
counter and rsp are never random destinations, so every access stays inside the data segment."""
import os, random, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PEMU = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'pemu')
BIS = len(sys.argv) > 3 and sys.argv[2] == '--bisect'
COUNT = int(sys.argv[2]) if len(sys.argv) > 2 and not BIS else 200
SEED = int(sys.argv[3]) if len(sys.argv) > 3 and not BIS else 1
CODE = 0x401000
DATA = 0x600000
DSZ = 0x4000
BASE = DATA + 0x1000          # r14
FSB = DATA + 0x2000
STK = DATA + 0x3800
R = random.Random()
NEST = 0                      # >0 while generating into a scratch buffer: no rip-relative operands, no calls (their fixups need final addresses)
FREE = [0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12]     # random destinations (no rsp, r13, r14, r15)
CCS = list(range(16))


class Mem:
    def __init__(s, base=None, index=None, scale=0, disp=0, seg=None, rip=None):
        s.base, s.index, s.scale, s.disp, s.seg, s.rip = base, index, scale, disp, seg, rip


class Asm:
    def __init__(s):
        s.b = bytearray()
        s.fix = []

    def here(s): return CODE + len(s.b)

    def insn(s, opc, reg, rm, imm=b'', w=0, p66=False, force_rex=False, pfx=b''):
        out = bytearray(pfx)
        if p66: out.append(0x66)
        rexr = (reg >> 3) & 1 if isinstance(reg, int) else 0
        mem = isinstance(rm, Mem)
        if mem:
            if rm.seg == 'fs': out.insert(0, 0x64)
            rexb = (rm.base >> 3) & 1 if rm.base is not None else 0
            rexx = (rm.index >> 3) & 1 if rm.index is not None else 0
        else:
            rexb = (rm >> 3) & 1; rexx = 0
        rex = 0x40 | w << 3 | rexr << 2 | rexx << 1 | rexb
        if rex != 0x40 or force_rex: out.append(rex)
        out += opc
        r = (reg & 7) << 3
        if not mem:
            out.append(0xC0 | r | (rm & 7))
        elif rm.rip is not None:
            out.append(r | 5)
            total = len(out) + 4 + len(imm)
            out += struct.pack('<i', rm.rip - (s.here() + total))
        elif rm.base is None and rm.index is None:
            out += bytes([r | 4, 0x25]) + struct.pack('<i', rm.disp)
        else:
            d = rm.disp
            mod = 0 if (d == 0 and (rm.base is None or (rm.base & 7) != 5)) else 1 if -128 <= d < 128 else 2
            if rm.base is None: mod = 0
            need_sib = rm.index is not None or rm.base is None or (rm.base & 7) == 4
            if need_sib:
                out.append(mod << 6 | r | 4)
                idx = 4 if rm.index is None else rm.index & 7
                bs = 5 if rm.base is None else rm.base & 7
                out.append(rm.scale << 6 | idx << 3 | bs)
            else:
                out.append(mod << 6 | r | (rm.base & 7))
            if rm.base is None: out += struct.pack('<i', d)
            elif mod == 1: out += struct.pack('<b', d)
            elif mod == 2: out += struct.pack('<i', d)
        out += imm
        s.b += out

    def raw(s, bs): s.b += bytes(bs)

    def mov_imm64(s, r, v):
        s.raw([0x48 | (r >> 3), 0xB8 + (r & 7)] + list(struct.pack('<Q', v & (2**64 - 1))))


def rv():
    k = R.randrange(10)
    if k < 3: return R.randrange(0, 16)
    if k < 5: return R.choice([0xff, 0x7f, 0x80, 0xffff, 0x7fff, 0x8000, 0x7fffffff, 0x80000000, 0xffffffff])
    if k < 7: return (-R.randrange(1, 300)) & (2**64 - 1)
    if k < 8: return R.choice([0x7fffffffffffffff, 0x8000000000000000, 2**64 - 1, 1 << R.randrange(64)])
    return R.getrandbits(64)


def imm_for(sz):
    k = R.randrange(6)
    if k == 0: v = 0
    elif k == 1: v = R.choice([1, -1, 0x7f, -0x80, 0xff, 0x7fffffff, -0x80000000])
    else: v = R.randrange(-128, 128) if k < 4 else R.getrandbits(32) - (1 << 31)
    return v


def mem_operand(a):
    k = R.randrange(10)
    d = R.randrange(0, 0x7f0) & ~0
    if k < 4: return Mem(14, None, 0, d - 0x100 if R.random() < .5 else d)
    if k < 6:
        a.raw([0x41, 0x81, 0xE5, 0x1f, 0, 0, 0])            # and r13d, 0x1f : index stays small
        return Mem(14, 13, R.randrange(4), d)
    if k == 6:
        a.raw([0x41, 0x81, 0xE5, 0x1f, 0, 0, 0])
        return Mem(None, 13, R.randrange(4), BASE + d)      # [index*scale + disp32]
    if k == 7: return Mem(None, None, 0, BASE + d)           # absolute
    if k == 8: return Mem(rip=BASE + d) if not NEST else Mem(None, None, 0, BASE + d)   # rip-relative
    return Mem(None, None, 0, R.randrange(0, 0x200), seg='fs')   # fs:[disp]


def reg_or_xmm_mem(a, y):
    return y if R.random() < .5 else mem_operand(a)


def reg_or_mem(a):
    return R.choice(FREE) if R.random() < .5 else mem_operand(a)


def sized(a, opc8, opc, sz, reg, rm, imm=b''):
    """emit an instruction with an 8-bit form (opc8) or 16/32/64-bit form (opc)"""
    if sz == 1:
        # byte registers 4..7 are ah..bh unless a REX prefix is present
        fr = R.random() < .4
        if not isinstance(rm, Mem) and rm in (5, 6, 7) and R.random() < .5: fr = False
        a.insn(opc8, reg, rm, imm, force_rex=fr)
    else:
        a.insn(opc, reg, rm, imm, w=int(sz == 8), p66=(sz == 2))


def immbytes(sz, v, full=True):
    if sz == 1: return struct.pack('<b', max(-128, min(127, v)) if v >= -128 else -128) if -128 <= v < 128 else struct.pack('<B', v & 0xff)
    if sz == 2: return struct.pack('<H', v & 0xffff)
    return struct.pack('<i', max(-(1 << 31), min((1 << 31) - 1, v)))


def gen_one(a, depth=0):
    k = R.randrange(100)
    sz = R.choice([1, 2, 4, 4, 8, 8])
    if k < 22:                                                   # ALU
        t = R.choice([0, 1, 4, 5, 6, 7, 0, 5, 7, 2, 3])
        form = R.randrange(4)
        if form == 0:
            rm = reg_or_mem(a); sized(a, bytes([t * 8]), bytes([t * 8 + 1]), sz, R.choice(FREE), rm)
        elif form == 1:
            rm = reg_or_mem(a); sized(a, bytes([t * 8 + 2]), bytes([t * 8 + 3]), sz, R.choice(FREE), rm)
        elif form == 2:
            rm = reg_or_mem(a); v = imm_for(sz)
            if sz == 1: sized(a, b'\x80', b'\x80', 1, t, rm, struct.pack('<B', v & 0xff))
            elif -128 <= v < 128 and R.random() < .6: a.insn(b'\x83', t, rm, struct.pack('<b', v), w=int(sz == 8), p66=sz == 2)
            else: a.insn(b'\x81', t, rm, immbytes(sz, v), w=int(sz == 8), p66=sz == 2)
        else:
            v = imm_for(sz)
            if sz == 1: a.raw([t * 8 + 4, v & 0xff])
            else:
                pre = [0x66] if sz == 2 else [0x48] if sz == 8 else []
                a.raw(pre + [t * 8 + 5]); a.raw(immbytes(sz, v))
    elif k < 28:                                                 # test
        if R.random() < .5:
            rm = reg_or_mem(a); sized(a, b'\x84', b'\x85', sz, R.choice(FREE), rm)
        else:
            rm = reg_or_mem(a); v = imm_for(sz)
            sized(a, b'\xf6', b'\xf7', sz, 0, rm, immbytes(sz, v) if sz != 1 else struct.pack('<B', v & 0xff))
    elif k < 44:                                                 # mov family
        f = R.randrange(6)
        if f == 0: rm = reg_or_mem(a); sized(a, b'\x88', b'\x89', sz, R.choice(FREE), rm)
        elif f == 1: rm = reg_or_mem(a); sized(a, b'\x8a', b'\x8b', sz, R.choice(FREE), rm)
        elif f == 2:
            rm = reg_or_mem(a); v = imm_for(sz)
            sized(a, b'\xc6', b'\xc7', sz, 0, rm, immbytes(sz, v) if sz != 1 else struct.pack('<B', v & 0xff))
        elif f == 3:
            r = R.choice(FREE)
            if sz == 1: a.raw(([0x40 | (r >> 3)] if r >= 8 or (r in (5, 6, 7) and R.random() < .5) else []) + [0xB0 + (r & 7), R.randrange(256)])
            elif sz == 8: a.mov_imm64(r, rv())
            elif sz == 4: a.raw(([0x41] if r >= 8 else []) + [0xB8 + (r & 7)] + list(struct.pack('<I', rv() & 0xffffffff)))
            else: a.raw([0x66] + ([0x41] if r >= 8 else []) + [0xB8 + (r & 7)] + list(struct.pack('<H', rv() & 0xffff)))
        elif f == 4:                                             # movzx / movsx
            rm = reg_or_mem(a); op = R.choice([0xB6, 0xB7, 0xBE, 0xBF]); dsz = R.choice([2, 4, 8, 8, 4])
            fr = R.random() < .4 and (op & 1) == 0
            a.insn(bytes([0x0F, op]), R.choice(FREE), rm, w=int(dsz == 8), p66=dsz == 2, force_rex=fr)
        else:                                                    # movsxd / lea
            if R.random() < .5:
                rm = reg_or_mem(a); a.insn(b'\x63', R.choice(FREE), rm, w=R.randrange(2))
            else:
                m = mem_operand(a)
                if m.rip is not None or m.seg: m = Mem(14, None, 0, 8)
                a.insn(b'\x8d', R.choice(FREE), m, w=int(sz == 8), p66=sz == 2)
    elif k < 50:                                                 # not / neg
        rm = reg_or_mem(a); sized(a, b'\xf6', b'\xf7', sz, R.choice([2, 3]), rm)
    elif k < 60:                                                 # shifts
        rm = reg_or_mem(a); t = R.choice([4, 5, 7, 4, 5, 7, 0, 1, 2, 3, 6]); f = R.randrange(3)
        if f == 0: sized(a, b'\xc0', b'\xc1', sz, t, rm, bytes([R.choice([0, 1, 2, 3, 7, 8, 15, 31, 33, 63, 64, 255])]))
        elif f == 1: sized(a, b'\xd0', b'\xd1', sz, t, rm)
        else: sized(a, b'\xd2', b'\xd3', sz, t, rm)
    elif k < 65:                                                 # imul
        rm = reg_or_mem(a); dsz = R.choice([2, 4, 8, 8, 4]); r = R.choice(FREE)
        f = R.randrange(3)
        if f == 0: a.insn(b'\x0f\xaf', r, rm, w=int(dsz == 8), p66=dsz == 2)
        elif f == 1: a.insn(b'\x6b', r, rm, struct.pack('<b', R.randrange(-128, 128)), w=int(dsz == 8), p66=dsz == 2)
        else: a.insn(b'\x69', r, rm, immbytes(dsz, imm_for(dsz)), w=int(dsz == 8), p66=dsz == 2)
    elif k < 71:                                                 # setcc / cmov
        cc = R.choice(CCS)
        if R.random() < .5:
            rm = reg_or_mem(a); a.insn(bytes([0x0F, 0x90 + cc]), 0, rm, force_rex=R.random() < .4)
        else:
            rm = reg_or_mem(a); dsz = R.choice([2, 4, 8, 8, 4]); a.insn(bytes([0x0F, 0x40 + cc]), R.choice(FREE), rm, w=int(dsz == 8), p66=dsz == 2)
    elif k < 75:                                                 # push / pop (balanced)
        r = R.choice(FREE)
        if R.random() < .3: a.raw([0x68] + list(struct.pack('<i', imm_for(4))))
        else: a.raw(([0x41] if r >= 8 else []) + [0x50 + (r & 7)])
        if R.random() < .5: gen_one(a, depth + 1) if depth < 2 else None
        r2 = R.choice(FREE); a.raw(([0x41] if r2 >= 8 else []) + [0x58 + (r2 & 7)])
    elif k < 78:                                                 # cdq family
        a.raw(R.choice([[0x98], [0x99], [0x48, 0x98], [0x48, 0x99], [0x66, 0x98], [0x66, 0x99]]))
    elif k < 82:                                                 # inc/dec, xchg, bt
        rm = reg_or_mem(a); f = R.randrange(3)
        if f == 0: sized(a, b'\xfe', b'\xff', sz, R.choice([0, 1]), rm)
        elif f == 1: sized(a, b'\x86', b'\x87', sz, R.choice(FREE), rm)
        else: a.insn(b'\x0f\xa3', R.choice(FREE), rm, w=int(sz == 8) if sz >= 4 else 0, p66=False)
    elif k < 86:                                                 # SSE: data moves, bit logic, movd/movq (native in the JIT) and others (fallback)
        x = R.randrange(16); y = R.randrange(16)
        f = R.randrange(14)
        if f == 0: a.insn(b'\x0f\x6f', x, reg_or_xmm_mem(a, y), pfx=R.choice([b'\xf3', b'']), p66=R.random() < .5)     # movdqu/movdqa load
        elif f == 1: a.insn(b'\x0f\x7f', x, reg_or_xmm_mem(a, y), pfx=R.choice([b'\xf3', b'']), p66=R.random() < .5)   # store
        elif f == 2: a.insn(bytes([0x0f, R.choice([0x10, 0x28])]), x, reg_or_xmm_mem(a, y), p66=R.random() < .5)         # movups/movaps load
        elif f == 3: a.insn(bytes([0x0f, R.choice([0x11, 0x29])]), x, reg_or_xmm_mem(a, y), p66=R.random() < .5)         # store
        elif f in (4, 5): a.insn(bytes([0x0f, R.choice([0xef, 0xeb, 0xdb, 0xdf])]), x, reg_or_xmm_mem(a, y), p66=True)   # pxor/por/pand/pandn
        elif f == 6: a.insn(b'\x0f\x6e', x, reg_or_mem(a), w=R.randrange(2), p66=True)                                  # movd/movq xmm, r/m
        elif f == 7: a.insn(b'\x0f\x7e', x, reg_or_mem(a), w=R.randrange(2), p66=True)                                  # movd/movq r/m, xmm
        elif f == 8: a.insn(b'\x0f\xfe', x, y, p66=True)                                                                 # paddd (fallback)
        elif f == 9: a.insn(b'\x0f\x74', x, reg_or_xmm_mem(a, y), p66=True)                                              # pcmpeqb (fallback)
        elif f == 10: a.insn(b'\x0f\xd7', R.choice(FREE), y, p66=True)                                                   # pmovmskb (fallback)
        else: a.insn(b'\x0f\x7e', x, R.choice(FREE), w=1, p66=True)
    elif k < 92 and depth < 2:                                   # forward conditional branch over a few instructions
        saved = a.b; a.b = bytearray()
        start_len = len(saved)
        sub_depth = depth + 1
        for _ in range(R.randrange(1, 4)): gen_one_nested(a, start_len, sub_depth)
        body = bytes(a.b); a.b = saved
        cc = R.choice(CCS)
        if len(body) < 120 and R.random() < .6: a.raw([0x70 + cc, len(body)])
        else: a.raw([0x0F, 0x80 + cc] + list(struct.pack('<i', len(body))))
        a.raw(body)
    elif k < 96 and depth < 1:                                   # short counted loop
        a.raw([0x41, 0xBF, R.randrange(2, 5), 0, 0, 0])          # mov r15d, n
        saved = a.b; a.b = bytearray()
        for _ in range(R.randrange(1, 4)): gen_one_nested(a, len(saved) + 6, depth + 1)
        a.raw([0x41, 0x83, 0xEF, 0x01])                          # sub r15d, 1
        body = bytes(a.b); a.b = saved
        a.raw(body)
        a.raw([0x75 if len(body) < 126 else 0x0F, ] + ([(-(len(body) + 2)) & 0xff] if len(body) < 126 else [0x85] + list(struct.pack('<i', -(len(body) + 6)))))
    elif k < 98 and not NEST:                                    # call a helper (call/ret, IC)
        a.raw([0xE8]); a.fix.append(len(a.b)); a.raw([0, 0, 0, 0])
    else:
        a.raw([0x90])


def gen_one_nested(a, off_in_parent, depth):
    global NEST
    NEST += 1
    try: gen_one(a, depth)
    finally: NEST -= 1


def build(seed, limit=None, marks=None):
    R.seed(seed)
    a = Asm()
    a.mov_imm64(14, BASE)
    a.raw([0x48, 0xBC] + list(struct.pack('<Q', STK)))            # mov rsp, STK
    # fs base = FSB via arch_prctl(ARCH_SET_FS)
    a.raw([0xB8, 158, 0, 0, 0, 0xBF, 0x02, 0x10, 0, 0]); a.mov_imm64(6, FSB); a.raw([0x0F, 0x05])
    for r in FREE + [13, 15]: a.mov_imm64(r, rv())
    # flags start from a defined state
    n_ins = R.randrange(30, 140)
    if limit is not None: n_ins = min(n_ins, limit)
    for i in range(n_ins):
        if marks is not None: marks.append(len(a.b))
        gen_one(a)
    helper_at = None
    # dump registers
    for i, r in enumerate(FREE + [13, 15]):
        a.insn(b'\x89', r, Mem(14, None, 0, 0x800 + 8 * i), w=1)
    a.raw([0x9C, 0x58])                                           # pushfq; pop rax  (flags observed here)
    a.insn(b'\x89', 0, Mem(14, None, 0, 0x800 + 8 * 20), w=1)
    for x in range(16): a.insn(b'\x0f\x7f', x, Mem(14, None, 0, 0x900 + 16 * x), pfx=b'\xf3')
    # write(1, DATA, DSZ); exit(0)
    a.raw([0xB8, 1, 0, 0, 0, 0xBF, 1, 0, 0, 0]); a.mov_imm64(6, DATA); a.raw([0xBA] + list(struct.pack('<I', DSZ))); a.raw([0x0F, 0x05])
    a.raw([0xB8, 60, 0, 0, 0, 0x31, 0xFF, 0x0F, 0x05])
    # helper function: a few ALU ops then ret
    hpos = len(a.b)
    a.raw([0x48, 0x01, 0xD8, 0x48, 0xD1, 0xE0, 0x48, 0x83, 0xF0, 0x55, 0xC3])
    for f in a.fix:
        rel = hpos - (f + 4)
        a.b[f:f + 4] = struct.pack('<i', rel)
    return bytes(a.b)


def elf(code, data):
    ehs, phs = 64, 56
    off_code = 0x1000
    off_data = off_code + ((len(code) + 0xfff) & ~0xfff)
    hdr = b'\x7fELF' + bytes([2, 1, 1, 0]) + b'\0' * 8 + struct.pack('<HHIQQQIHHHHHH', 2, 62, 1, CODE, ehs, 0, 0, ehs, phs, 2, 0, 0, 0)
    ph1 = struct.pack('<IIQQQQQQ', 1, 5, off_code, CODE, CODE, len(code), len(code), 0x1000)
    ph2 = struct.pack('<IIQQQQQQ', 1, 6, off_data, DATA, DATA, len(data), DSZ, 0x1000)
    img = bytearray(hdr + ph1 + ph2)
    img += b'\0' * (off_code - len(img)); img += code
    img += b'\0' * (off_data - len(img)); img += data
    return bytes(img)


def run(exe, jit, timeout=120):
    env = dict(os.environ); env['PEMU_JIT'] = '1' if jit else '0'
    try:
        p = subprocess.run([PEMU, exe], capture_output=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return None, b'timeout'
    return p.returncode, p.stdout + b'|' + p.stderr[-300:]


def bisect(seed):
    """find the first top-level instruction after which JIT and interpreter disagree and disassemble it"""
    with tempfile.TemporaryDirectory() as td:
        def bad(limit):
            marks = []
            code = build(seed, limit, marks)
            R.seed(seed ^ 0x5555); data = bytes(R.getrandbits(8) for _ in range(DSZ))
            exe = os.path.join(td, 'b'); open(exe, 'wb').write(elf(code, data)); os.chmod(exe, 0o755)
            return run(exe, False) != run(exe, True), code, marks
        full, code, marks = bad(None)
        if not full: print('no mismatch for seed', seed); return
        lo, hi = 0, len(marks)
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if bad(mid)[0]: hi = mid
            else: lo = mid
        _, code, marks = bad(hi)
        st = marks[hi - 1]; en = (marks[hi] if hi < len(marks) else len(code))
        print('first bad instruction index %d of %d, bytes: %s' % (hi - 1, len(marks), code[st:min(en, st + 40)].hex(' ')))
        open('/tmp/bis.bin', 'wb').write(code[max(0, st - 24):st + 40])
        print('context saved to /tmp/bis.bin (24 bytes before the instruction)')

def main():
    if BIS: bisect(int(sys.argv[3])); return 0
    bad = 0
    with tempfile.TemporaryDirectory() as td:
        for n in range(COUNT):
            seed = SEED * 100003 + n
            code = build(seed)
            R.seed(seed ^ 0x5555)
            data = bytes(R.getrandbits(8) for _ in range(DSZ))
            exe = os.path.join(td, 'f%d' % n)
            open(exe, 'wb').write(elf(code, data)); os.chmod(exe, 0o755)
            a = run(exe, False); b = run(exe, True)
            if a != b:
                bad += 1
                keep = '/tmp/fuzz_fail_%d' % seed
                open(keep, 'wb').write(elf(code, data)); os.chmod(keep, 0o755)
                why = ''
                if a[0] != b[0]: why = 'rc %s vs %s ' % (a[0], b[0])
                oa, ob = a[1], b[1]
                diff = [i for i in range(min(len(oa), len(ob))) if oa[i] != ob[i]][:6]
                print('MISMATCH seed=%d %s first differing offsets %s -> %s  (saved %s)' % (seed, why, diff, [hex(d) for d in diff], keep))
                if bad >= 5: break
    print('RESULT %s (%d programs, seed %d)' % ('FAIL' if bad else 'PASS', COUNT, SEED))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
