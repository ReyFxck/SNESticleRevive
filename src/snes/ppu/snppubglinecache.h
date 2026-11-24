/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Exact tile-fetch key used by the decoded BG scanline cache.
 */

#ifndef _SNPPUBGLINECACHE_H
#define _SNPPUBGLINECACHE_H

#include "types.h"
#include <string.h>
#include "snppurender.h"

#ifndef SNPPU_BG_LINE_CACHE_LINES
#define SNPPU_BG_LINE_CACHE_LINES 512u
#endif
#if SNPPU_BG_LINE_CACHE_LINES != 256 && SNPPU_BG_LINE_CACHE_LINES != 512
#error BG line cache supports 256 or 512 world rows
#endif
#define SNPPU_BG_LINE_PIXELS      (33u * 8u)
#define SNPPU_BG_LINE_MASK_BYTES  40u
#define SNPPU_BG_LINE_SCROLL_MAX  8u

#ifndef SNPPU_BG_LINE_CACHE_WAYS
#define SNPPU_BG_LINE_CACHE_WAYS 2u
#endif
#ifndef SNPPU_BG_CACHE_PACKED_KEY
#define SNPPU_BG_CACHE_PACKED_KEY 1
#endif

#if SNPPU_BG_CACHE_PACKED_KEY
struct SnesPPUBGLineCacheKeyT
{
	Uint64 uGenerationState;
	Uint64 uAddrPair;
	Uint64 uMeta;
};
#else
struct SnesPPUBGLineCacheKeyT
{
	Uint32 uGeneration;
	Uint32 uVramState;
	Uint32 uScrAddr;
	Uint32 uChrAddr;
	Uint8  uBG;
	Uint8  uMode;
	Uint8  uScrSize;
	Uint8  uChrSize;
	Uint8  uBitDepth;
	Uint8  uPalBase;
	Uint8  uPriority;
	/* Mosaic is part of the rendered line, unlike ordinary fine-X.  Cache
	   mosaic rows only for the exact amount and horizontal phase that
	   produced them. Non-mosaic rows keep fine-X excluded so the existing
	   cheap mask-shift reuse remains unchanged. */
	Uint8  uMosaic;
	Uint8  uMosaicFineX;
};
#endif

#if SNPPU_BG_CACHE_PACKED_KEY
_INLINE Uint32 SnesPPUBGLineCachePackMeta(Uint32 uBG, Uint32 uMode,
	const SnesBGInfoT *pInfo, Uint32 uVramState)
{
	return ((uBG & 3u) << 0) |
	       ((uMode & 7u) << 2) |
	       (((Uint32)pInfo->uScrSize & 3u) << 5) |
	       (((Uint32)pInfo->uChrSize & 1u) << 7) |
	       (((Uint32)pInfo->uBitDepth & 15u) << 8) |
	       (((Uint32)pInfo->uPalBase) << 12) |
	       (((Uint32)pInfo->Priority & 7u) << 20) |
	       (((Uint32)pInfo->uMosaic & 15u) << 23) |
	       ((pInfo->uMosaic ? ((uVramState >> 16) & 7u) : 0u) << 27);
}

_INLINE Uint32 SnesPPUBGLineCacheKeyState(const SnesPPUBGLineCacheKeyT *pKey)
{
	return (Uint32)(pKey->uGenerationState >> 32);
}
#else
_INLINE Uint32 SnesPPUBGLineCacheKeyState(const SnesPPUBGLineCacheKeyT *pKey)
{
	return pKey->uVramState;
}
#endif

/* Fine X changes which 256-pixel window is selected from the decoded
   33-tile row, but not the row itself. Keep every other FetchBG state bit:
   tilemap X/Y, 16x16 half selection and fine Y. */
_INLINE Uint32 SnesPPUBGLineCacheState(Uint32 uVramState)
{
	return uVramState & ~(7u << 16);
}

/* One direct-mapped slot per wrapped world scanline. Horizontal tile changes
   replace that world's row, while pixel-by-pixel scrolling within the tile
   reuses it. A collision is harmless because the complete key is compared. */
