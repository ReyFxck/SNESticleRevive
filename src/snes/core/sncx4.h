/*
 * sncx4.h - CX4 (Capcom/Hitachi HG51B169) coprocessor HLE
 *
 * O CX4 e' um coprocessador matematico programavel da Capcom, usado em
 * Mega Man X2 e X3 (e Rockman X2/X3). Ele faz:
 *   - transformacoes 3D / wireframe (intros, chefes, "X-buster" charge)
 *   - escala e rotacao de sprites (efeitos de boss/estagio)
 *   - varias funcoes trigonometricas (sin/cos/atan/pitagoras)
 *   - montagem de tabela de OAM
 *
 * Sem o chip os jogos travam nas cenas de wireframe (logo, intros,
 * apresentacao de chefe) e mostram graficos escalados/girados corrompidos.
 *
 * Implementacao HLE clean-room a partir do comportamento documentado do
 * chip (engenharia reversa publica, ex.: sneslab / anomie / Overload):
 *   - https://problemkaputt.de/fullsnes.htm#snescartcx4hitachihg51b
 *   - https://www.sneslab.net/wiki/CX4
 *
 * Diferente dos DSPs/OBC1, o CX4 LE dados direto da ROM (vertices de
 * wireframe, tabelas de sprite e blocos de DMA). Para isso recebe um
 * callback de leitura de memoria SNES de 24 bits (resolvido pela CPU via
 * SNCPUPeek8 no emulador; stub na bancada de testes host).
 *
 * As tabelas numericas (seno, multiplicador, arco-tangente) sao constantes
 * factuais derivadas da matematica do chip; a tabela de cosseno e' derivada
 * da de seno (cos(x) = sin(x + 90 graus)).
 */
#ifndef _SNCX4_H
#define _SNCX4_H

#include "types.h"

// callback: le um byte do espaco de enderecamento SNES de 24 bits (ROM/RAM)
typedef Uint8 (*CX4ReadMemFn)(void *pCtx, Uint32 uAddr24);

class SNCX4
{
public:
    SNCX4();

    void  Reset();
    void  SetMemReader(CX4ReadMemFn fn, void *pCtx) { m_pReadMem = fn; m_pReadCtx = pCtx; }

    // uAddr = 16 bits baixos do acesso da CPU ($6000-$7FFF)
    Uint8 Read (Uint32 uAddr);
    void  Write(Uint32 uAddr, Uint8 uData);

#ifdef SNCX4_TESTHOOK
    // acesso direto a C4RAM apenas para a bancada de testes host
    Uint8 *TestRam() { return m_Ram; }
#endif

private:
    // RAM interna (C4RAM), visivel em $6000-$7FFF (0x2000 bytes). Reserva-se
    // uma folga extra de scratch para que escalas/rotacoes de sprites grandes
    // nao estourem o buffer no PS2 (o snes9x usa a folga do FillRAM, 0x8000).
    enum { CX4_VISIBLE = 0x2000, CX4_RAMSIZE = 0x8000 };
    Uint8 m_Ram[CX4_RAMSIZE];

    CX4ReadMemFn m_pReadMem;
    void        *m_pReadCtx;

    // estado das transformacoes de wireframe
    Int16 C4WFXVal, C4WFYVal, C4WFZVal, C4WFX2Val, C4WFY2Val, C4WFDist, C4WFScale;
    Int16 C41FXVal, C41FYVal, C41FAngleRes, C41FDist, C41FDistVal;
    Int32 m_tanval;

    // ---- acesso a memoria SNES (ROM) ----
    Uint8 RdMem(Uint32 a) { return m_pReadMem ? m_pReadMem(m_pReadCtx, a & 0xFFFFFF) : 0; }

    // ---- acesso little-endian a C4RAM ----
    Uint16 RdW(Int32 off) const { return (Uint16)(m_Ram[off] | (m_Ram[off + 1] << 8)); }
    void   WrW(Int32 off, Uint16 v) { m_Ram[off] = (Uint8)v; m_Ram[off + 1] = (Uint8)(v >> 8); }
    Uint32 Rd3(Int32 off) const { return (Uint32)(m_Ram[off] | (m_Ram[off + 1] << 8) | (m_Ram[off + 2] << 16)); }
    void   Wr3(Int32 off, Uint32 v) { m_Ram[off] = (Uint8)v; m_Ram[off + 1] = (Uint8)(v >> 8); m_Ram[off + 2] = (Uint8)(v >> 16); }

    // ---- nucleo matematico ----
    Int16 Sin(Int16 Angle);
    Int16 Cos(Int16 Angle);
    Int16 Atan2(Int16 x, Int16 y);
    static Int32 ISqrt(Int32 r);

    void TransfWireFrame();
    void TransfWireFrame2();
    void CalcWireFrame();

    // ---- operacoes ----
    void ProcessSprites();
    void ConvOAM();
    void DoScaleRotate(Int32 row_padding);
    void DrawLine(Int32 X1, Int32 Y1, Int16 Z1, Int32 X2, Int32 Y2, Int16 Z2, Uint8 Color);
    void DrawWireFrame();
    void TransformLines();
    void BitPlaneWave();
    void SprDisintegrate();

    void Command(Uint8 uByte);
    void DmaTransfer();
};

#endif
