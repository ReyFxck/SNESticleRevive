/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Small, testable helpers for splitting one native-hires SNES tile pair
 *   into the even (sub) and odd (main) phases used by the PS2 renderer.
 */

#ifndef _SNPPUHIRES_H
#define _SNPPUHIRES_H

#include "types.h"

struct SnesPPUHiresPairT
{
	Uint64 uMain;
	Uint64 uSub;
	Uint8  uMainOpaque;
	Uint8  uSubOpaque;
};

/* Pack bytes 0,2,4,6 or 1,3,5,7 into one 32-bit word.  Revive and the EE
   target are little-endian; spelling this out with integer operations lets
   the R5900 avoid the old per-pixel load/store loop and its eight branches. */
_INLINE Uint32 SnesPPUHiresEvenBytes(Uint64 uRow)
{
	Uint32 uLo = (Uint32)uRow;
	Uint32 uHi = (Uint32)(uRow >> 32);

	return (uLo & 0x000000FFu) |
	       ((uLo >> 8) & 0x0000FF00u) |
	       ((uHi & 0x000000FFu) << 16) |
	       ((uHi << 8) & 0xFF000000u);
}

_INLINE Uint32 SnesPPUHiresOddBytes(Uint64 uRow)
{
	Uint32 uLo = (Uint32)uRow;
	Uint32 uHi = (Uint32)(uRow >> 32);

	return ((uLo >> 8) & 0x000000FFu) |
	       ((uLo >> 16) & 0x0000FF00u) |
	       ((uHi << 8) & 0x00FF0000u) |
	       (uHi & 0xFF000000u);
}

_INLINE Uint8 SnesPPUHiresEvenMask(Uint32 uMask)
{
	uMask &= 0x55u;
	uMask = (uMask | (uMask >> 1)) & 0x33u;
	uMask = (uMask | (uMask >> 2)) & 0x0Fu;
	return (Uint8)uMask;
}

_INLINE Uint8 SnesPPUHiresOddMask(Uint32 uMask)
{
	return SnesPPUHiresEvenMask(uMask >> 1);
}

_INLINE void SnesPPUPackHiresPair(Uint64 uRow0, Uint32 uOpaque0,
	Uint64 uRow1, Uint32 uOpaque1, Bool bMosaic,
	SnesPPUHiresPairT *pPair)
{
	Uint64 uSub = (Uint64)SnesPPUHiresEvenBytes(uRow0) |
		((Uint64)SnesPPUHiresEvenBytes(uRow1) << 32);
	Uint8 uSubOpaque = (Uint8)(SnesPPUHiresEvenMask(uOpaque0) |
		(SnesPPUHiresEvenMask(uOpaque1) << 4));

	pPair->uSub = uSub;
	pPair->uSubOpaque = uSubOpaque;
	if (bMosaic)
	{
		pPair->uMain = uSub;
		pPair->uMainOpaque = uSubOpaque;
	}
	else
	{
		pPair->uMain = (Uint64)SnesPPUHiresOddBytes(uRow0) |
			((Uint64)SnesPPUHiresOddBytes(uRow1) << 32);
		pPair->uMainOpaque = (Uint8)(SnesPPUHiresOddMask(uOpaque0) |
			(SnesPPUHiresOddMask(uOpaque1) << 4));
	}
}

#endif // _SNPPUHIRES_H
