/* nesstate.h
 *
 * NES save-state container for the NesSystem wrapper around InfoNES.
 *
 * This is a Phase 2 PLACEHOLDER. The real NES state will be filled in
 * during Phase 5 once the InfoNES core is actually running and we know
 * exactly which globals (CPU registers, PPU registers, RAM, VRAM, SRAM,
 * pAPU regs, mapper state) need to round-trip through SaveState /
 * RestoreState. For now the struct exists only to satisfy code that
 * needs to declare or sizeof() a NesStateT.
 *
 * The mainloop branches that actually read/write NesStateT through
 * FileReadMem/FileWriteMem are still gated behind #if 0 in
 * mainloop_state.cpp, so this placeholder is never serialised to disk.
 */

#ifndef _NESSTATE_H
#define _NESSTATE_H

#include "types.h"

struct NesStateT
{
    /* Versioned header so we can detect a stale on-card NesStateT after
       Phase 5 lands the real layout. */
    Uint32 uMagic;      /* 'NSST' */
    Uint32 uVersion;    /* monotonically increasing */

    /* Reserve enough room that the file size on the memcard does not
       change when Phase 5 fills in the actual fields. The PS2 memcard
       cluster size is 1 KB, so rounding to 64 KB keeps headroom for
       NES RAM (8K) + SRAM (8K) + VRAM (16K) + sprite/palette/regs. */
    Uint8 aReserved[64 * 1024 - 2 * sizeof(Uint32)];
};

#endif
