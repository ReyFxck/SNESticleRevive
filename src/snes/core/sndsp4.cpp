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
    m_bReplacementProgram = TRUE;
    memset(m_Program, 0, sizeof(m_Program));
    memset(m_DataRom, 0, sizeof(m_DataRom));
    memset(&m_Repl, 0, sizeof(m_Repl));
    UseReplacementProgram();
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

    m_bReplacementProgram = FALSE;

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

void SNDSP4::UseReplacementProgram()
{
    m_bReplacementProgram = TRUE;
    m_bLoaded = TRUE;
    Reset();
}

Bool SNDSP4::IsReady() const
{
    if (m_bReplacementProgram)
        return TRUE;

    return (m_bLoaded && !m_bFaulted && (m_State.SR & SR_RQM)) ? TRUE : FALSE;
}

void SNDSP4::Reset()
{
    memset(&m_State, 0, sizeof(m_State));
    memset(m_Ram, 0, sizeof(m_Ram));
    memset(m_Stack, 0, sizeof(m_Stack));
    m_OpCode = 0;
    m_bFaulted = FALSE;

    if (m_bReplacementProgram)
    {
        ReplacementReset();
        return;
    }

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
    if (m_bReplacementProgram)
        return ReplacementStatus();

    RunUntilRqm();
    return m_bLoaded ? (Uint8)(m_State.SR >> 8) : 0x00;
}

