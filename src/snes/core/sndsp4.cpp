/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * NEC DSP execution core adapted for SNESticleRevive from MesenCE:
 *   Core/SNES/Coprocessors/DSP/NecDsp.cpp
 *   Copyright (C) 2014-2026 Sour, 2026 MesenCE contributors
 *
 * Licensed under GNU GPL v3 or later. See LICENSE and NOTICE.
 *
 * Description:
 *   Low-level emulation of the NEC uPD7725 used as DSP-4.
 */

#include "types.h"
#include "sndsp4.h"

#include <stdio.h>
#include <string.h>

SNDSP4::SNDSP4()
{
    m_bLoaded = FALSE;
    m_bFaulted = FALSE;
    m_bLittleEndianFirmware = TRUE;
    memset(m_Program, 0, sizeof(m_Program));
    memset(m_DataRom, 0, sizeof(m_DataRom));
    Reset();
}

void SNDSP4::DecodeFirmware(const Uint8 *pImage, Bool bLittleEndian)
{
    Uint32 i;

    for (i = 0; i < PROGRAM_WORDS; ++i)
    {
        const Uint8 *p = pImage + i * 3;
        if (bLittleEndian)
            m_Program[i] = (Uint32)p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16);
        else
            m_Program[i] = ((Uint32)p[0] << 16) | ((Uint32)p[1] << 8) | (Uint32)p[2];
    }

    for (i = 0; i < DATA_WORDS; ++i)
    {
        const Uint8 *p = pImage + PROGRAM_BYTES + i * 2;
        if (bLittleEndian)
            m_DataRom[i] = (Uint16)((Uint16)p[0] | ((Uint16)p[1] << 8));
        else
            m_DataRom[i] = (Uint16)(((Uint16)p[0] << 8) | (Uint16)p[1]);
    }
}

Bool SNDSP4::LoadFirmware(const Uint8 *pImage, Uint32 nBytes)
{
    if (!pImage || nBytes != FIRMWARE_BYTES)
        return FALSE;

    /* MesenCE's firmware loader treats the combined image as little-endian.
       Keep a big-endian fallback because older standalone uPD7725 dump sets
       circulate in that byte order. Booting to RQM is a cheap validity test. */
    m_bLoaded = TRUE;

    DecodeFirmware(pImage, TRUE);
    m_bLittleEndianFirmware = TRUE;
    Reset();
    if (IsReady())
    {
        printf("[dsp4] NEC firmware loaded (little-endian)\n");
        return TRUE;
    }

    DecodeFirmware(pImage, FALSE);
    m_bLittleEndianFirmware = FALSE;
    Reset();
    if (IsReady())
    {
        printf("[dsp4] NEC firmware loaded (big-endian fallback)\n");
        return TRUE;
    }

    printf("[dsp4] firmware did not boot to RQM\n");
    m_bLoaded = FALSE;
    Reset();
    return FALSE;
}

Bool SNDSP4::IsReady() const
{
    return (m_bLoaded && !m_bFaulted && (m_State.SR & SR_RQM)) ? TRUE : FALSE;
}

void SNDSP4::Reset()
{
    memset(&m_State, 0, sizeof(m_State));
    memset(m_Ram, 0, sizeof(m_Ram));
    memset(m_Stack, 0, sizeof(m_Stack));
    m_OpCode = 0;
    m_bFaulted = FALSE;

    if (m_bLoaded)
        RunUntilRqm();
}

Uint16 SNDSP4::ReadRom(Uint32 uAddr) const
{
    return m_DataRom[uAddr & (DATA_WORDS - 1)];
}

Uint16 SNDSP4::ReadRam(Uint32 uAddr) const
{
    return m_Ram[uAddr & (RAM_WORDS - 1)];
}

void SNDSP4::WriteRam(Uint32 uAddr, Uint16 uValue)
{
    m_Ram[uAddr & (RAM_WORDS - 1)] = uValue;
}

void SNDSP4::RunUntilRqm()
{
    Uint32 n;

    if (!m_bLoaded || m_bFaulted || (m_State.SR & SR_RQM))
        return;

    for (n = 0; n < BUS_CYCLE_BUDGET; ++n)
    {
        StepOne();
        if (m_State.SR & SR_RQM)
            return;
    }

    m_bFaulted = TRUE;
    printf("[dsp4] NEC core exceeded %u instructions without RQM (PC=%04X SR=%04X)\n",
           (unsigned)BUS_CYCLE_BUDGET,
           (unsigned)m_State.PC,
           (unsigned)m_State.SR);
}

