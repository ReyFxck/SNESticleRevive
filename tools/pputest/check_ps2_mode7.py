"""Compile the production Mode 7 helpers for R5900 and compare their pixels
with the scalar SNES addressing contract. Requires Unicorn and pyelftools.
R5900 MULT's third destination is modeled. This does not model caches/GS/FPS.
"""
from pathlib import Path
from io import BytesIO
import random, argparse, subprocess, tempfile, shutil
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS64, UC_MODE_LITTLE_ENDIAN, UC_HOOK_CODE
import unicorn.mips_const as regs
from elftools.elf.elffile import ELFFile

OUTPUT, VRAM, STACK, RETURN = 0x2000000, 0x2100000, 0x2200000, 0x3000000
rng = random.Random(7219)
vram = bytes(rng.randrange(256) for _ in range(65536))

class Runner:
    def __init__(self, filename):
        elf = ELFFile(BytesIO(Path(filename).read_bytes()))
        self.cpu = Uc(UC_ARCH_MIPS, UC_MODE_MIPS64 | UC_MODE_LITTLE_ENDIAN)
        pages = set()
        for segment in elf.iter_segments():
            if segment['p_type'] == 'PT_LOAD':
                begin = segment['p_vaddr'] & ~4095
                end = (segment['p_vaddr'] + segment['p_memsz'] + 4095) & ~4095
                for page in range(begin, end, 4096):
                    if page not in pages:
                        self.cpu.mem_map(page, 4096)
                        pages.add(page)
                self.cpu.mem_write(segment['p_vaddr'], segment.data())
        self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        for start in (OUTPUT, VRAM, STACK, RETURN): self.cpu.mem_map(start, 65536)
        self.cpu.mem_write(VRAM, vram)
        self.instructions = 0
        self.cpu.hook_add(UC_HOOK_CODE, self.count)

    def count(self, cpu, address, size, userdata):
        self.instructions += 1
        word=int.from_bytes(cpu.mem_read(address,4),'little')
        rd=(word>>11)&31
        if (word & 0xfc00003f) in (0x18,0x19) and rd:
            # R5900 adds a GPR destination to MULT/MULTU; standard MIPS64
            # rejects that encoding. Reproduce the integer instruction only.
            rs=(word>>21)&31; rt=(word>>16)&31
            a=cpu.reg_read(getattr(regs,'UC_MIPS_REG_'+str(rs)))&0xffffffff
            b=cpu.reg_read(getattr(regs,'UC_MIPS_REG_'+str(rt)))&0xffffffff
            if (word&63)==0x18:
                if a&0x80000000:a-=1<<32
                if b&0x80000000:b-=1<<32
            product=a*b
            lo=product&0xffffffff; hi=(product>>32)&0xffffffff
            if lo&0x80000000:lo-=1<<32
            if hi&0x80000000:hi-=1<<32
            cpu.reg_write(regs.UC_MIPS_REG_LO,lo&0xffffffffffffffff)
            cpu.reg_write(regs.UC_MIPS_REG_HI,hi&0xffffffffffffffff)
            cpu.reg_write(getattr(regs,'UC_MIPS_REG_'+str(rd)),lo&0xffffffffffffffff)
            cpu.reg_write(regs.UC_MIPS_REG_PC,address+4)

    def run(self, name, vector, length=256):
        for number in range(1, 32): self.cpu.reg_write(getattr(regs, 'UC_MIPS_REG_' + str(number)), 0)
        self.cpu.reg_write(regs.UC_MIPS_REG_SP, STACK + 60000)
        self.cpu.reg_write(regs.UC_MIPS_REG_RA, RETURN)
        for number, value in enumerate((OUTPUT, length, VRAM, *vector), 4):
            self.cpu.reg_write(getattr(regs, 'UC_MIPS_REG_' + str(number)), value & 0xffffffffffffffff)
        self.cpu.mem_write(OUTPUT, bytes([165]) * 512)
        self.instructions = 0
        try:
            self.cpu.emu_start(self.symbols[name], RETURN, count=100000)
        except Exception:
            pc=self.cpu.reg_read(regs.UC_MIPS_REG_PC)
            print('Failed PC', hex(pc), 'name/vec/n',name,vector,length)
            raise
        assert self.cpu.reg_read(regs.UC_MIPS_REG_PC) == RETURN
        return bytes(self.cpu.mem_read(OUTPUT, 512)), self.instructions

