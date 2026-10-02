#include "sngsuplanes.h"
extern "C" void TestGsuPack(const Uint8 *colors, Uint32 flags, Uint32 bpp, Uint8 *output)
{
 if(!flags) return;
 Uint64 planes = SNGSUPackPixelPlanes(colors);
 Uint8 coverage = SNGSUPixelCoverage(flags);
 for(Uint32 b=0; b<bpp; ++b) {
  Uint8 byte = flags==255 ? 0 : output[b];
  output[b] = (byte & ~coverage) | ((Uint8)planes & coverage);
  planes >>= 8;
 }
}
extern "C" void TestGsuLegacyPack(const Uint8 *colors, Uint32 flags, Uint32 bpp, Uint8 *output)
{
 if(!flags) return;
 for(Uint32 b=0; b<bpp; ++b) {
  Uint8 byte = flags==255 ? 0 : output[b];
  for(Uint32 i=0; i<8; ++i) if(flags & (1u<<i)) {
   Uint8 mask = 1u << (7-i);
   if((colors[i]>>b)&1) byte |= mask;
   else byte &= ~mask;
  }
  output[b]=byte;
 }
}
