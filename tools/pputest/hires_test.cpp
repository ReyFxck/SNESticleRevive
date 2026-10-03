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

class CaptureHiresBlend : public ISNPPUBlend
{
public:
	Uint16 Pixels[512];
	Int32 Line;
	void Begin(CRenderSurface *) override {}
	void Exec(SNPPUBlendInfoT *, Int32, Uint32, SNMaskT *, Bool, Uint32, Bool) override {}
	void ExecHires512(const Uint16 *pPixels, Int32 iLine) override
	{
		std::memcpy(Pixels, pPixels, sizeof(Pixels));
		Line = iLine;
	}
	void Clear(SNPPUBlendInfoT *, Int32) override {}
	void End() override {}
	void UpdatePalette(SNPPUBlendInfoT *, Uint16 *, Uint32) override {}
	void UpdatePaletteEntry(SNPPUBlendInfoT *, Uint32, Uint32, Uint32) override {}
};

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

static Uint16 ReferenceGS16(Uint16 uColor, Uint32 uIntensity)
{
	Uint32 r = uColor & 0x1Fu;
	Uint32 g = (uColor >> 5) & 0x1Fu;
	Uint32 b = (uColor >> 10) & 0x1Fu;

	if (uIntensity < 15u)
	{
		r = r * uIntensity / 15u;
		g = g * uIntensity / 15u;
		b = b * uIntensity / 15u;
	}
	return (Uint16)(r | (g << 5) | (b << 10) | 0x8000u);
}

static void TestHiresPalette()
{
	Uint16 cgram[256];
	Uint16 palette[256];
	Uint8 mainLine[256];
	Uint8 subLine[256];
	Uint32 output[256];
	Uint32 state = 0x2468ACE1u;
	Uint32 intensity;
	Uint32 i;

	for (i = 0; i < 256u; ++i)
	{
		cgram[i] = (Uint16)(NextRandom(&state) & 0x7FFFu);
		mainLine[i] = (Uint8)NextRandom(&state);
		subLine[i] = (Uint8)NextRandom(&state);
	}

	for (intensity = 0; intensity <= 15u; ++intensity)
	{
		CaptureHiresBlend blend;
		Uint16 cached[528];
		std::memset(cached, 0xA5, sizeof(cached));
		SnesPPUBuildHiresPalette16(palette, cgram, intensity);
		SnesPPUBuildHiresOutput32(output, mainLine, subLine, palette);
		blend.ExecHiresIndexed(mainLine, subLine, palette, 37, cached + 8);
		Check("indexed output preserves line", blend.Line, 37);
		Check("indexed output and cache", std::memcmp(blend.Pixels, cached + 8, 1024) == 0, TRUE);
		Check("indexed output pixels", std::memcmp(blend.Pixels, output, 1024) == 0, TRUE);
		for (Uint32 guard = 0; guard < 8; ++guard)
		{
			Check("indexed cache leading guard", cached[guard], 0xA5A5);
			Check("indexed cache trailing guard", cached[520 + guard], 0xA5A5);
		}
		blend.ExecHiresIndexed(mainLine, subLine, palette, 38);
		Check("indexed output without admission", std::memcmp(blend.Pixels, output, 1024) == 0, TRUE);

		for (i = 0; i < 256u; ++i)
		{
			Uint32 expected = ReferenceGS16(cgram[subLine[i]], intensity) |
				((Uint32)ReferenceGS16(cgram[mainLine[i]], intensity) << 16);
			if (output[i] != expected)
			{
				std::printf("FAIL hires palette intensity=%u pair=%u\n",
					intensity, i);
				g_Failures++;
				return;
			}
		}
	}
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

	SnesPPUBGLineCacheSetKey(&key, 9, 0x12345678, 1, 5, &info);
	Check("exact line key", SnesPPUBGLineCacheKeyMatches(&key, 9,
		0x12345678, 1, 5, &info), TRUE);
	info.uScrollX = 1;
	Check("scroll coordinate is represented by fetch state",
		SnesPPUBGLineCacheKeyMatches(&key, 9, 0x12345678, 1, 5,
			&info), TRUE);
	info.uScrollX = 7;
	info.uScrollY = 20;
	Check("world coordinate is represented by fetch state",
		SnesPPUBGLineCacheKeyMatches(&key, 9, 0x12345678, 1, 5,
			&info), TRUE);
	info.uScrollY = 19;
	Check("VRAM generation invalidates", SnesPPUBGLineCacheKeyMatches(
		&key, 10, 0x12345678, 1, 5, &info), FALSE);
	Check("raster state invalidates", SnesPPUBGLineCacheKeyMatches(
		&key, 9, 0x12345679, 1, 5, &info), FALSE);
	Check("fine X reuses decoded row", SnesPPUBGLineCacheKeyMatches(&key, 9,
		(0x12345678 & ~(7u << 16)) | (2u << 16), 1, 5, &info), TRUE);
	Check("mode invalidates", SnesPPUBGLineCacheKeyMatches(&key, 9,
		0x12345678, 1, 1, &info), FALSE);
	info.uChrAddr ^= 0x1000;
	Check("CHR base invalidates", SnesPPUBGLineCacheKeyMatches(&key, 9,
		0x12345678, 1, 5, &info), FALSE);
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
	state.uField = SnesPPUHiresLineFieldKey(0x00, 0x80);
	Check("non-interlace field ignored", state.uField, 0);

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
	state.uObsel = 0;
	state.uField = SnesPPUHiresLineFieldKey(0x00, 0x00);
	Check("non-interlace field toggle keeps key",
		SnesPPUHiresLineCacheKeyMatches(&key, 41, 224, &state), TRUE);
	state.uSetIni = 0x01;
	state.uField = SnesPPUHiresLineFieldKey(0x01, 0x00);
	SnesPPUHiresLineCacheSetKey(&key, 41, 224, &state);
	state.uField = SnesPPUHiresLineFieldKey(0x01, 0x80);
	Check("interlace field preserved", state.uField, 0x80);
	Check("interlace field invalidates",
		SnesPPUHiresLineCacheKeyMatches(&key, 41, 224, &state), FALSE);
}

int main()
{
	TestPacking();
	TestHiresPalette();
	TestLineKey();
	TestHiresLineKey();
	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