_INLINE Uint32 SnesPPUBGLineCacheIndex(Uint32 uVramState, Uint32 uChrSize)
{
	Uint32 uTileY = ((uVramState >> 5) & 0x1Fu) |
		(((uVramState >> 11) & 1u) << 5);
	Uint32 uFineY = (uVramState >> 24) & 7u;

	if (uChrSize)
	{
		Uint32 uHalfY = (uVramState >> 13) & 1u;
		return ((uTileY << 4) | (uHalfY << 3) | uFineY) &
			(SNPPU_BG_LINE_CACHE_LINES - 1u);
	}
	return ((uTileY << 3) | uFineY) &
		(SNPPU_BG_LINE_CACHE_LINES - 1u);
}

_INLINE void SnesPPUBGLineCacheSetKey(SnesPPUBGLineCacheKeyT *pKey,
	Uint32 uGeneration, Uint32 uVramState, Uint32 uBG,
	Uint32 uMode, const SnesBGInfoT *pInfo)
{
#if SNPPU_BG_CACHE_PACKED_KEY
	const Uint32 uState = SnesPPUBGLineCacheState(uVramState);
	pKey->uGenerationState = (Uint64)uGeneration | ((Uint64)uState << 32);
	pKey->uAddrPair = (Uint64)pInfo->uScrAddr | ((Uint64)pInfo->uChrAddr << 32);
	pKey->uMeta = (Uint64)SnesPPUBGLineCachePackMeta(uBG, uMode, pInfo, uVramState);
#else
	pKey->uGeneration = uGeneration;
	pKey->uVramState = SnesPPUBGLineCacheState(uVramState);
	pKey->uScrAddr = pInfo->uScrAddr;
	pKey->uChrAddr = pInfo->uChrAddr;
	pKey->uBG = (Uint8)uBG;
	pKey->uMode = (Uint8)uMode;
	pKey->uScrSize = pInfo->uScrSize;
	pKey->uChrSize = pInfo->uChrSize;
	pKey->uBitDepth = pInfo->uBitDepth;
	pKey->uPalBase = pInfo->uPalBase;
	pKey->uPriority = pInfo->Priority;
	pKey->uMosaic = pInfo->uMosaic;
	pKey->uMosaicFineX = pInfo->uMosaic ?
		(Uint8)((uVramState >> 16) & 7u) : 0u;
#endif
}

_INLINE Bool SnesPPUBGLineCacheKeyMatches(
	const SnesPPUBGLineCacheKeyT *pKey,
	Uint32 uGeneration, Uint32 uVramState, Uint32 uBG,
	Uint32 uMode, const SnesBGInfoT *pInfo)
{
#if SNPPU_BG_CACHE_PACKED_KEY
	const Uint32 uState = SnesPPUBGLineCacheState(uVramState);
	const Uint64 uGenerationState =
		(Uint64)uGeneration | ((Uint64)uState << 32);
	const Uint64 uAddrPair =
		(Uint64)pInfo->uScrAddr | ((Uint64)pInfo->uChrAddr << 32);
	const Uint64 uMeta =
		(Uint64)SnesPPUBGLineCachePackMeta(uBG, uMode, pInfo, uVramState);
	return pKey->uGenerationState == uGenerationState &&
	       pKey->uAddrPair == uAddrPair &&
	       pKey->uMeta == uMeta;
#else
	return pKey->uGeneration == uGeneration &&
	       pKey->uVramState == SnesPPUBGLineCacheState(uVramState) &&
	       pKey->uScrAddr == pInfo->uScrAddr &&
	       pKey->uChrAddr == pInfo->uChrAddr &&
	       pKey->uBG == (Uint8)uBG &&
	       pKey->uMode == (Uint8)uMode &&
	       pKey->uScrSize == pInfo->uScrSize &&
	       pKey->uChrSize == pInfo->uChrSize &&
	       pKey->uBitDepth == pInfo->uBitDepth &&
	       pKey->uPalBase == pInfo->uPalBase &&
	       pKey->uPriority == pInfo->Priority &&
	       pKey->uMosaic == pInfo->uMosaic &&
	       pKey->uMosaicFineX == (pInfo->uMosaic ?
			(Uint8)((uVramState >> 16) & 7u) : 0u);
#endif
}

