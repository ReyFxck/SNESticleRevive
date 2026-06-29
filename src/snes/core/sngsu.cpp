/*
 * sngsu.cpp - SuperFX / GSU core (Etapa 1: infra + control + opcodes minimos)
 *
 * Clean-room a partir da documentacao publica (fullsnes/sneslab/nesdev).
 * Nenhum codigo de emulador foi copiado.  GPLv2 (veja LICENSE).
 *
 * Escopo desta etapa: estado, SFR, mapa de memoria, MMIO ($3000-$34FF),
 * controle GO/STOP/IRQ, e um loop fetch/execute com:
 *   STOP(00) NOP(01) CACHE(02) TO(10-1F) WITH(20-2F) ALT1/2/3(3D-3F)
 *   ADD/ADC(50-5F) SUB/SBC/CMP(60-6F) FROM(B0-BF) IWT(F0-FF)
 * O set completo (~90 opcodes) e os graficos (PLOT/pixel cache) vem depois.
 */

#include "types.h"
#include "sngsu.h"

#include <string.h>

SNGSU::SNGSU()
{
    m_pRom = NULL; m_uRomSize = 0; m_uRomMask = 0;
    m_pRam = NULL; m_uRamSize = 0; m_uRamMask = 0;
    Reset();
}

void SNGSU::SetMemory(Uint8 *pRom, Uint32 uRomSize, Uint8 *pRam, Uint32 uRamSize)
{
    m_pRom = pRom; m_uRomSize = uRomSize; m_uRomMask = uRomSize ? uRomSize - 1 : 0;
    m_pRam = pRam; m_uRamSize = uRamSize; m_uRamMask = uRamSize ? uRamSize - 1 : 0;
}

void SNGSU::Reset()
{
    memset(m_R, 0, sizeof(m_R));
    m_RegLatch = 0;
    m_bZ = m_bCY = m_bS = m_bOV = FALSE;
    m_bGo = FALSE;
    m_bRomRead = FALSE;
    m_bAlt1 = m_bAlt2 = FALSE;
    m_bIL = m_bIH = FALSE;
    m_bB = FALSE;
    m_bIrq = FALSE;
    m_Sreg = 0; m_Dreg = 0;
    m_PBR = 0; m_ROMBR = 0; m_RAMBR = 0;
    m_CFGR = 0; m_SCBR = 0; m_CLSR = 0; m_SCMR = 0;
    m_VCR = 0x04;            // GSU2 por padrao (01h = MC1/Star Fox)
    m_CBR = 0;
    m_RomBuffer = 0; m_RomBufValid = FALSE;
    memset(m_Cache, 0, sizeof(m_Cache));
}

//==========================================================================
//  SFR (Status/Flag Register)
//==========================================================================
Uint8 SNGSU::SfrLow() const
{
    Uint8 v = 0;
    if (m_bZ)       v |= 0x02;
    if (m_bCY)      v |= 0x04;
    if (m_bS)       v |= 0x08;
    if (m_bOV)      v |= 0x10;
    if (m_bGo)      v |= 0x20;
    if (m_bRomRead) v |= 0x40;
    return v;
}

Uint8 SNGSU::SfrHigh() const
{
    Uint8 v = 0;
    if (m_bAlt1) v |= 0x01;
    if (m_bAlt2) v |= 0x02;
    if (m_bIL)   v |= 0x04;
    if (m_bIH)   v |= 0x08;
    if (m_bB)    v |= 0x10;
    if (m_bIrq)  v |= 0x80;
    return v;
}

void SNGSU::SfrWriteLow(Uint8 v)
{
    // bits 1-5 sao escrevveis; escrever pode limpar GO (aborta o programa)
    m_bZ  = (v & 0x02) != 0;
    m_bCY = (v & 0x04) != 0;
    m_bS  = (v & 0x08) != 0;
    m_bOV = (v & 0x10) != 0;
    m_bGo = (v & 0x20) != 0;
}

//==========================================================================
//  Memoria do cartucho (visao do GSU)
//==========================================================================
Uint32 SNGSU::RomOffset(Uint8 uBank, Uint16 uAddr) const
{
    if (uBank < 0x40)
        return ((Uint32)uBank << 15) | (uAddr & 0x7FFF);   // LoROM ($8000-$FFFF)
    // $40-$5F: HiROM contiguo (espelho do mesmo ROM)
    return ((Uint32)(uBank & 0x3F) << 16) | uAddr;
}

