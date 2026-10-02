#include "mesence_bridge.h"
#include "Shared/Emulator.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>
static uint64_t audioFrames=0, audible=0;
static void sink(void *, const int16_t *left, const int16_t *right, uint32_t frames) {
 audioFrames+=frames;
 for(uint32_t i=0;i<frames;++i) if(left[i]||right[i]) ++audible;
}
static uint32_t hash(const std::vector<uint32_t> &p) {
 uint32_t h=2166136261u; for(auto n:p) h=(h^n)*16777619u; return h;
}
/* Original tiny NROM fixture: input in battery RAM, pulse tone, background
   palette updated at vblank. No commercial game data or test-ROM download. */
static std::vector<uint8_t> rom(bool pal) {
 std::vector<uint8_t> data(16+32768+8192,0);
 memcpy(data.data(),"NES\x1a",4); data[4]=2; data[5]=1; data[6]=2; data[9]=pal;
 const uint8_t code[]={
  0x78,0xd8,0xa2,0xff,0x9a,0xa9,0,0x8d,0,0x20,0x8d,1,0x20,
  0xa9,1,0x8d,0x15,0x40,0xa9,0xbf,0x8d,0,0x40,
  0xa9,0x40,0x8d,2,0x40,0xa9,8,0x8d,3,0x40,
  0x2c,2,0x20,0x10,0xfb,0xa9,8,0x8d,1,0x20,
  0xa9,1,0x8d,0x16,0x40,0xa9,0,0x8d,0x16,0x40,
  0xad,0x16,0x40,0x29,1,0x8d,0,0x60,
  0x2c,2,0x20,0x10,0xfb,
  0xa9,0x3f,0x8d,6,0x20,0xa9,0,0x8d,6,0x20,
  0xad,0,0x60,0x09,0x20,0x8d,7,0x20,
  0xa9,0,0x8d,5,0x20,0x8d,5,0x20,
  0x4c,0x2b,0x80
 };
 memcpy(data.data()+16,code,sizeof(code));
 for(unsigned i=0x7ffa;i<0x8000;i+=2) {data[16+i]=0;data[16+i+1]=0x80;}
 return data;
}
static void loadState(MesenceCore *core, const std::vector<uint8_t>& state) {
 assert(MesenceAllocateState(core,state.size()));
 uint32_t bytes; auto ptr=MesenceStateData(core,&bytes);
 memcpy(ptr,state.data(),bytes);
}
static std::vector<uint8_t> snapshot(MesenceCore *core) {
 assert(MesenceSnapshot(core));
 uint32_t bytes; auto ptr=MesenceStateData(core,&bytes);
 return std::vector<uint8_t>(ptr,ptr+bytes);
}
static void checkChrBattery(MesenceCore *core) {
 auto image=rom(false); image.resize(16+32768);
 image[5]=0; image[7]=8; image[10]=0x70; image[11]=0x70;
 const uint8_t code[]={0x78,0xd8,0xa2,0xff,0x9a,
  0xa9,0x12,0x8d,6,0x20,0xa9,0x34,0x8d,6,0x20,
  0xad,7,0x20,0xad,7,0x20,0x8d,1,0x60,0x4c,5,0x80};
 memcpy(image.data()+16,code,sizeof(code));
 assert(MesenceLoad(core,image.data(),image.size()));
 uint32_t bytes; auto ram=MesenceSram(core,&bytes);
 if(bytes!=16384) fprintf(stderr,"CHR battery size: %u\n",bytes);
 assert(ram && bytes==16384);
 ram[123]=0xa5; ram[8192+0x1234]=0x5a;
 uint8_t pads[2]={};
 for(unsigned i=0;i<3;++i) assert(MesenceFrame(core,pads,nullptr,0,nullptr,nullptr));
 assert(ram[1]==0x5a && ram[123]==0xa5);
 auto saved=snapshot(core);
 ram[8192+0x1234]=0x7c;
 for(unsigned i=0;i<3;++i) assert(MesenceFrame(core,pads,nullptr,0,nullptr,nullptr));
 assert(ram[1]==0x7c);
 loadState(core,saved); assert(MesenceRestore(core));
 for(unsigned i=0;i<3;++i) assert(MesenceFrame(core,pads,nullptr,0,nullptr,nullptr));
 assert(ram[1]==0x5a && ram[8192+0x1234]==0x5a);
 assert(MesenceReset(core,1)); ram=MesenceSram(core,&bytes);
 for(unsigned i=0;i<3;++i) assert(MesenceFrame(core,pads,nullptr,0,nullptr,nullptr));
 assert(ram[1]==0x5a && ram[123]==0xa5);
 puts("MesenCE NES 2.0: PRG/CHR nonvolatile RAM and restore/reset PASS");
}
static void checkMapper(MesenceCore *core, unsigned mapper) {
 std::vector<uint8_t> image(16+65536+8192,0);
 memcpy(image.data(),"NES\x1a",4); image[4]=4; image[5]=1; image[6]=(mapper<<4)|2;
 unsigned bankBytes=mapper==1?16384:8192;
 for(unsigned bank=0;bank<65536/bankBytes;++bank) image[16+bank*bankBytes]=0x11*(bank+1);
 std::vector<uint8_t> code={0x78,0xd8,0xa2,0xff,0x9a};
 auto write=[&](uint16_t addr,uint8_t value) {
  const uint8_t op[]={0xa9,value,0x8d,(uint8_t)addr,(uint8_t)(addr>>8)};
  code.insert(code.end(),op,op+sizeof(op));
 };
 if(mapper==4) write(0xa001,0x80); // enable battery RAM
 const uint8_t first[]={0xad,0,0x80,0x8d,0,0x60};
 code.insert(code.end(),first,first+sizeof(first));
 if(mapper==1) {write(0x8000,0x80);for(unsigned i=0;i<5;++i) write(0xe000,i==0?1:0);}
 else {write(0x8000,6);write(0x8001,2);}
 const uint8_t second[]={0xad,0,0x80,0x8d,1,0x60};
 code.insert(code.end(),second,second+sizeof(second));
 unsigned start=65536-bankBytes; uint16_t pc=mapper==1?0xc000:0xe000;
 uint16_t stop=pc+code.size();code.push_back(0x4c);code.push_back(stop);code.push_back(stop>>8);
 memcpy(image.data()+16+start,code.data(),code.size());
 for(unsigned v=0xfffa;v<0x10000;v+=2) {image[16+v]=pc;image[16+v+1]=pc>>8;}
 assert(MesenceLoad(core,image.data(),image.size()));
 uint8_t pads[2]={};
 for(unsigned i=0;i<3;++i) assert(MesenceFrame(core,pads,nullptr,0,sink,nullptr));
 uint32_t bytes; auto ram=MesenceSram(core,&bytes);
 assert(ram && bytes==8192 && ram[0]==0x11 && ram[1]==(mapper==1?0x22:0x33));
 auto saved=snapshot(core); uint32_t frame=MesenceFrameCount(core);
 assert(MesenceFrame(core,pads,nullptr,0,nullptr,nullptr));
 loadState(core,saved); assert(MesenceRestore(core) && MesenceFrameCount(core)==frame);
 assert(MesenceFrame(core,pads,nullptr,0,nullptr,nullptr));
 assert(ram[0]==0x11 && ram[1]==(mapper==1?0x22:0x33));
 printf("MesenCE MMC%u: bank writes/battery/state PASS\n",mapper==1?1:3);
}
int main() {
 auto core=MesenceCreate(); assert(core);
 std::vector<uint32_t> pixels(256*256,0x12345678);
 for(bool pal:{false,true}) {
  auto image=rom(pal); assert(MesenceLoad(core,image.data(),image.size()));
  assert(MesenceFrameRate(core)==(pal?50u:60u));
  uint32_t initialFrame=MesenceFrameCount(core);
  uint8_t pads[2]={0,0};
  for(unsigned i=0;i<30;++i) MesenceFrame(core,pads,pixels.data(),256,sink,nullptr);
  uint32_t sramBytes; auto sram=MesenceSram(core,&sramBytes);
  if(!sram || sramBytes!=8192 || sram[0]!=0) fprintf(stderr,"SRAM %p bytes=%u first=%u frames=%u\n",(void*)sram,sramBytes,sram?sram[0]:255,MesenceFrameCount(core));
  assert(sram && sramBytes==8192 && sram[0]==0);
  uint32_t noInputHash=hash(pixels);
  pads[0]=1;
  for(unsigned i=0;i<5;++i) MesenceFrame(core,pads,pixels.data(),256,sink,nullptr);
  assert(sram[0]==1 && hash(pixels)!=noInputHash);
  assert(MesenceFrameCount(core)==initialFrame+35);
  /* Conversion must leave the unused texture rows alone. */
  for(unsigned i=256*240;i<pixels.size();++i) assert(pixels[i]==0x12345678);
  assert(MesenceSnapshot(core));
  uint32_t bytes; auto ptr=MesenceStateData(core,&bytes);
  assert(ptr && bytes>16 && bytes<MESENCE_MAX_STATE_BYTES);
  std::vector<uint8_t> state(ptr,ptr+bytes);
  std::vector<uint32_t> expected;
  for(unsigned i=0;i<12;++i) {
   pads[0]=i&1;
   MesenceFrame(core,pads,pixels.data(),256,sink,nullptr);
   expected.push_back(hash(pixels));
  }
  assert(MesenceAllocateState(core,bytes));
  ptr=MesenceStateData(core,&bytes); memcpy(ptr,state.data(),bytes);
  assert(MesenceRestore(core) && MesenceFrameCount(core)==initialFrame+35);
  for(unsigned i=0;i<12;++i) {
   pads[0]=i&1;
   MesenceFrame(core,pads,pixels.data(),256,sink,nullptr);
   assert(hash(pixels)==expected[i]);
  }
  ptr[0]^=0xff; assert(!MesenceRestore(core));
  /* Remove the final complete serializer entry: parsing succeeds, but a
     field is missing after earlier CPU/PPU fields have already been read. */
  auto live=snapshot(core), incomplete=state;
  size_t last=17, cursor=17;
  while(cursor<incomplete.size()) {
   last=cursor;
   while(incomplete[cursor]) ++cursor;
   uint32_t size; memcpy(&size,incomplete.data()+cursor+1,4);
   cursor+=1+4+size; assert(cursor<=incomplete.size());
  }
  incomplete.resize(last); uint32_t shortBytes=incomplete.size();
  memcpy(incomplete.data()+8,&shortBytes,4);
  loadState(core,incomplete); assert(!MesenceRestore(core));
  assert(snapshot(core)==live); // the current game remains intact
  loadState(core,state);
  assert(!MesenceAllocateState(core,0));
  assert(!MesenceAllocateState(core,MESENCE_MAX_STATE_BYTES+1));
  sram=MesenceSram(core,&sramBytes); sram[123]=0x5a;
  assert(MesenceReset(core,1)); sram=MesenceSram(core,&sramBytes); assert(sram[123]==0x5a);
  assert(MesenceReset(core,0));
  printf("MesenCE %s: input/video/state replay/SRAM/reset PASS (%u-byte snapshot)\n",pal?"PAL":"NTSC",bytes);
 }
 checkChrBattery(core);
 checkMapper(core,1); checkMapper(core,4);
 fprintf(stderr,"audio: %llu frames, %llu audible\n",(unsigned long long)audioFrames,(unsigned long long)audible);
 assert(audioFrames>50000 && audible>50000);
 MesenceUnload(core); assert(MesenceFrameCount(core)==0);
 MesenceDestroy(core);
 {
  /* Check the provided Emulator implementation, not a copied lifecycle. */
  Emulator emu; auto image=rom(false);
  assert(emu.LoadRom(VirtualFile(image.data(),image.size(),"fixture.nes"),VirtualFile(),true,false));
  assert(emu.GetRomInfo().RomFile.GetSize()==image.size());
  assert(emu.GetMemory(MemoryType::NesSaveRam).Size==8192);
  emu.Stop(false,false,false);
  assert(!emu.GetRomInfo().RomFile.IsValid());
  assert(!emu.GetConsoleUnsafe());
  assert(!emu.GetMemory(MemoryType::NesSaveRam).Memory && !emu.GetMemory(MemoryType::NesSaveRam).Size);
 }
 printf("MesenCE bridge: audio PASS (%llu stereo frames, %llu audible)\n",(unsigned long long)audioFrames,(unsigned long long)audible);
}
