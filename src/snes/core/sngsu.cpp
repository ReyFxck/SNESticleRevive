/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements sngsu behavior for the SNES emulation core.
 */

/*
 * sngsu.cpp - SuperFX / GSU core
 *
 * Clean-room a partir da documentacao publica (fullsnes/sneslab/nesdev).
 * Nenhum codigo de emulador foi copiado.  GPLv2 (veja LICENSE).
 *
 * Implementacao funcional do conjunto de opcodes, MMIO, code-cache,
 * prefetch de ROM e PLOT/RPIX. A temporizacao e' aproximada pelo scheduler
 * por scanline; detalhes de wait-state continuam isolados para evolucao.
 */

#include "types.h"
#include "sngsu.h"

#include <string.h>

extern "C" void DLog(const char *fmt, ...);

SNGSU::SNGSU()
{
    m_pRom = NULL; m_uRomSize = 0; m_uRomMask = 0;
    m_pRam = NULL; m_uRamSize = 0; m_uRamMask = 0;
    m_ConfigVCR = 0x04;
    Reset();
}

void SNGSU::SetMemory(Uint8 *pRom, Uint32 uRomSize, Uint8 *pRam, Uint32 uRamSize)
{
    m_pRom = pRom; m_uRomSize = uRomSize; m_uRomMask = uRomSize ? uRomSize - 1 : 0;
    m_pRam = pRam; m_uRamSize = uRamSize; m_uRamMask = uRamSize ? uRamSize - 1 : 0;
    UpdateRomBuffer();
}

void SNGSU::SetVersion(Uint8 uVersion)
{
    m_ConfigVCR = (uVersion == 0x01) ? 0x01 : 0x04;
    m_VCR = m_ConfigVCR;
}

void SNGSU::Reset()
{
    memset(m_R, 0, sizeof(m_R));
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
    m_VCR = m_ConfigVCR;
    m_CBR = 0;
    m_RomBuffer = 0; m_RomBufValid = FALSE;
    m_Runaway = 0;
    m_WatchdogReported = FALSE;
#if SNDBG_LOG
    memset(&m_Diag, 0, sizeof(m_Diag));
#endif
    m_Pipeline = 0x01;                 // o primeiro byte executado e' um NOP
    m_PCModified = FALSE;
    m_LastRamAddr = 0;
    m_Color = 0; m_POR = 0;
    memset(m_PixColor, 0, sizeof(m_PixColor));
    m_PixFlags[0] = m_PixFlags[1] = 0;
    m_PixOffset[0] = m_PixOffset[1] = 0xFFFF;
    memset(m_Cache, 0, sizeof(m_Cache));
    m_CacheValid = 0;
}

//  SFR (Status/Flag Register)
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

//  Memoria do cartucho (visao do GSU)
Uint32 SNGSU::RomOffset(Uint8 uBank, Uint16 uAddr) const
{
    if (uBank < 0x40)
        return ((Uint32)uBank << 15) | (uAddr & 0x7FFF);   // LoROM ($8000-$FFFF)
    // $40-$5F: HiROM contiguo (espelho do mesmo ROM)
    return ((Uint32)(uBank & 0x3F) << 16) | uAddr;
}

Uint8 SNGSU::RomReadByte(Uint8 uBank, Uint16 uAddr) const
{
    // Na visao do GSU, $60-$7F seleciona a Game Pak RAM inclusive para
    // opcodes (PBR) e para o prefetch de dados (ROMBR). O tamanho fisico da
    // RAM faz esses bancos espelharem naturalmente em RamReadByte().
    if (uBank >= 0x60 && uBank <= 0x7F)
        return RamReadByte(((Uint32)(uBank & 1) << 16) | uAddr);

    if (!m_pRom || !m_uRomSize) return 0xFF;
    Uint32 off = RomOffset(uBank, uAddr);
    // Todos os cartuchos SuperFX comerciais tem ROM com tamanho potencia de
    // dois. Evitar a divisao de 32 bits aqui reduz bastante o custo na EE do
    // PS2; imagens homebrew de tamanho irregular conservam o fallback.
    if ((m_uRomSize & m_uRomMask) == 0)
        return m_pRom[off & m_uRomMask];
    return m_pRom[off % m_uRomSize];
}