def reference(name, vector, length):
    x, y, dx, dy = vector
    out = bytearray([165] * 512)
    for i in range(max(0, length)):
        px, py = x >> 8, y >> 8
        outside = px < 0 or py < 0 or px > 1023 or py > 1023
        tile = ((py & 1023) >> 3) * 128 + ((px & 1023) >> 3)
        char = vram[tile * 2]
        if outside and name == 'clamp': char = 0
        out[i] = 0 if outside and name == 'black' else vram[(char * 64 + (py & 7) * 8 + (px & 7)) * 2 + 1]
        x += dx
        y += dy
    return bytes(out)




parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cxx', default=shutil.which('mips64r5900el-ps2-elf-g++'))
args = parser.parse_args()
if not args.cxx: parser.error('provide --cxx /path/to/mips64r5900el-ps2-elf-g++')
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='ps2-mode7-') as directory:
    work = Path(directory)
    (work/'wrapper.cpp').write_text('#include "snppumode7.h"\nextern "C" {\nvoid repeat(Uint8 *o,Int32 n,const Uint8 *v,Int32 x,Int32 y,Int32 dx,Int32 dy) { SnesPPUMode7FetchRepeat(o,n,v,x,y,dx,dy); }\nvoid clamp(Uint8 *o,Int32 n,const Uint8 *v,Int32 x,Int32 y,Int32 dx,Int32 dy) { SnesPPUMode7FetchClamp(o,n,v,x,y,dx,dy); }\nvoid black(Uint8 *o,Int32 n,const Uint8 *v,Int32 x,Int32 y,Int32 dx,Int32 dy) { SnesPPUMode7FetchBlack(o,n,v,x,y,dx,dy); }\nUint8 mask(Uint64 v) { return SnesPPUMode7ByteHighBits(v); }\n}\n')
    (work/'link.ld').write_text('SECTIONS { . = 0x100000; .text : { *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) } }')
    flags = ['-G0','-O2','-fno-strict-aliasing','-fno-tree-vectorize',
             '-fno-aggressive-loop-optimizations','-fno-tree-pre',
             '-fno-tree-loop-distribute-patterns','-fno-delete-null-pointer-checks',
             '-fno-isolate-erroneous-paths-dereference','-fwrapv','-fsigned-char',
             '-DCODE_PLATFORM=2','-DCODE_DEBUG=0','-DCODE_PROFILE=0',
             '-I'+str(root/'src/common/base'),'-I'+str(root/'src/snes/ppu')]
    subprocess.run([args.cxx,*flags,'-c',str(work/'wrapper.cpp'),'-o',str(work/'wrapper.o')],check=True)
    subprocess.run([args.cxx,'-nostdlib','-Wl,-T,'+str(work/'link.ld'),'-Wl,-e,repeat',str(work/'wrapper.o'),'-lgcc','-o',str(work/'test.elf')],check=True)
    test = Runner(work/'test.elf')
    vectors = [(0,0,0,0),(-1,-1,-32768,32768),(2047,2047,32768,-32768),
               (0x20000000,0,256,0),(-0x20000001,0,-256,0),(0,0,40000,-40000)]
    vectors += [(rng.randrange(-4000000,4000000),rng.randrange(-4000000,4000000),rng.randrange(-32768,32769),rng.randrange(-32768,32769)) for _ in range(40)]
    cases = 0
    for name in ('repeat','clamp','black'):
        for vector in vectors:
            for length in (0,1,3,4,7,8,9,15,255,256):
                actual,_ = test.run(name,vector,length)
                assert actual == reference(name,vector,length),(name,vector,length)
                cases += 1
    for bits in range(256):
        value = sum((((bits>>i)&1)*128 | rng.randrange(128))<<(i*8) for i in range(8))
        test.cpu.reg_write(regs.UC_MIPS_REG_4,value)
        test.cpu.reg_write(regs.UC_MIPS_REG_RA,RETURN)
        test.cpu.reg_write(regs.UC_MIPS_REG_SP,STACK+60000)
        test.cpu.emu_start(test.symbols['mask'],RETURN,count=1000)
        assert test.cpu.reg_read(regs.UC_MIPS_REG_PC)==RETURN
        assert test.cpu.reg_read(regs.UC_MIPS_REG_2)&255==bits
    print('PASS:',cases,'compiled R5900 repeat/clamp/black vectors with guards; 256 byte-mask patterns')
    print('Modeled integer instructions only; no physical PS2 timing or GS execution.')
