/* Accurate NES 2A03 APU bridge for SNESticleRevive. */

#ifndef InfoNES_PAPU_H_INCLUDED
#define InfoNES_PAPU_H_INCLUDED

#include "InfoNES_Types.h"

/* InfoNES's CPU write dispatcher keeps one callback per $4000-$4013
   register.  The callbacks now feed a cycle-timestamped Nes_Snd_Emu core
   directly instead of accumulating a lossy frame-sized event queue. */
typedef void (*ApuWritefunc)(WORD addr, BYTE value);
extern ApuWritefunc pAPUSoundRegs[20];

void InfoNES_pAPUWriteControl(WORD addr, BYTE value);
void InfoNES_pAPUWriteFrameCounter(WORD addr, BYTE value);
BYTE InfoNES_pAPUReadStatus(void);

void InfoNES_pAPUInit(void);
void InfoNES_pAPUDone(void);
void InfoNES_pAPUVsync(void);

/* Opaque, pointer-free APU snapshot used by the NES save-state layer. */
#define INFONES_APU_STATE_MAX (16 * 1024)
int InfoNES_pAPUSaveState(void *pState, int nStateBytes);
int InfoNES_pAPULoadState(const void *pState, int nStateBytes);

#endif /* InfoNES_PAPU_H_INCLUDED */
