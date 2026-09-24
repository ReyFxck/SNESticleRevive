/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Exact key used by the conservative decoded BG scanline cache.
 */

#ifndef _SNPPUBGLINECACHE_H
#define _SNPPUBGLINECACHE_H

#include "types.h"
#include "snppurender.h"

#define SNPPU_BG_LINE_CACHE_LINES 256u
#define SNPPU_BG_LINE_PIXELS      (33u * 8u)
#define SNPPU_BG_LINE_MASK_DATA_BYTES 33u
#define SNPPU_BG_LINE_MASK_BYTES  40u

/* The decoded 33-tile row does not change during the eight fine-X phases.
   Only the final 256-bit masks and the source pointer shift; both are rebuilt
   cheaply when an entry is restored. */
_INLINE Uint32 SnesPPUBGLineCacheRasterState(Uint32 uVramState)
{
	return uVramState & ~(7u << 16);
}

_INLINE Uint32 SnesPPUBGLineCacheWorldLine(
	const SnesBGInfoT *pInfo, Uint32 uLine)
{
	return pInfo->uScrollY + uLine;
}

_INLINE Uint32 SnesPPUBGLineCacheIndex(
	const SnesBGInfoT *pInfo, Uint32 uLine)
{
	return SnesPPUBGLineCacheWorldLine(pInfo, uLine) &
		(SNPPU_BG_LINE_CACHE_LINES - 1u);
}

struct SnesPPUBGLineCacheKeyT
{
	Uint32 uGeneration;
	Uint32 uVramState;
	Uint32 uScrollX;
	Uint32 uScrollY;
	Uint32 uScrAddr;
	Uint32 uChrAddr;
	Uint32 uMosaic;
	Uint16 uLine;
	Uint8  uBG;
	Uint8  uMode;
	Uint8  uScrSize;
	Uint8  uChrSize;
	Uint8  uBitDepth;
	Uint8  uPalBase;
	Uint8  uPriority;
};

_INLINE void SnesPPUBGLineCacheSetKey(SnesPPUBGLineCacheKeyT *pKey,
	Uint32 uGeneration, Uint32 uVramState, Uint32 uLine, Uint32 uBG,
	Uint32 uMode, const SnesBGInfoT *pInfo)
{
	Uint32 uWorldLine = SnesPPUBGLineCacheWorldLine(pInfo, uLine);

	pKey->uGeneration = uGeneration;
	pKey->uVramState = SnesPPUBGLineCacheRasterState(uVramState);
	pKey->uScrollX = pInfo->uScrollX & ~7u;
	pKey->uScrollY = uWorldLine;
	pKey->uScrAddr = pInfo->uScrAddr;
	pKey->uChrAddr = pInfo->uChrAddr;
	pKey->uMosaic = pInfo->uMosaic;
	pKey->uLine = (Uint16)uWorldLine;
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
	Uint32 uGeneration, Uint32 uVramState, Uint32 uLine, Uint32 uBG,
	Uint32 uMode, const SnesBGInfoT *pInfo)
{
	Uint32 uWorldLine = SnesPPUBGLineCacheWorldLine(pInfo, uLine);

	return pKey->uGeneration == uGeneration &&
	       pKey->uVramState == SnesPPUBGLineCacheRasterState(uVramState) &&
	       pKey->uScrollX == (pInfo->uScrollX & ~7u) &&
	       pKey->uScrollY == uWorldLine &&
	       pKey->uScrAddr == pInfo->uScrAddr &&
	       pKey->uChrAddr == pInfo->uChrAddr &&
	       pKey->uMosaic == pInfo->uMosaic &&
	       pKey->uLine == (Uint16)uWorldLine &&
	       pKey->uBG == (Uint8)uBG &&
	       pKey->uMode == (Uint8)uMode &&
	       pKey->uScrSize == pInfo->uScrSize &&
	       pKey->uChrSize == pInfo->uChrSize &&
	       pKey->uBitDepth == pInfo->uBitDepth &&
	       pKey->uPalBase == pInfo->uPalBase &&
	       pKey->uPriority == pInfo->Priority;
}

#endif // _SNPPUBGLINECACHE_H
