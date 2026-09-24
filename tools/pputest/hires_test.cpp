/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Verifies branch-free native-hires phase packing and exact line-cache
 *   key invalidation against straightforward reference implementations.
 */

#include <cstdio>
#include <cstring>

#include "types.h"
#include "snppuhires.h"
#include "snppubglinecache.h"
#include "snppuhirlinecache.h"

static int g_Failures;

static void Check(const char *pName, Uint64 uGot, Uint64 uExpected)
{
	if (uGot != uExpected)
	{
		std::printf("FAIL %s: %llX != %llX\n", pName,
			(unsigned long long)uGot, (unsigned long long)uExpected);
		g_Failures++;
	}
}

static void ReferencePair(Uint64 uRow0, Uint32 uOpaque0,
	Uint64 uRow1, Uint32 uOpaque1, Bool bMosaic,
	SnesPPUHiresPairT *pPair)
{
	const Uint8 *pRow0 = (const Uint8 *)&uRow0;
	const Uint8 *pRow1 = (const Uint8 *)&uRow1;
	Uint8 *pMain = (Uint8 *)&pPair->uMain;
	Uint8 *pSub = (Uint8 *)&pPair->uSub;
	Uint32 x;

	pPair->uMain = 0;
	pPair->uSub = 0;
	pPair->uMainOpaque = 0;
	pPair->uSubOpaque = 0;
	for (x = 0; x < 4; ++x)
	{
		Uint8 s0 = pRow0[x << 1];
		Uint8 m0 = bMosaic ? s0 : pRow0[(x << 1) + 1];
		Uint8 s1 = pRow1[x << 1];
		Uint8 m1 = bMosaic ? s1 : pRow1[(x << 1) + 1];
		pSub[x] = s0;
		pMain[x] = m0;
		pSub[x + 4] = s1;
		pMain[x + 4] = m1;
		if (uOpaque0 & (1u << (x << 1)))
			pPair->uSubOpaque |= (Uint8)(1u << x);
		if (uOpaque0 & (1u << ((x << 1) + (bMosaic ? 0 : 1))))
			pPair->uMainOpaque |= (Uint8)(1u << x);
		if (uOpaque1 & (1u << (x << 1)))
			pPair->uSubOpaque |= (Uint8)(1u << (x + 4));
		if (uOpaque1 & (1u << ((x << 1) + (bMosaic ? 0 : 1))))
			pPair->uMainOpaque |= (Uint8)(1u << (x + 4));
	}
}

static Uint32 NextRandom(Uint32 *pState)
{
	*pState = *pState * 1664525u + 1013904223u;
	return *pState;
}

static void TestPacking()
{
	Uint32 state = 0x13579BDFu;
	Uint32 i;

	for (i = 0; i < 65536u; ++i)
	{
		Uint32 a = i & 0xFFu;
		Uint32 b = i >> 8;
		SnesPPUHiresPairT got;
		SnesPPUHiresPairT expected;
		SnesPPUPackHiresPair(0, a, 0, b, FALSE, &got);
		ReferencePair(0, a, 0, b, FALSE, &expected);
		if (got.uMainOpaque != expected.uMainOpaque ||
		    got.uSubOpaque != expected.uSubOpaque)
		{
			std::printf("FAIL phase masks at %04X\n", i);
			g_Failures++;
			break;
		}
	}

	for (i = 0; i < 20000u; ++i)
	{
		Uint64 row0 = (Uint64)NextRandom(&state) |
			((Uint64)NextRandom(&state) << 32);
		Uint64 row1 = (Uint64)NextRandom(&state) |
			((Uint64)NextRandom(&state) << 32);
		Uint32 opaque0 = NextRandom(&state) & 0xFFu;
		Uint32 opaque1 = NextRandom(&state) & 0xFFu;
		Bool mosaic = (NextRandom(&state) & 1u) ? TRUE : FALSE;
		SnesPPUHiresPairT got;
		SnesPPUHiresPairT expected;

		SnesPPUPackHiresPair(row0, opaque0, row1, opaque1,
			mosaic, &got);
		ReferencePair(row0, opaque0, row1, opaque1, mosaic, &expected);
		if (got.uMain != expected.uMain || got.uSub != expected.uSub ||
		    got.uMainOpaque != expected.uMainOpaque ||
		    got.uSubOpaque != expected.uSubOpaque)
		{
			std::printf("FAIL phase packing vector %u\n", i);
			g_Failures++;
			break;
		}
	}
}

static void TestLineKey()
{
	SnesBGInfoT info;
	SnesPPUBGLineCacheKeyT key;

	std::memset(&info, 0, sizeof(info));
	std::memset(&key, 0, sizeof(key));
	info.uScrollX = 7;
	info.uScrollY = 19;
	info.uScrAddr = 0x2400;
	info.uChrAddr = 0x6000;
	info.uScrSize = 2;
	info.uChrSize = 1;
	info.uBitDepth = 4;
	info.uPalBase = 1;
	info.Priority = 5;

	SnesPPUBGLineCacheSetKey(&key, 9, 0x12345678, 223, 1, 5,
		&info);
	Check("exact line key", SnesPPUBGLineCacheKeyMatches(&key, 9,
		0x12345678, 223, 1, 5, &info), TRUE);
	Check("VRAM generation invalidates", SnesPPUBGLineCacheKeyMatches(
		&key, 10, 0x12345678, 223, 1, 5, &info), FALSE);
	Check("raster state invalidates", SnesPPUBGLineCacheKeyMatches(
		&key, 9, 0x12345679, 223, 1, 5, &info), FALSE);
	Check("scanline invalidates", SnesPPUBGLineCacheKeyMatches(&key, 9,
		0x12345678, 222, 1, 5, &info), FALSE);
	Check("mode invalidates", SnesPPUBGLineCacheKeyMatches(&key, 9,
		0x12345678, 223, 1, 1, &info), FALSE);
	info.uChrAddr ^= 0x1000;
	Check("CHR base invalidates", SnesPPUBGLineCacheKeyMatches(&key, 9,
		0x12345678, 223, 1, 5, &info), FALSE);
}

static void TestHiresLineKey()
{
	SnesPPUHiresLineStateT state;
	SnesPPUHiresLineCacheKeyT key;

	std::memset(&state, 0, sizeof(state));
	std::memset(&key, 0, sizeof(key));
	state.uScroll[0] = 17;
	state.uScroll[3] = 511;
	state.uOAMPriority = 23;
	state.uBGMode = 5;
	state.uTM = 0x13;
	state.uTS = 0x13;
	state.uRenderTM = 0x3F;
	state.uField = 0x80;

	SnesPPUHiresLineCacheSetKey(&key, 41, 224, &state);
	Check("exact hires line key", SnesPPUHiresLineCacheKeyMatches(
		&key, 41, 224, &state), TRUE);
	Check("hires memory generation invalidates",
		SnesPPUHiresLineCacheKeyMatches(&key, 42, 224, &state), FALSE);
	Check("hires scanline invalidates", SnesPPUHiresLineCacheKeyMatches(
		&key, 41, 223, &state), FALSE);
	state.uScroll[0]++;
	Check("hires scroll invalidates", SnesPPUHiresLineCacheKeyMatches(
		&key, 41, 224, &state), FALSE);
	state.uScroll[0]--;
	state.uObsel = 0x63;
	Check("hires sprite state invalidates", SnesPPUHiresLineCacheKeyMatches(
		&key, 41, 224, &state), FALSE);
}

int main()
{
	TestPacking();
	TestLineKey();
	TestHiresLineKey();
	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
