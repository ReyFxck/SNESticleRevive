/*
 * sngsu.h - SuperFX / GSU coprocessor (Graphic Support Unit)
 *
 * RISC-like 16-bit CPU usado por Star Fox, Yoshi's Island, Stunt Race FX,
 * Doom, etc.  Implementacao clean-room a partir da documentacao publica
 * (nocash fullsnes, sneslab, nesdev) -- nenhum codigo de emulador foi
 * copiado.  Projeto sob GPLv2 (veja LICENSE).
 *
 * ETAPA 1 (este arquivo): estado + registradores + SFR + mapa de memoria +
 * MMIO ($3000-$34FF) + controle GO/STOP + loop fetch/execute com um
 * subconjunto minimo de opcodes (STOP, NOP, CACHE, prefixos TO/FROM/WITH/
 * ALT, IWT, ADD/ADC, SUB/SBC).  O set completo e os graficos (PLOT/pixel
 * cache) vem nas etapas seguintes.
 *
 * Referencia de registradores (fullsnes):
 *   $3000-$301F  R0-R15 (16-bit; R15=PC; escrever $301F dispara GO)
 *   $3030/$3031  SFR (Z,CY,S,OV,GO,R, ALT1,ALT2,IL,IH,B, IRQ)
 *   $3034 PBR    Program Bank   $3036 ROMBR (R)   $303C RAMBR (R)
 *   $3037 CFGR   $3038 SCBR     $3039 CLSR        $303A SCMR
 *   $303B VCR    (versao: 01h=MC1, 04h=GSU2)      $303E/$303F CBR (cache base)
 *   $3100-$32FF  cache RAM (512 bytes)
 */
#ifndef _SNGSU_H
#define _SNGSU_H

#include "types.h"

class SNGSU
{
public:
    SNGSU();

    // Conecta os buffers de Game Pak ROM/RAM (propriedade do SnesSystem).
    void  SetMemory(Uint8 *pRom, Uint32 uRomSize, Uint8 *pRam, Uint32 uRamSize);

    void  Reset();

    // --- Acesso do lado SNES aos registradores/cache ($3000-$34FF) ---
    // uOffset = endereco & 0x3FFF (ja relativo a $3000? nao: passamos o
    // endereco baixo 0x3000-0x34FF e tratamos os espelhos aqui).
    Uint8 ReadReg (Uint16 uAddrLow);          // uAddrLow = endereco & 0xFFFF
    void  WriteReg(Uint16 uAddrLow, Uint8 uData);

    // --- Acesso do lado SNES a Game Pak ROM/RAM (arbitragem via SCMR) ---
    // Quando o SNES tem o barramento (RON/RAN=0) ele le direto; durante a
    // execucao do GSU esses acessos devolvem open-bus aproximado.
    Bool  SnesCanAccessRom() const;           // RON bit do SCMR
    Bool  SnesCanAccessRam() const;           // RAN bit do SCMR

    // Executa o GSU por ~nClocks ciclos enquanto GO=1.
    void  Run(Int32 nClocks);

    Bool  IsRunning() const { return m_bGo; }
    // IRQ pendente para o SNES (set on STOP, a menos que mascarado em CFGR).
    Bool  IrqPending() const { return m_bIrq; }

    // --- helpers expostos para o harness de teste host-side ---
    Uint16 GetReg(Int32 i) const { return m_R[i & 15]; }
    void   SetReg(Int32 i, Uint16 v) { m_R[i & 15] = v; }

private:
    // ---- estado de CPU ----
    Uint16 m_R[16];          // R0-R15 (R15 = PC)
    Uint8  m_RegLatch;       // latch das escritas MMIO em endereco par

    // flags do SFR
    Bool   m_bZ, m_bCY, m_bS, m_bOV;   // bits 1-4
    Bool   m_bGo;                      // bit 5 (rodando)
    Bool   m_bRomRead;                 // bit 6 (lendo ROM via R14)
    Bool   m_bAlt1, m_bAlt2;           // bits 8-9 (prefixos)
    Bool   m_bIL, m_bIH;               // bits 10-11 (internos)
    Bool   m_bB;                       // bit 12 (prefixo WITH)
    Bool   m_bIrq;                     // bit 15

    // prefixo source/dest (resetam para R0 apos op nao-prefixo)
    Uint8  m_Sreg, m_Dreg;

