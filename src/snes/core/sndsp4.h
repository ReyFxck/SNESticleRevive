/*
 * sndsp4.h - DSP-4 (NEC uPD7725) coprocessor HLE
 *
 * The DSP-4 is used ONLY by "Top Gear 3000" (USA) and "The Planet's Champ
 * TG3000" (Japan).  It generates, in real time, the perspective projection
 * of the race track and the sprite (OAM) positions along it -- including the
 * track splitting into two paths.
 *
 * Implementation:
 *   This class is a thin bus wrapper around the ZSNES DSP-4 HLE
 *   (src/snes/core/dsp4emu.cpp / .h, (C) 1997-2008 ZSNES Team, GPLv2).
 *   That code owns the full transfer protocol and the projection math; this
 *   wrapper just maps the SNES Data Register byte stream onto it:
 *     - WriteData(byte) -> DSP4SetByte()   (command + parameter bytes)
 *     - ReadData()      -> DSP4GetByte()   (output bytes; 0xFF past the end)
 *     - ReadStatus()    -> 0x80            (RQM always ready; HLE is sync)
 *   It is self-contained (no external dsp4.rom firmware required).
 *
 * Reuses the DSP trap/decode in snes.cpp (ReadDSP1/WriteDSP1 +
 * _SnesDsp1IsStatus): point m_pDsp at an instance of this class when the
 * cartridge is DSP-4.
 *
 * See LICENSE: this project is GPLv2; the DSP-4 code is from ZSNES.
 */
#ifndef _SNDSP4_H
#define _SNDSP4_H

#include "types.h"
#include "sndsp.h"

class SNDSP4 : public ISNDSP
{
public:
    SNDSP4();

    // ISNDSP
    void  Reset();
    void  WriteData(Uint32 uAddr, Uint8 uData);
    Uint8 ReadData (Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);
};

#endif
