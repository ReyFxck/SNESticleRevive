/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements the temporary DSP-4 bus placeholder for the SNES core.
 */

/*
 * The previous DSP-4 HLE was removed from the current source tree.
 *
 * Keep a minimal ISNDSP implementation so cartridge detection and the real
 * DSP-4 address map can remain wired while a GPLv3-compatible NEC DSP core is
 * integrated. This placeholder does not emulate DSP-4 commands.
 */

#include "types.h"
#include "sndsp4.h"

SNDSP4::SNDSP4()
{
    Reset();
}

void SNDSP4::Reset()
{
}

void SNDSP4::WriteData(Uint32 /*uAddr*/, Uint8 /*uData*/)
{
}

Uint8 SNDSP4::ReadData(Uint32 /*uAddr*/)
{
    /* Open/idle data until the replacement NEC DSP backend is connected. */
    return 0xFF;
}

Uint8 SNDSP4::ReadStatus(Uint32 /*uAddr*/)
{
    /* Keep RQM ready so unsupported DSP-4 traffic cannot deadlock the SNES. */
    return 0x80;
}