Uint8 SNDSP4::ReadData(Uint32 /*uAddr*/)
{
    Uint8 uValue;

    if (m_bReplacementProgram)
        return ReplacementRead();

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
    if (m_bReplacementProgram)
    {
        ReplacementWrite(uData);
        return;
    }

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


/* -------------------------------------------------------------------------
 * Self-contained DSP-4 replacement program
 * -------------------------------------------------------------------------
 *
 * This is not Nintendo/NEC firmware data. It is a project-authored command
 * engine that lives behind the same 16-bit DR/SR interface exposed by the
 * uPD7725. Command framing and arithmetic are reconstructed from public
 * hardware research by Overload/The Dumper and the published DSP-4 protocol.
 *
 * The first milestone covers the command stream used to initialise OAM and
 * the single-player road projection path. More complex solid-polygon,
 * lighting and sprite-projection commands are intentionally reported as
 * unsupported rather than silently pretending to be correct.
 */

void SNDSP4::ReplacementReset()
{
    memset(&m_Repl, 0, sizeof(m_Repl));
    m_Repl.WaitingCommand = TRUE;
    m_Repl.OamRowMax = 33;
}

Uint8 SNDSP4::ReplacementStatus() const
{
    /* Public DSP-4 documentation: all transfers are 16-bit and RQM is the
       only status bit Top Gear 3000 needs for normal handshaking. */
    return 0x80;
}

Uint16 SNDSP4::ReplacementReadWord(Uint16 uOffset) const
{
    if ((Uint32)uOffset + 1 >= REPL_INPUT_BYTES)
        return 0xffff;

    return (Uint16)(
        (Uint16)m_Repl.Input[uOffset] |
        ((Uint16)m_Repl.Input[uOffset + 1] << 8)
    );
}

Int16 SNDSP4::ReplacementReadSWord(Uint16 uOffset) const
{
    return (Int16)ReplacementReadWord(uOffset);
}

Int32 SNDSP4::ReplacementReadDword(Uint16 uOffset) const
{
    Uint32 lo = ReplacementReadWord(uOffset);
    Uint32 hi = ReplacementReadWord((Uint16)(uOffset + 2));
    return (Int32)(lo | (hi << 16));
}

void SNDSP4::ReplacementClearOutput()
{
    m_Repl.OutCount = 0;
    m_Repl.OutPos = 0;
}

void SNDSP4::ReplacementWriteByte(Uint8 uValue)
{
    if (m_Repl.OutCount < REPL_OUTPUT_BYTES)
        m_Repl.Output[m_Repl.OutCount++] = uValue;
}

void SNDSP4::ReplacementWriteWord(Uint16 uValue)
{
    ReplacementWriteByte((Uint8)(uValue & 0xff));
    ReplacementWriteByte((Uint8)(uValue >> 8));
}

void SNDSP4::ReplacementExpect(Uint16 nBytes, Uint8 uPhase)
{
    m_Repl.Need = nBytes;
    m_Repl.InPos = 0;
    m_Repl.Phase = uPhase;
}

void SNDSP4::ReplacementFinish()
{
    m_Repl.WaitingCommand = TRUE;
    m_Repl.HalfCommand = FALSE;
    m_Repl.Need = 0;
    m_Repl.InPos = 0;
    m_Repl.Phase = 0;
}

Int16 SNDSP4::ReplacementInverse(Int16 nLines) const
{
    Int32 n = nLines;

    if (n <= 0)
        return 0;
    if (n > 63)
        n = 63;

    return (Int16)(0x8000 / n);
}

void SNDSP4::ReplacementOp0B(
    Int16 x,
    Int16 y,
    Int16 attr,
    Bool bLarge,
    Bool bEmitStop)
{
    Uint16 row0 = (Uint16)((y >> 3) & 0x1f);
    Uint16 row1 = (Uint16)((row0 + 1) & 0x1f);
    Bool bDraw = TRUE;

    if (!((y < 0) || (((Uint16)y & 0x01ff) < 0x00eb)))
        bDraw = FALSE;

    if (bLarge)
    {
        if ((Uint16)(m_Repl.OamRow[row0] + 1) >= m_Repl.OamRowMax)
            bDraw = FALSE;
        if ((Uint16)(m_Repl.OamRow[row1] + 1) >= m_Repl.OamRowMax)
            bDraw = FALSE;
    }
    else if (m_Repl.OamRow[row0] >= m_Repl.OamRowMax)
    {
        bDraw = FALSE;
    }

    if (m_Repl.SpriteCount >= 128)
        bDraw = FALSE;

    if (!bDraw)
    {
        if (bEmitStop)
            ReplacementWriteWord(0);
        return;
    }

    if (bLarge)
    {
        m_Repl.OamRow[row0] = (Uint8)(m_Repl.OamRow[row0] + 2);
        m_Repl.OamRow[row1] = (Uint8)(m_Repl.OamRow[row1] + 2);
    }
    else
    {
        m_Repl.OamRow[row0]++;
    }

    ReplacementWriteWord(1);
    ReplacementWriteByte((Uint8)x);
    ReplacementWriteByte((Uint8)y);
    ReplacementWriteWord((Uint16)attr);
    m_Repl.SpriteCount++;

    if (m_Repl.OamIndex < 32)
    {
        Uint16 bit = m_Repl.OamBits;
        if (x < 0 || x > 255)
            m_Repl.OamAttr[m_Repl.OamIndex] |= (Uint8)(1u << bit);

        bit++;
        if (bLarge)
            m_Repl.OamAttr[m_Repl.OamIndex] |= (Uint8)(1u << bit);

        bit++;
        m_Repl.OamBits = bit;
        if (m_Repl.OamBits >= 8)
        {
            m_Repl.OamBits = 0;
            m_Repl.OamIndex++;
        }
    }
}

void SNDSP4::ReplacementProject01()
{
    Int32 projectedX;
    Int32 projectedY;
    Int32 scrollX;
    Int32 scrollY;
    Int32 stepX;
    Int32 stepY;
    Int16 segments;
    Int16 inverse;
    Int32 i;

    projectedX =
        ((((m_Repl.WorldX + m_Repl.WorldXEnv) >> 16) *
          (Int32)m_Repl.Distance) >> 15) +
        (((Int32)m_Repl.TurnX * (Int32)m_Repl.Distance) >> 15);

    projectedY =
        (((m_Repl.WorldY >> 16) * (Int32)m_Repl.Distance) >> 15);

    m_Repl.ViewX2 = (Int16)projectedX;
    m_Repl.ViewY2 = (Int16)projectedY;
    m_Repl.ViewXOfs2 = m_Repl.ViewX2;
    m_Repl.ViewYOfs2 = (Int16)(
        (((Int32)m_Repl.WorldYOfs * (Int32)m_Repl.Distance) >> 15) +
        m_Repl.PolyBottom -
        m_Repl.ViewY2
    );

    ReplacementClearOutput();
    ReplacementWriteWord((Uint16)((m_Repl.WorldX + m_Repl.WorldXEnv) >> 16));
    ReplacementWriteWord((Uint16)m_Repl.ViewX2);
    ReplacementWriteWord((Uint16)(m_Repl.WorldY >> 16));
    ReplacementWriteWord((Uint16)m_Repl.ViewY2);

    segments = (Int16)(m_Repl.PolyRaster - m_Repl.ViewY2);

    if (m_Repl.ViewY2 >= m_Repl.PolyRaster)
    {
        segments = 0;
    }
    else
    {
        m_Repl.PolyRaster = m_Repl.ViewY2;
    }

    if (m_Repl.ViewY2 < m_Repl.PolyTop)
    {
        segments = 0;
        if (m_Repl.ViewY1 >= m_Repl.PolyTop)
            segments = (Int16)(m_Repl.ViewY1 - m_Repl.PolyTop);
    }

    if (segments < 0)
        segments = 0;

    ReplacementWriteWord((Uint16)segments);

    if (segments > 0)
    {
        inverse = ReplacementInverse(segments);
        stepX =
            (Int32)(m_Repl.ViewXOfs2 - m_Repl.ViewXOfs1) *
            (Int32)inverse * 2;
        stepY =
            (Int32)(m_Repl.ViewYOfs2 - m_Repl.ViewYOfs1) *
            (Int32)inverse * 2;

        scrollX =
            (Int32)(m_Repl.PolyCxX + m_Repl.ViewXOfs1) * 65536;
        scrollY =
            (Int32)(
                -m_Repl.ViewportBottom +
                m_Repl.ViewYOfs1 +
                m_Repl.ViewYOfsEnv +
                m_Repl.PolyCxY -
                m_Repl.WorldYOfs
            ) * 65536;

        for (i = 0; i < segments; ++i)
        {
            ReplacementWriteWord((Uint16)m_Repl.PolyPtr);
            ReplacementWriteWord((Uint16)((scrollY + 0x8000) >> 16));
            ReplacementWriteWord((Uint16)((scrollX + 0x8000) >> 16));

            m_Repl.PolyPtr = (Int16)(m_Repl.PolyPtr - 4);
            scrollX += stepX;
            scrollY += stepY;
        }
    }

    m_Repl.ViewX1 = m_Repl.ViewX2;
    m_Repl.ViewY1 = m_Repl.ViewY2;
    m_Repl.ViewXOfs1 = m_Repl.ViewXOfs2;
    m_Repl.ViewYOfs1 = m_Repl.ViewYOfs2;

    m_Repl.WorldDx += (Int32)m_Repl.WorldDdx * 256;
    m_Repl.WorldDy += (Int32)m_Repl.WorldDdy * 256;
    m_Repl.WorldX += m_Repl.WorldDx + m_Repl.WorldXEnv;
    m_Repl.WorldY += m_Repl.WorldDy;

    m_Repl.TurnX = (Int16)(m_Repl.TurnX + m_Repl.TurnDx);
}

void SNDSP4::ReplacementProject07()
{
    Int32 scrollX;
    Int32 scrollY;
    Int32 stepX;
    Int32 stepY;
    Int16 segments;
    Int16 inverse;
    Int32 i;

    m_Repl.ViewX2 = (Int16)(m_Repl.ViewX2 + m_Repl.ViewDx);
    m_Repl.ViewY2 = (Int16)(m_Repl.ViewY2 + m_Repl.ViewDy);
    m_Repl.ViewXOfs2 = m_Repl.ViewX2;
    m_Repl.ViewYOfs2 = (Int16)(
        (((Int32)m_Repl.WorldYOfs * (Int32)m_Repl.Distance) >> 15) +
        m_Repl.PolyBottom -
        m_Repl.ViewY2
    );

    ReplacementClearOutput();
    ReplacementWriteWord((Uint16)m_Repl.ViewX2);
    ReplacementWriteWord((Uint16)m_Repl.ViewY2);

    segments = (Int16)(m_Repl.ViewY1 - m_Repl.ViewY2);

    if (m_Repl.ViewY2 >= m_Repl.PolyRaster)
    {
        segments = 0;
    }
    else
    {
        m_Repl.PolyRaster = m_Repl.ViewY2;
    }

    if (m_Repl.ViewY2 < m_Repl.PolyTop)
    {
        segments = 0;
        if (m_Repl.ViewY1 >= m_Repl.PolyTop)
            segments = (Int16)(m_Repl.ViewY1 - m_Repl.PolyTop);
    }

    if (segments < 0)
        segments = 0;

    ReplacementWriteWord((Uint16)segments);

    if (segments > 0)
    {
        inverse = ReplacementInverse(segments);
        stepX =
            (Int32)(m_Repl.ViewXOfs2 - m_Repl.ViewXOfs1) *
            (Int32)inverse * 2;
        stepY =
            (Int32)(m_Repl.ViewYOfs2 - m_Repl.ViewYOfs1) *
            (Int32)inverse * 2;

        scrollX =
            (Int32)(m_Repl.PolyCxX + m_Repl.ViewXOfs1) * 65536;
        scrollY =
            (Int32)(
                -m_Repl.ViewportBottom +
                m_Repl.ViewYOfs1 +
                m_Repl.ViewYOfsEnv +
                m_Repl.PolyCxY -
                m_Repl.WorldYOfs
            ) * 65536;

        for (i = 0; i < segments; ++i)
        {
            ReplacementWriteWord((Uint16)m_Repl.PolyPtr);
            ReplacementWriteWord((Uint16)((scrollY + 0x8000) >> 16));
            ReplacementWriteWord((Uint16)((scrollX + 0x8000) >> 16));

            m_Repl.PolyPtr = (Int16)(m_Repl.PolyPtr - 4);
            scrollX += stepX;
            scrollY += stepY;
        }
    }

    m_Repl.ViewX1 = m_Repl.ViewX2;
    m_Repl.ViewY1 = m_Repl.ViewY2;
    m_Repl.ViewXOfs1 = m_Repl.ViewXOfs2;
    m_Repl.ViewYOfs1 = m_Repl.ViewYOfs2;
}


Uint16 SNDSP4::ReplacementLightColor(Int16 nDistance, Uint16 uColor) const
{
    Int32 r = (Int32)(uColor & 0x1f);
    Int32 g = (Int32)((uColor >> 5) & 0x1f);
    Int32 b = (Int32)((uColor >> 10) & 0x1f);

    r = (r * (Int32)nDistance) >> 15;
    g = (g * (Int32)nDistance) >> 15;
    b = (b * (Int32)nDistance) >> 15;

    return (Uint16)(
        ((Uint16)r & 0x1f) |
        (((Uint16)g & 0x1f) << 5) |
        (((Uint16)b & 0x1f) << 10)
    );
}

void SNDSP4::ReplacementAppendRaster()
{
    Int32 scrollX;
    Int32 scrollY;
    Int32 stepX;
    Int32 stepY;
    Int16 inverse;
    Int32 i;

    if (m_Repl.Segments <= 0)
        return;

    inverse = ReplacementInverse(m_Repl.Segments);
    stepX =
        (Int32)(m_Repl.ViewXOfs2 - m_Repl.ViewXOfs1) *
        (Int32)inverse * 2;
    stepY =
        (Int32)(m_Repl.ViewYOfs2 - m_Repl.ViewYOfs1) *
        (Int32)inverse * 2;

    scrollX =
        (Int32)(m_Repl.PolyCxX + m_Repl.ViewXOfs1) * 65536;
    scrollY =
        (Int32)(
            -m_Repl.ViewportBottom +
            m_Repl.ViewYOfs1 +
            m_Repl.ViewYOfsEnv +
            m_Repl.PolyCxY -
            m_Repl.WorldYOfs
        ) * 65536;

    for (i = 0; i < m_Repl.Segments; ++i)
    {
        ReplacementWriteWord((Uint16)m_Repl.PolyPtr);
        ReplacementWriteWord((Uint16)((scrollY + 0x8000) >> 16));
        ReplacementWriteWord((Uint16)((scrollX + 0x8000) >> 16));

        m_Repl.PolyPtr = (Int16)(m_Repl.PolyPtr - 4);
        scrollX += stepX;
        scrollY += stepY;
    }
}

void SNDSP4::ReplacementPostRoad(Bool bAdvanceWorld)
{
    m_Repl.ViewX1 = m_Repl.ViewX2;
    m_Repl.ViewY1 = m_Repl.ViewY2;
    m_Repl.ViewXOfs1 = m_Repl.ViewXOfs2;
    m_Repl.ViewYOfs1 = m_Repl.ViewYOfs2;

    if (bAdvanceWorld)
    {
        m_Repl.WorldDx += (Int32)m_Repl.WorldDdx * 256;
        m_Repl.WorldDy += (Int32)m_Repl.WorldDdy * 256;
        m_Repl.WorldX += m_Repl.WorldDx + m_Repl.WorldXEnv;
        m_Repl.WorldY += m_Repl.WorldDy;
        m_Repl.TurnX = (Int16)(m_Repl.TurnX + m_Repl.TurnDx);
    }
}

void SNDSP4::ReplacementProject0F()
{
    Int32 projectedX;
    Int32 projectedY;
    Int16 segments;

    projectedX =
        (((m_Repl.WorldX + m_Repl.WorldXEnv) >> 16) *
         (Int32)m_Repl.Distance) >> 15;
    projectedY =
        ((m_Repl.WorldY >> 16) * (Int32)m_Repl.Distance) >> 15;

    m_Repl.ViewX2 = (Int16)projectedX;
    m_Repl.ViewY2 = (Int16)projectedY;
    m_Repl.ViewXOfs2 = m_Repl.ViewX2;
    m_Repl.ViewYOfs2 = (Int16)(
        (((Int32)m_Repl.WorldYOfs * (Int32)m_Repl.Distance) >> 15) +
        m_Repl.PolyBottom -
        m_Repl.ViewY2
    );

    ReplacementClearOutput();
    ReplacementWriteWord((Uint16)((m_Repl.WorldX + m_Repl.WorldXEnv) >> 16));
    ReplacementWriteWord((Uint16)m_Repl.ViewX2);
    ReplacementWriteWord((Uint16)(m_Repl.WorldY >> 16));
    ReplacementWriteWord((Uint16)m_Repl.ViewY2);

    segments = (Int16)(m_Repl.PolyRaster - m_Repl.ViewY2);
    if (m_Repl.ViewY2 >= m_Repl.PolyRaster)
    {
        segments = 0;
    }
    else
    {
        m_Repl.PolyRaster = m_Repl.ViewY2;
    }

    if (m_Repl.ViewY2 < m_Repl.PolyTop)
    {
        segments = 0;
        if (m_Repl.ViewY1 >= m_Repl.PolyTop)
            segments = (Int16)(m_Repl.ViewY1 - m_Repl.PolyTop);
    }

    if (segments < 0)
        segments = 0;

    m_Repl.Segments = segments;
    m_Repl.LightIndex = 0;
    ReplacementWriteWord((Uint16)segments);

    if (segments > 0)
    {
        ReplacementExpect(4, 1);
    }
    else
    {
        ReplacementPostRoad(TRUE);
        ReplacementExpect(2, 2);
    }
}

void SNDSP4::ReplacementProject10()
{
    Int16 segments;

    m_Repl.ViewX2 = (Int16)(m_Repl.ViewX2 + m_Repl.ViewDx);
    m_Repl.ViewY2 = (Int16)(m_Repl.ViewY2 + m_Repl.ViewDy);
    m_Repl.ViewXOfs2 = m_Repl.ViewX2;
    m_Repl.ViewYOfs2 = (Int16)(
        (((Int32)m_Repl.WorldYOfs * (Int32)m_Repl.Distance) >> 15) +
        m_Repl.PolyBottom -
        m_Repl.ViewY2
    );

    ReplacementClearOutput();
    ReplacementWriteWord((Uint16)m_Repl.ViewX2);
    ReplacementWriteWord((Uint16)m_Repl.ViewY2);

    segments = (Int16)(m_Repl.ViewY1 - m_Repl.ViewY2);
    if (m_Repl.ViewY2 >= m_Repl.PolyRaster)
    {
        segments = 0;
    }
    else
    {
        m_Repl.PolyRaster = m_Repl.ViewY2;
    }

    if (m_Repl.ViewY2 < m_Repl.PolyTop)
    {
        segments = 0;
        if (m_Repl.ViewY1 >= m_Repl.PolyTop)
            segments = (Int16)(m_Repl.ViewY1 - m_Repl.PolyTop);
    }

    if (segments < 0)
        segments = 0;

    m_Repl.Segments = segments;
    m_Repl.LightIndex = 0;
    ReplacementWriteWord((Uint16)segments);

    if (segments > 0)
    {
        ReplacementExpect(4, 1);
    }
    else
    {
        ReplacementPostRoad(FALSE);
        ReplacementExpect(2, 2);
    }
}

void SNDSP4::ReplacementBeginCommand(Uint16 uCommand)
{
    m_Repl.Command = uCommand;
    m_Repl.Phase = 0;
    m_Repl.InPos = 0;
    m_Repl.Need = 0;
    ReplacementClearOutput();

    switch (uCommand)
    {
        case 0x0000: ReplacementExpect(4, 0); break;
        case 0x0001: ReplacementExpect(44, 0); break;
        case 0x0003: ReplacementExpect(0, 0); break;
        case 0x0005: ReplacementExpect(0, 0); break;
        case 0x0006: ReplacementExpect(0, 0); break;
        case 0x0007: ReplacementExpect(34, 0); break;
        case 0x0008: ReplacementExpect(90, 0); break;
        case 0x0009: ReplacementExpect(14, 0); break;
        case 0x000a: ReplacementExpect(6, 0); break;
        case 0x000b: ReplacementExpect(6, 0); break;
        case 0x000d: ReplacementExpect(42, 0); break;
        case 0x000e: ReplacementExpect(0, 0); break;
        case 0x000f: ReplacementExpect(46, 0); break;
        case 0x0010: ReplacementExpect(36, 0); break;
        case 0x0011: ReplacementExpect(8, 0); break;

        default:
            printf("[dsp4] replacement program: unknown command %04X\n",
                   (unsigned)uCommand);
            ReplacementFinish();
            return;
    }

    if (m_Repl.Need == 0)
        ReplacementDispatch();
}

void SNDSP4::ReplacementDispatch()
{
    switch (m_Repl.Command)
    {
        case 0x0000:
        {
            Int32 a = ReplacementReadSWord(0);
            Int32 b = ReplacementReadSWord(2);
            Int32 product = a * b;

            ReplacementClearOutput();
            ReplacementWriteWord((Uint16)product);
            ReplacementWriteWord((Uint16)((Uint32)product >> 16));
            ReplacementFinish();
            break;
        }

        case 0x0001:
            if (m_Repl.Phase == 0)
            {
                m_Repl.WorldY = ReplacementReadDword(0);
                m_Repl.PolyBottom = ReplacementReadSWord(4);
                m_Repl.PolyTop = ReplacementReadSWord(6);
                m_Repl.PolyCxY = ReplacementReadSWord(8);
                m_Repl.ViewportBottom = ReplacementReadSWord(10);
                m_Repl.WorldX = ReplacementReadDword(12);
                m_Repl.PolyCxX = ReplacementReadSWord(16);
                m_Repl.PolyPtr = ReplacementReadSWord(18);
                m_Repl.WorldYOfs = ReplacementReadSWord(20);
                m_Repl.WorldDy = ReplacementReadDword(22);
                m_Repl.WorldDx = ReplacementReadDword(26);
                m_Repl.Distance = ReplacementReadSWord(30);
                m_Repl.WorldXEnv = ReplacementReadDword(34);
                m_Repl.WorldDdy = ReplacementReadSWord(38);
                m_Repl.WorldDdx = ReplacementReadSWord(40);
                m_Repl.ViewYOfsEnv = ReplacementReadSWord(42);

                m_Repl.ViewX1 =
                    (Int16)((m_Repl.WorldX + m_Repl.WorldXEnv) >> 16);
                m_Repl.ViewY1 = (Int16)(m_Repl.WorldY >> 16);
                m_Repl.ViewXOfs1 = (Int16)(m_Repl.WorldX >> 16);
                m_Repl.ViewYOfs1 = m_Repl.WorldYOfs;
                m_Repl.TurnX = 0;
                m_Repl.TurnDx = 0;
                m_Repl.PolyRaster = m_Repl.PolyBottom;

                ReplacementProject01();
                ReplacementExpect(2, 1);
            }
            else if (m_Repl.Phase == 1)
            {
                m_Repl.Distance = ReplacementReadSWord(0);
                if (m_Repl.Distance == (Int16)0x8000)
                {
                    ReplacementFinish();
                }
                else if ((Uint16)m_Repl.Distance == 0x8001)
                {
                    ReplacementExpect(6, 2);
                }
                else
                {
                    ReplacementExpect(6, 3);
                }
            }
            else if (m_Repl.Phase == 2)
            {
                m_Repl.Distance = ReplacementReadSWord(0);
                m_Repl.TurnX = ReplacementReadSWord(2);
                m_Repl.TurnDx = ReplacementReadSWord(4);

                {
                    Int32 turn =
                        ((Int32)m_Repl.TurnX *
                         (Int32)m_Repl.Distance) >> 15;
                    m_Repl.ViewX1 = (Int16)(m_Repl.ViewX1 + turn);
                    m_Repl.ViewXOfs1 =
                        (Int16)(m_Repl.ViewXOfs1 + turn);
                }

                m_Repl.TurnX =
                    (Int16)(m_Repl.TurnX + m_Repl.TurnDx);
                ReplacementExpect(2, 1);
            }
            else
            {
                m_Repl.WorldDdy = ReplacementReadSWord(0);
                m_Repl.WorldDdx = ReplacementReadSWord(2);
                m_Repl.ViewYOfsEnv = ReplacementReadSWord(4);
                m_Repl.WorldXEnv = 0;

                ReplacementProject01();
                ReplacementExpect(2, 1);
            }
            break;

        case 0x0003:
            m_Repl.OamRowMax = 33;
            memset(m_Repl.OamRow, 0, sizeof(m_Repl.OamRow));
            ReplacementFinish();
            break;

        case 0x0005:
            m_Repl.OamIndex = 0;
            m_Repl.OamBits = 0;
            m_Repl.SpriteCount = 0;
            memset(m_Repl.OamAttr, 0, sizeof(m_Repl.OamAttr));
            ReplacementFinish();
            break;

        case 0x0006:
        {
            Uint32 i;
            ReplacementClearOutput();
            for (i = 0; i < sizeof(m_Repl.OamAttr); ++i)
                ReplacementWriteByte(m_Repl.OamAttr[i]);
            ReplacementFinish();
            break;
        }

        case 0x0007:
            if (m_Repl.Phase == 0)
            {
                m_Repl.WorldY = ReplacementReadDword(0);
                m_Repl.PolyBottom = ReplacementReadSWord(4);
                m_Repl.PolyTop = ReplacementReadSWord(6);
                m_Repl.PolyCxY = ReplacementReadSWord(8);
                m_Repl.ViewportBottom = ReplacementReadSWord(10);
                m_Repl.WorldX = ReplacementReadDword(12);
                m_Repl.PolyCxX = ReplacementReadSWord(16);
                m_Repl.PolyPtr = ReplacementReadSWord(18);
                m_Repl.WorldYOfs = ReplacementReadSWord(20);
                m_Repl.Distance = ReplacementReadSWord(22);
                m_Repl.ViewY2 = ReplacementReadSWord(24);
                m_Repl.ViewDy = (Int16)(
                    ((Int32)ReplacementReadSWord(26) *
                     (Int32)m_Repl.Distance) >> 15
                );
                m_Repl.ViewX2 = ReplacementReadSWord(28);
                m_Repl.ViewDx = (Int16)(
                    ((Int32)ReplacementReadSWord(30) *
                     (Int32)m_Repl.Distance) >> 15
                );
                m_Repl.ViewYOfsEnv = ReplacementReadSWord(32);

                m_Repl.ViewX1 = (Int16)(m_Repl.WorldX >> 16);
                m_Repl.ViewY1 = (Int16)(m_Repl.WorldY >> 16);
                m_Repl.ViewXOfs1 = m_Repl.ViewX1;
                m_Repl.ViewYOfs1 = m_Repl.WorldYOfs;
                m_Repl.PolyRaster = m_Repl.PolyBottom;

                ReplacementProject07();
                ReplacementExpect(2, 1);
            }
            else if (m_Repl.Phase == 1)
            {
                m_Repl.Distance = ReplacementReadSWord(0);
                if (m_Repl.Distance == (Int16)0x8000)
                    ReplacementFinish();
                else
                    ReplacementExpect(10, 2);
            }
            else
            {
                m_Repl.ViewY2 = ReplacementReadSWord(0);
                m_Repl.ViewDy = (Int16)(
                    ((Int32)ReplacementReadSWord(2) *
                     (Int32)m_Repl.Distance) >> 15
                );
                m_Repl.ViewX2 = ReplacementReadSWord(4);
                m_Repl.ViewDx = (Int16)(
                    ((Int32)ReplacementReadSWord(6) *
                     (Int32)m_Repl.Distance) >> 15
                );
                m_Repl.ViewYOfsEnv = ReplacementReadSWord(8);

                ReplacementProject07();
                ReplacementExpect(2, 1);
            }
            break;

        case 0x000a:
        {
            Uint16 v = ReplacementReadWord(2);
            Uint16 nib[4];
            Uint16 mapped[4];
            int i;

            for (i = 0; i < 4; ++i)
            {
                Int32 n;
                nib[i] = (Uint16)((v >> (i * 4)) & 0x0f);
                n = nib[i];
                if (n < 8)
                    mapped[i] = (Uint16)(n * 0x30);
                else
                    mapped[i] = (Uint16)(-0x180 + (n - 8) * 0x30);
            }

            ReplacementClearOutput();
            ReplacementWriteWord(mapped[2]);
            ReplacementWriteWord(mapped[3]);
            ReplacementWriteWord(mapped[0]);
            ReplacementWriteWord(mapped[1]);
            ReplacementFinish();
            break;
        }

        case 0x000b:
            ReplacementClearOutput();
            ReplacementOp0B(
                ReplacementReadSWord(0),
                ReplacementReadSWord(2),
                ReplacementReadSWord(4),
                FALSE,
                TRUE
            );
            ReplacementFinish();
            break;

        case 0x000e:
            m_Repl.OamRowMax = 16;
            memset(m_Repl.OamRow, 0, sizeof(m_Repl.OamRow));
            ReplacementFinish();
            break;

        case 0x000f:
            if (m_Repl.Phase == 0)
            {
                m_Repl.WorldY = ReplacementReadDword(2);
                m_Repl.PolyBottom = ReplacementReadSWord(6);
                m_Repl.PolyTop = ReplacementReadSWord(8);
                m_Repl.PolyCxY = ReplacementReadSWord(10);
                m_Repl.ViewportBottom = ReplacementReadSWord(12);
                m_Repl.WorldX = ReplacementReadDword(14);
                m_Repl.PolyCxX = ReplacementReadSWord(18);
                m_Repl.PolyPtr = ReplacementReadSWord(20);
                m_Repl.WorldYOfs = ReplacementReadSWord(22);
                m_Repl.WorldDy = ReplacementReadDword(24);
                m_Repl.WorldDx = ReplacementReadDword(28);
                m_Repl.Distance = ReplacementReadSWord(32);
                m_Repl.WorldXEnv = ReplacementReadDword(36);
                m_Repl.WorldDdy = ReplacementReadSWord(40);
                m_Repl.WorldDdx = ReplacementReadSWord(42);
                m_Repl.ViewYOfsEnv = ReplacementReadSWord(44);

                m_Repl.ViewX1 =
                    (Int16)((m_Repl.WorldX + m_Repl.WorldXEnv) >> 16);
                m_Repl.ViewY1 = (Int16)(m_Repl.WorldY >> 16);
                m_Repl.ViewXOfs1 = (Int16)(m_Repl.WorldX >> 16);
                m_Repl.ViewYOfs1 = m_Repl.WorldYOfs;
                m_Repl.TurnX = 0;
                m_Repl.TurnDx = 0;
                m_Repl.PolyRaster = m_Repl.PolyBottom;

                ReplacementProject0F();
            }
            else if (m_Repl.Phase == 1)
            {
                Uint16 color = ReplacementLightColor(
                    ReplacementReadSWord(0),
                    ReplacementReadWord(2)
                );

                ReplacementClearOutput();
                ReplacementWriteWord(color);
                m_Repl.LightIndex++;

                if (m_Repl.LightIndex < 4)
                {
                    ReplacementExpect(4, 1);
                }
                else
                {
                    ReplacementAppendRaster();
                    ReplacementPostRoad(TRUE);
                    ReplacementExpect(2, 2);
                }
            }
            else if (m_Repl.Phase == 2)
            {
                m_Repl.Distance = ReplacementReadSWord(0);

                if (m_Repl.Distance == (Int16)0x8000)
                {
                    ReplacementFinish();
                }
                else if ((Uint16)m_Repl.Distance == 0x8001)
                {
                    ReplacementExpect(6, 3);
                }
                else
                {
                    ReplacementExpect(6, 4);
                }
            }
            else if (m_Repl.Phase == 3)
            {
                Int32 turn;

                m_Repl.Distance = ReplacementReadSWord(0);
                m_Repl.TurnX = ReplacementReadSWord(2);
                m_Repl.TurnDx = ReplacementReadSWord(4);

                turn =
                    ((Int32)m_Repl.TurnX *
                     (Int32)m_Repl.Distance) >> 15;
                m_Repl.ViewX1 = (Int16)(m_Repl.ViewX1 + turn);
                m_Repl.ViewXOfs1 =
                    (Int16)(m_Repl.ViewXOfs1 + turn);
                m_Repl.TurnX =
                    (Int16)(m_Repl.TurnX + m_Repl.TurnDx);

                ReplacementExpect(2, 2);
            }
            else
            {
                m_Repl.WorldDdy = ReplacementReadSWord(0);
                m_Repl.WorldDdx = ReplacementReadSWord(2);
                m_Repl.ViewYOfsEnv = ReplacementReadSWord(4);
                m_Repl.WorldXEnv = 0;

                ReplacementProject0F();
            }
            break;

        case 0x0010:
            if (m_Repl.Phase == 0)
            {
                m_Repl.WorldY = ReplacementReadDword(2);
                m_Repl.PolyBottom = ReplacementReadSWord(6);
                m_Repl.PolyTop = ReplacementReadSWord(8);
                m_Repl.PolyCxY = ReplacementReadSWord(10);
                m_Repl.ViewportBottom = ReplacementReadSWord(12);
                m_Repl.WorldX = ReplacementReadDword(14);
                m_Repl.PolyCxX = ReplacementReadSWord(18);
                m_Repl.PolyPtr = ReplacementReadSWord(20);
                m_Repl.WorldYOfs = ReplacementReadSWord(22);
                m_Repl.Distance = ReplacementReadSWord(24);
                m_Repl.ViewY2 = ReplacementReadSWord(26);
                m_Repl.ViewDy = (Int16)(
                    ((Int32)ReplacementReadSWord(28) *
                     (Int32)m_Repl.Distance) >> 15
                );
                m_Repl.ViewX2 = ReplacementReadSWord(30);
                m_Repl.ViewDx = (Int16)(
                    ((Int32)ReplacementReadSWord(32) *
                     (Int32)m_Repl.Distance) >> 15
                );
                m_Repl.ViewYOfsEnv = ReplacementReadSWord(34);

                m_Repl.ViewX1 = (Int16)(m_Repl.WorldX >> 16);
                m_Repl.ViewY1 = (Int16)(m_Repl.WorldY >> 16);
                m_Repl.ViewXOfs1 = m_Repl.ViewX1;
                m_Repl.ViewYOfs1 = m_Repl.WorldYOfs;
                m_Repl.PolyRaster = m_Repl.PolyBottom;

                ReplacementProject10();
            }
            else if (m_Repl.Phase == 1)
            {
                Uint16 color = ReplacementLightColor(
                    ReplacementReadSWord(0),
                    ReplacementReadWord(2)
                );

                ReplacementClearOutput();
                ReplacementWriteWord(color);
                m_Repl.LightIndex++;

                if (m_Repl.LightIndex < 4)
                {
                    ReplacementExpect(4, 1);
                }
                else
                {
                    ReplacementAppendRaster();
                    ReplacementPostRoad(FALSE);
                    ReplacementExpect(2, 2);
                }
            }
            else if (m_Repl.Phase == 2)
            {
                m_Repl.Distance = ReplacementReadSWord(0);
                if (m_Repl.Distance == (Int16)0x8000)
                    ReplacementFinish();
                else
                    ReplacementExpect(10, 3);
            }
            else
            {
                m_Repl.ViewY2 = ReplacementReadSWord(0);
                m_Repl.ViewDy = (Int16)(
                    ((Int32)ReplacementReadSWord(2) *
                     (Int32)m_Repl.Distance) >> 15
                );
                m_Repl.ViewX2 = ReplacementReadSWord(4);
                m_Repl.ViewDx = (Int16)(
                    ((Int32)ReplacementReadSWord(6) *
                     (Int32)m_Repl.Distance) >> 15
                );

                /* The fifth word in this continuation packet is not used by
                   the public DSP-4 algorithm. Keep consuming it so command
                   framing stays byte-exact. */
                (void)ReplacementReadWord(8);

                ReplacementProject10();
            }
            break;

        case 0x0011:
        {
            Int32 d = ReplacementReadSWord(0);
            Int32 c = ReplacementReadSWord(2);
            Int32 b = ReplacementReadSWord(4);
            Int32 a = ReplacementReadSWord(6);
            Uint16 result;

            result = (Uint16)(
                (((a * 341) >> 2)  & 0xf000) |
                (((b * 341) >> 6)  & 0x0f00) |
                (((c * 341) >> 10) & 0x00f0) |
                (((d * 341) >> 14) & 0x000f)
            );

            ReplacementClearOutput();
            ReplacementWriteWord(result);
            ReplacementFinish();
            break;
        }

        /* These are recognised so the host protocol cannot desynchronise,
           but their geometry is deliberately left for the next milestone. */
        case 0x0008:
        case 0x0009:
        case 0x000d:
        {
            Uint32 bit = (Uint32)m_Repl.Command & 31u;
            Uint32 mask = (Uint32)1u << bit;
            if (!(m_Repl.UnsupportedMask & mask))
            {
                printf("[dsp4] replacement program: command %04X recognised but not implemented yet\n",
                       (unsigned)m_Repl.Command);
                m_Repl.UnsupportedMask |= mask;
            }
            ReplacementClearOutput();
            ReplacementFinish();
            break;
        }

        default:
            ReplacementFinish();
            break;
    }
}

void SNDSP4::ReplacementWrite(Uint8 uData)
{
    if (m_Repl.OutPos < m_Repl.OutCount)
    {
        /* Real games do not normally write while unread output is pending.
           Keep the historical DSP-4 bus behaviour: a write consumes one
           pending byte rather than letting command framing drift. */
        m_Repl.OutPos++;
        return;
    }

    if (m_Repl.WaitingCommand)
    {
        if (!m_Repl.HalfCommand)
        {
            m_Repl.Command = uData;
            m_Repl.HalfCommand = TRUE;
            return;
        }

        m_Repl.Command =
            (Uint16)(m_Repl.Command | ((Uint16)uData << 8));
        m_Repl.HalfCommand = FALSE;
        m_Repl.WaitingCommand = FALSE;
        ReplacementBeginCommand(m_Repl.Command);
        return;
    }

    if (m_Repl.InPos < REPL_INPUT_BYTES)
        m_Repl.Input[m_Repl.InPos++] = uData;

    if (m_Repl.InPos >= m_Repl.Need)
        ReplacementDispatch();
}

Uint8 SNDSP4::ReplacementRead()
{
    if (m_Repl.OutPos < m_Repl.OutCount)
    {
        Uint8 uValue = m_Repl.Output[m_Repl.OutPos++];
        if (m_Repl.OutPos >= m_Repl.OutCount)
        {
            m_Repl.OutPos = 0;
            m_Repl.OutCount = 0;
        }
        return uValue;
    }

    /* Public DSP-4 protocol requires DR=0xffff after command completion. */
    return 0xff;
}