Uint8 SNGSU::RawCodeRead(Uint16 uAddr) const
{
    return RomReadByte(m_PBR, uAddr);
}

void SNGSU::UpdateRomBuffer()
{
    if (!m_pRom || !m_uRomSize) { m_RomBuffer = 0xFF; m_RomBufValid = FALSE; return; }
    m_RomBuffer = RomReadByte(m_ROMBR, m_R[14]);
    m_RomBufValid = TRUE;
    m_bRomRead = FALSE; // o modelo funcional completa o prefetch imediatamente
}

void SNGSU::FlushCodeCache()
{
    m_CacheValid = 0;
}

#if SNDBG_LOG
void SNGSU::ClearDiagWindow()
{
    // Um comando pode atravessar a fronteira da janela de diagnostico. Mantem
    // seu tamanho corrente para que o maximo continue significativo.
    Uint32 current = m_Diag.CurrentJobInstructions;
    memset(&m_Diag, 0, sizeof(m_Diag));
    m_Diag.CurrentJobInstructions = current;
    m_Diag.MaxJobInstructions = current;
}
#endif

Uint8 SNGSU::RamReadByte(Uint32 uAddr) const
{
    if (!m_pRam || !m_uRamSize) return 0xFF;
    if ((m_uRamSize & m_uRamMask) == 0)
        return m_pRam[uAddr & m_uRamMask];
    return m_pRam[uAddr % m_uRamSize];
}

