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

// Trace de diagnostico (via DLog -> SIO da EE, visivel no log do NetherSX2).
// Limitado a poucos eventos para nao floodar.  Mostra se o jogo realmente
// dispara o GSU, com qual PBR:R15/SCMR, e se ele termina (STOP) ou enrosca
// (RUNAWAY).  No bench host, DLog e' stub.
extern "C" void DLog(const char *fmt, ...);
static int s_gsuLog = 0;
#define GSU_LOG(...) do { if (s_gsuLog < 1200) { DLog(__VA_ARGS__); s_gsuLog++; } } while (0)

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
    m_Runaway = 0;
    m_PlotCount = 0;
    m_BranchPending = FALSE; m_BranchTarget = 0;
    m_BranchSetPBR = FALSE; m_BranchPBR = 0;
    m_LastRamAddr = 0;
    m_Color = 0; m_POR = 0;
    memset(m_PixColor, 0, sizeof(m_PixColor));
    m_PixFlags = 0; m_PixXBase = 0; m_PixY = 0; m_PixValid = FALSE;
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

// RAMBR(0x70/0x71):addr -> offset linear na Game Pak RAM
Uint32 SNGSU::RamLinear(Uint16 uAddr) const
{
    return (((Uint32)(m_RAMBR & 1)) << 16) | uAddr;
}

// Leitura de word.  Em endereco impar o GSU acessa [addr AND NOT 1] com os
// bytes LSB/MSB trocados (quirk documentado).
Uint16 SNGSU::RamReadWord(Uint16 uAddr) const
{
    Uint16 base = (Uint16)(uAddr & ~1);
    Uint8 b0 = RamReadByte(RamLinear(base));
    Uint8 b1 = RamReadByte(RamLinear((Uint16)(base + 1)));
    if (uAddr & 1) return (Uint16)((b0 << 8) | b1);   // swapped
    return (Uint16)(b0 | (b1 << 8));                  // normal (LE)
}

