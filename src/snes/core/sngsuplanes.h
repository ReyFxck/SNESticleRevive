#ifndef SNGSUPLANES_H
#define SNGSUPLANES_H
#include "types.h"
/* Pixel zero becomes the high bit of each SNES plane byte. No alignment
   assumption: both pixel caches are byte arrays in the existing GSU state. */
_INLINE Uint64 SNGSUPackPixelPlanes(const Uint8 *colors)
{
    Uint64 bits = ((Uint64)colors[0] << 56) | ((Uint64)colors[1] << 48) |
                  ((Uint64)colors[2] << 40) | ((Uint64)colors[3] << 32) |
                  ((Uint64)colors[4] << 24) | ((Uint64)colors[5] << 16) |
                  ((Uint64)colors[6] << 8) | colors[7];
    Uint64 swap = (bits ^ (bits >> 7)) & 0x00AA00AA00AA00AAULL;
    bits ^= swap ^ (swap << 7);
    swap = (bits ^ (bits >> 14)) & 0x0000CCCC0000CCCCULL;
    bits ^= swap ^ (swap << 14);
    swap = (bits ^ (bits >> 28)) & 0x00000000F0F0F0F0ULL;
    return bits ^ swap ^ (swap << 28);
}
_INLINE Uint8 SNGSUPixelCoverage(Uint8 flags)
{
    Uint32 bits = ((flags & 0xF0) >> 4) | ((flags & 0x0F) << 4);
    bits = ((bits & 0xCC) >> 2) | ((bits & 0x33) << 2);
    return (Uint8)(((bits & 0xAA) >> 1) | ((bits & 0x55) << 1));
}
#endif
