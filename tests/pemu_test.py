#!/usr/bin/env python3
"""Self-test for pemu: assembles small static aarch64 ELF programs with a tiny
assembler (no cross toolchain needed) and runs them under pemu.

usage: pemu_test.py [path/to/pemu]
"""
import os, struct, subprocess, sys, tempfile

PEMU = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'pemu')
BASE = 0x400000

SYS = dict(write=64, exit=93, kill=129, getpid=172, wait4=260, clone=220, openat=56, read=63,
           brk=214, mmap=222, newfstatat=79, uname=160, rt_sigaction=134, close=57)


class Asm:
    def __init__(self):
        self.code = []      # (kind, payload)
        self.labels = {}
        self.data = b''

    # --- plumbing
    def emit(self, w): self.code.append(('w', w & 0xffffffff))
    def label(self, n): self.labels[n] = len(self.code)
    def rel(self, fn, target): self.code.append(('r', (fn, target)))
    def pc_of(self, idx): return BASE + 0x78 + idx * 4

    def assemble(self):
        out = []
        for idx, (k, p) in enumerate(self.code):
            if k == 'w': out.append(p)
            else:
                fn, t = p
                off = (self.labels[t] - idx) * 4
                out.append(fn(off) & 0xffffffff)
        return b''.join(struct.pack('<I', w) for w in out)

    # --- instructions
    def movz(self, rd, imm, hw=0): self.emit(0xD2800000 | hw << 21 | (imm & 0xffff) << 5 | rd)
    def movk(self, rd, imm, hw=0): self.emit(0xF2800000 | hw << 21 | (imm & 0xffff) << 5 | rd)
    def movn(self, rd, imm, hw=0): self.emit(0x92800000 | hw << 21 | (imm & 0xffff) << 5 | rd)
    def mov_imm(self, rd, v):
        v &= (1 << 64) - 1
        self.movz(rd, v & 0xffff)
        for hw in (1, 2, 3):
            part = (v >> (16 * hw)) & 0xffff
            if part: self.movk(rd, part, hw)
    def mov(self, rd, rm): self.emit(0xAA0003E0 | rm << 16 | rd)
    def add_i(self, rd, rn, imm): self.emit(0x91000000 | imm << 10 | rn << 5 | rd)
    def sub_i(self, rd, rn, imm): self.emit(0xD1000000 | imm << 10 | rn << 5 | rd)
    def subs_i(self, rd, rn, imm): self.emit(0xF1000000 | imm << 10 | rn << 5 | rd)
    def cmp_i(self, rn, imm): self.subs_i(31, rn, imm)
    def add(self, rd, rn, rm): self.emit(0x8B000000 | rm << 16 | rn << 5 | rd)
    def sub(self, rd, rn, rm): self.emit(0xCB000000 | rm << 16 | rn << 5 | rd)
    def adds(self, rd, rn, rm): self.emit(0xAB000000 | rm << 16 | rn << 5 | rd)
    def subs(self, rd, rn, rm): self.emit(0xEB000000 | rm << 16 | rn << 5 | rd)
    def cmp(self, rn, rm): self.subs(31, rn, rm)
    def and_(self, rd, rn, rm): self.emit(0x8A000000 | rm << 16 | rn << 5 | rd)
    def eor(self, rd, rn, rm): self.emit(0xCA000000 | rm << 16 | rn << 5 | rd)
    def mul(self, rd, rn, rm): self.emit(0x9B007C00 | rm << 16 | rn << 5 | rd)
    def udiv(self, rd, rn, rm): self.emit(0x9AC00800 | rm << 16 | rn << 5 | rd)
    def sdiv(self, rd, rn, rm): self.emit(0x9AC00C00 | rm << 16 | rn << 5 | rd)
    def umulh(self, rd, rn, rm): self.emit(0x9BC07C00 | rm << 16 | rn << 5 | rd)
    def lsl_i(self, rd, rn, n): self.emit(0xD3400000 | ((-n) % 64) << 16 | (63 - n) << 10 | rn << 5 | rd)
    def lsr_i(self, rd, rn, n): self.emit(0xD3400000 | n << 16 | 63 << 10 | rn << 5 | rd)
    def asr_i(self, rd, rn, n): self.emit(0x93400000 | n << 16 | 63 << 10 | rn << 5 | rd)
    def ubfx(self, rd, rn, lsb, w): self.emit(0xD3400000 | lsb << 16 | (lsb + w - 1) << 10 | rn << 5 | rd)
    def clz(self, rd, rn): self.emit(0xDAC01000 | rn << 5 | rd)
    def rev(self, rd, rn): self.emit(0xDAC00C00 | rn << 5 | rd)
    def csel(self, rd, rn, rm, cc): self.emit(0x9A800000 | rm << 16 | cc << 12 | rn << 5 | rd)
    def cset(self, rd, cc): self.emit(0x9A9F07E0 | (cc ^ 1) << 12 | rd)
    def ldr(self, rt, rn, off=0): self.emit(0xF9400000 | (off // 8) << 10 | rn << 5 | rt)
    def str(self, rt, rn, off=0): self.emit(0xF9000000 | (off // 8) << 10 | rn << 5 | rt)
    def ldrb(self, rt, rn, off=0): self.emit(0x39400000 | off << 10 | rn << 5 | rt)
    def strb(self, rt, rn, off=0): self.emit(0x39000000 | off << 10 | rn << 5 | rt)
    def ldrsb(self, rt, rn, off=0): self.emit(0x39800000 | off << 10 | rn << 5 | rt)
    def ldr_w(self, rt, rn, off=0): self.emit(0xB9400000 | (off // 4) << 10 | rn << 5 | rt)
    def str_w(self, rt, rn, off=0): self.emit(0xB9000000 | (off // 4) << 10 | rn << 5 | rt)
    def stp_pre(self, a, b, rn, off): self.emit(0xA9800000 | ((off // 8) & 0x7f) << 15 | b << 10 | rn << 5 | a)
    def ldp_post(self, a, b, rn, off): self.emit(0xA8C00000 | ((off // 8) & 0x7f) << 15 | b << 10 | rn << 5 | a)
    def stp(self, a, b, rn, off=0): self.emit(0xA9000000 | ((off // 8) & 0x7f) << 15 | b << 10 | rn << 5 | a)
    def ldp(self, a, b, rn, off=0): self.emit(0xA9400000 | ((off // 8) & 0x7f) << 15 | b << 10 | rn << 5 | a)
    def str_post(self, rt, rn, imm): self.emit(0xF8000400 | (imm & 0x1ff) << 12 | rn << 5 | rt)
    def ldr_pre(self, rt, rn, imm): self.emit(0xF8400C00 | (imm & 0x1ff) << 12 | rn << 5 | rt)
    def ldr_reg(self, rt, rn, rm): self.emit(0xF8606800 | rm << 16 | rn << 5 | rt)   # [rn, rm, lsl 0]? option=011 S=0
    def ldxr(self, rt, rn): self.emit(0xC85F7C00 | rn << 5 | rt)
    def stxr(self, rs, rt, rn): self.emit(0xC8007C00 | rs << 16 | rn << 5 | rt)
    def svc(self): self.emit(0xD4000001)
    def ret(self): self.emit(0xD65F03C0)
    def blr(self, rn): self.emit(0xD63F0000 | rn << 5)
    def b(self, t): self.rel(lambda o: 0x14000000 | (o // 4 & 0x3ffffff), t)
    def bl(self, t): self.rel(lambda o: 0x94000000 | (o // 4 & 0x3ffffff), t)
    def bcond(self, cc, t): self.rel(lambda o: 0x54000000 | (o // 4 & 0x7ffff) << 5 | cc, t)
    def cbz(self, rt, t): self.rel(lambda o: 0xB4000000 | (o // 4 & 0x7ffff) << 5 | rt, t)
    def cbnz(self, rt, t): self.rel(lambda o: 0xB5000000 | (o // 4 & 0x7ffff) << 5 | rt, t)
    def adr(self, rd, t):
        self.rel(lambda o: 0x10000000 | (o & 3) << 29 | ((o >> 2) & 0x7ffff) << 5 | rd, t)
    def mrs_tpidr(self, rd): self.emit(0xD53BD040 | rd)
    def msr_tpidr(self, rt): self.emit(0xD51BD040 | rt)
    # fp
    def fmov_d_imm(self, rd, imm8): self.emit(0x1E601000 | imm8 << 13 | rd)
    def fadd(self, d, n, m): self.emit(0x1E602800 | m << 16 | n << 5 | d)
    def fmul(self, d, n, m): self.emit(0x1E600800 | m << 16 | n << 5 | d)
    def fdiv(self, d, n, m): self.emit(0x1E601800 | m << 16 | n << 5 | d)
    def fsqrt(self, d, n): self.emit(0x1E61C000 | n << 5 | d)
    def fcvtzs(self, xd, dn): self.emit(0x9E780000 | dn << 5 | xd)
    def scvtf(self, dd, xn): self.emit(0x9E620000 | xn << 5 | dd)
    def fmov_x_d(self, xd, dn): self.emit(0x9E660000 | dn << 5 | xd)
    def stp_q(self, a, b, rn, off=0): self.emit(0xAD000000 | ((off // 16) & 0x7f) << 15 | b << 10 | rn << 5 | a)
    def ldp_q(self, a, b, rn, off=0): self.emit(0xAD400000 | ((off // 16) & 0x7f) << 15 | b << 10 | rn << 5 | a)
    def movi_zero(self, vd): self.emit(0x6F00E400 | vd)
    def ldr_q(self, rt, rn, off=0): self.emit(0x3DC00000 | (off // 16) << 10 | rn << 5 | rt)
    def str_q(self, rt, rn, off=0): self.emit(0x3D800000 | (off // 16) << 10 | rn << 5 | rt)
    # helpers
    def sys(self, name, *args):
        for i, a in enumerate(args):
            if isinstance(a, str): self.adr(i, a)
            elif isinstance(a, tuple): self.mov(i, a[0])      # ('reg',)
            else: self.mov_imm(i, a)
        self.mov_imm(8, SYS[name]); self.svc()


EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE = range(14)


def elf(code: bytes, data: bytes, bss=0x10000):
    ehsz, phsz = 64, 56
    code_off = 0x78
    data_off = (code_off + len(code) + 15) & ~15
    total = data_off + len(data)
    hdr = b'\x7fELF' + bytes([2, 1, 1, 0]) + b'\0' * 8
    hdr += struct.pack('<HHIQQQIHHHHHH', 2, 183, 1, BASE + code_off, ehsz, 0, 0, ehsz, phsz, 1, 0, 0, 0)
    ph = struct.pack('<IIQQQQQQ', 1, 7, 0, BASE, BASE, total, total + bss, 0x1000)
    img = hdr + ph
    img += b'\0' * (code_off - len(img)) + code
    img += b'\0' * (data_off - len(img)) + data
    return img, BASE + data_off


def run(prog_builder, name, expect_rc=0, expect_out=None, argv=()):
    a = Asm()
    prog_builder(a)
    code = a.assemble()
    img, dbase = elf(code, a.data)
    # patch absolute data address users: builder uses a.data_addr via adr only
    with tempfile.NamedTemporaryFile(delete=False, suffix='.elf') as f:
        f.write(img)
        path = f.name
    os.chmod(path, 0o755)
    try:
        p = subprocess.run([PEMU, *argv, path], capture_output=True, timeout=20)
    finally:
        os.unlink(path)
    ok = p.returncode == expect_rc and (expect_out is None or p.stdout == expect_out)
    print(('PASS ' if ok else 'FAIL ') + name + ('' if ok else f'  rc={p.returncode} out={p.stdout!r} err={p.stderr.decode()[:300]!r}'))
    return ok


def with_data(a, blob):
    """place blob in the data area; returns label usable with adr (code->data offset fixed at assemble time)"""
    off = len(a.data)
    a.data += blob
    return off


# -------------------------------------------------------------- test programs
def t_hello(a):
    # data is addressed pc-relatively: adr to label 'msg' defined past the code via helper
    a.adr(1, 'msg')
    a.mov_imm(0, 1); a.mov_imm(2, 13); a.mov_imm(8, 64); a.svc()
    a.mov_imm(0, 0); a.mov_imm(8, 93); a.svc()
    a.label('msg')
    # message embedded as code words
    for w in struct.unpack('<4I', b'Hello, pemu!\n\0\0\0'):
        a.emit(w)


def t_selftest(a):
    # each check: compute -> cmp -> b.ne fail_N ; fail code in x0
    checks = []

    def chk(n):
        a.bcond(NE, f'f{n}')
        checks.append(n)

    # 1: mul/udiv/sdiv
    a.mov_imm(1, 123); a.mov_imm(2, 456); a.mul(3, 1, 2)
    a.mov_imm(4, 56088); a.cmp(3, 4); chk(1)
    a.udiv(5, 3, 2); a.cmp(5, 1); chk(2)
    a.movn(6, 99)                    # -100
    a.mov_imm(7, 7); a.sdiv(5, 6, 7)
    a.movn(4, 13); a.cmp(5, 4); chk(3)                         # -14
    # 4: shifts / bitfields
    a.mov_imm(1, 0x1234); a.lsl_i(2, 1, 8); a.mov_imm(3, 0x123400); a.cmp(2, 3); chk(4)
    a.lsr_i(2, 3, 8); a.cmp(2, 1); chk(5)
    a.movn(1, 0xff)                  # 0xffffffffffffff00 (=-256)
    a.asr_i(2, 1, 4); a.movn(3, 0xf); a.cmp(2, 3); chk(6)      # -16
    a.mov_imm(1, 0xabcdef12); a.ubfx(2, 1, 8, 8); a.mov_imm(3, 0xef); a.cmp(2, 3); chk(7)
    a.mov_imm(1, 1); a.clz(2, 1); a.mov_imm(3, 63); a.cmp(2, 3); chk(8)
    a.mov_imm(1, 0x0102030405060708); a.rev(2, 1); a.mov_imm(3, 0x0807060504030201); a.cmp(2, 3); chk(9)
    # 10: csel / cset / flags
    a.mov_imm(1, 5); a.mov_imm(2, 9); a.cmp(1, 2); a.csel(3, 1, 2, LT); a.cmp(3, 1); chk(10)
    a.cmp(2, 1); a.cset(3, HI); a.cmp_i(3, 1); chk(11)
    a.movn(1, 0); a.mov_imm(2, 1); a.adds(3, 1, 2); a.cset(4, CS); a.cmp_i(4, 1); chk(12)     # carry out
    a.cmp_i(3, 0); chk(13)
    a.mov_imm(1, 1 << 63); a.mov_imm(2, 1 << 63); a.adds(3, 1, 2); a.cset(4, VS); a.cmp_i(4, 1); chk(14)  # signed overflow
    a.mov_imm(1, 0xffffffffffffffff); a.mov_imm(2, 2); a.umulh(3, 1, 2); a.cmp_i(3, 1); chk(15)
    # 16: load/store, pairs, pre/post index (stack)
    a.sub_i(31, 31, 64)
    a.mov_imm(1, 0x1122334455667788); a.str(1, 31, 8); a.ldr(2, 31, 8); a.cmp(1, 2); chk(16)
    a.ldrb(2, 31, 8); a.cmp_i(2, 0x88); chk(17)
    a.movz(3, 0x80); a.strb(3, 31, 0); a.ldrsb(4, 31, 0); a.movn(5, 0x7f); a.cmp(4, 5); chk(18)
    a.mov_imm(1, 111); a.mov_imm(2, 222); a.stp(1, 2, 31, 16); a.ldp(3, 4, 31, 16); a.cmp(3, 1); chk(19)
    a.cmp(4, 2); chk(20)
    a.mov_imm(1, 77); a.str_post(1, 31, -8 & 0x1ff); a.ldr_pre(2, 31, 8); a.cmp(2, 1); chk(21)
    a.mov_imm(1, 8); a.add_i(2, 31, 0); a.sub_i(2, 2, 8); a.ldr_reg(3, 2, 1)     # [x2, x1] -> sp+... same cell as stack+8-8+8
    a.ldr(4, 31, 0); a.cmp(3, 4); chk(22)
    a.add_i(31, 31, 64)
    # 23: bl / ret recursion (factorial 10)
    a.mov_imm(0, 10); a.bl('fact'); a.mov_imm(3, 3628800); a.cmp(0, 3); chk(23)
    # 24: fp
    a.fmov_d_imm(0, 0x70)            # 1.0
    a.fmov_d_imm(1, 0x00)            # 2.0
    a.fadd(2, 0, 1)                  # 3.0
    a.fmul(3, 2, 2)                  # 9.0
    a.fadd(3, 3, 0)                  # 10.0
    a.fdiv(3, 3, 1)                  # 5.0
    a.fsqrt(4, 3)                    # sqrt(5)
    a.fmul(4, 4, 4)                  # ~5
    a.scvtf(5, 1)
    a.fcvtzs(1, 3); a.cmp_i(1, 5); chk(24)
    # 25: exclusive monitor
    a.sub_i(31, 31, 16); a.mov_imm(1, 41); a.str(1, 31, 0)
    a.add_i(2, 31, 0)
    a.label('llsc'); a.ldxr(3, 2); a.add_i(3, 3, 1); a.stxr(4, 3, 2); a.cbnz(4, 'llsc')
    a.ldr(5, 31, 0); a.cmp_i(5, 42); chk(25)
    # Q-register pair copy (memcpy style)
    a.mov_imm(1, 0xdeadbeefcafef00d); a.str(1, 31, 0); a.str(1, 31, 8)
    a.ldr_q(0, 31, 0)
    a.sub_i(31, 31, 32)
    a.str_q(0, 31, 16); a.ldr(6, 31, 24); a.cmp(6, 1); chk(26)
    a.add_i(31, 31, 48)
    # TPIDR
    a.mov_imm(1, 0x4444); a.msr_tpidr(1); a.mrs_tpidr(2); a.cmp(1, 2); chk(27)
    a.mov_imm(0, 0); a.mov_imm(8, 93); a.svc()
    for n in checks:
        a.label(f'f{n}'); a.mov_imm(0, n); a.mov_imm(8, 93); a.svc()
    # fact(n): n<=1 -> 1 else n*fact(n-1)
    a.label('fact')
    a.cmp_i(0, 1); a.bcond(HI, 'fact_rec'); a.mov_imm(0, 1); a.ret()
    a.label('fact_rec')
    a.stp_pre(29, 30, 31, -16)
    a.mov(1, 0); a.sub_i(0, 0, 1)
    a.sub_i(31, 31, 16); a.str(1, 31, 0)
    a.bl('fact')
    a.ldr(1, 31, 0); a.add_i(31, 31, 16)
    a.mul(0, 0, 1)
    a.ldp_post(29, 30, 31, 16)
    a.ret()


def t_syscalls(a):
    # uname machine, newfstatat("/"), openat+read /etc/passwd, brk, mmap, getpid
    a.sub_i(31, 31, 1024)
    a.add_i(1, 31, 0)
    a.mov_imm(8, 160); a.mov(0, 1); a.svc()                       # uname(sp)
    a.ldrb(2, 31, 65 * 4); a.cmp_i(2, ord('a')); a.bcond(NE, 'f1')
    a.ldrb(2, 31, 65 * 4 + 1); a.cmp_i(2, ord('a')); a.bcond(NE, 'f1')   # "aarch64"
    a.ldrb(2, 31, 65 * 4 + 2); a.cmp_i(2, ord('r')); a.bcond(NE, 'f1')
    # newfstatat(AT_FDCWD=-100, "/", sp+512, 0)
    a.movn(0, 99); a.adr(1, 'root'); a.add_i(2, 31, 512); a.mov_imm(3, 0); a.mov_imm(8, 79); a.svc()
    a.cbnz(0, 'f2')
    a.ldr_w(2, 31, 512 + 16); a.mov_imm(3, 0xf000); a.and_(2, 2, 3)
    a.mov_imm(3, 0x4000); a.cmp(2, 3); a.bcond(NE, 'f3')
    # openat(AT_FDCWD, "/etc/passwd", O_RDONLY) + read 4 bytes
    a.movn(0, 99); a.adr(1, 'pw'); a.mov_imm(2, 0); a.mov_imm(8, 56); a.svc()
    a.cmp_i(0, 0); a.bcond(LT, 'f4')
    a.mov(9, 0); a.add_i(1, 31, 256); a.mov_imm(2, 4); a.mov_imm(8, 63); a.svc()
    a.cmp_i(0, 4); a.bcond(NE, 'f5')
    a.ldrb(2, 31, 256); a.cmp_i(2, ord('r')); a.bcond(NE, 'f5')
    # brk
    a.mov_imm(0, 0); a.mov_imm(8, 214); a.svc(); a.mov(9, 0)
    a.add_i(0, 9, 4095); a.mov_imm(8, 214); a.svc()
    a.cmp(0, 9); a.bcond(LS, 'f6')
    a.mov_imm(2, 0x55); a.strb(2, 9, 10); a.ldrb(3, 9, 10); a.cmp(2, 3); a.bcond(NE, 'f6')
    # mmap anon 8192
    a.mov_imm(0, 0); a.mov_imm(1, 8192); a.mov_imm(2, 3); a.mov_imm(3, 0x22); a.movn(4, 0); a.mov_imm(5, 0)
    a.mov_imm(8, 222); a.svc()
    a.cmp_i(0, 4095); a.bcond(LS, 'f7')
    a.mov_imm(2, 0x66); a.strb(2, 0, 8191); a.ldrb(3, 0, 8191); a.cmp(2, 3); a.bcond(NE, 'f7')
    # getpid > 0
    a.mov_imm(8, 172); a.svc(); a.cmp_i(0, 0); a.bcond(LE, 'f8')
    a.mov_imm(0, 0); a.mov_imm(8, 93); a.svc()
    for n in range(1, 9):
        a.label(f'f{n}'); a.mov_imm(0, 100 + n); a.mov_imm(8, 93); a.svc()
    a.label('root'); a.emit(struct.unpack('<I', b'/\0\0\0')[0])
    a.label('pw')
    for w in struct.unpack('<4I', b'/etc/passwd\0\0\0\0\0'):
        a.emit(w)


def t_fork_wait(a):
    a.sub_i(31, 31, 64)
    # clone(SIGCHLD, 0, 0, 0, 0)
    a.mov_imm(0, 17); a.mov_imm(1, 0); a.mov_imm(2, 0); a.mov_imm(3, 0); a.mov_imm(4, 0); a.mov_imm(8, 220); a.svc()
    a.cbz(0, 'child')
    a.mov(9, 0)
    # wait4(pid, &status, 0, 0)
    a.mov(0, 9); a.add_i(1, 31, 0); a.mov_imm(2, 0); a.mov_imm(3, 0); a.mov_imm(8, 260); a.svc()
    a.cmp(0, 9); a.bcond(NE, 'bad')
    a.ldr_w(2, 31, 0); a.lsr_i(2, 2, 8); a.cmp_i(2, 7); a.bcond(NE, 'bad')
    a.mov_imm(0, 0); a.mov_imm(8, 93); a.svc()
    a.label('bad'); a.mov_imm(0, 99); a.mov_imm(8, 93); a.svc()
    a.label('child'); a.mov_imm(0, 7); a.mov_imm(8, 93); a.svc()


def t_signal(a):
    # sigaction(SIGUSR1, {handler, 0, 0}, NULL) ; kill(getpid(), SIGUSR1); handler stores 1 in flag; exit(flag ? 0 : 1)
    a.sub_i(31, 31, 64)
    a.adr(1, 'handler'); a.str(1, 31, 0); a.mov_imm(2, 0); a.str(2, 31, 8); a.str(2, 31, 16)
    a.mov_imm(0, 10); a.add_i(1, 31, 0); a.mov_imm(2, 0); a.mov_imm(3, 8); a.mov_imm(8, 134); a.svc()
    a.cbnz(0, 'bad')
    a.mov_imm(20, 0)                       # flag lives in callee-saved x20 (restored by sigreturn unless handler changes it)
    a.mov_imm(8, 172); a.svc(); a.mov_imm(1, 10); a.mov_imm(8, 129); a.svc()
    # the handler can't change x20 (sigreturn restores) -> it writes to memory instead
    a.adr(5, 'flag'); a.ldr(2, 5, 0); a.cmp_i(2, 0x42); a.bcond(NE, 'bad')
    a.mov_imm(0, 0); a.mov_imm(8, 93); a.svc()
    a.label('bad'); a.mov_imm(0, 1); a.mov_imm(8, 93); a.svc()
    a.label('handler')
    a.cmp_i(0, 10); a.bcond(NE, 'hret')        # x0 must be the signal number
    a.adr(5, 'flag'); a.mov_imm(6, 0x42); a.str(6, 5, 0)
    a.label('hret'); a.ret()
    a.label('flag'); a.emit(0); a.emit(0)


def t_hello_args(a):
    # write argv[1] to stdout then exit(argc)
    a.ldr(2, 31, 0)                              # argc
    a.ldr(1, 31, 16)                             # argv[1]
    a.mov(21, 2)
    a.mov(3, 1)
    a.mov_imm(2, 0)
    a.label('len'); a.ldrb(4, 3, 0); a.cbz(4, 'out'); a.add_i(3, 3, 1); a.add_i(2, 2, 1); a.b('len')
    a.label('out'); a.mov_imm(0, 1); a.mov_imm(8, 64); a.svc()
    a.mov(0, 21); a.mov_imm(8, 93); a.svc()


def t_smc(a):
    # self-modifying code: write "movz x0,#7; ret", call it, rewrite as "movz x0,#9; ret", call again (needs IC invalidation)
    a.adr(9, 'buf')
    a.mov_imm(1, 0xD28000E0); a.str_w(1, 9, 0); a.mov_imm(1, 0xD65F03C0); a.str_w(1, 9, 4)
    a.emit(0xD50B7520 | 9); a.emit(0xD5033B9F); a.emit(0xD5033FDF)        # ic ivau,x9 ; dsb ish ; isb
    a.blr(9); a.mov(20, 0)
    a.mov_imm(1, 0xD2800120); a.str_w(1, 9, 0)
    a.emit(0xD50B7520 | 9); a.emit(0xD5033B9F); a.emit(0xD5033FDF)
    a.blr(9); a.add(0, 0, 20)                                               # 9 + 7 = 16
    a.mov_imm(8, 93); a.svc()
    a.label('buf'); a.emit(0); a.emit(0)


def t_async(a):
    # SIGALRM from setitimer must interrupt a tight polling loop (needs signal polling on backward branches)
    a.sub_i(31, 31, 64)
    a.adr(1, 'handler'); a.str(1, 31, 0); a.mov_imm(2, 0); a.str(2, 31, 8); a.str(2, 31, 16)
    a.mov_imm(0, 14); a.add_i(1, 31, 0); a.mov_imm(2, 0); a.mov_imm(3, 8); a.mov_imm(8, 134); a.svc()
    a.mov_imm(2, 0); a.str(2, 31, 32); a.str(2, 31, 40); a.str(2, 31, 48); a.mov_imm(2, 30000); a.str(2, 31, 56)
    a.mov_imm(0, 0); a.add_i(1, 31, 32); a.mov_imm(2, 0); a.mov_imm(8, 103); a.svc()
    a.adr(5, 'flag')
    a.label('spin'); a.ldr(2, 5, 0); a.cbz(2, 'spin')
    a.mov_imm(0, 0); a.mov_imm(8, 93); a.svc()
    a.label('handler'); a.adr(5, 'flag'); a.mov_imm(6, 1); a.str(6, 5, 0); a.ret()
    a.label('flag'); a.emit(0); a.emit(0)


def main():
    ok = True
    ok &= run(t_hello, 'hello', 0, b'Hello, pemu!\n')
    ok &= run(t_selftest, 'integer/fp/ldst selftest')
    ok &= run(t_syscalls, 'syscalls (uname, stat, open/read, brk, mmap)')
    ok &= run(t_fork_wait, 'fork + wait4')
    ok &= run(t_signal, 'signal delivery + sigreturn')
    ok &= run(t_smc, 'self-modifying code + IC invalidation', 16)
    ok &= run(t_async, 'async signal interrupts tight loop')
    # argv test: program path is last; extra args follow it
    a = Asm(); t_hello_args(a)
    img, _ = elf(a.assemble(), a.data)
    with tempfile.NamedTemporaryFile(delete=False, suffix='.elf') as f:
        f.write(img); path = f.name
    os.chmod(path, 0o755)
    p = subprocess.run([PEMU, path, 'world'], capture_output=True, timeout=20)
    good = p.returncode == 2 and p.stdout == b'world'
    print(('PASS ' if good else 'FAIL ') + 'argv passing' + ('' if good else f' rc={p.returncode} out={p.stdout!r} err={p.stderr!r}'))
    os.unlink(path)
    ok &= good
    print('RESULT ' + ('PASS' if ok else 'FAIL'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