void SNGSU::RamWriteWord(Uint16 uAddr, Uint16 v)
{
    Uint16 base = (Uint16)(uAddr & ~1);
    if (uAddr & 1) {
        RamWriteByte(RamLinear(base),               (Uint8)(v >> 8));
        RamWriteByte(RamLinear((Uint16)(base + 1)), (Uint8)(v & 0xFF));
    } else {
        RamWriteByte(RamLinear(base),               (Uint8)(v & 0xFF));
        RamWriteByte(RamLinear((Uint16)(base + 1)), (Uint8)(v >> 8));
    }
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
        {
            m_bGo = TRUE;
            m_Runaway = 0;      // reinicia o watchdog a cada novo START
            m_PlotCount = 0;    // diag: conta PLOTs desta rotina
            GSU_LOG("[gsu] GO pbr=%02X r15=%04X scmr=%02X scbr=%02X",
                    (unsigned)m_PBR, (unsigned)m_R[15],
                    (unsigned)m_SCMR, (unsigned)m_SCBR);
        }
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

//==========================================================================
//  Graficos (PLOT / pixel cache)
//==========================================================================
Int32 SNGSU::ScreenBpp() const
{
    switch (m_SCMR & 0x03) {         // MD0-1
    case 0:  return 2;               // 4 cores
    case 3:  return 8;               // 256 cores
    default: return 4;               // 16 cores (1) e reservado (2)
    }
}

// Numero do tile (caractere) que contem o pixel (x,y), conforme a altura
// de tela (SCMR.HT0/HT1).
Uint32 SNGSU::PixelTileNo(Uint8 x, Uint8 y) const
{
    Uint32 cx = x >> 3, cy = y >> 3;
    Uint32 ht = (((m_SCMR >> 5) & 1) << 1) | ((m_SCMR >> 2) & 1);
    switch (ht) {
    case 0:  return cx * 0x10 + cy;                       // 128 pixels
    case 1:  return cx * 0x14 + cy;                       // 160 pixels
    case 2:  return cx * 0x18 + cy;                       // 192 pixels
    default:                                              // OBJ 256x256
        return (((Uint32)y >> 7) * 0x200) + (((Uint32)x >> 7) * 0x100)
             + ((cy & 0x0F) * 0x10) + (cx & 0x0F);
    }
}

// Endereco (offset linear na Game Pak RAM) da linha de bitplanes do tile.
Uint32 SNGSU::PixelRowAddr(Uint8 x, Uint8 y) const
{
    Uint32 tile = PixelTileNo(x, y);
    Uint32 tileSize = (Uint32)(8 * ScreenBpp());          // 16/32/64
    return tile * tileSize + ((Uint32)m_SCBR << 10) + (Uint32)(y & 7) * 2;
}

// Descarrega o cache de pixels para a RAM (formato bitplane do SNES).
void SNGSU::PixFlush()
{
    if (!m_PixValid || m_PixFlags == 0) { m_PixFlags = 0; m_PixValid = FALSE; return; }
    Int32  bpp = ScreenBpp();
    Uint32 rowAddr = PixelRowAddr(m_PixXBase, m_PixY);
    for (Int32 b = 0; b < bpp; b++) {
        // plano b: par (b>>1) a offset (b>>1)*16, byte (b&1) dentro do par
        Uint32 addr = rowAddr + (Uint32)((b >> 1) * 16 + (b & 1));
        Uint8  byte = RamReadByte(addr);
        for (Int32 i = 0; i < 8; i++) {
            if (m_PixFlags & (1 << i)) {
                Uint8 mask = (Uint8)(1 << (7 - i));        // pixel 0 = bit7
                if ((m_PixColor[i] >> b) & 1) byte |= mask;
                else                          byte &= (Uint8)~mask;
            }
        }
        RamWriteByte(addr, byte);
    }
    m_PixFlags = 0; m_PixValid = FALSE;
}

void SNGSU::Plot()
{
    m_PlotCount++;                    // diag
    Uint8 x = (Uint8)(m_R[1] & 0xFF);
    Uint8 y = (Uint8)(m_R[2] & 0xFF);

    // transparencia (igual ao hardware): por PADRAO (bit transparent=POR.0
    // LIMPO) a cor 0 nao e' desenhada.  Em 8bpp (MD=3) checa a cor inteira
    // (ou so' o low nibble se freezehigh); em 2/4bpp checa o low nibble.
    if (!(m_POR & 0x01))
    {
        Bool skip;
        if ((m_SCMR & 0x03) == 3)
            skip = (m_POR & 0x08) ? ((m_Color & 0x0F) == 0) : (m_Color == 0);
        else
            skip = ((m_Color & 0x0F) == 0);
        if (skip) { m_R[1]++; return; }
    }

    // cor a plotar, com dither (POR.1) -- dither nao se aplica em 8bpp
    Uint8 color = m_Color;
    if ((m_POR & 0x02) && (m_SCMR & 0x03) != 3)
    {
        if (((x ^ y) & 1) != 0) color = (Uint8)(color >> 4);
        color &= 0x0F;
    }

    Uint8 xbase = (Uint8)(x & 0xF8);
    if (m_PixValid && (xbase != m_PixXBase || y != m_PixY)) PixFlush();
    m_PixXBase = xbase; m_PixY = y; m_PixValid = TRUE;
    m_PixColor[x & 7] = color;
    m_PixFlags |= (Uint8)(1 << (x & 7));
    m_R[1]++;

    if (m_PixFlags == 0xFF) PixFlush();
}

Uint16 SNGSU::Rpix()
{
    PixFlush();                       // RPIX sempre forca o flush antes de ler
    Uint8 x = (Uint8)(m_R[1] & 0xFF);
    Uint8 y = (Uint8)(m_R[2] & 0xFF);
    Int32 bpp = ScreenBpp();
    Uint32 rowAddr = PixelRowAddr(x, y);
    Uint16 color = 0;
    for (Int32 b = 0; b < bpp; b++) {
        Uint32 addr = rowAddr + (Uint32)((b >> 1) * 16 + (b & 1));
        Uint8  byte = RamReadByte(addr);
        Uint8  bit  = (Uint8)((byte >> (7 - (x & 7))) & 1);
        color |= (Uint16)(bit << b);
    }
    return color;
}

// Pipeline de escrita de COLOR (usado por COLOR e GETC), com POR.2/POR.3.
void SNGSU::ColorWrite(Uint8 src)
{
    if (m_POR & 0x04)       m_Color = (Uint8)((m_Color & 0xF0) | (src >> 4));   // high-nibble
    else if (m_POR & 0x08)  m_Color = (Uint8)((m_Color & 0xF0) | (src & 0x0F)); // freeze-high
    else                    m_Color = src;
}

void SNGSU::Run(Int32 nClocks)
{
    while (m_bGo && nClocks-- > 0)
        Step();
}

void SNGSU::Step()
{
    // watchdog: rede de seguranca contra um programa que nunca alcance STOP
    // (bug nosso ou ROM corrompida).  Apos um teto de instrucoes, forca a
    // parada (+IRQ) para nao travar a EE.  Rotinas reais do Star Fox terminam
    // bem antes disto.
    if (++m_Runaway > 2000000)
    {
        GSU_LOG("[gsu] RUNAWAY! r15=%04X pbr=%02X", (unsigned)m_R[15], (unsigned)m_PBR);
        m_bGo = FALSE; m_bIrq = TRUE; m_Runaway = 0;
        return;
    }

    Uint8 op = CodeFetch();

    Bool  bIsPrefix = FALSE;
    Bool  doBranch  = m_BranchPending;   // delay slot do salto anterior

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
    else if (op == 0x4C)                     // PLOT / RPIX (ALT1)
    {
        if (m_bAlt1) { Uint16 px = Rpix(); SetZSfromWord(px); m_R[m_Dreg] = px; }
        else         { Plot(); }
    }
    else if (op == 0x4E)                     // COLOR / CMODE (ALT1)
    {
        if (m_bAlt1) m_POR = (Uint8)(sr & 0x1F);     // CMODE: por = Rs & 1Fh
        else         ColorWrite((Uint8)(sr & 0xFF)); // COLOR: color = Rs
    }
    else if (op == 0xEF)                      // GETB / GETBH / GETBL / GETBS
    {
        Uint8 byte = RomReadByte(m_ROMBR, m_R[14]);
        if (m_bAlt1 && m_bAlt2)               // GETBS (3F): sign-expand
            m_R[m_Dreg] = (Uint16)(Int16)(Int8)byte;
        else if (m_bAlt1)                     // GETBH (3D): hi=byte, lo unchanged
            m_R[m_Dreg] = (Uint16)((m_R[m_Dreg] & 0x00FF) | (byte << 8));
        else if (m_bAlt2)                     // GETBL (3E): lo=byte, hi unchanged
            m_R[m_Dreg] = (Uint16)((m_R[m_Dreg] & 0xFF00) | byte);
        else                                  // GETB: zero-expand
            m_R[m_Dreg] = (Uint16)byte;
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
    else if (op == 0x9F)                     // FMULT / LMULT (ALT1)
    {
        // Produto com sinal de Sreg x R6 (32 bits).  FMULT: 16 bits altos ->
        // destino, CY = bit15 do produto.  LMULT (ALT1): alem disso, 16 bits
        // baixos -> R4.  R4 nunca pode ser destino do byte alto.
        Int32  p  = (Int32)(Int16)sr * (Int32)(Int16)m_R[6];
        Uint32 up = (Uint32)p;
        Uint16 hi = (Uint16)(up >> 16);
        if (m_bAlt1) m_R[4] = (Uint16)(up & 0xFFFF);   // LMULT: low 16 -> R4
        if (m_Dreg != 4) m_R[m_Dreg] = hi;             // R4 nao pode receber o alto
        SetZSfromWord(hi);
        m_bCY = ((up >> 15) & 1) != 0;                 // CY = bit15 do produto
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
    else if (op >= 0xA0 && op <= 0xAF)       // IBT Rn,#imm8 / LMS / SMS
    {
        if (m_bAlt1) {                        // LMS Rn,(yy): Rn = word[ramb:kk*2]
            Uint16 addr = (Uint16)(CodeFetch() * 2);
            m_R[n] = RamReadWord(addr); m_LastRamAddr = addr;
        } else if (m_bAlt2) {                 // SMS (yy),Rn: word[ramb:kk*2] = Rn
            Uint16 addr = (Uint16)(CodeFetch() * 2);
            RamWriteWord(addr, m_R[n]); m_LastRamAddr = addr;
        } else {                              // IBT Rn,#imm8 (sign-extend)
            Uint8 imm = CodeFetch();
            m_R[n] = (Uint16)(Int16)(Int8)imm;
        }
    }
    else if (op >= 0xF0 && op <= 0xFF)       // IWT Rn,#imm16 / LM / SM
    {
        if (m_bAlt1) {                        // LM Rn,(hilo)
            Uint8 lo = CodeFetch(), hi = CodeFetch();
            Uint16 addr = (Uint16)((hi << 8) | lo);
            m_R[n] = RamReadWord(addr); m_LastRamAddr = addr;
        } else if (m_bAlt2) {                 // SM (hilo),Rn
            Uint8 lo = CodeFetch(), hi = CodeFetch();
            Uint16 addr = (Uint16)((hi << 8) | lo);
            RamWriteWord(addr, m_R[n]); m_LastRamAddr = addr;
        } else {                              // IWT Rn,#imm16
            Uint8 lo = CodeFetch(), hi = CodeFetch();
            m_R[n] = (Uint16)(((Uint16)hi << 8) | lo);
        }
    }
    else if (op >= 0x05 && op <= 0x0F)       // branches (delay slot)
    {
        Int8 disp = (Int8)CodeFetch();
        Bool take = FALSE;
        switch (op) {
        case 0x05: take = TRUE;               break;   // BRA
        case 0x06: take = (m_bS == m_bOV);    break;   // BGE  (S^V=0)
        case 0x07: take = (m_bS != m_bOV);    break;   // BLT  (S^V=1)
        case 0x08: take = !m_bZ;              break;   // BNE
        case 0x09: take =  m_bZ;              break;   // BEQ
        case 0x0A: take = !m_bS;              break;   // BPL
        case 0x0B: take =  m_bS;              break;   // BMI
        case 0x0C: take = !m_bCY;             break;   // BCC
        case 0x0D: take =  m_bCY;             break;   // BCS
        case 0x0E: take = !m_bOV;             break;   // BVC
        case 0x0F: take =  m_bOV;             break;   // BVS
        }
        if (take) { m_BranchTarget = (Uint16)(m_R[15] + disp); m_BranchPending = TRUE; }
    }
    else if (op == 0x3C)                      // LOOP (delay slot)
    {
        m_R[12] = (Uint16)(m_R[12] - 1);
        SetZSfromWord(m_R[12]);
        if (m_R[12] != 0) { m_BranchTarget = m_R[13]; m_BranchPending = TRUE; }
    }
    else if (op >= 0x30 && op <= 0x3B)        // STW (Rn) / STB (Rn) [ALT1]
    {
        Uint16 addr = m_R[n];
        if (m_bAlt1) RamWriteByte(RamLinear(addr), (Uint8)(m_R[m_Sreg] & 0xFF)); // STB
        else         RamWriteWord(addr, m_R[m_Sreg]);                           // STW
        m_LastRamAddr = addr;
    }
    else if (op >= 0x40 && op <= 0x4B)        // LDW (Rn) / LDB (Rn) [ALT1]
    {
        Uint16 addr = m_R[n];
        if (m_bAlt1) m_R[m_Dreg] = (Uint16)RamReadByte(RamLinear(addr)); // LDB (zero-ext)
        else         m_R[m_Dreg] = RamReadWord(addr);                    // LDW
        m_LastRamAddr = addr;
    }
    else if (op == 0x90)                      // SBK (escreve no ultimo end. RAM)
    {
        RamWriteWord(m_LastRamAddr, m_R[m_Sreg]);
    }
    else if (op >= 0x91 && op <= 0x94)        // LINK #n
    {
        m_R[11] = (Uint16)(m_R[15] + (op & 0x0F));
    }
    else if (op >= 0x98 && op <= 0x9D)        // JMP Rn / LJMP Rn (delay slot)
    {
        if (m_bAlt1) {                         // LJMP: R15=Rsreg, PBR=Rn
            m_BranchTarget = m_R[m_Sreg];
            m_BranchPBR    = (Uint8)(m_R[n] & 0x7F);
            m_BranchSetPBR = TRUE;
            m_CBR = (Uint16)(m_BranchTarget & 0xFFF0);
        } else {                               // JMP: R15=Rn
            m_BranchTarget = m_R[n];
        }
        m_BranchPending = TRUE;
    }
    else if (op == 0xDF)                       // GETC / RAMB / ROMB
    {
        if (m_bAlt1 && m_bAlt2)  m_ROMBR = (Uint8)(m_R[m_Sreg] & 0xFF);  // ROMB (3F DF)
        else if (m_bAlt2)        m_RAMBR = (Uint8)(m_R[m_Sreg] & 0x01);  // RAMB (3E DF)
        else                     ColorWrite(RomReadByte(m_ROMBR, m_R[14])); // GETC
    }
    else if (op == 0x02)                     // CACHE
    {
        m_CBR = m_R[15] & 0xFFF0;
    }
    else if (op == 0x00)                     // STOP
    {
        GSU_LOG("[gsu] STOP steps=%u plots=%u r15=%04X scmr=%02X scbr=%02X",
                (unsigned)m_Runaway, (unsigned)m_PlotCount,
                (unsigned)m_R[15], (unsigned)m_SCMR, (unsigned)m_SCBR);
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

    // delay slot: aplica o salto pendente DEPOIS de executar a instrucao
    // seguinte ao branch/JMP/LOOP (1 delay slot, como no hardware).
    if (doBranch)
    {
        if (m_BranchSetPBR) m_PBR = m_BranchPBR;
        m_R[15]        = m_BranchTarget;
        m_BranchPending = FALSE;
        m_BranchSetPBR  = FALSE;
    }
}
