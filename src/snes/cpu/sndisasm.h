/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the sndisasm interface for SNES CPU emulation.
 */

#ifndef _SNDISASM_H
#define _SNDISASM_H

Int32 SNDisasm(Char *pStr, Uint8 *pOpcode, Uint32 PC, Uint8 *pFlags);

#endif
