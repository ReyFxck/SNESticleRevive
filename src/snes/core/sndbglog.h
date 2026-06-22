/*
 * sndbglog.h - Instrumentacao TEMPORARIA de diagnostico de TIMING.
 *
 *  Objetivo: caçar a "travadinha" do SMK (engasgo a 100% de velocidade).
 *  Mede, por frame: tempo de emulacao (ciclos da EE via COP0 Count) e a
 *  scanline em que o H-IRQ da tela dividida dispara (jitter da divisao).
 *  Resume a cada SNDBG_FRAME_PERIOD frames numa unica linha (sem floodar
 *  o SIO, o que falsearia a medida).
 *
 *  Saida via DLog() (EE SIO) -> logs.txt do NetherSX2, prefixo [snes-tmg].
 *
 *  >>> definir SNDBG_LOG 0 (ou remover) antes de release. <<<
 */
#ifndef _SNDBGLOG_H
#define _SNDBGLOG_H

#include "types.h"

#define SNDBG_LOG 1

// resume a cada N frames (60 = ~1 s)
#define SNDBG_FRAME_PERIOD 60

#ifdef __cplusplus
extern "C" {
#endif
void DLog(const char *fmt, ...);   // definido em src/modules/sjpcm/sjpcm_rpc.c
#ifdef __cplusplus
}
#endif

#if SNDBG_LOG
// acumuladores de ciclos por frame das secoes quentes do render.
// Definidos em snes.cpp, alimentados em snppurender8.cpp (RenderLine8).
extern Uint32 g_TmgCycM7;    // ciclos no _FetchMode7 (Mode-7)
extern Uint32 g_TmgCycObj;   // ciclos em FetchOBJ + RenderOBJ8 (sprites)
#endif

#endif // _SNDBGLOG_H