void SNDSP4::StepOne()
{
    m_OpCode = m_Program[m_State.PC & (PROGRAM_WORDS - 1)];
    m_State.PC++;

    switch (m_OpCode & 0xC00000)
    {
        case 0x000000: ExecOp(); break;
        case 0x400000: ExecAndReturn(); break;
        case 0x800000: Jump(); break;
        case 0xC00000: Load((Uint8)(m_OpCode & 0x0F), (Uint16)(m_OpCode >> 6)); break;
    }

    {
        Int32 nProduct = (Int32)(Int16)m_State.K * (Int32)(Int16)m_State.L;
        m_State.M = (Uint16)(nProduct >> 15);
        m_State.N = (Uint16)((Uint32)nProduct << 1);
    }

    m_State.CycleCount++;
}

void SNDSP4::RunAluOp(Uint8 uOperation, Uint16 uSource)
{
    Uint16 uResult = 0;
    Uint8 uAccSelect = (Uint8)((m_OpCode >> 15) & 0x01);
    AccFlags Flags = uAccSelect ? m_State.FlagsB : m_State.FlagsA;
    Uint16 uAcc = uAccSelect ? m_State.B : m_State.A;
    Uint8 uOtherCarry = uAccSelect ? m_State.FlagsA.Carry : m_State.FlagsB.Carry;
    Uint8 uPSelect = (Uint8)((m_OpCode >> 20) & 0x03);
    Uint16 uP = 0;

    switch (uPSelect)
    {
        case 0: uP = ReadRam(m_State.DP); break;
        case 1: uP = uSource; break;
        case 2: uP = m_State.M; break;
        case 3: uP = m_State.N; break;
    }

    switch (uOperation)
    {
        case 0x00: break;
        case 0x01: uResult = (Uint16)(uAcc | uP); break;
        case 0x02: uResult = (Uint16)(uAcc & uP); break;
        case 0x03: uResult = (Uint16)(uAcc ^ uP); break;
        case 0x04: uResult = (Uint16)(uAcc - uP); break;
        case 0x05: uResult = (Uint16)(uAcc + uP); break;
        case 0x06: uResult = (Uint16)(uAcc - uP - uOtherCarry); break;
        case 0x07: uResult = (Uint16)(uAcc + uP + uOtherCarry); break;

        case 0x08:
            uResult = (Uint16)(uAcc - 1);
            uP = 1;
            break;

        case 0x09:
            uResult = (Uint16)(uAcc + 1);
            uP = 1;
            break;

        case 0x0A: uResult = (Uint16)~uAcc; break;
        case 0x0B: uResult = (Uint16)((uAcc >> 1) | (uAcc & 0x8000)); break;
        case 0x0C: uResult = (Uint16)((uAcc << 1) | (Uint16)uOtherCarry); break;
        case 0x0D: uResult = (Uint16)((uAcc << 2) | 0x0003); break;
        case 0x0E: uResult = (Uint16)((uAcc << 4) | 0x000F); break;
        case 0x0F: uResult = (Uint16)((uAcc << 8) | (uAcc >> 8)); break;
    }

    Flags.Zero = (uResult == 0) ? 1 : 0;
    Flags.Sign0 = (Uint8)((uResult & 0x8000) >> 15);
    if (!Flags.Overflow1)
        Flags.Sign1 = Flags.Sign0;

    switch (uOperation)
    {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x0A:
        case 0x0D:
        case 0x0E:
        case 0x0F:
            Flags.Carry = 0;
            Flags.Overflow0 = 0;
            Flags.Overflow1 = 0;
            break;

        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        {
            Uint16 uOverflow =
                (Uint16)((uAcc ^ uResult) &
                         (uP ^ ((uOperation & 0x01) ? uResult : uAcc)));

            Flags.Overflow0 = (Uint8)((uOverflow & 0x8000) >> 15);
            if (Flags.Overflow0 && Flags.Overflow1)
                Flags.Overflow1 = (Flags.Sign0 == Flags.Sign1) ? 1 : 0;
            else
                Flags.Overflow1 = (Uint8)(Flags.Overflow1 | Flags.Overflow0);

            Flags.Carry =
                (Uint8)(((uAcc ^ uP ^ uResult ^ uOverflow) & 0x8000) >> 15);
            break;
        }

        case 0x0B:
            Flags.Carry = (uAcc & 0x0001) ? 1 : 0;
            Flags.Overflow0 = 0;
            Flags.Overflow1 = 0;
            break;

        case 0x0C:
            Flags.Carry = (Uint8)((uAcc >> 15) & 0x01);
            Flags.Overflow0 = 0;
            Flags.Overflow1 = 0;
            break;
    }

    if (uAccSelect)
    {
        m_State.B = uResult;
        m_State.FlagsB = Flags;
    }
    else
    {
        m_State.A = uResult;
        m_State.FlagsA = Flags;
    }
}

