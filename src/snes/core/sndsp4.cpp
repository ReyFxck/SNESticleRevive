/*
 * sndsp4.cpp - DSP-4 (NEC uPD7725) coprocessor HLE  -- bus wrapper.
 *
 * Thin adapter between SNESticle's ISNDSP bus interface and the ZSNES DSP-4
 * HLE engine in dsp4emu.cpp ((C) 1997-2008 ZSNES Team, GPLv2 -- see LICENSE).
 * The engine is byte-oriented (DSP4SetByte/DSP4GetByte exchange one byte at a
 * time through the global dsp4_byte), which matches how the SNES CPU accesses
 * the 16-bit Data Register one byte at a time (LSB then MSB).
 *
 * SNESticleRevive (2026): wrapper + optional bus capture.  GPLv2.
 */

#include "types.h"
#include "sndsp4.h"
#include "dsp4emu.h"
#include "console.h"

#include <string.h>

// --------------------------------------------------------------------------
//  Optional bus capture (diagnostic).  Logs every word in/out via DLog
//  (-> EE SIO, visible in the emulator log) in the .vec format consumed by
//  tools/dsp4test/dsp4_vectors.  Words are reassembled from the byte stream
//  (LSB then MSB) per direction.  Kept lightweight; bounded by DSP4_CAP_MAX.
// --------------------------------------------------------------------------
extern "C" void DLog(const char *fmt, ...);

#define DSP4_CAP_MAX 4096
static int   s_capN        = 0;
static int   s_capInit     = 0;
static Bool  s_capWrHaveLo = FALSE;
static Uint8 s_capWrLo     = 0;
static Bool  s_capRdHaveLo = FALSE;
static Uint8 s_capRdLo     = 0;

static void Dsp4CapWriteByte(Uint8 b)
{
    if (!s_capInit) { DLog("# === DSP4 CAPTURE (.vec): ativo (DSP-4 em uso) ==="); s_capInit = 1; }
    if (s_capN > DSP4_CAP_MAX) return;
    if (!s_capWrHaveLo) { s_capWrLo = b; s_capWrHaveLo = TRUE; return; }
    s_capWrHaveLo = FALSE;
    if (s_capN == DSP4_CAP_MAX) { DLog("# === DSP4 CAPTURE FIM (%d palavras) ===", s_capN); s_capN++; return; }
    DLog("W %04X", (unsigned)(s_capWrLo | ((Uint16)b << 8)));
    s_capN++;
}

static void Dsp4CapReadByte(Uint8 b)
{
    if (s_capN > DSP4_CAP_MAX) return;
    if (!s_capRdHaveLo) { s_capRdLo = b; s_capRdHaveLo = TRUE; return; }
    s_capRdHaveLo = FALSE;
    if (s_capN == DSP4_CAP_MAX) { DLog("# === DSP4 CAPTURE FIM (%d palavras) ===", s_capN); s_capN++; return; }
    DLog("R %04X", (unsigned)(s_capRdLo | ((Uint16)b << 8)));
    s_capN++;
}

//==========================================================================

SNDSP4::SNDSP4()
{
    Reset();
}

void SNDSP4::Reset()
{
    InitDSP4();
    s_capWrHaveLo = FALSE;
    s_capRdHaveLo = FALSE;
}

void SNDSP4::WriteData(Uint32 /*uAddr*/, Uint8 uData)
{
    Dsp4CapWriteByte(uData);
    dsp4_byte = uData;
    DSP4SetByte();
}

Uint8 SNDSP4::ReadData(Uint32 /*uAddr*/)
{
    DSP4GetByte();
    Uint8 uByte = dsp4_byte;
    Dsp4CapReadByte(uByte);
    return uByte;
}

Uint8 SNDSP4::ReadStatus(Uint32 /*uAddr*/)
{
    // HLE computes synchronously -> always ready (RQM = bit7).
    return 0x80;
}