    // bancos / config
    Uint8  m_PBR;            // program bank
    Uint8  m_ROMBR;          // rom data bank
    Uint8  m_RAMBR;          // ram data bank (0 -> $70, 1 -> $71)
    Uint8  m_CFGR;           // config (IRQ mask bit7, multiplier speed bit5)
    Uint8  m_SCBR;           // screen base
    Uint8  m_CLSR;           // clock select
    Uint8  m_SCMR;           // screen mode (RON bit4, RAN bit3, height, md)
    Uint8  m_VCR;            // version code register (read-only)
    Uint16 m_CBR;            // cache base register

    // buffers de prefetch/IO
    Uint8  m_RomBuffer;      // byte pre-lido de ROM[ROMBR:R14]
    Bool   m_RomBufValid;

    // watchdog: se o programa rodar demais sem STOP (ex.: opcodes ainda
    // incompletos durante o desenvolvimento), forca a parada e devolve o
    // controle ao SNES, evitando travar a EE.
    Uint32 m_Runaway;
    Uint32 m_PlotCount;      // diag: pixels plotados na rotina atual (por GO)

    // delay-slot dos saltos: o GSU executa SEMPRE a instrucao seguinte ao
    // branch/JMP/LOOP antes do salto tomar efeito (1 delay slot).
    Bool   m_BranchPending;
    Uint16 m_BranchTarget;
    Bool   m_BranchSetPBR;
    Uint8  m_BranchPBR;

    // ultimo endereco de RAM acessado (para SBK)
    Uint16 m_LastRamAddr;

    // --- graficos (PLOT / pixel cache) ---
    Uint8  m_Color;            // registrador COLOR
    Uint8  m_POR;              // Plot Option Register (via CMODE): bit0 transp,
                               // bit1 dither, bit2 high-nibble, bit3 freeze-high,
                               // bit4 obj-mode
    Uint8  m_PixColor[8];      // cache primario: cor de cada um dos 8 pixels
    Uint8  m_PixFlags;         // 1 bit por pixel plotado (nao-transparente)
    Uint8  m_PixXBase;         // X & 0xF8 do bloco em cache
    Uint8  m_PixY;             // Y do bloco em cache
    Bool   m_PixValid;

    // cache de codigo (512 bytes) em $3100-$32FF
    Uint8  m_Cache[512];

    // memoria do cartucho (nao e' nossa)
    Uint8 *m_pRom;  Uint32 m_uRomSize;  Uint32 m_uRomMask;
    Uint8 *m_pRam;  Uint32 m_uRamSize;  Uint32 m_uRamMask;

    // ---- helpers internos ----
    Uint8  SfrLow()  const;
    Uint8  SfrHigh() const;
    void   SfrWriteLow(Uint8 v);

    Uint32 RomOffset(Uint8 uBank, Uint16 uAddr) const;  // GSU addr -> offset linear
    Uint8  CodeFetch();                                 // le opcode em PBR:R15++
    Uint8  RomReadByte(Uint8 uBank, Uint16 uAddr) const;
    Uint8  RamReadByte(Uint32 uAddr) const;
    void   RamWriteByte(Uint32 uAddr, Uint8 v);
    Uint32 RamLinear(Uint16 uAddr) const;     // RAMBR:addr -> offset linear
    Uint16 RamReadWord(Uint16 uAddr) const;   // com swap em endereco impar
    void   RamWriteWord(Uint16 uAddr, Uint16 v);

    void   ResetPrefix();    // Sreg=Dreg=0, alt1=alt2=b=0 (apos op normal)
    void   SetZSfromWord(Uint16 v);   // atualiza Z e S a partir de um resultado

    // graficos
    Int32  ScreenBpp() const;                 // 2, 4 ou 8 (de SCMR.MD)
    Uint32 PixelTileNo(Uint8 x, Uint8 y) const;
    Uint32 PixelRowAddr(Uint8 x, Uint8 y) const;
    void   PixFlush();                        // descarrega o cache para a RAM
    void   Plot();                            // PLOT: desenha COLOR em (R1,R2)
    Uint16 Rpix();                            // RPIX: flush + le pixel (R1,R2)
    void   ColorWrite(Uint8 src);             // pipeline COLOR/GETC (POR.2/.3)

    void   Step();           // executa uma instrucao
};

#endif
