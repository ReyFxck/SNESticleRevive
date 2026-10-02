from pathlib import Path
from io import BytesIO
import ctypes,struct,random,json,argparse,subprocess,tempfile
parser=argparse.ArgumentParser(description="Compare linked PS2 CPU assembly against the C interpreter (no hardware FPS claim).")
parser.add_argument("elf",type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
tmp=tempfile.TemporaryDirectory(prefix="sncpu-check-")
fixture=Path(tmp.name)/"fixture.so"
subprocess.run(["gcc","-shared","-fPIC","-O2","-fwrapv","-fno-strict-aliasing","-DCODE_PLATFORM=1","-DCODE_DEBUG=0","-DCODE_PROFILE=0","-DSNDBG_LOG=0",*["-I"+str(root/p) for p in ["src/common/base","src/common/debug","src/snes/core","src/snes/cpu","src/snes"]],str(root/"tools/cputest/cpu_fixture.c"),*[str(root/"src/snes/cpu"/p) for p in ["sncpu.c","sncpu_c.c","sndisasm.c"]],"-o",str(fixture)],check=True)
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_MIPS,UC_MODE_MIPS64,UC_MODE_LITTLE_ENDIAN,UC_HOOK_CODE
import unicorn.mips_const as regs
ELF=args.elf
BASE=0x03000000;SIZE=0x1010000;CPU=0x02800000;STACK=0x02900000;RET=0x02a00000
lib=ctypes.CDLL(str(fixture));lib.FixtureMemory.restype=ctypes.POINTER(ctypes.c_ubyte)
lib.FixtureRun.argtypes=[ctypes.POINTER(ctypes.c_ubyte)]
mem=lib.FixtureMemory();elf=ELFFile(BytesIO(Path(ELF).read_bytes()));uc=Uc(UC_ARCH_MIPS,UC_MODE_MIPS64|UC_MODE_LITTLE_ENDIAN)
uc.ctl_set_cpu_model(regs.UC_CPU_MIPS64_MIPS64R2_GENERIC)
pages=set()
for seg in elf.iter_segments():
 if seg['p_type']!='PT_LOAD':continue
 for page in range(seg['p_vaddr']&~4095,(seg['p_vaddr']+seg['p_memsz']+4095)&~4095,4096):
  if page not in pages:uc.mem_map(page,4096);pages.add(page)
 uc.mem_write(seg['p_vaddr'],seg.data())
symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s['st_value']}
for a,n in [(BASE,SIZE),(CPU,0x10000),(STACK,0x10000),(RET,4096)]:uc.mem_map(a,n)
for name in ['_SNCpuDecimalADC8','_SNCpuDecimalADC16','_SNCpuDecimalSBC8','_SNCpuDecimalSBC16']:
 getattr(lib,name).argtypes=[ctypes.c_uint32]*3;getattr(lib,name).restype=ctypes.c_uint32
calls={symbols[name]:getattr(lib,name) for name in ['_SNCpuDecimalADC8','_SNCpuDecimalADC16','_SNCpuDecimalSBC8','_SNCpuDecimalSBC16']}
trace=[]
def hook(cpu,address,size,data):
 if len(trace)<1000:trace.append(address)
 if address in calls:
  values=[cpu.reg_read(getattr(regs,'UC_MIPS_REG_'+str(i)))&0xffffffff for i in (4,5,6)]
  cpu.reg_write(regs.UC_MIPS_REG_2,calls[address](*values));cpu.reg_write(regs.UC_MIPS_REG_PC,cpu.reg_read(regs.UC_MIPS_REG_RA))
uc.hook_add(UC_HOOK_CODE,hook)
banks=b''.join(struct.pack('<IIIBBBB',BASE,0,0,8,255,0,0) for _ in range(SIZE//8192))
rng=random.Random(713);zero=bytes(SIZE)
def check(op,mode=0,p=0x34,dp=0x100,pc=0x018000,cycles=1,core="SNCPUExecute_ASM_Plain"):
 global trace
 state=bytearray(60);a=0x1234;x=0x17;y=0x21;s=0x1ff
 struct.pack_into('<HHHHBBHII',state,0,a,x,y,s,p,mode,dp,pc,0x020000)
 struct.pack_into('<iIIII',state,20,cycles,100,100,100,100);state[49]=1;state[52]=0x75;struct.pack_into('<I',state,56,1000)
 uc.mem_write(BASE,zero);ctypes.memset(mem,0,SIZE)
 updates={pc:bytes([op,0x34,0x12,0x03,0xea,0xea]),0xffe0:bytes(range(32)),0x0100:bytes([0x34,0x12,0x07])*85}
 for addr,v in updates.items():uc.mem_write(BASE+addr,v);ctypes.memmove(ctypes.addressof(mem.contents)+addr,v,len(v))
 uc.mem_write(CPU,bytes(state)+banks)
 cstate=(ctypes.c_ubyte*60).from_buffer_copy(state);lib.FixtureRun(cstate)
 for i in range(1,32):uc.reg_write(getattr(regs,'UC_MIPS_REG_'+str(i)),0)
 uc.reg_write(regs.UC_MIPS_REG_4,CPU);uc.reg_write(regs.UC_MIPS_REG_SP,STACK+0xff00);uc.reg_write(regs.UC_MIPS_REG_RA,RET)
 trace=[]
 try:uc.emu_start(symbols[core],RET,count=10000)
 except Exception as e:
  print('exception',hex(op),mode,hex(p),hex(uc.reg_read(regs.UC_MIPS_REG_PC)),[hex(a) for a in trace[-8:]],e);raise
 assert uc.reg_read(regs.UC_MIPS_REG_PC)==RET,(op,'no return')
 out=bytes(uc.mem_read(CPU,60));expected=bytes(cstate)
 diffs=[(i,out[i],expected[i]) for i in list(range(40))+[49,52,56,57,58,59] if out[i]!=expected[i]]
 if diffs:return {'pc':hex(pc),'budget':cycles,'opcode':hex(op),'E':mode,'P':hex(p),'diffs':diffs,'mips':out.hex(),'c':expected.hex()}
 if bytes(uc.mem_read(BASE,SIZE))!=ctypes.string_at(mem,SIZE):return {'pc':hex(pc),'budget':cycles,'opcode':hex(op),'E':mode,'P':hex(p),'memory':'different'}
 return None
if __name__=='__main__':
 fails=[];n=0
 for core in ['SNCPUExecute_ASM_Plain','SNCPUExecute_ASM']:
  for e,flags in [(1,0x34),(0,0x34),(0,0x24),(0,0x14),(0,0x04)]:
   for op in range(256):
    result=check(op,e,flags,core=core);n+=1
    if result:fails.append(result);print(json.dumps({'core':core,**result}),flush=True)
  for pc in [0x017fff,0x019fff,0x01ffff]:
   for op in [0xea,0xa9,0xa2,0x00,0x42]:
    for cycles in [0,-1,1,40]:
     result=check(op,pc=pc,cycles=cycles,core=core);n+=1
     if result:fails.append(result);print(json.dumps({'core':core,**result}),flush=True)
 print('cases',n,'failures',len(fails))
 raise SystemExit(bool(fails))
