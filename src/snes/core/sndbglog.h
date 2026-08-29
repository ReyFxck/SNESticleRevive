/*
 * sndbglog.h - Instrumentacao TEMPORARIA de diagnostico de TIMING.
 *
 *  SNES_DIAGNOSTICS=1 mede os blocos quentes e resume a cada 60 frames.
 *  SNES_DIAGNOSTICS=2 acrescenta hashes/capturas e contadores de hot loops;
 *  esse modo profundo e' intencionalmente mais intrusivo.
 *
 *  Saida via DLog() (EE SIO) -> logs.txt do NetherSX2, prefixo [snes-tmg].
 *
 *  O build normal define SNDBG_LOG=0.
 */
#ifndef _SNDBGLOG_H
#define _SNDBGLOG_H

#include "types.h"

/* The normal build keeps the counters out of every emulated instruction.
   A diagnostic build can opt in with -DSNDBG_LOG=1 (the Makefile exposes a
   friendly switch for that). */
#ifndef SNDBG_LOG
#define SNDBG_LOG 0
#endif

#ifndef SNDBG_DEEP
#define SNDBG_DEEP 0
#endif

#ifndef SNPPU_OBJ_CACHE
#define SNPPU_OBJ_CACHE 1
#endif

#ifndef SNPPU_BG_CACHE
#define SNPPU_BG_CACHE 0
#endif

#if SNDBG_DEEP && !SNDBG_LOG
#undef SNDBG_LOG
#define SNDBG_LOG 1
#endif

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
extern Uint32 g_TmgCycPPU;   // RenderLine completo
extern Uint32 g_TmgCycCPU;   // loop 65816 por scanline (inclusivo)
extern Uint32 g_TmgCycGSU;   // execucao SuperFX/GSU
extern Uint32 g_TmgCycMDMA;  // DMA geral (inclui uploads de OAM/VRAM)
extern Uint32 g_TmgCycHDMA;  // HDMA por scanline
extern Uint32 g_TmgCycAPU;   // execucao SPC700
extern Uint32 g_TmgCycMix;   // mixer DSP de audio
extern Uint32 g_TmgCycBlend; // composicao final main/sub da PPU
extern Uint32 g_TmgCycPPUSync;    // SyncPPU completo (fila + render, inclusivo)
extern Uint32 g_TmgCycBGInfo;     // decodificacao dos registradores de BG
extern Uint32 g_TmgCycBGOffset;   // busca de offset-per-tile (modos 2/4)
extern Uint32 g_TmgCycBGMap;      // busca/cache de entradas do tilemap
extern Uint32 g_TmgCycBGChr;      // decodificacao das linhas de tiles BG
extern Uint32 g_TmgCycBGMain;     // composicao dos BGs na tela principal
extern Uint32 g_TmgCycBGSub;      // composicao/limpeza da subtela
extern Uint32 g_TmgCycColorMath;  // criacao das mascaras de color math
extern Uint32 g_TmgCycObjUpdate;  // decode OAM + visibilidade por scanline
extern Uint32 g_TmgCycObjFetch;   // fetch/decode das linhas OBJ
extern Uint32 g_TmgCycObjDraw;    // composicao OBJ main/sub
extern Uint32 g_TmgCycHDMAData;   // fase de transferencia dos canais HDMA
extern Uint32 g_TmgCycHDMATable;  // contador/tabela dos canais HDMA

// Totais da janela atual (60 frames), alimentados pela PPU/render.
extern Uint32 g_DbgOAMWrites;
extern Uint32 g_DbgVRAMWrites;
extern Uint32 g_DbgCGRAMWrites;
extern Uint32 g_DbgCGRAMCommits;
extern Uint32 g_DbgCGRAMUnchanged;
extern Uint32 g_DbgVideoRenderedFrames;
extern Uint32 g_DbgVideoSkippedFrames;
extern Uint32 g_DbgObjEnabledLines;
extern Uint32 g_DbgObjOamRefs;
extern Uint32 g_DbgObjTiles;
extern Uint32 g_DbgObjCacheHits;
extern Uint32 g_DbgObjCacheMisses;
extern Uint32 g_DbgObjCacheRefreshes;
extern Uint32 g_DbgBGCacheHits;
extern Uint32 g_DbgBGCacheMisses;
extern Uint32 g_DbgBGCacheRefreshes;
extern Uint32 g_DbgChrCacheInvalidations;
extern Uint32 g_DbgAudioSamples;
extern Uint32 g_DbgObjOpaqueTiles;
extern Uint32 g_DbgObjCandidatePixels;
extern Uint32 g_DbgObjDrawnPixels;
extern Uint32 g_DbgObjClippedTiles;
extern Uint32 g_DbgObjEmptyLines;
extern Uint32 g_DbgObjRangeLimitLines;
extern Uint32 g_DbgObjLimitLines;
extern Uint8  g_DbgObjOBSEL;
extern Uint8  g_DbgObjTM;
extern Uint8  g_DbgObjTS;
extern Uint16 g_DbgObjPriority;

// Sincronizacao PPU e descricao das DMAs iniciadas por $420B. Estes dados
// distinguem "o port recebeu N bytes" de "qual canal/modo/endereco enviou
// esses bytes", que e' essencial para rastrear OAM/VRAM corrompida.
extern Uint32 g_DbgPPUSyncCalls;
extern Uint32 g_DbgPPURenderLines;
extern Uint32 g_DbgDMAStarts;
extern Uint32 g_DbgDMAReadBytes;
extern Uint32 g_DbgDMAOAMBytes;
extern Uint32 g_DbgDMAVRAMBytes;
extern Uint32 g_DbgDMACGRAMBytes;
extern Uint32 g_DbgDMAOtherBytes;
extern Uint32 g_DbgDMAWraps;
extern Uint32 g_DbgDMAMaxBytes;
extern Uint32 g_DbgDMAModes[8];
extern Uint32 g_DbgHDMAScrollBytes;
extern Uint32 g_DbgHDMACGRAMBytes;
extern Uint32 g_DbgHDMAWindowColorBytes;
extern Uint32 g_DbgHDMAOtherBytes;
extern Uint32 g_DbgPPUQueuedWrites;
extern Uint32 g_DbgPPUAppliedWrites;
extern Uint32 g_DbgPPUQueueFull;
extern Uint32 g_DbgHDMALines;
extern Uint32 g_DbgHDMAActiveChannels;
extern Uint32 g_DbgHDMATransferChannels;
extern Uint32 g_DbgBGActiveLayers;
extern Uint32 g_DbgBGMapReloads;
extern Uint32 g_DbgBGChrRows;
extern Uint32 g_DbgBGChrBlankRows;
extern Uint32 g_DbgBGChrRepeatRows;

// Periodicamente uma build diagnostica captura dois scanlines OBJ e suas
// fontes em VRAM. O dump e' pequeno para nao bloquear o SIO do NetherSX2.
extern Bool   g_DbgCaptureActive;
extern Uint32 g_DbgCaptureFrameNo;
#endif

#endif // _SNDBGLOG_H
