/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Exercises chrcache test behavior in the pputest regression suite.
 */

#include <cstdio>
#include <cstring>

#include "types.h"
#include "snppuchrcache.h"

static int g_Failures;
static SnesPPUChrCacheT g_Cache;

static void Check(const char *pName, Uint64 uGot, Uint64 uExpected)
{
	if (uGot != uExpected)
	{
		std::printf("FAIL %s: %llX != %llX\n", pName,
			(unsigned long long)uGot, (unsigned long long)uExpected);
		g_Failures++;
	}
}

int main()
{
	Uint64 uData = 0;
	Uint32 uOpaque = 0;
	Uint32 nInvalidated;

	std::memset(&g_Cache, 0, sizeof(g_Cache));
	Check("OBJ CHR cache bytes", sizeof(g_Cache), 149504);
	Check("4bpp cold miss", SnesPPUChrCacheLookup4(&g_Cache,
		0x2345, FALSE, &uData, &uOpaque), FALSE);

	/* A entrada OBJ guarda indices crus; paleta nao faz parte da chave. */
	SnesPPUChrCacheStore4(&g_Cache, 0x2345,
		0x0F0E0D0C0B0A0908ULL, 0x96);
	Check("4bpp OBJ hit", SnesPPUChrCacheLookup4(&g_Cache,
		0x2345, FALSE, &uData, &uOpaque), TRUE);
	Check("4bpp data", uData, 0x0F0E0D0C0B0A0908ULL);
	Check("4bpp hflip hit", SnesPPUChrCacheLookup4(&g_Cache,
		0x2345, TRUE, &uData, &uOpaque), TRUE);
	Check("4bpp hflip data", uData, 0x08090A0B0C0D0E0FULL);
	Check("4bpp hflip opaque", uOpaque, 0x69);

	/* Uma palavra tocada invalida so o tile OBJ fisico correspondente. */
	SnesPPUChrCacheStore4(&g_Cache, 0x3451, 0x22, 2);
	SnesPPUChrCacheStore4(&g_Cache, 0x3461, 0x33, 3);
	nInvalidated = SnesPPUChrCacheInvalidateRange(&g_Cache, 0x3457, 1);
	Check("single write invalidated tiles", nInvalidated, 1);

	{
		SnesPPUBGRowReuseT reuse;
		Uint64 uReuseData = 0;
		Uint32 uReuseMask = 0;
		SnesPPUBGRowReuseReset(&reuse);
		Check("BG row reuse cold", SnesPPUBGRowReuseLookup(&reuse,
			0x8123u, &uReuseData, &uReuseMask), FALSE);
		SnesPPUBGRowReuseStore(&reuse, 0x8123u,
			0x0102030405060708ULL, 0xA5u);
		Check("BG row reuse hit", SnesPPUBGRowReuseLookup(&reuse,
			0x8123u, &uReuseData, &uReuseMask), TRUE);
		Check("BG row reuse data", uReuseData, 0x0102030405060708ULL);
		Check("BG row reuse mask", uReuseMask, 0xA5u);
		Check("BG row reuse flip key distinct", SnesPPUBGRowReuseLookup(&reuse,
			0x0123u, &uReuseData, &uReuseMask), FALSE);
	}
	{
		SnesPPUBGAddrCacheT cache;
		Uint64 uAddrData = 0;
		Uint32 uAddrMask = 0;
		SnesPPUBGAddrCacheReset(&cache);
		Check("BG addr cache bytes", sizeof(cache), 104);
		Check("BG addr cache cold", SnesPPUBGAddrCacheLookup(&cache,
			0x4123u, &uAddrData, &uAddrMask), FALSE);
		SnesPPUBGAddrCacheStore(&cache, 0x4123u,
			0x8877665544332211ULL, 0x96u);
		Check("BG addr cache hit", SnesPPUBGAddrCacheLookup(&cache,
			0x4123u, &uAddrData, &uAddrMask), TRUE);
		Check("BG addr cache data", uAddrData, 0x8877665544332211ULL);
		Check("BG addr cache mask", uAddrMask, 0x96u);
		Check("BG addr cache flip distinct", SnesPPUBGAddrCacheLookup(&cache,
			0xC123u, &uAddrData, &uAddrMask), FALSE);
	}

	Check("single write clears 4bpp", SnesPPUChrCacheLookup4(&g_Cache,
		0x3451, FALSE, &uData, &uOpaque), FALSE);
	Check("single write keeps neighbor", SnesPPUChrCacheLookup4(&g_Cache,
		0x3461, FALSE, &uData, &uOpaque), TRUE);

	/* O burst pode cruzar $7FFF->$0000; os dois lados devem ser limpos. */
	SnesPPUChrCacheStore4(&g_Cache, 0x7FFF, 0x55, 5);
	SnesPPUChrCacheStore4(&g_Cache, 0x0000, 0x77, 7);
	nInvalidated = SnesPPUChrCacheInvalidateRange(&g_Cache, 0x7FFF, 2);
	Check("wrap invalidated tiles", nInvalidated, 2);
	Check("wrap clears high 4bpp", SnesPPUChrCacheLookup4(&g_Cache,
		0x7FFF, FALSE, &uData, &uOpaque), FALSE);
	Check("wrap clears low 4bpp", SnesPPUChrCacheLookup4(&g_Cache,
		0x0000, FALSE, &uData, &uOpaque), FALSE);

	SnesPPUChrCacheStore4(&g_Cache, 0x2222, 0x99, 9);
	nInvalidated = SnesPPUChrCacheInvalidateRange(&g_Cache, 0, 0x8000);
	Check("full clear slot coverage", nInvalidated, SNPPU_CHR4_TILE_COUNT);
	Check("full clear 4bpp", SnesPPUChrCacheLookup4(&g_Cache,
		0x2222, FALSE, &uData, &uOpaque), FALSE);

	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
