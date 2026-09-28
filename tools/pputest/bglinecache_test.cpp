/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Verifies the exact tile-fetch identity used by the BG line cache.
 */

#include <cstdio>
#include <cstring>

#include "types.h"
#include "snppubglinecache.h"

static int g_Failures;

static void Check(const char *pName, Uint32 uGot, Uint32 uExpected)
{
	if (uGot != uExpected)
	{
		std::printf("FAIL %s: %u != %u\n", pName,
			(unsigned)uGot, (unsigned)uExpected);
		g_Failures++;
	}
}

int main()
{
	SnesPPUBGLineCacheKeyT Key;
	SnesBGInfoT Info;
	Uint32 uState = 0;

	std::memset(&Key, 0, sizeof(Key));
	std::memset(&Info, 0, sizeof(Info));
	Info.uScrollX = 0x121u;
	Info.uScrollY = 0x0A5u;
	Info.uScrAddr = 0x1800u;
	Info.uChrAddr = 0x4000u;
	Info.uScrSize = 3;
	Info.uChrSize = 0;
	Info.uBitDepth = 4;
	Info.uPalBase = 2;
	Info.Priority = 3;

	/* tile X=7, tile Y=37, fine X=1, fine Y=5 */
	uState |= 7u;
	uState |= 5u << 5;
	uState |= 1u << 11;
	uState |= 1u << 16;
	uState |= 5u << 24;

	SnesPPUBGLineCacheSetKey(&Key, 9u, uState, 1u, 1u, &Info);
	Check("exact state", SnesPPUBGLineCacheKeyMatches(&Key, 9u,
		uState, 1u, 1u, &Info), TRUE);
	Check("fine X is reusable", SnesPPUBGLineCacheKeyMatches(&Key, 9u,
		(uState & ~(7u << 16)) | (6u << 16), 1u, 1u, &Info), TRUE);
	Check("tile X is exact", SnesPPUBGLineCacheKeyMatches(&Key, 9u,
		uState ^ 1u, 1u, 1u, &Info), FALSE);
	Check("fine Y is exact", SnesPPUBGLineCacheKeyMatches(&Key, 9u,
		uState ^ (1u << 24), 1u, 1u, &Info), FALSE);
	Check("generation is exact", SnesPPUBGLineCacheKeyMatches(&Key, 10u,
		uState, 1u, 1u, &Info), FALSE);

	Check("8x8 world row", SnesPPUBGLineCacheIndex(uState, 0u),
		((37u << 3) | 5u) & 255u);
	Check("horizontal position keeps slot",
		SnesPPUBGLineCacheIndex(uState ^ 0x401u, 0u),
		SnesPPUBGLineCacheIndex(uState, 0u));
	Check("fine X keeps slot",
		SnesPPUBGLineCacheIndex(uState ^ (7u << 16), 0u),
		SnesPPUBGLineCacheIndex(uState, 0u));

	Info.uChrSize = 1;
	uState |= 1u << 13;
	Check("16x16 world row", SnesPPUBGLineCacheIndex(uState, 1u),
		((37u << 4) | (1u << 3) | 5u) & 255u);

	Info.uChrAddr ^= 0x1000u;
	Check("CHR base is exact", SnesPPUBGLineCacheKeyMatches(&Key, 9u,
		uState & ~(1u << 13), 1u, 1u, &Info), FALSE);

	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
