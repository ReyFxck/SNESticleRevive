/*
 * sndbglog.h - Instrumentacao TEMPORARIA de diagnostico do caminho
 *              DSP-1 -> HDMA -> Mode-7 (registradores M7A-M7D).
 *
 *  Objetivo: descobrir POR QUE a pista do Super Mario Kart aparece
 *  achatada (matriz Mode-7 constante por scanline).
 *
 *  Saida via ConDebug (=printf), capturada no logs.txt do NetherSX2.
 *
 *  Throttle: imprime apenas 1 "snapshot" a cada SNDBG_FRAME_PERIOD
 *  frames (~5s @60fps) e limita o volume do dump do DSP por frame,
 *  para nao explodir o log.
 *
 *  >>> REMOVER (ou definir SNDBG_LOG 0) antes de release. <<<
 */
#ifndef _SNDBGLOG_H
#define _SNDBGLOG_H

#include "types.h"
#include "console.h"

// liga/desliga toda a instrumentacao de uma vez
#define SNDBG_LOG 1

// 1 snapshot a cada N frames (300 = ~5 segundos)
#define SNDBG_FRAME_PERIOD 300
// quantos acessos ao DSP logar por frame de snapshot (cap)
#define SNDBG_DSP_MAXLOG   80

#if SNDBG_LOG

// definidos em snes.cpp
extern int g_SnesDbgFrame;   // frame atual
extern int g_SnesDbgLine;    // scanline atual
extern int g_SnesDbgDspN;    // contador de acessos DSP no frame (reset por frame)

// true no frame de snapshot
static inline int SnesDbgWin(void)
{
	return (g_SnesDbgFrame >= 0) && ((g_SnesDbgFrame % SNDBG_FRAME_PERIOD) == 0);
}

#define SNDBG(...)        do { if (SnesDbgWin()) ConDebug(__VA_ARGS__); } while (0)

#else

static inline int SnesDbgWin(void) { return 0; }
#define SNDBG(...)        do {} while (0)

#endif // SNDBG_LOG

#endif // _SNDBGLOG_H
