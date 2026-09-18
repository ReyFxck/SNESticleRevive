/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * NEC DSP execution core adapted for SNESticleRevive from MesenCE:
 *   Core/SNES/Coprocessors/DSP/NecDsp.*
 *   Copyright (C) 2014-2026 Sour, 2026 MesenCE contributors
 *
 * Licensed under GNU GPL v3 or later. See LICENSE and NOTICE.
 *
 * Description:
 *   Emulates the NEC uPD7725 used as DSP-4 by Top Gear 3000.
 */

#ifndef _SNDSP4_H
#define _SNDSP4_H

#include "types.h"
#include "sndsp.h"

class SNDSP4 : public ISNDSP
{
public:
    SNDSP4();

    /* Combined firmware layout used by MesenCE:
       0x0000-0x17ff: 2048 x 24-bit program ROM
       0x1800-0x1fff: 1024 x 16-bit data ROM
       Standard MesenCE dumps are little-endian. A big-endian fallback is
       accepted for older uPD7725 dump sets. */
    Bool LoadFirmware(const Uint8 *pImage, Uint32 nBytes);
    Bool IsLoaded() const { return m_bLoaded; }
    Bool IsReady() const;

    void  Reset();
    void  WriteData(Uint32 uAddr, Uint8 uData);
    Uint8 ReadData(Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);

private:
    enum
    {
        PROGRAM_WORDS = 0x800,
        PROGRAM_BYTES = 0x1800,
        DATA_WORDS    = 0x400,
        DATA_BYTES    = 0x0800,
        FIRMWARE_BYTES= 0x2000,
        RAM_WORDS     = 0x100,
        STACK_WORDS   = 4,
        BUS_CYCLE_BUDGET = 0x40000
    };

    enum StatusFlags
    {
        SR_RQM = 0x8000,
        SR_USF1 = 0x4000,
        SR_USF0 = 0x2000,
        SR_DRS = 0x1000,
        SR_DMA = 0x0800,
        SR_DRC = 0x0400,
        SR_SOC = 0x0200,
        SR_SIC = 0x0100,
        SR_EI  = 0x0080
    };

    struct AccFlags
    {
        Uint8 Carry;
        Uint8 Zero;
        Uint8 Overflow0;
        Uint8 Overflow1;
        Uint8 Sign0;
        Uint8 Sign1;
    };

    struct State
    {
        Uint64 CycleCount;
        Uint16 A;
        Uint16 B;
        AccFlags FlagsA;
        AccFlags FlagsB;
        Uint16 TR;
        Uint16 TRB;
        Uint16 PC;
        Uint16 RP;
        Uint16 DP;
        Uint16 DR;
        Uint16 SR;
        Uint16 K;
        Uint16 L;
        Uint16 M;
        Uint16 N;
        Uint16 SerialOut;
        Uint16 SerialIn;
        Uint8 SP;
    };

    State  m_State;
    Uint32 m_OpCode;
    Uint32 m_Program[PROGRAM_WORDS];
    Uint16 m_DataRom[DATA_WORDS];
    Uint16 m_Ram[RAM_WORDS];
    Uint16 m_Stack[STACK_WORDS];

    Bool m_bLoaded;
    Bool m_bFaulted;
    Bool m_bLittleEndianFirmware;

    void DecodeFirmware(const Uint8 *pImage, Bool bLittleEndian);
    void RunUntilRqm();
    void StepOne();

    void RunAluOp(Uint8 uOperation, Uint16 uSource);
    void UpdateDataPointer();
    void ExecOp();
    void ExecAndReturn();
    void Jump();
    void Load(Uint8 uDest, Uint16 uValue);
    Uint16 GetSourceValue(Uint8 uSource);

    Uint16 ReadRom(Uint32 uAddr) const;
    Uint16 ReadRam(Uint32 uAddr) const;
    void WriteRam(Uint32 uAddr, Uint16 uValue);
};

#endif
