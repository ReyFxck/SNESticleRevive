/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Exact state key for conservative native-hires output-line reuse.
 */

#ifndef _SNPPUHIRLINECACHE_H
#define _SNPPUHIRLINECACHE_H

#include <string.h>
#include "types.h"

#define SNPPU_HIRES_LINE_CACHE_LINES  256u
#define SNPPU_HIRES_LINE_PIXELS       512u

/* Only state that can affect a Mode 5 output line is recorded here.  The
   explicit padding keeps the key and every cached 1024-byte line aligned to
   a 64-byte boundary on the EE.  State is zero-initialised before filling,
   so the byte-for-byte comparison includes no indeterminate padding. */
struct SnesPPUHiresLineStateT
{
	Uint16 uScroll[8];
	Uint16 uOAMPriority;
	Uint16 uColorData;
	Uint8  uInidisp;
	Uint8  uObsel;
	Uint8  uBGMode;
	Uint8  uMosaic;
	Uint8  uBG1SC;
	Uint8  uBG2SC;
	Uint8  uBG3SC;
	Uint8  uBG4SC;
	Uint8  uBG12NBA;
	Uint8  uBG34NBA;
	Uint8  uW12Sel;
	Uint8  uW34Sel;
	Uint8  uWObjSel;
	Uint8  uWH0;
	Uint8  uWH1;
	Uint8  uWH2;
	Uint8  uWH3;
	Uint8  uWBGLog;
	Uint8  uWObjLog;
	Uint8  uTM;
	Uint8  uTS;
	Uint8  uTMW;
	Uint8  uTSW;
	Uint8  uCGWSel;
	Uint8  uCGADSub;
	Uint8  uSetIni;
	Uint8  uField;
	Uint8  uRenderTM;
	Uint8  uRenderTS;
	Uint8  uRenderTMW;
	Uint8  uRenderTSW;
	Uint8  uReserved[5];
};

struct SnesPPUHiresLineCacheKeyT
{
	Uint32 uGeneration;
	Uint16 uLine;
	Uint16 uReserved;
	SnesPPUHiresLineStateT State;
};

typedef char SnesPPUHiresLineStateSizeCheck[
	(sizeof(SnesPPUHiresLineStateT) == 56) ? 1 : -1];
typedef char SnesPPUHiresLineKeySizeCheck[
	(sizeof(SnesPPUHiresLineCacheKeyT) == 64) ? 1 : -1];

/* STAT78.7 toggles every field even while SETINI interlace is disabled.
   In that common case it cannot affect the rendered scanline and including
   it in the key would force every cached line to miss on every frame. */
_INLINE Uint8 SnesPPUHiresLineFieldKey(Uint8 uSetIni, Uint8 uStat78)
{
	return (uSetIni & 0x01u) ? (uStat78 & 0x80u) : 0;
}

_INLINE void SnesPPUHiresLineCacheSetKey(
	SnesPPUHiresLineCacheKeyT *pKey, Uint32 uGeneration, Uint32 uLine,
	const SnesPPUHiresLineStateT *pState)
{
	pKey->uGeneration = uGeneration;
	pKey->uLine = (Uint16)uLine;
	pKey->uReserved = 0;
	memcpy(&pKey->State, pState, sizeof(pKey->State));
}

_INLINE Bool SnesPPUHiresLineCacheKeyMatches(
	const SnesPPUHiresLineCacheKeyT *pKey, Uint32 uGeneration,
	Uint32 uLine, const SnesPPUHiresLineStateT *pState)
{
	return pKey->uGeneration == uGeneration &&
	       pKey->uLine == (Uint16)uLine &&
	       memcmp(&pKey->State, pState, sizeof(pKey->State)) == 0;
}

#endif // _SNPPUHIRLINECACHE_H
