from pathlib import Path
from io import BytesIO
import random,struct,json,argparse
from elftools.elf.elffile import ELFFile
parser=argparse.ArgumentParser(description="Check linked R5900 echo MMI against its scalar arithmetic contract.")
parser.add_argument("elf",type=Path)
ELF=parser.parse_args().elf
e=ELFFile(BytesIO(ELF.read_bytes()));sym=next(s for s in e.get_section_by_name('.symtab').iter_symbols() if s.name=='_Z8_MixEchoPsPiS_iii');start=sym['st_value']
t=e.get_section_by_name('.text');data=t.data();code={t['sh_addr']+i:int.from_bytes(data[i:i+4],'little') for i in range(0,len(data),4)}
MASK=(1<<128)-1;LOW=(1<<64)-1
rng=random.Random(2608)
def signed(v,b):v&=(1<<b)-1;return v-(1<<b) if v>>(b-1) else v
def lanes(v,b):return [signed(v>>(i*b),b) for i in range(128//b)]
def pack(a,b):return sum((v&((1<<b)-1))<<(i*b) for i,v in enumerate(a))
def clamp(v):return max(-32768,min(32767,v))
def check(n,mv,ev):
 main=[rng.choice([-2147483648,2147483647,-4194304,4194304,rng.randrange(-5000000,5000000)]) for _ in range(n)]
 echo=[rng.randrange(-32768,32768) for _ in range(n)]
 r=[0]*32;r[4]=0x200010;r[5]=0x210000;r[6]=0x220000;r[7]=n;r[8]=mv&LOW;r[9]=ev&LOW;r[31]=0x300000
 mem={0x200000:bytearray(b'\xa5'*(n*2+32)),0x210000:bytearray(struct.pack('<'+'i'*n,*main)),0x220000:bytearray(struct.pack('<'+'h'*n,*echo))}
 def load(a,n):
  for b,v in mem.items():
   if b<=a and a+n<=b+len(v):return int.from_bytes(v[a-b:a-b+n],'little')
  raise AssertionError(('read outside',hex(a),n))
 def store(a,v,n):
  for b,m in mem.items():
   if b<=a and a+n<=b+len(m):m[a-b:a-b+n]=(v&((1<<(8*n))-1)).to_bytes(n,'little');return
  raise AssertionError(('write outside',hex(a),n))
 hi=lo=0;pc=start;steps=0
 def ins(w):
  nonlocal hi,lo
  op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;sh=(w>>6)&31;fn=w&63;imm=signed(w,16);sg=w&0xfc0007ff
  if not w:return
  if op==0 and fn==0x25:r[rd]=r[rs]|r[rt]
  elif op==9:r[rt]=(r[rt]&~LOW)|(signed(r[rs]+imm,32)&LOW)
  elif op==0x1e:r[rt]=load((r[rs]+imm)&0xffffffff,16)
  elif op==0x1f:store((r[rs]+imm)&0xffffffff,r[rt],16)
  elif op==0x1c:
   if sg==0x700006e9:r[rd]=pack([lanes(r[rt],16)[0]]*4+[lanes(r[rt],16)[4]]*4,16)
   elif sg==0x70000389:r[rd]=((r[rs]&LOW)<<64)|(r[rt]&LOW)
   elif sg==0x700003a9:r[rd]=(r[rs]>>64)|(r[rt]&(~LOW&MASK))
   elif sg==0x700004e9:r[rd]=~(r[rs]|r[rt])&MASK
   elif fn in [0x3c,0x3e,0x3f]:
    a=lanes(r[rt],32);r[rd]=pack([v<<sh if fn==0x3c else (v&0xffffffff)>>sh if fn==0x3e else v>>sh for v in a],32)
   elif sg in [0x700000e8,0x700000c8]:r[rd]=pack([(min if sg==0x700000e8 else max)(a,b) for a,b in zip(lanes(r[rs],32),lanes(r[rt],32))],32)
   elif sg==0x700005c8:r[rd]=pack(lanes(r[rt],32)+lanes(r[rs],32),16)
   elif sg in [0x70000709,0x70000409]:
    products=[a*b for a,b in zip(lanes(r[rs],16),lanes(r[rt],16))]
    lv=[products[i] for i in [0,1,4,5]];hv=[products[i] for i in [2,3,6,7]]
    if sg==0x70000409:lv=[a+b for a,b in zip(lanes(lo,32),lv)];hv=[a+b for a,b in zip(lanes(hi,32),hv)]
    lo=pack(lv,32);hi=pack(hv,32)
   elif sg==0x70000249:r[rd]=lo
   elif sg==0x70000209:r[rd]=hi
   else:raise AssertionError(('MMI',hex(w),hex(sg)))
  else:raise AssertionError(('instruction',hex(w)))
  r[0]=0
 while pc!=0x300000:
  assert steps<30000;steps+=1;w=code[pc];op=w>>26;rs=(w>>21)&31
  if op==7 or (op==0 and w&63==8):
   dest=pc+4+4*signed(w,16) if op==7 and signed(r[rs],64)>0 else pc+8 if op==7 else r[rs]&0xffffffff
   ins(code[pc+4]);pc=dest
  else:ins(w);pc+=4
 expected=[clamp((clamp(a>>7)*mv+b*ev)>>6) for a,b in zip(main,echo)]
 assert mem[0x200000]==b'\xa5'*16+struct.pack('<'+'h'*n,*expected)+b'\xa5'*16,(n,mv,ev)
for _ in range(2000):check(rng.choice([8,16,24,512,1024,1072]),rng.randrange(-128,128),rng.randrange(-128,128))
print(json.dumps({'result':'PASS','cases':2000,'scope':'compiled R5900 echo MMI, int32 extremes, signed volumes, PCM and destination guards','limits':'modeled instructions, not PS2 playback or FPS'}))
