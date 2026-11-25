#!/usr/bin/env python3
"""Check actual linked scalar MIPS voice mixer instructions, including delay slots.
Also accepts a generic MIPS relocatable object for host development. Not a timing model.
"""
import argparse
import random
import re
import struct
import subprocess
from pathlib import Path


def signed(v, bits):
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def read_code(path):
    syms = subprocess.check_output(['readelf', '-sW', str(path)], text=True)
    matches = [line.split() for line in syms.splitlines()
               if line.split() and line.split()[-1] == 'SNSpcMixVoicePS2'
               and 'FUNC' in line and 'UND' not in line]
    assert matches, 'voice symbol missing: use unstripped ELF'
    sym = matches[0]
    addr, size, section = int(sym[1], 16), int(sym[2]), int(sym[6])
    sections = subprocess.check_output(['readelf', '-SW', str(path)], text=True)
    match = re.search(r'^\s*\[\s*' + str(section) + r'\]\s+\S+\s+\S+\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)', sections, re.M)
    assert match and size and size % 4 == 0
    base, offset = (int(x, 16) for x in match.groups())
    start = offset + addr - base
    return struct.unpack('<' + 'I' * (size // 4), path.read_bytes()[start:start + size])


def check(code, rng, n, vl, vr):
    count = max(n, 0)
    pcm = [rng.randrange(-32768, 32768) for _ in range(count)]
    env = [rng.randrange(256) for _ in range(count)]
    left = [rng.randrange(-2**31, 2**31) for _ in range(count)]
    right = [rng.randrange(-2**31, 2**31) for _ in range(count)]
    # Guards also catch writes when n <= 0 and out-of-bounds reads.
    mem = {}
    for addr, data in ((0x1000, struct.pack('<'+'i'*count, *left)),
                       (0x2000, struct.pack('<'+'i'*count, *right)),
                       (0x3000, struct.pack('<'+'h'*count, *pcm)),
                       (0x4000, bytes(env))):
        mem[addr] = bytearray(data)
    def access(addr, size):
        for base, buf in mem.items():
            if base <= addr and addr + size <= base + len(buf):
                return buf, addr - base
        raise AssertionError(('out of bounds', hex(addr), size))
    r = [0]*32
    r[4:11] = [0x1000, 0x2000, 0x3000, 0x4000, n, vl, vr]
    r[31] = -4
    lo = 0
    def execute(w):
        nonlocal lo
        op, rs, rt, rd, sh, fn = w>>26, (w>>21)&31, (w>>16)&31, (w>>11)&31, (w>>6)&31, w&63
        imm = signed(w, 16)
        if w == 0: return
        if op in (0x21, 0x24, 0x23):  # lh, lbu, lw
            sz = {0x21:2, 0x24:1, 0x23:4}[op]
            buf, off = access(r[rs]+imm, sz)
            r[rt] = int.from_bytes(buf[off:off+sz], 'little', signed=op!=0x24)
        elif op == 0x2b:
            buf, off = access(r[rs]+imm, 4)
            buf[off:off+4] = (r[rt]&0xffffffff).to_bytes(4,'little')
        elif op == 9: r[rt] = signed(r[rs]+imm, 32)
        elif op == 0 and fn == 0x18:
            lo = signed(r[rs]*r[rt],32)
            if rd: r[rd] = lo
        elif op == 0 and fn == 0x12: r[rd] = lo
        elif op == 0 and fn == 3: r[rd] = signed(r[rt],32)>>sh
        elif op == 0 and fn == 0x21: r[rd] = signed(r[rs]+r[rt],32)
        else: raise AssertionError(('unexpected instruction', hex(w)))
        r[0] = 0
    pc = 0
    steps = 0
    while pc != -1:
        assert 0 <= pc < len(code) and steps < 100000
        steps += 1
        w = code[pc]
        op, rs = w>>26, (w>>21)&31
        if op in (6,7) or (op == 0 and w&63 == 8):
            if op == 0: target = r[rs]//4
            else:
                taken = r[rs] <= 0 if op == 6 else r[rs] > 0
                target = pc+1+signed(w,16) if taken else pc+2
            execute(code[pc+1]); pc = target
        else:
            execute(w); pc += 1
    def expected(samples, vol):
        return [signed(a+((p*e)>>7)*vol,32) for a,p,e in zip(samples,pcm,env)]
    assert mem[0x1000] == struct.pack('<'+'i'*count, *expected(left,vl))
    assert mem[0x2000] == struct.pack('<'+'i'*count, *expected(right,vr))
    assert mem[0x3000] == struct.pack('<'+'h'*count, *pcm)
    assert mem[0x4000] == bytes(env)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    code = read_code(parser.parse_args().elf)
    rng = random.Random(6516)
    for i in range(2048):
        check(code, rng, rng.choice([-3,0,1,2,3,7,8,17,67,68,69]),
              rng.choice([-128,-1,0,1,127,rng.randrange(-128,128)]),
              rng.choice([-128,-1,0,1,127,rng.randrange(-128,128)]))
    print('PS2 voice instruction model: PASS (2048 cases; PCM, overflow, zero count, bounds)')