void SNGSU::RamWriteByte(Uint32 uAddr, Uint8 v)
{
    if (!m_pRam || !m_uRamSize) return;
#if SNDBG_DEEP
    m_Diag.RamWrites++;
#endif
    if ((m_uRamSize & m_uRamMask) == 0)
        m_pRam[uAddr & m_uRamMask] = v;
    else
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

Uint8 SNGSU::CodeRead(Uint16 pc)
{
    Uint16 cacheOffset = (Uint16)(pc - m_CBR);
    Uint8 b;

    if (cacheOffset < 512)
    {
        Uint32 line = cacheOffset >> 4;
        if (!(m_CacheValid & ((Uint32)1 << line)))
        {
#if SNDBG_DEEP
            m_Diag.CacheMisses++;
#endif
            Uint16 base = (Uint16)(m_CBR + (line << 4));
            Uint32 i;
            for (i = 0; i < 16; i++)
                m_Cache[(line << 4) + i] = RawCodeRead((Uint16)(base + i));
            m_CacheValid |= (Uint32)1 << line;
        }
#if SNDBG_DEEP
        else
        {
            m_Diag.CacheHits++;
        }
#endif
        b = m_Cache[cacheOffset];
    }
    else
    {
        b = RawCodeRead(pc);
    }
    return b;
}

// Retorna o byte ja prebuscado e coloca no pipeline o byte seguinte. O
// incremento feito aqui e' parte do mecanismo interno de prefetch, nao uma
// escrita de R15 pela instrucao em execucao.
Uint8 SNGSU::Pipe()
{
    Uint8 b = m_Pipeline;
    m_R[15]++;
    m_Pipeline = CodeRead(m_R[15]);
    m_PCModified = FALSE;
    return b;
}

//  Arbitragem ROM/RAM (SCMR)
Bool SNGSU::SnesCanAccessRom() const { return (m_SCMR & 0x10) == 0; }  // RON
Bool SNGSU::SnesCanAccessRam() const { return (m_SCMR & 0x08) == 0; }  // RAN

//  MMIO do lado SNES ($3000-$34FF)
Uint8 SNGSU::ReadReg(Uint16 uAddrLow)
{
    Uint16 a = uAddrLow & 0xFFFF;

    // A janela da CPU ($3100-$32FF) soma os nove bits baixos do CBR ao
    // indice fisico. Com CBR=$C3A0, por exemplo, o byte logico zero aparece
    // em $3160: ($060 + $1A0) & $1FF = 0.
    if (a >= 0x3100 && a <= 0x32FF)
    {
        Uint16 off = (Uint16)(((a - 0x3100) + (m_CBR & 0x01FF)) & 0x01FF);
        return m_Cache[off];
    }

    // GSU2 espelha os 64 bytes de registradores nestas janelas.
    if (a >= 0x3040 && a <= 0x30FF) a = (Uint16)(0x3000 | (a & 0x3F));
    else if (a >= 0x3300 && a <= 0x34FF) a = (Uint16)(0x3000 | (a & 0x3F));

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

    if (a >= 0x3100 && a <= 0x32FF)
    {
        Uint32 off = ((a - 0x3100) + (m_CBR & 0x01FF)) & 0x01FF;
        m_Cache[off] = uData;
        if ((off & 15) == 15) m_CacheValid |= (Uint32)1 << (off >> 4);
        return;
    }

    if (a >= 0x3040 && a <= 0x30FF) a = (Uint16)(0x3000 | (a & 0x3F));
    else if (a >= 0x3300 && a <= 0x34FF) a = (Uint16)(0x3000 | (a & 0x3F));

    // R0-R15 sao byte-addressable. Preservar o outro byte do proprio
    // registrador tambem cobre escritas isoladas ou intercaladas; um latch
    // global misturava o byte baixo de registradores diferentes.
    if (a >= 0x3000 && a <= 0x301F)
    {
        Int32 idx = (a - 0x3000) >> 1;
        if ((a & 1) == 0)
            m_R[idx] = (Uint16)((m_R[idx] & 0xFF00) | uData);
        else
            m_R[idx] = (Uint16)(((Uint16)uData << 8) | (m_R[idx] & 0x00FF));
        if (idx == 14) UpdateRomBuffer();
        if (a == 0x301F)        // escrita em R15.MSB dispara GO
        {
            m_bGo = TRUE;
            m_Runaway = 0;      // reinicia o watchdog a cada novo START
#if SNDBG_LOG
            m_Diag.Starts++;
            m_Diag.CurrentJobInstructions = 0;
#endif
        }
        return;
    }

    switch (a)
    {
    case 0x3030:
        {
            Bool wasGo = m_bGo;
            SfrWriteLow(uData);
            // Limpa CBR/cache somente na transicao explicita GO=1 -> GO=0.
            // Uma escrita de flags enquanto ja parado nao pode destruir o
            // codigo que a CPU acabou de carregar na janela de cache.
            if (wasGo && !m_bGo) { m_CBR = 0; FlushCodeCache(); }
            if (!wasGo && m_bGo)
            {
                m_Runaway = 0;
#if SNDBG_LOG
                m_Diag.Starts++;
                m_Diag.CurrentJobInstructions = 0;
#endif
            }
#if SNDBG_LOG
            if (wasGo && !m_bGo) m_Diag.Aborts++;
#endif
        }
        break;
    case 0x3031:
        m_bAlt1 = (uData & 0x01) != 0;
        m_bAlt2 = (uData & 0x02) != 0;
        m_bIL   = (uData & 0x04) != 0;
        m_bIH   = (uData & 0x08) != 0;
        m_bB    = (uData & 0x10) != 0;
        m_bIrq  = (uData & 0x80) != 0;
        break;
    case 0x3033: /* BRAMR (backup ram enable) - ignorado por enquanto */ break;
    case 0x3034: m_PBR  = uData & 0x7F; FlushCodeCache(); break;
    case 0x3037: m_CFGR = uData; break;
    case 0x3038: m_SCBR = uData; break;
    case 0x3039: m_CLSR = uData & 1; break;
    case 0x303A: m_SCMR = uData; break;
    default: break;
    }
}

//  Execucao
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

void SNGSU::WriteRegister(Uint8 n, Uint16 val)
{
    n &= 15;
    if (n == 15)
    {
        // O byte seguinte ja esta no pipeline e sera executado no proximo
        // Step; somente o endereco da proxima prebusca muda agora.
        m_R[15] = val;
        m_PCModified = TRUE;
        return;
    }

    m_R[n] = val;
    if (n == 14) UpdateRomBuffer();
}

//  Graficos (PLOT / pixel cache)
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
    Uint32 ht = (m_POR & 0x10)
              ? 3
              : ((((m_SCMR >> 5) & 1) << 1) | ((m_SCMR >> 2) & 1));
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

// Descarrega um dos dois caches de pixels para a RAM (bitplanes do SNES).
void SNGSU::PixFlush(Int32 nCache)
{
    nCache &= 1;
    Uint8 flags = m_PixFlags[nCache];
    if (flags == 0) return;

    Uint16 offset = m_PixOffset[nCache];
    Uint8 xbase = (Uint8)(offset << 3);
    Uint8 y = (Uint8)(offset >> 5);
    Int32  bpp = ScreenBpp();
    Uint32 rowAddr = PixelRowAddr(xbase, y);
    for (Int32 b = 0; b < bpp; b++) {
        // plano b: par (b>>1) a offset (b>>1)*16, byte (b&1) dentro do par
        Uint32 addr = rowAddr + (Uint32)((b >> 1) * 16 + (b & 1));
        Uint8  byte = (flags == 0xFF) ? 0 : RamReadByte(addr);
        for (Int32 i = 0; i < 8; i++) {
            if (flags & (1 << i)) {
                Uint8 mask = (Uint8)(1 << (7 - i));        // pixel 0 = bit7
                if ((m_PixColor[nCache][i] >> b) & 1) byte |= mask;
                else                          byte &= (Uint8)~mask;
            }
        }
        RamWriteByte(addr, byte);
    }
    m_PixFlags[nCache] = 0;
}

void SNGSU::PixMovePrimaryToSecondary()
{
    // O hardware espera o secundario terminar antes de reutiliza-lo.
    PixFlush(1);
    memcpy(m_PixColor[1], m_PixColor[0], sizeof(m_PixColor[0]));
    m_PixFlags[1] = m_PixFlags[0];
    m_PixOffset[1] = m_PixOffset[0];
    m_PixFlags[0] = 0;
}

void SNGSU::PixFlushAll()
{
    // O secundario e' sempre o bloco mais antigo.
    PixFlush(1);
    PixFlush(0);
}

void SNGSU::Plot()
{
#if SNDBG_DEEP
    m_Diag.Plots++;
#endif
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

    Uint16 offset = (Uint16)(((Uint16)y << 5) + (x >> 3));
    if (offset != m_PixOffset[0])
    {
        PixMovePrimaryToSecondary();
        m_PixOffset[0] = offset;
    }
    m_PixColor[0][x & 7] = color;
    m_PixFlags[0] |= (Uint8)(1 << (x & 7));
    m_R[1]++;

    if (m_PixFlags[0] == 0xFF)
        PixMovePrimaryToSecondary();
}

Uint16 SNGSU::Rpix()
{
#if SNDBG_DEEP
    m_Diag.Rpix++;
#endif
    PixFlushAll();                    // RPIX espera ambos antes de ler a RAM
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

#if defined(__GNUC__)
#define SNGSU_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define SNGSU_ALWAYS_INLINE inline
#endif

SNGSU_ALWAYS_INLINE void SNGSU::Step()
{
#if SNDBG_DEEP
    m_Diag.Instructions++;
    m_Diag.CurrentJobInstructions++;
    if (m_Diag.CurrentJobInstructions > m_Diag.MaxJobInstructions)
        m_Diag.MaxJobInstructions = m_Diag.CurrentJobInstructions;
#endif
    // watchdog: rede de seguranca contra um programa que nunca alcance STOP
    // (bug nosso ou ROM corrompida).  Apos um teto de instrucoes, forca a
    // parada (+IRQ) para nao travar a EE.  Rotinas reais do Star Fox terminam
    // bem antes disto.
    if (++m_Runaway > 2000000)
    {
        // Desenvolvimento: uma captura curta e unica permite identificar o
        // laço real caso algum jogo ainda alcance esta rede de seguranca.
        // Em execucao correta nada e' impresso e nao existe custo de SIO.
        if (!m_WatchdogReported)
        {
            m_WatchdogReported = TRUE;
#if SNDBG_DEEP
            SnesDbgRequestCapture(SNDBG_CAPTURE_CHIP);
#endif
            DLog("[gsu-watchdog] PBR=%02X PC=%04X CBR=%04X PIPE=%02X NEXT=%02X SFR=%02X%02X",
                 (unsigned)m_PBR, (unsigned)m_R[15], (unsigned)m_CBR,
                 (unsigned)m_Pipeline, (unsigned)RawCodeRead(m_R[15]),
                 (unsigned)SfrHigh(), (unsigned)SfrLow());
            DLog("[gsu-watchdog] R0=%04X R1=%04X R2=%04X R3=%04X R4=%04X R5=%04X R6=%04X R7=%04X",
                 (unsigned)m_R[0], (unsigned)m_R[1], (unsigned)m_R[2], (unsigned)m_R[3],
                 (unsigned)m_R[4], (unsigned)m_R[5], (unsigned)m_R[6], (unsigned)m_R[7]);
            DLog("[gsu-watchdog] R8=%04X R9=%04X R10=%04X R11=%04X R12=%04X R13=%04X R14=%04X R15=%04X",
                 (unsigned)m_R[8], (unsigned)m_R[9], (unsigned)m_R[10], (unsigned)m_R[11],
                 (unsigned)m_R[12], (unsigned)m_R[13], (unsigned)m_R[14], (unsigned)m_R[15]);
        }
#if SNDBG_LOG
        m_Diag.Watchdogs++;
#endif
        m_bGo = FALSE; m_bIrq = TRUE; m_Runaway = 0;
        return;
    }

    // Pipeline de um byte do GSU: executa o byte que ja estava prebuscado e
    // busca o byte apontado por R15. Uma escrita posterior em R15 conserva
    // esse byte como delay slot e muda somente a proxima origem de fetch.
    Uint8 op = m_Pipeline;
    m_Pipeline = CodeRead(m_R[15]);
    m_PCModified = FALSE;

    Bool  bIsPrefix = FALSE;

    Uint8  n   = op & 0x0F;
    Uint16 sr  = m_R[m_Sreg];               // valor source

    if (op >= 0x10 && op <= 0x1F)            // TO Rn / MOVE
    {
        if (!m_bB) { m_Dreg = n; bIsPrefix = TRUE; }
        else       { WriteRegister(n, m_R[m_Sreg]); } // MOVE (B): Rn = Rsreg
    }
    else if (op >= 0x20 && op <= 0x2F)       // WITH Rn (Sreg=Dreg=n, B=1)
    {
        m_Sreg = n; m_Dreg = n; m_bB = TRUE;
        bIsPrefix = TRUE;
    }
    else if (op >= 0xB0 && op <= 0xBF)       // FROM Rn / MOVES
    {
        if (!m_bB) { m_Sreg = n; bIsPrefix = TRUE; }
        else
        {
            Uint16 res = m_R[n];
            WriteRegister(m_Dreg, res);
            m_bOV = (res & 0x0080) != 0;
            SetZSfromWord(res);
        } // MOVES
    }
    else if (op == 0x3D) { m_bB = FALSE; m_bAlt1 = TRUE; bIsPrefix = TRUE; }   // ALT1
    else if (op == 0x3E) { m_bB = FALSE; m_bAlt2 = TRUE; bIsPrefix = TRUE; }   // ALT2
    else if (op == 0x3F) { m_bB = FALSE; m_bAlt1 = TRUE; m_bAlt2 = TRUE; bIsPrefix = TRUE; } // ALT3
    else if (op >= 0x50 && op <= 0x5F)       // ADD / ADC / ADD#imm / ADC#imm
    {
        Uint32 a = sr;
        Uint32 b = m_bAlt2 ? (Uint32)n : (Uint32)m_R[n];
        Uint32 cin = m_bAlt1 ? (m_bCY ? 1u : 0u) : 0u;   // ALT1 => ADC
        Uint32 r = a + b + cin;
        m_bCY = (r > 0xFFFF);
        m_bOV = ((~(a ^ b)) & (a ^ r) & 0x8000) != 0;
        Uint16 res = (Uint16)r; SetZSfromWord(res); WriteRegister(m_Dreg, res);
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
        if (!bCmp) WriteRegister(m_Dreg, res);
    }
    else if (op == 0x70)                     // MERGE
    {
        Uint16 res = (Uint16)((m_R[7] & 0xFF00) | ((m_R[8] >> 8) & 0x00FF));
        m_bOV = (res & 0xC0C0) != 0;
        m_bS  = (res & 0x8080) != 0;
        m_bCY = (res & 0xE0E0) != 0;
        m_bZ  = (res & 0xF0F0) != 0;
        WriteRegister(m_Dreg, res);
    }
    else if (op >= 0x71 && op <= 0x7F)       // AND / BIC / AND#imm / BIC#imm
    {
        Uint16 b = m_bAlt2 ? (Uint16)n : m_R[n];
        if (m_bAlt1) b = (Uint16)~b;          // ALT1 => BIC (AND NOT)
        Uint16 res = (Uint16)(sr & b);
        SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0xC0)                     // HIB (high byte -> low)
    {
        Uint16 res = (Uint16)(sr >> 8);
        m_bZ = (res == 0); m_bS = (res & 0x80) != 0;
        WriteRegister(m_Dreg, res);
    }
    else if (op >= 0xC1 && op <= 0xCF)       // OR / XOR / OR#imm / XOR#imm
    {
        Uint16 b = m_bAlt2 ? (Uint16)n : m_R[n];
        Uint16 res = m_bAlt1 ? (Uint16)(sr ^ b) : (Uint16)(sr | b);
        SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0x4F)                     // NOT
    {
        Uint16 res = (Uint16)~sr; SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0x4C)                     // PLOT / RPIX (ALT1)
    {
        if (m_bAlt1) { Uint16 px = Rpix(); SetZSfromWord(px); WriteRegister(m_Dreg, px); }
        else         { Plot(); }
    }
    else if (op == 0x4E)                     // COLOR / CMODE (ALT1)
    {
        if (m_bAlt1) m_POR = (Uint8)(sr & 0x1F);     // CMODE: por = Rs & 1Fh
        else         ColorWrite((Uint8)(sr & 0xFF)); // COLOR: color = Rs
    }
    else if (op == 0xEF)                      // GETB / GETBH / GETBL / GETBS
    {
        Uint8 byte;
        if (!m_RomBufValid) UpdateRomBuffer();
        byte = m_RomBuffer;
        if (m_bAlt1 && m_bAlt2)               // GETBS (3F): sign-expand
            WriteRegister(m_Dreg, (Uint16)(Int16)(Int8)byte);
        else if (m_bAlt1)                     // GETBH (3D): hi=byte, lo unchanged
            WriteRegister(m_Dreg, (Uint16)((sr & 0x00FF) | (byte << 8)));
        else if (m_bAlt2)                     // GETBL (3E): lo=byte, hi unchanged
            WriteRegister(m_Dreg, (Uint16)((sr & 0xFF00) | byte));
        else                                  // GETB: zero-expand
            WriteRegister(m_Dreg, (Uint16)byte);
    }
    else if (op == 0x03)                     // LSR
    {
        m_bCY = (sr & 1) != 0;
        Uint16 res = (Uint16)(sr >> 1); SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0x04)                     // ROL
    {
        Uint16 res = (Uint16)((sr << 1) | (m_bCY ? 1 : 0));
        m_bCY = (sr & 0x8000) != 0; SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0x96)                     // ASR / DIV2 (ALT1)
    {
        m_bCY = (sr & 1) != 0;
        Uint16 res = (Uint16)(((Int16)sr) >> 1);
        if (m_bAlt1 && sr == 0xFFFF) res = 0;  // DIV2 arredonda p/ zero
        SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0x97)                     // ROR
    {
        Uint16 res = (Uint16)((m_bCY ? 0x8000 : 0) | (sr >> 1));
        m_bCY = (sr & 1) != 0; SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0x4D)                     // SWAP (troca bytes)
    {
        Uint16 res = (Uint16)((sr >> 8) | (sr << 8));
        SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0x95)                     // SEX (sign-extend byte)
    {
        Uint16 res = (Uint16)(Int16)(Int8)(sr & 0xFF);
        SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op == 0x9E)                     // LOB (low byte)
    {
        Uint16 res = (Uint16)(sr & 0x00FF);
        m_bZ = (res == 0); m_bS = (res & 0x80) != 0;
        WriteRegister(m_Dreg, res);
    }
    else if (op == 0x9F)                     // FMULT / LMULT (ALT1)
    {
        // Produto com sinal de Sreg x R6 (32 bits).  FMULT: 16 bits altos ->
        // destino, CY = bit15 do produto.  LMULT (ALT1): alem disso, 16 bits
        // baixos -> R4 antes de escrever o destino (inclusive se Dreg=R4).
        Int32  p  = (Int32)(Int16)sr * (Int32)(Int16)m_R[6];
        Uint32 up = (Uint32)p;
        Uint16 hi = (Uint16)(up >> 16);
        if (m_bAlt1) m_R[4] = (Uint16)(up & 0xFFFF);   // LMULT: low 16 -> R4
        WriteRegister(m_Dreg, hi);
        SetZSfromWord(hi);
        m_bCY = ((up >> 15) & 1) != 0;                 // CY = bit15 do produto
    }
    else if (op >= 0x80 && op <= 0x8F)       // MULT / UMULT / +#imm (low 16)
    {
        Int32 r;
        if (m_bAlt1) {                        // UMULT (sem sinal)
            Uint32 b = m_bAlt2 ? (Uint32)n : (Uint32)(Uint8)m_R[n];
            r = (Int32)((Uint32)(Uint8)sr * b);
        } else {                              // MULT (com sinal)
            Int32 b = m_bAlt2 ? (Int32)n : (Int32)(Int8)m_R[n];
            r = (Int32)(Int8)sr * b;
        }
        Uint16 res = (Uint16)r; SetZSfromWord(res); WriteRegister(m_Dreg, res);
    }
    else if (op >= 0xD0 && op <= 0xDE)       // INC Rn
    {
        Uint16 res = (Uint16)(m_R[n] + 1); SetZSfromWord(res); WriteRegister(n, res);
    }
    else if (op >= 0xE0 && op <= 0xEE)       // DEC Rn
    {
        Uint16 res = (Uint16)(m_R[n] - 1); SetZSfromWord(res); WriteRegister(n, res);
    }
    else if (op >= 0xA0 && op <= 0xAF)       // IBT Rn,#imm8 / LMS / SMS
    {
        if (m_bAlt1) {                        // LMS Rn,(yy): Rn = word[ramb:kk*2]
            Uint16 addr = (Uint16)(Pipe() * 2);
            Uint16 v = RamReadWord(addr); m_LastRamAddr = addr;
            WriteRegister(n, v);
        } else if (m_bAlt2) {                 // SMS (yy),Rn: word[ramb:kk*2] = Rn
            Uint16 addr = (Uint16)(Pipe() * 2);
            RamWriteWord(addr, m_R[n]); m_LastRamAddr = addr;
        } else {                              // IBT Rn,#imm8 (sign-extend)
            Uint8 imm = Pipe();
            WriteRegister(n, (Uint16)(Int16)(Int8)imm);
        }
    }
    else if (op >= 0xF0 && op <= 0xFF)       // IWT Rn,#imm16 / LM / SM
    {
        if (m_bAlt1) {                        // LM Rn,(hilo)
            Uint8 lo = Pipe(), hi = Pipe();
            Uint16 addr = (Uint16)((hi << 8) | lo);
            Uint16 v = RamReadWord(addr); m_LastRamAddr = addr;
            WriteRegister(n, v);
        } else if (m_bAlt2) {                 // SM (hilo),Rn
            Uint8 lo = Pipe(), hi = Pipe();
            Uint16 addr = (Uint16)((hi << 8) | lo);
            RamWriteWord(addr, m_R[n]); m_LastRamAddr = addr;
        } else {                              // IWT Rn,#imm16
            Uint8 lo = Pipe(), hi = Pipe();
            WriteRegister(n, (Uint16)(((Uint16)hi << 8) | lo));
        }
    }
    else if (op >= 0x05 && op <= 0x0F)       // branches (delay slot)
    {
        Int8 disp = (Int8)Pipe();
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
#if SNDBG_DEEP
        m_Diag.Branches++;
        if (take) m_Diag.BranchesTaken++;
#endif
        if (take) { m_R[15] = (Uint16)(m_R[15] + disp); m_PCModified = TRUE; }
        bIsPrefix = TRUE;                    // Bxx nao limpa ALT/TO/FROM/WITH
    }
    else if (op == 0x3C)                      // LOOP (delay slot)
    {
        m_R[12] = (Uint16)(m_R[12] - 1);
        SetZSfromWord(m_R[12]);
        if (m_R[12] != 0) { m_R[15] = m_R[13]; m_PCModified = TRUE; }
#if SNDBG_DEEP
        m_Diag.Branches++;
        if (m_R[12] != 0) m_Diag.BranchesTaken++;
#endif
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
        if (m_bAlt1) WriteRegister(m_Dreg, (Uint16)RamReadByte(RamLinear(addr))); // LDB
        else         WriteRegister(m_Dreg, RamReadWord(addr));                    // LDW
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
#if SNDBG_DEEP
        m_Diag.Jumps++;
#endif
        if (m_bAlt1) {                         // LJMP: R15=Rsreg, PBR=Rn
            m_PBR = (Uint8)(m_R[n] & 0x7F);
            m_R[15] = m_R[m_Sreg];
            m_CBR = (Uint16)(m_R[15] & 0xFFF0);
            FlushCodeCache();
        } else {                               // JMP: R15=Rn
            m_R[15] = m_R[n];
        }
        m_PCModified = TRUE;
    }
    else if (op == 0xDF)                       // GETC / RAMB / ROMB
    {
        if (m_bAlt1 && m_bAlt2)  m_ROMBR = (Uint8)(m_R[m_Sreg] & 0x7F);  // ROMB (3F DF)
        else if (m_bAlt2)        m_RAMBR = (Uint8)(m_R[m_Sreg] & 0x01);  // RAMB (3E DF)
        else
        {
            if (!m_RomBufValid) UpdateRomBuffer();
            ColorWrite(m_RomBuffer);                                    // GETC
        }
    }
    else if (op == 0x02)                     // CACHE
    {
        Uint16 next = (Uint16)(m_R[15] & 0xFFF0);
        if (next != m_CBR) { m_CBR = next; FlushCodeCache(); }
    }
    else if (op == 0x00)                     // STOP
    {
#if SNDBG_LOG
        m_Diag.Stops++;
#endif
        m_bGo = FALSE;
        // IRQ ao SNES so' se NAO mascarado em CFGR.irq (bit7).  Igual hardware
        // /bsnes: instructionSTOP so' levanta irq quando cfgr.irq==0.  Setar
        // incondicionalmente (versao antiga) era espurio e quebrava o boot.
        if (!(m_CFGR & 0x80)) m_bIrq = TRUE;
        m_POR = 0;
        m_Pipeline = 0x01;                    // proxima partida inicia por NOP
        ResetPrefix();
        bIsPrefix = TRUE;
    }
    else if (op == 0x01)                     // NOP
    {
        /* nada */
    }
    else
    {
        // Todos os 256 bytes possuem rota acima. Mantem este fallback como
        // protecao caso a tabela seja alterada no futuro.
    }

    if (!bIsPrefix)
        ResetPrefix();

    // R15 aponta para o byte que acabou de ser colocado no pipeline. Se a
    // instrucao nao escreveu o PC, avanca para o proximo endereco. STOP usa
    // esta mesma regra e por isso termina em $+2, como no silicio.
    if (m_PCModified)
        m_PCModified = FALSE;
    else
        m_R[15]++;

}

void SNGSU::Run(Int32 nClocks)
{
    // Step e' o caminho mais quente dos jogos SuperFX (milhoes de chamadas
    // por minuto). Mantê-lo visivel acima permite ao GCC incorporar o loop,
    // removendo uma chamada C++ por instrucao emulada no EE do PS2.
    while (m_bGo && nClocks-- > 0)
        Step();
}

#undef SNGSU_ALWAYS_INLINE
