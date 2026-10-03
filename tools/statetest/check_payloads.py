"""Exercise the production bank reader/writer with fixed and variable payloads."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/platform/ps2/system/mainloop_state.cpp').read_text()
header = source[source.index('struct MainLoopStateFileHeaderT'):source.index('struct MainLoopStateConfigT')]
functions = source[source.index('static Int32 _MainLoopStateReadHeader('):source.index('static Bool _MainLoopStateGenerationNewer(')]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "miniz.h"
#include "mesence_bridge.h"
typedef uint8_t Uint8;
typedef uint32_t Uint32;
typedef int32_t Int32;
typedef char Char;
typedef bool Bool;
#define TRUE true
#define FALSE false
#define NES_MESENCE 1
#define MAINLOOP_STATE_FORMAT_VERSION 1
#define MAINLOOP_STATE_PAYLOAD_RAW 0
#define MAINLOOP_STATE_PAYLOAD_DEFLATE 1
struct MockNes {
 std::vector<Uint8> state;
 bool failAllocation=false;
 bool AllocateState(Uint32 n) { if(failAllocation) return false; state.resize(n); return true; }
 Uint8 *GetSnapshotData() {return state.data();}
} nes;
MockNes *_pNes=&nes;
void *_pSystem=_pNes;
static Uint8 snes[529304];
static Uint8 _MainLoop_StateCompressed[600000];
static Uint32 _MainLoopStateGetPayloadBytes() {return _pSystem==_pNes?nes.state.size():sizeof(snes);}
static Uint8 *_MainLoopStateGetPayloadData() {return _pSystem==_pNes?nes.state.data():snes;}
static Uint32 _MainLoopStateGetSystemId() {return _pSystem==_pNes?1:0;}
static const Uint8 _MainLoop_StateMagic[8]={'S','N','R','S','T','A','T','E'};
'''
suffix = r'''
static void writeHeader(const MainLoopStateFileHeaderT &h) {
 FILE *f=fopen("bank","r+b"); assert(f);
 assert(fwrite(&h,1,sizeof(h),f)==sizeof(h)); assert(!fclose(f));
}
int main() {
 static_assert(sizeof(MainLoopStateFileHeaderT)==64);
 for(bool isNes:{false,true}) for(bool compressed:{false,true}) {
  _pSystem=isNes?(void*)_pNes:(void*)snes;
  Uint32 n=isNes?24004:sizeof(snes);
  std::vector<Uint8> original(n);
  for(Uint32 i=0;i<n;++i) original[i]=(i*7+(i/43))&255;
  MainLoopStateFileHeaderT h={};
  memcpy(h.Magic,_MainLoop_StateMagic,8);
  h.uVersion=1;h.nHeaderBytes=64;h.uRomCRC=0x1357;h.nRomBytes=262144;
  h.uRomFlags=7;h.iSlot=2;h.uGeneration=5;
  h.uPayloadCRC=mz_crc32(0,original.data(),n);
  h.Reserved[0]=compressed?1:0;h.Reserved[2]=isNes?1:0;
  if(isNes) {h.Reserved[3]=n;h.Reserved[4]=MESENCE_STATE_FORMAT;}
  std::vector<Uint8> stored=original;
  if(compressed) {
   stored.resize(mz_compressBound(n));mz_ulong len=stored.size();
   assert(mz_compress2(stored.data(),&len,original.data(),n,MZ_BEST_SPEED)==MZ_OK);
   stored.resize(len);h.Reserved[1]=mz_crc32(0,stored.data(),stored.size());
  }
  h.nPayloadBytes=stored.size();
  assert(_MainLoopStateWriteBank("bank",&h,stored.data(),stored.size()));
  MainLoopStateFileHeaderT read;
  if(isNes) nes.state.clear(); // no previous snapshot size may be required
  else memset(snes,0,sizeof(snes));
  assert(_MainLoopStateReadHeader("bank",2,0x1357,262144,7,&read)==1);
  assert(_MainLoopStateReadPayload("bank",&read));
  assert(!memcmp(_MainLoopStateGetPayloadData(),original.data(),n));
  assert(_MainLoopStateReadHeader("bank",2,0x1358,262144,7,&read)==-2);
  assert(_MainLoopStateReadHeader("bank",1,0x1357,262144,7,&read)==-1);
  auto bad=h;bad.uPayloadCRC^=1;writeHeader(bad);
  assert(_MainLoopStateReadHeader("bank",2,0x1357,262144,7,&read)==1);
  assert(!_MainLoopStateReadPayload("bank",&read));
  if(isNes) {
   bad=h;bad.Reserved[4]=0;writeHeader(bad); // legacy InfoNES layout rejected
   assert(_MainLoopStateReadHeader("bank",2,0x1357,262144,7,&read)==-1);
   for(Uint32 size:{0u,16u,MESENCE_MAX_STATE_BYTES+1}) {
    bad=h;bad.Reserved[3]=size;writeHeader(bad);
    assert(_MainLoopStateReadHeader("bank",2,0x1357,262144,7,&read)==-1);
   }
   writeHeader(h);nes.failAllocation=true;
   assert(!_MainLoopStateReadPayload("bank",&h));nes.failAllocation=false;
  }
  bad=h;bad.Reserved[2]^=1;writeHeader(bad);
  assert(_MainLoopStateReadHeader("bank",2,0x1357,262144,7,&read)==-1);
  bad=h;memset(bad.Magic,0,8);writeHeader(bad); // interrupted bank commit
  assert(_MainLoopStateReadHeader("bank",2,0x1357,262144,7,&read)==-1);
  FILE *f=fopen("bank","wb");assert(f);fwrite(&h,1,sizeof(h),f);fclose(f);
  assert(_MainLoopStateReadHeader("bank",2,0x1357,262144,7,&read)==1);
  assert(!_MainLoopStateReadPayload("bank",&read)); // truncated payload
 }
 puts("State banks: PASS (SNES legacy, Mesen variable, raw/deflate, CRC, size/core/ROM checks, failed allocations and incomplete commits)");
}
'''
with tempfile.TemporaryDirectory(prefix='state-payloads-') as tmp:
    out = Path(tmp)
    cpp = out / 'fixture.cpp'
    cpp.write_text(prefix + header + functions + suffix)
    objects = []
    for name in ['miniz.c', 'miniz_tdef.c', 'miniz_tinfl.c', 'miniz_zip.c']:
        obj = out / (name + '.o')
        subprocess.run(['gcc', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-c', str(root / 'src/third_party/miniz' / name), '-o', str(obj)], check=True)
        objects.append(str(obj))
    subprocess.run(['g++', '-std=c++17', '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(root / 'src/third_party/miniz'), '-I' + str(root / 'src/nes/mesence'),
                    str(cpp), *objects, '-o', str(out / 'fixture')], check=True)
    subprocess.run([str(out / 'fixture')], cwd=out, check=True)
