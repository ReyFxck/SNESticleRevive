/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the sncpu c interface for SNES CPU emulation.
 */

#ifndef _SNCPU_C_H
#define _SNCPU_C_H

#include "sncpu.h"

Int32 SNCPUExecute_C(SNCpuT *pCpu);

/* Shared with the PS2 interpreter so decimal mode has exactly the same
   behavior for valid and invalid BCD digits in both execution cores. */
Uint32 _SNCpuDecimalADC8(Uint32 uCarry, Uint32 uTarget, Uint32 uSrc);
Uint32 _SNCpuDecimalADC16(Uint32 uCarry, Uint32 uTarget, Uint32 uSrc);
Uint32 _SNCpuDecimalSBC8(Uint32 uCarry, Uint32 uTarget, Uint32 uSrc);
Uint32 _SNCpuDecimalSBC16(Uint32 uCarry, Uint32 uTarget, Uint32 uSrc);

#endif
