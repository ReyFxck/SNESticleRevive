#ifndef SNPPUPALETTEDIRTY_H
#define SNPPUPALETTEDIRTY_H
#include "types.h"

/* Two 32-bit words keep the group update cheap on the EE. The entry bitmap
   remains authoritative for CPU copies; this is just its cached projection. */
struct SnesPPUPaletteGroups
{
    Uint32 low, high;
    void Clear() { low = high = 0; }
    void All() { low = high = 0xFFFFFFFFu; }
    void Mark(Uint32 entry)
    {
        const Uint32 group = (entry & 255u) >> 2;
        if (group < 32u) low |= 1u << group;
        else high |= 1u << (group - 32u);
    }
    Uint64 Bits() const { return (Uint64)low | ((Uint64)high << 32); }
};
/* Consume one nonempty CLUT row in ascending order. Caller selects a
   32-bit half (rows 0..7 or 8..15). Never call ctz on zero. */
_INLINE Uint32 SnesPPUPopPaletteRow(Uint32 &bits, Uint32 &row) {
    row = (Uint32)__builtin_ctz(bits) >> 2;
    Uint32 shift = row * 4u;
    Uint32 groups = (bits >> shift) & 15u;
    bits &= ~(15u << shift);
    return groups;
}
#endif