/* Reuse an overlapping 33-cell window only when every other fetch input is
   exact. Normal 16x16 maps have two 8-dot cells per horizontal map entry;
   native hires has one logical cell (two physical CHR halves) per entry. */
_INLINE Int32 SnesPPUBGLineCacheScrollStep(
	const SnesPPUBGLineCacheKeyT *pKey, Uint32 uGeneration,
	Uint32 uVramState, Uint32 uBG, Uint32 uMode, const SnesBGInfoT *pInfo)
{
	/* Cached mosaic pixels are screen-phase dependent.  Exact-key hits are
	   safe (and catch the vertically repeated mosaic lines), but moving an
	   already-mosaicked 8-pixel cell window can change the phase whenever
	   the mosaic period does not divide that move. Decode the exposed row
	   normally instead of attempting overlap reuse. */
	if (pInfo->uMosaic)
		return 0;

	const Bool bHalfX = uMode == 1u && pInfo->uChrSize;
	const Uint32 uXMask = 0x041Fu | (bHalfX ? 0x1000u : 0u);
	Uint32 uSameXState = (uVramState & ~uXMask) | (SnesPPUBGLineCacheKeyState(pKey) & uXMask);
	if (!SnesPPUBGLineCacheKeyMatches(pKey, uGeneration,
		uSameXState, uBG, uMode, pInfo)) return 0;
	Uint32 uOldX = (SnesPPUBGLineCacheKeyState(pKey) & 31u) | ((SnesPPUBGLineCacheKeyState(pKey) >> 5) & 32u);
	Uint32 uNewX = (uVramState & 31u) | ((uVramState >> 5) & 32u);
	if (bHalfX)
	{
		uOldX = (uOldX << 1) | ((SnesPPUBGLineCacheKeyState(pKey) >> 12) & 1u);
		uNewX = (uNewX << 1) | ((uVramState >> 12) & 1u);
	}
	Uint32 uMask = bHalfX ? 127u : 63u;
	Uint32 uDelta = (uNewX - uOldX) & uMask;
	if (uDelta && uDelta <= SNPPU_BG_LINE_SCROLL_MAX)
		return (Int32)uDelta;
	Uint32 uReverseDelta = (uOldX - uNewX) & uMask;
	return uReverseDelta && uReverseDelta <= SNPPU_BG_LINE_SCROLL_MAX
		? -(Int32)uReverseDelta : 0;
}

/* Both pixel buffers are 8-byte aligned. Retain at least 25 cells; beyond
   eight exposed cells use the ordinary decoder instead of paying for a
   mostly discarded row. Sources and destinations are separate buffers. */
_INLINE void SnesPPUBGLineCacheCopyOverlap(
	Uint8 *pDest, Uint8 *pOpaque, Uint8 *pPriority,
	const Uint8 *pSource, const Uint8 *pSourceOpaque,
	const Uint8 *pSourcePriority, Int32 nStep)
{
	Uint32 nExposed = (Uint32)(nStep > 0 ? nStep : -nStep);
	Uint32 nOverlap = 33u - nExposed;
	Uint32 uSourceCell = nStep > 0 ? nExposed : 0u;
	Uint32 uDestCell = nStep > 0 ? 0u : nExposed;
	for (Uint32 i = 0; i < nOverlap; ++i)
		((Uint64 *)pDest)[uDestCell + i] =
			((const Uint64 *)pSource)[uSourceCell + i];
	memcpy(pOpaque + uDestCell, pSourceOpaque + uSourceCell, nOverlap);
	memcpy(pPriority + uDestCell, pSourcePriority + uSourceCell, nOverlap);
}

#endif // _SNPPUBGLINECACHE_H
