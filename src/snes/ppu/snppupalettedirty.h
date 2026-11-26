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
#endif
