/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the temporary DSP-4 bus placeholder for the SNES core.
 */

/*
 * DSP-4 is used by Top Gear 3000 / The Planet's Champ TG3000.
 *
 * The former external HLE implementation has been removed. This class keeps
 * the cartridge-facing bus interface stable until a GPLv3-compatible NEC DSP
 * implementation replaces the placeholder.
 */

#ifndef _SNDSP4_H
#define _SNDSP4_H

#include "types.h"
#include "sndsp.h"

class SNDSP4 : public ISNDSP
{
public:
    SNDSP4();

    void  Reset();
    void  WriteData(Uint32 uAddr, Uint8 uData);
    Uint8 ReadData(Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);
};

#endif
