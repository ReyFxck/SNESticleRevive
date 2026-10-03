#include "sngsuplanes.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern "C" void TestGsuPack(const Uint8*,Uint32,Uint32,Uint8*);
extern "C" void TestGsuLegacyPack(const Uint8*,Uint32,Uint32,Uint8*);
static Uint32 rng=0xb714fed;
static Uint8 randomByte() { rng=rng*1664525u+1013904223u; return rng>>24; }
int main() {
 for(unsigned pixel=0;pixel<8;++pixel) for(unsigned bit=0;bit<8;++bit) {
  Uint8 colors[9]={0}; colors[pixel+1]=1u<<bit;
  assert(SNGSUPackPixelPlanes(colors+1)==((Uint64)(1u<<(7-pixel))<<(bit*8)));
 }
 unsigned cases=0;
 for(unsigned flags=0;flags<256;++flags) for(unsigned pass=0;pass<128;++pass)
  for(unsigned bpp=2;bpp<=8;bpp*=2) {
   Uint8 colors[9], actual[10], expected[10];
   for(auto &c:colors) c=randomByte();
   for(auto &c:actual) c=randomByte();
   memcpy(expected,actual,sizeof actual);
   TestGsuPack(colors+1,flags,bpp,actual+1);
   TestGsuLegacyPack(colors+1,flags,bpp,expected+1);
   assert(!memcmp(actual,expected,sizeof actual)); ++cases;
  }
 printf("GSU planes: PASS (%u cases, every coverage mask, 2/4/8bpp, unaligned colors, guards)\n",cases);
}
