/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the sndbglog interface for the SNES emulation core.
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

#if SNDBG_DEEP && !SNDBG_LOG
#undef SNDBG_LOG
#define SNDBG_LOG 1
#endif

/* Contrato estavel consumido por scripts e comparacoes entre jogos. */
#define SNDBG_SCHEMA             "snesdiag-v1"
#define SNDBG_FRAME_PERIOD       120u
#define SNDBG_EE_COUNT_HZ        147456000u
#define SNDBG_SLOW_PERCENT       105u
#define SNDBG_CAPTURE_COOLDOWN   60u

/* Razoes combinaveis para a proxima captura profunda. */
#define SNDBG_CAPTURE_MANUAL     0x0001u
#define SNDBG_CAPTURE_SLOW_FRAME 0x0002u
#define SNDBG_CAPTURE_PPU_QUEUE  0x0004u
#define SNDBG_CAPTURE_DMA_WRAP   0x0008u
#define SNDBG_CAPTURE_OBJ        0x0010u
#define SNDBG_CAPTURE_FRAMESKIP  0x0020u
#define SNDBG_CAPTURE_AUDIO      0x0040u
#define SNDBG_CAPTURE_CHIP       0x0080u

/* Categorias de coprocessador: identidade informa, nunca muda o renderer. */
#define SNDBG_CHIP_DSP           0u
#define SNDBG_CHIP_GSU           1u
#define SNDBG_CHIP_OBC1          2u
#define SNDBG_CHIP_CX4           3u
#define SNDBG_CHIP_SDD1          4u
#define SNDBG_CHIP_SRTC          5u
#define SNDBG_CHIP_COUNT         6u

_INLINE Uint32 SnesDbgFrameBudget(Uint32 uTargetHz)
{
	return SNDBG_EE_COUNT_HZ / (uTargetHz ? uTargetHz : 60u);
}

_INLINE Bool SnesDbgFrameIsSlow(Uint32 uCycles, Uint32 uBudget)
{
	return ((Uint64)uCycles * 100u >
	        (Uint64)uBudget * SNDBG_SLOW_PERCENT) ? TRUE : FALSE;
}

#ifdef __cplusplus
extern "C" {
#endif
void DLog(const char *fmt, ...);   // definido em src/modules/audio/audio_audsrv.c
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

// Totais da janela atual (120 frames), alimentados pela PPU/render.
extern Uint32 g_DbgOAMWrites;
extern Uint32 g_DbgVRAMWrites;
extern Uint32 g_DbgCGRAMWrites;
extern Uint32 g_DbgCGRAMCommits;
extern Uint32 g_DbgCGRAMUnchanged;
extern Uint32 g_DbgVideoRenderedFrames;
extern Uint32 g_DbgVideoSkippedFrames;
extern Uint32 g_DbgHostRefreshHz;
extern Uint32 g_DbgSessionId;
extern Uint32 g_DbgObjEnabledLines;
extern Uint32 g_DbgObjOamRefs;
extern Uint32 g_DbgObjTiles;
extern Uint32 g_DbgObjCacheHits;
extern Uint32 g_DbgObjCacheMisses;
extern Uint32 g_DbgObjCacheInvalidations;
extern Uint32 g_DbgAudioSamples;
extern Uint32 g_DbgAudioMixCalls;
extern Uint32 g_DbgAudioZeroMixes;
extern Uint32 g_DbgAudioMinSamples;
extern Uint32 g_DbgAudioMaxSamples;
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
extern Uint32 g_DbgBGChrRowsByDepth[3];

// Cobertura universal de modos, camadas e recursos PPU por scanline.
extern Uint32 g_DbgPPUModeLines[8];
extern Uint32 g_DbgPPUModeChanges;
extern Uint8  g_DbgPPULastMode;
extern Uint32 g_DbgPPUMainLayerLines[4];
extern Uint32 g_DbgPPUSubLayerLines[4];
extern Uint32 g_DbgPPUFetchLayerLines[4];
extern Uint32 g_DbgPPUForcedBlankLines;
extern Uint32 g_DbgPPUMosaicLines;
extern Uint32 g_DbgPPUOffsetLines;
extern Uint32 g_DbgPPUWindowLines;
extern Uint32 g_DbgPPUColorMathLines;
extern Uint32 g_DbgPPUDirectColorLines;
extern Uint32 g_DbgPPUInterlaceLines;
extern Uint32 g_DbgPPUObjInterlaceLines;
extern Uint32 g_DbgPPUOverscanLines;
extern Uint32 g_DbgPPUHiresLines;
extern Uint32 g_DbgPPUExtBGLines;

extern Uint32 g_DbgChipReads[SNDBG_CHIP_COUNT];
extern Uint32 g_DbgChipWrites[SNDBG_CHIP_COUNT];
extern Uint32 g_DbgSDD1DmaTransfers;
extern Uint32 g_DbgSDD1DecompressedBytes;
extern Uint32 g_DbgSDD1Remaps;
extern Uint32 g_DbgSDD1SourceFailures;

// Captura profunda: o pedido e' consumido no inicio do proximo frame.
extern Bool   g_DbgCaptureActive;
extern Uint32 g_DbgCaptureFrameNo;
extern Uint32 g_DbgCaptureReasons;
#if SNDBG_DEEP
extern Uint32 g_DbgCapturePendingReasons;
extern Uint32 g_DbgPPURegWrites[0x40];

_INLINE void SnesDbgRequestCapture(Uint32 uReason)
{
	g_DbgCapturePendingReasons |= uReason;
}
#else
_INLINE void SnesDbgRequestCapture(Uint32 uReason)
{
	(void)uReason;
}
#endif
#endif

#endif // _SNDBGLOG_H