void SNDSP4::UpdateDataPointer()
{
    Uint16 uDp = m_State.DP;

    switch ((m_OpCode >> 13) & 0x03)
    {
        case 0: break;
        case 1: uDp = (Uint16)((uDp & 0xF0) | ((uDp + 1) & 0x0F)); break;
        case 2: uDp = (Uint16)((uDp & 0xF0) | ((uDp - 1) & 0x0F)); break;
        case 3: uDp = (Uint16)(uDp & 0xF0); break;
    }

    m_State.DP = (Uint16)(uDp ^ (((m_OpCode >> 9) & 0x0F) << 4));
}

void SNDSP4::ExecOp()
{
    Uint8 uAluOperation = (Uint8)((m_OpCode >> 16) & 0x0F);
    Uint16 uSource = GetSourceValue((Uint8)((m_OpCode >> 4) & 0x0F));
    Uint8 uDest;

    if (uAluOperation)
        RunAluOp(uAluOperation, uSource);

    uDest = (Uint8)(m_OpCode & 0x0F);
    Load(uDest, uSource);

    if (uDest != 0x04)
        UpdateDataPointer();

    if (((m_OpCode >> 8) & 0x01) && uDest != 0x05)
        m_State.RP--;
}

void SNDSP4::ExecAndReturn()
{
    ExecOp();
    m_State.SP = (Uint8)((m_State.SP - 1) & (STACK_WORDS - 1));
    m_State.PC = m_Stack[m_State.SP];
}

void SNDSP4::Jump()
{
    Uint8 uBank = (Uint8)(m_OpCode & 0x03);
    Uint16 uAddress = (Uint16)((m_OpCode >> 2) & 0x07FF);
    Uint16 uTarget =
        (Uint16)((m_State.PC & 0x2000) | ((Uint16)uBank << 11) | uAddress);
    Uint32 uCondition = 0;
    Uint16 uJumpType = (Uint16)((m_OpCode >> 13) & 0x01FF);

    switch (uJumpType)
    {
        case 0x00: m_State.PC = m_State.SerialOut; break;

        case 0x80: uCondition = !m_State.FlagsA.Carry; break;
        case 0x82: uCondition =  m_State.FlagsA.Carry; break;
        case 0x84: uCondition = !m_State.FlagsB.Carry; break;
        case 0x86: uCondition =  m_State.FlagsB.Carry; break;

        case 0x88: uCondition = !m_State.FlagsA.Zero; break;
        case 0x8A: uCondition =  m_State.FlagsA.Zero; break;
        case 0x8C: uCondition = !m_State.FlagsB.Zero; break;
        case 0x8E: uCondition =  m_State.FlagsB.Zero; break;

        case 0x90: uCondition = !m_State.FlagsA.Overflow0; break;
        case 0x92: uCondition =  m_State.FlagsA.Overflow0; break;
        case 0x94: uCondition = !m_State.FlagsB.Overflow0; break;
        case 0x96: uCondition =  m_State.FlagsB.Overflow0; break;

        case 0x98: uCondition = !m_State.FlagsA.Overflow1; break;
        case 0x9A: uCondition =  m_State.FlagsA.Overflow1; break;
        case 0x9C: uCondition = !m_State.FlagsB.Overflow1; break;
        case 0x9E: uCondition =  m_State.FlagsB.Overflow1; break;

        case 0xA0: uCondition = !m_State.FlagsA.Sign0; break;
        case 0xA2: uCondition =  m_State.FlagsA.Sign0; break;
        case 0xA4: uCondition = !m_State.FlagsB.Sign0; break;
        case 0xA6: uCondition =  m_State.FlagsB.Sign0; break;

        case 0xA8: uCondition = !m_State.FlagsA.Sign1; break;
        case 0xAA: uCondition =  m_State.FlagsA.Sign1; break;
        case 0xAC: uCondition = !m_State.FlagsB.Sign1; break;
        case 0xAE: uCondition =  m_State.FlagsB.Sign1; break;

        case 0xB0: uCondition = !(m_State.DP & 0x0F); break;
        case 0xB1: uCondition =  (m_State.DP & 0x0F); break;
        case 0xB2: uCondition = ((m_State.DP & 0x0F) == 0x0F); break;
        case 0xB3: uCondition = ((m_State.DP & 0x0F) != 0x0F); break;

        case 0xB4: uCondition = !(m_State.SR & SR_SIC); break;
        case 0xB6: uCondition =  (m_State.SR & SR_SIC); break;
        case 0xB8: uCondition = !(m_State.SR & SR_SOC); break;
        case 0xBA: uCondition =  (m_State.SR & SR_SOC); break;
        case 0xBC: uCondition = !(m_State.SR & SR_RQM); break;
        case 0xBE: uCondition =  (m_State.SR & SR_RQM); break;

        case 0x100:
            m_State.PC = (Uint16)(uTarget & ~0x2000);
            break;

        case 0x101:
            m_State.PC = (Uint16)(uTarget | 0x2000);
            break;

        case 0x140:
            m_Stack[m_State.SP] = m_State.PC;
            m_State.SP = (Uint8)((m_State.SP + 1) & (STACK_WORDS - 1));
            m_State.PC = (Uint16)(uTarget & ~0x2000);
            break;

        case 0x141:
            m_Stack[m_State.SP] = m_State.PC;
            m_State.SP = (Uint8)((m_State.SP + 1) & (STACK_WORDS - 1));
            m_State.PC = (Uint16)(uTarget | 0x2000);
            break;
    }

    if (uCondition)
        m_State.PC = uTarget;
}

