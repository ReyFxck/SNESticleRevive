#ifndef SNPPUDIRECTCOLOR_H
#define SNPPUDIRECTCOLOR_H
#include "types.h"
#include "snppuhires.h"
/* Mode 7 has no tile palette-group bits. Zero index is transparent. */
_INLINE Uint16 SnesPPUMode7Direct15(Uint8 index) {
    return (Uint16)(((index & 7u) << 2) | ((index & 0x38u) << 4) | ((index & 0xC0u) << 7));
}
_INLINE void SnesPPUMode7DirectLine(Uint16 *out, const Uint8 *indices,
    const Uint32 *rank0, const Uint32 *drawnOBJ, const Uint16 *cgram, Uint32 intensity) {
    for (unsigned x=0; x<256; ++x) {
        unsigned bit = 1u << (x & 31);
        Uint8 index = indices[x];
        // Rank bit 0 is unique to BG1 in Mode 7. Actual OBJ draws override it.
        bool direct = index && (rank0[x>>5] & bit) && !(drawnOBJ[x>>5] & bit);
        Uint16 c = SnesPPUColor15ToGS16(direct ? SnesPPUMode7Direct15(index) : cgram[index], intensity);
        out[2*x] = out[2*x+1] = c;
    }
}
#endif
