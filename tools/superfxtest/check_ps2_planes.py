"""Compare real R5900 plane conversion instructions, not PS2 frame rates."""
import argparse,json,os,random,struct,subprocess,tempfile
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS64,UC_MODE_LITTLE_ENDIAN,UC_HOOK_CODE
import unicorn.mips_const as regs
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ps2dev',type=Path,default=Path(os.environ.get('PS2DEV',str(Path.home()/'.local/ps2dev'))))
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='gsu-planes-') as tmp:
 obj=Path(tmp)/'planes.o'; binary=Path(tmp)/'planes.elf'
 prefix=str(args.ps2dev/'ee/bin/mips64r5900el-ps2-elf-')
 subprocess.run([prefix+'g++','-G0','-O2','-fno-exceptions','-fno-rtti','-fno-tree-vectorize','-DCODE_PLATFORM=3','-I'+str(root/'src/common/base'),'-I'+str(root/'src/snes/core'),'-c',str(root/'tools/superfxtest/planes_fixture.cpp'),'-o',str(obj)],check=True)
 subprocess.run([prefix+'ld','-Ttext=0x01000000','-e','TestGsuPack',str(obj),'-o',str(binary)],check=True)
 with binary.open('rb') as stream:
  elf=ELFFile(stream);uc=Uc(UC_ARCH_MIPS,UC_MODE_MIPS64|UC_MODE_LITTLE_ENDIAN)
  uc.ctl_set_cpu_model(regs.UC_CPU_MIPS64_MIPS64R2_GENERIC)
  pages=set()
  for seg in elf.iter_segments():
   if seg['p_type']!='PT_LOAD':continue
   for page in range(seg['p_vaddr']&~4095,(seg['p_vaddr']+seg['p_memsz']+4095)&~4095,4096):
    if page not in pages:uc.mem_map(page,4096);pages.add(page)
   uc.mem_write(seg['p_vaddr'],seg.data())
  symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 DATA=0x03000000;RET=DATA+4096;uc.mem_map(DATA,8192)
 count=0
 def hook(cpu,addr,size,data):
  global count
  count+=1
 uc.hook_add(UC_HOOK_CODE,hook)
 rng=random.Random(346712);totals={b:{'legacy':0,'packed':0,'cases':0} for b in (2,4,8)}
 coverage={b:{bits:{'legacy':0,'packed':0,'cases':0} for bits in range(1,9)} for b in totals}
 for bpp in totals:
  for flags in range(256):
   for sample in range(4):
    colors=bytes(rng.randrange(256) for _ in range(8));old=bytes(rng.randrange(256) for _ in range(10))
    expected=bytearray(old)
    for b in range(bpp):
     for pixel in range(8):
      if flags&(1<<pixel):
       m=1<<(7-pixel)
       expected[b+1]=(expected[b+1]&~m)|(((colors[pixel]>>b)&1)<<(7-pixel))
    for label,name in [('packed','TestGsuPack'),('legacy','TestGsuLegacyPack')]:
     uc.mem_write(DATA+1,colors);uc.mem_write(DATA+32,old)
     for i in range(1,32):uc.reg_write(getattr(regs,'UC_MIPS_REG_'+str(i)),0)
     for i,value in [(4,DATA+1),(5,flags),(6,bpp),(7,DATA+33),(31,RET)]:uc.reg_write(getattr(regs,'UC_MIPS_REG_'+str(i)),value)
     count=0;uc.emu_start(symbols[name],RET,count=5000)
     assert uc.reg_read(regs.UC_MIPS_REG_PC)==RET
     assert bytes(uc.mem_read(DATA+32,10))==expected,(name,flags,bpp,colors.hex())
     if flags: coverage[bpp][flags.bit_count()][label]+=count
     if flags==255:totals[bpp][label]+=count
    if flags: coverage[bpp][flags.bit_count()]['cases']+=1
    if flags==255:totals[bpp]['cases']+=1
 print(json.dumps({'result':'PASS','cases':3072,'full_cache_mean_instructions':{b:{k:v/totals[b]['cases'] for k,v in totals[b].items() if k!='cases'} for b in totals},'mean_instructions_by_covered_pixels':{b:{bits:{k:v/row['cases'] for k,v in row.items() if k!='cases'} for bits,row in rows.items()} for b,rows in coverage.items()},'limits':'conversion wrapper instructions; no RAM-access timing, whole-chip or PS2 FPS claim'}))