Uint8 SNGSU::RomReadByte(Uint8 uBank, Uint16 uAddr) const
{
    if (!m_pRom || !m_uRomSize) return 0xFF;
    Uint32 off = RomOffset(uBank, uAddr);
    return m_pRom[off % m_uRomSize];
}

Uint8 SNGSU::RamReadByte(Uint32 uAddr) const
{
    if (!m_pRam || !m_uRamSize) return 0xFF;
    return m_pRam[uAddr % m_uRamSize];
}

void SNGSU::RamWriteByte(Uint32 uAddr, Uint8 v)
{
    if (!m_pRam || !m_uRamSize) return;
    m_pRam[uAddr % m_uRamSize] = v;
}

Uint8 SNGSU::CodeFetch()
{
    Uint8 b;
    if (m_PBR >= 0x70)
    {
        // codigo em Game Pak RAM ($70/$71)
        Uint32 off = ((Uint32)(m_PBR & 1) << 16) | m_R[15];
        b = RamReadByte(off);
    }
    else
    {
        b = RomReadByte(m_PBR, m_R[15]);
    }
    m_R[15]++;
    return b;
}

//==========================================================================
//  Arbitragem ROM/RAM (SCMR)
//==========================================================================
Bool SNGSU::SnesCanAccessRom() const { return (m_SCMR & 0x10) == 0; }  // RON
Bool SNGSU::SnesCanAccessRam() const { return (m_SCMR & 0x08) == 0; }  // RAN

//==========================================================================
//  MMIO do lado SNES ($3000-$34FF)
//==========================================================================
Uint8 SNGSU::ReadReg(Uint16 uAddrLow)
{
    Uint16 a = uAddrLow & 0xFFFF;

    // cache RAM
    if (a >= 0x3100 && a <= 0x32FF)
        return m_Cache[a - 0x3100];

    // R0-R15
    if (a >= 0x3000 && a <= 0x301F)
    {
        Int32 idx = (a - 0x3000) >> 1;
        return (a & 1) ? (Uint8)(m_R[idx] >> 8) : (Uint8)(m_R[idx] & 0xFF);
    }

    switch (a)
    {
    case 0x3030: return SfrLow();
    case 0x3031: { Uint8 v = SfrHigh(); m_bIrq = FALSE; return v; } // leitura limpa IRQ
    case 0x3034: return m_PBR;
    case 0x3036: return m_ROMBR;
    case 0x3037: return m_CFGR;
    case 0x3038: return m_SCBR;
    case 0x3039: return m_CLSR;
    case 0x303A: return m_SCMR;
    case 0x303B: return m_VCR;                 // version code (read-only)
    case 0x303C: return m_RAMBR;
    case 0x303E: return (Uint8)(m_CBR & 0xFF);
    case 0x303F: return (Uint8)(m_CBR >> 8);
    default:     return 0x00;
    }
}

void SNGSU::WriteReg(Uint16 uAddrLow, Uint8 uData)
{
    Uint16 a = uAddrLow & 0xFFFF;

    if (a >= 0x3100 && a <= 0x32FF) { m_Cache[a - 0x3100] = uData; return; }

    // R0-R15: par = LATCH; impar = aplica (MSB=data, LSB=latch)
    if (a >= 0x3000 && a <= 0x301F)
    {
        if ((a & 1) == 0) { m_RegLatch = uData; return; }
        Int32 idx = (a - 0x3000) >> 1;
        m_R[idx] = (Uint16)(((Uint16)uData << 8) | m_RegLatch);
        if (a == 0x301F)        // escrita em R15.MSB dispara GO
            m_bGo = TRUE;
        return;
    }

    switch (a)
    {
    case 0x3030: SfrWriteLow(uData); break;
    case 0x3031: /* high: normalmente nao escrito pelo SNES */ break;
    case 0x3033: /* BRAMR (backup ram enable) - ignorado por enquanto */ break;
    case 0x3034: m_PBR  = uData & 0x7F; break;
    case 0x3037: m_CFGR = uData; break;
    case 0x3038: m_SCBR = uData; break;
    case 0x3039: m_CLSR = uData; break;
    case 0x303A: m_SCMR = uData; break;
    default: break;
    }
}