void SNDSP4::Load(Uint8 uDest, Uint16 uValue)
{
    switch (uDest)
    {
        case 0x00: break;
        case 0x01: m_State.A = uValue; break;
        case 0x02: m_State.B = uValue; break;
        case 0x03: m_State.TR = uValue; break;
        case 0x04: m_State.DP = uValue; break;
        case 0x05: m_State.RP = uValue; break;

        case 0x06:
            m_State.DR = uValue;
            m_State.SR |= SR_RQM;
            break;

        case 0x07:
            m_State.SR =
                (Uint16)((m_State.SR & 0x907C) |
                         (uValue & (Uint16)~0x907C));
            break;

        case 0x08:
        case 0x09:
            m_State.SerialOut = uValue;
            break;

        case 0x0A:
            m_State.K = uValue;
            break;

        case 0x0B:
            m_State.K = uValue;
            m_State.L = ReadRom(m_State.RP);
            break;

        case 0x0C:
            m_State.L = uValue;
            m_State.K = ReadRam((Uint16)(m_State.DP | 0x40));
            break;

        case 0x0D:
            m_State.L = uValue;
            break;

        case 0x0E:
            m_State.TRB = uValue;
            break;

        case 0x0F:
            WriteRam(m_State.DP, uValue);
            break;
    }
}

Uint16 SNDSP4::GetSourceValue(Uint8 uSource)
{
    switch (uSource)
    {
        case 0x00: return m_State.TRB;
        case 0x01: return m_State.A;
        case 0x02: return m_State.B;
        case 0x03: return m_State.TR;
        case 0x04: return m_State.DP;
        case 0x05: return m_State.RP;
        case 0x06: return ReadRom(m_State.RP);
        case 0x07: return (Uint16)(0x8000 - m_State.FlagsA.Sign1);

        case 0x08:
            m_State.SR |= SR_RQM;
            return m_State.DR;

        case 0x09: return m_State.DR;
        case 0x0A: return m_State.SR;
        case 0x0B:
        case 0x0C: return m_State.SerialIn;
        case 0x0D: return m_State.K;
        case 0x0E: return m_State.L;
        case 0x0F: return ReadRam(m_State.DP);
    }

    return 0;
}

Uint8 SNDSP4::ReadStatus(Uint32 /*uAddr*/)
{
    RunUntilRqm();
    return m_bLoaded ? (Uint8)(m_State.SR >> 8) : 0x00;
}

Uint8 SNDSP4::ReadData(Uint32 /*uAddr*/)
{
    Uint8 uValue;

    if (!m_bLoaded)
        return 0x00;

    RunUntilRqm();
    if (m_bFaulted)
        return 0x00;

    if (m_State.SR & SR_DRC)
    {
        m_State.SR &= (Uint16)~SR_RQM;
        uValue = (Uint8)m_State.DR;
    }
    else if (m_State.SR & SR_DRS)
    {
        m_State.SR &= (Uint16)~SR_RQM;
        m_State.SR &= (Uint16)~SR_DRS;
        uValue = (Uint8)(m_State.DR >> 8);
    }
    else
    {
        m_State.SR |= SR_DRS;
        uValue = (Uint8)m_State.DR;
    }

    if (!(m_State.SR & SR_RQM))
        RunUntilRqm();

    return uValue;
}

void SNDSP4::WriteData(Uint32 /*uAddr*/, Uint8 uData)
{
    if (!m_bLoaded)
        return;

    RunUntilRqm();
    if (m_bFaulted)
        return;

    if (m_State.SR & SR_DRC)
    {
        m_State.SR &= (Uint16)~SR_RQM;
        m_State.DR = (Uint16)((m_State.DR & 0xFF00) | uData);
    }
    else if (m_State.SR & SR_DRS)
    {
        m_State.SR &= (Uint16)~SR_RQM;
        m_State.SR &= (Uint16)~SR_DRS;
        m_State.DR =
            (Uint16)((m_State.DR & 0x00FF) | ((Uint16)uData << 8));
    }
    else
    {
        m_State.SR |= SR_DRS;
        m_State.DR = (Uint16)((m_State.DR & 0xFF00) | uData);
    }

    if (!(m_State.SR & SR_RQM))
        RunUntilRqm();
}
