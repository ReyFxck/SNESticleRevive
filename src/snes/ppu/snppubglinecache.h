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
	pKey->uGeneration = uGeneration;
	pKey->uVramState = uVramState;
	pKey->uScrollX = pInfo->uScrollX;
	pKey->uScrollY = pInfo->uScrollY;
	pKey->uScrAddr = pInfo->uScrAddr;
	pKey->uChrAddr = pInfo->uChrAddr;
	pKey->uMosaic = pInfo->uMosaic;
	pKey->uLine = (Uint16)uLine;
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
	return pKey->uGeneration == uGeneration &&
	       pKey->uVramState == uVramState &&
	       pKey->uScrollX == pInfo->uScrollX &&
	       pKey->uScrollY == pInfo->uScrollY &&
	       pKey->uScrAddr == pInfo->uScrAddr &&
	       pKey->uChrAddr == pInfo->uChrAddr &&
	       pKey->uMosaic == pInfo->uMosaic &&
	       pKey->uLine == (Uint16)uLine &&
	       pKey->uBG == (Uint8)uBG &&
	       pKey->uMode == (Uint8)uMode &&
	       pKey->uScrSize == pInfo->uScrSize &&
	       pKey->uChrSize == pInfo->uChrSize &&
	       pKey->uBitDepth == pInfo->uBitDepth &&
	       pKey->uPalBase == pInfo->uPalBase &&
	       pKey->uPriority == pInfo->Priority;
}

#endif // _SNPPUBGLINECACHE_H