//==========================================================================
//  Execucao
//==========================================================================
void SNGSU::ResetPrefix()
{
    m_Sreg = 0; m_Dreg = 0;
    m_bAlt1 = FALSE; m_bAlt2 = FALSE; m_bB = FALSE;
}

void SNGSU::SetZSfromWord(Uint16 v)
{
    m_bZ = (v == 0);
    m_bS = (v & 0x8000) != 0;
}

void SNGSU::Run(Int32 nClocks)
{
    while (m_bGo && nClocks-- > 0)
        Step();
}

void SNGSU::Step()
{
    Uint8 op = CodeFetch();
    Bool  bIsPrefix = FALSE;

    Uint8  n   = op & 0x0F;
    Uint16 sr  = m_R[m_Sreg];               // valor source

    if (op >= 0x10 && op <= 0x1F)            // TO Rn / MOVE
    {
        if (!m_bB) { m_Dreg = n; bIsPrefix = TRUE; }
        else       { m_R[n] = m_R[m_Sreg]; } // MOVE (B): Rn = Rsreg
    }
    else if (op >= 0x20 && op <= 0x2F)       // WITH Rn (Sreg=Dreg=n, B=1)
    {
        m_Sreg = n; m_Dreg = n; m_bB = TRUE;
        bIsPrefix = TRUE;
    }
    else if (op >= 0xB0 && op <= 0xBF)       // FROM Rn / MOVES
    {
        if (!m_bB) { m_Sreg = n; bIsPrefix = TRUE; }
        else       { m_R[m_Dreg] = m_R[n]; SetZSfromWord(m_R[n]); } // MOVES
    }
    else if (op == 0x3D) { m_bAlt1 = TRUE; bIsPrefix = TRUE; }   // ALT1
    else if (op == 0x3E) { m_bAlt2 = TRUE; bIsPrefix = TRUE; }   // ALT2
    else if (op == 0x3F) { m_bAlt1 = TRUE; m_bAlt2 = TRUE; bIsPrefix = TRUE; } // ALT3
    else if (op >= 0x50 && op <= 0x5F)       // ADD / ADC / ADD#imm / ADC#imm
    {
        Uint32 a = sr;
        Uint32 b = m_bAlt2 ? (Uint32)n : (Uint32)m_R[n];
        Uint32 cin = m_bAlt1 ? (m_bCY ? 1u : 0u) : 0u;   // ALT1 => ADC
        Uint32 r = a + b + cin;
        m_bCY = (r > 0xFFFF);
        m_bOV = ((~(a ^ b)) & (a ^ r) & 0x8000) != 0;
        Uint16 res = (Uint16)r; SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op >= 0x60 && op <= 0x6F)       // SUB / SBC / SUB#imm / CMP
    {
        Bool bCmp = (m_bAlt1 && m_bAlt2);
        Uint32 a = sr;
        Uint32 b = (m_bAlt2 && !m_bAlt1) ? (Uint32)n : (Uint32)m_R[n];
        Uint32 cin = (m_bAlt1 && !m_bAlt2) ? (m_bCY ? 1u : 0u) : 1u;  // ALT1 => SBC
        Uint32 r = a + ((~b) & 0xFFFF) + cin;
        m_bCY = (r > 0xFFFF);
        m_bOV = ((a ^ b) & (a ^ r) & 0x8000) != 0;
        Uint16 res = (Uint16)r; SetZSfromWord(res);
        if (!bCmp) m_R[m_Dreg] = res;
    }
    else if (op == 0x70)                     // MERGE
    {
        Uint16 res = (Uint16)((m_R[7] & 0xFF00) | ((m_R[8] >> 8) & 0x00FF));
        SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op >= 0x71 && op <= 0x7F)       // AND / BIC / AND#imm / BIC#imm
    {
        Uint16 b = m_bAlt2 ? (Uint16)n : m_R[n];
        if (m_bAlt1) b = (Uint16)~b;          // ALT1 => BIC (AND NOT)
        Uint16 res = (Uint16)(sr & b);
        SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0xC0)                     // HIB (high byte -> low)
    {
        Uint16 res = (Uint16)(sr >> 8);
        m_bZ = (res == 0); m_bS = (res & 0x80) != 0;
        m_R[m_Dreg] = res;
    }
    else if (op >= 0xC1 && op <= 0xCF)       // OR / XOR / OR#imm / XOR#imm
    {
        Uint16 b = m_bAlt2 ? (Uint16)n : m_R[n];
        Uint16 res = m_bAlt1 ? (Uint16)(sr ^ b) : (Uint16)(sr | b);
        SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0x4F)                     // NOT
    {
        Uint16 res = (Uint16)~sr; SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0x03)                     // LSR
    {
        m_bCY = (sr & 1) != 0;
        Uint16 res = (Uint16)(sr >> 1); SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0x04)                     // ROL
    {
        Uint16 res = (Uint16)((sr << 1) | (m_bCY ? 1 : 0));
        m_bCY = (sr & 0x8000) != 0; SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0x96)                     // ASR / DIV2 (ALT1)
    {
        m_bCY = (sr & 1) != 0;
        Uint16 res = (Uint16)(((Int16)sr) >> 1);
        if (m_bAlt1 && sr == 0xFFFF) res = 0;  // DIV2 arredonda p/ zero
        SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0x97)                     // ROR
    {
        Uint16 res = (Uint16)((m_bCY ? 0x8000 : 0) | (sr >> 1));
        m_bCY = (sr & 1) != 0; SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0x4D)                     // SWAP (troca bytes)
    {
        Uint16 res = (Uint16)((sr >> 8) | (sr << 8));
        SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0x95)                     // SEX (sign-extend byte)
    {
        Uint16 res = (Uint16)(Int16)(Int8)(sr & 0xFF);
        SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op == 0x9E)                     // LOB (low byte)
    {
        Uint16 res = (Uint16)(sr & 0x00FF);
        m_bZ = (res == 0); m_bS = (res & 0x80) != 0;
        m_R[m_Dreg] = res;
    }
    else if (op >= 0x80 && op <= 0x8F)       // MULT / UMULT / +#imm (low 16)
    {
        Int32 r;
        if (m_bAlt1) {                        // UMULT (sem sinal)
            Uint32 b = m_bAlt2 ? (Uint32)n : (Uint32)m_R[n];
            r = (Int32)((Uint32)sr * b);
        } else {                              // MULT (com sinal)
            Int32 b = m_bAlt2 ? (Int32)n : (Int32)(Int16)m_R[n];
            r = (Int32)(Int16)sr * b;
        }
        Uint16 res = (Uint16)r; SetZSfromWord(res); m_R[m_Dreg] = res;
    }
    else if (op >= 0xD0 && op <= 0xDE)       // INC Rn
    {
        Uint16 res = (Uint16)(m_R[n] + 1); SetZSfromWord(res); m_R[n] = res;
    }
    else if (op >= 0xE0 && op <= 0xEE)       // DEC Rn
    {
        Uint16 res = (Uint16)(m_R[n] - 1); SetZSfromWord(res); m_R[n] = res;
    }
    else if (op >= 0xA0 && op <= 0xAF)       // IBT Rn,#imm8 (ALT off)
    {
        // (ALT1=LMS / ALT2=SMS de memoria ficam para a etapa de memoria)
        Uint8 imm = CodeFetch();
        m_R[n] = (Uint16)(Int16)(Int8)imm;    // sign-extend
    }
    else if (op >= 0xF0 && op <= 0xFF)       // IWT Rn,#imm16 (ALT off)
    {
        Uint8 lo = CodeFetch();
        Uint8 hi = CodeFetch();
        m_R[n] = (Uint16)(((Uint16)hi << 8) | lo);
    }
    else if (op == 0x02)                     // CACHE
    {
        m_CBR = m_R[15] & 0xFFF0;
    }
    else if (op == 0x00)                     // STOP
    {
        m_bGo = FALSE; m_bIrq = TRUE; bIsPrefix = TRUE;
    }
    else if (op == 0x01)                     // NOP
    {
        /* nada */
    }
    else
    {
        // Opcodes ainda nao implementados nesta etapa (branches, LOOP, JMP,
        // memoria LDW/STW/LM/SM/SBK, LINK, e graficos PLOT/RPIX/COLOR/CMODE).
        // Tratados como NOP para nao travar; vem nas proximas etapas.
    }

    if (!bIsPrefix)
        ResetPrefix();
}
