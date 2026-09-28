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
#include "snppurender.h"

#define SNPPU_BG_LINE_CACHE_LINES 256u
#define SNPPU_BG_LINE_PIXELS      (33u * 8u)
#define SNPPU_BG_LINE_MASK_BYTES  40u

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
};

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
}

_INLINE Bool SnesPPUBGLineCacheKeyMatches(
	const SnesPPUBGLineCacheKeyT *pKey,
	Uint32 uGeneration, Uint32 uVramState, Uint32 uBG,
	Uint32 uMode, const SnesBGInfoT *pInfo)
{
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
	       pKey->uPriority == pInfo->Priority;
}

#endif // _SNPPUBGLINECACHE_H
