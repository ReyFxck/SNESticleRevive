/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Verifies the exact tile-fetch identity used by the BG line cache.
 */

#include <cstdio>
#include <cstring>
#include <initializer_list>

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

static Uint32 EncodeX(Uint32 x, bool half)
{
	Uint32 tile = half ? x >> 1 : x;
	return (tile & 31u) | ((tile & 32u) << 5) |
		(half ? (x & 1u) << 12 : 0u);
}

static void CheckScrollReuse()
{
	for (Uint32 mode : { 1u, 5u })
		for (Uint32 size : { 0u, 1u })
		{
			SnesBGInfoT info = {};
			info.uBitDepth = 4;
			info.uChrSize = size;
			bool half = mode == 1 && size;
			Uint32 mask = half ? 127 : 63;
			for (Uint32 oldX = 0; oldX <= mask; ++oldX)
				for (Int32 step = -32; step <= 32; ++step)
				{
					SnesPPUBGLineCacheKeyT key = {};
					Uint32 oldState = EncodeX(oldX, half) | (11u << 5) | (3u << 24);
					Uint32 newState = EncodeX((oldX + step) & mask, half) |
						(11u << 5) | (3u << 24) | (7u << 16);
					SnesPPUBGLineCacheSetKey(&key, 41, oldState, 0, mode, &info);
					Int32 expected = step && step >= -8 && step <= 8 ? step : 0;
					Check("overlapping scroll including wrap", SnesPPUBGLineCacheScrollStep(
						&key, 41, newState, 0, mode, &info), expected);
					Check("changed row rejects scroll reuse", SnesPPUBGLineCacheScrollStep(
						&key, 41, newState ^ (1u << 24), 0, mode, &info), 0);
					Check("changed VRAM rejects scroll reuse", SnesPPUBGLineCacheScrollStep(
						&key, 42, newState, 0, mode, &info), 0);
				}
		}
	for (Int32 step = -8; step <= 8; ++step)
	{
		if (!step) continue;
		Uint8 source[272] _ALIGN(16), got[272] _ALIGN(16), expected[272] _ALIGN(16);
		Uint8 sourceOpaque[48], sourcePriority[48], opaque[48], priority[48];
		Uint8 expectedOpaque[48], expectedPriority[48];
		for (Uint32 i = 0; i < sizeof(source); ++i) source[i] = (Uint8)(i * 79u + 13u);
		for (Uint32 i = 0; i < 48; ++i)
		{
			sourceOpaque[i] = (Uint8)(i * 29u);
			sourcePriority[i] = (Uint8)(i * 43u);
		}
		std::memset(got, 0xA5, sizeof(got));
		std::memset(opaque, 0xA5, sizeof(opaque));
		std::memset(priority, 0xA5, sizeof(priority));
		std::memcpy(expected, got, sizeof(got));
		std::memcpy(expectedOpaque, opaque, sizeof(opaque));
		std::memcpy(expectedPriority, priority, sizeof(priority));
		Uint32 exposed = (Uint32)(step > 0 ? step : -step);
		Uint32 src = step > 0 ? exposed : 0, dst = step > 0 ? 0 : exposed;
		for (Uint32 i = 0; i < (33u - exposed) * 8u; ++i)
			expected[8 + dst * 8 + i] = source[src * 8 + i];
		for (Uint32 i = 0; i < 33u - exposed; ++i)
		{
			expectedOpaque[dst + i] = sourceOpaque[src + i];
			expectedPriority[dst + i] = sourcePriority[src + i];
		}
		SnesPPUBGLineCacheCopyOverlap(got + 8, opaque, priority,
			source, sourceOpaque, sourcePriority, step);
		Check("overlap pixels and guards", std::memcmp(got, expected, sizeof(got)) == 0, TRUE);
		Check("overlap opacity", std::memcmp(opaque, expectedOpaque, sizeof(opaque)) == 0, TRUE);
		Check("overlap priority", std::memcmp(priority, expectedPriority, sizeof(priority)) == 0, TRUE);
	}
}

int main()
{
	CheckScrollReuse();
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
