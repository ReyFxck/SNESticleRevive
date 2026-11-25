#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "snppuvramstamp.h"

static int failures;
static void Check(bool ok, const char *label)
{
	if (!ok) { std::printf("FAIL %s\n", label); ++failures; }
}

static void CheckFetchCoverage()
{
	/* Enumerate actual tilemap/CHR addresses independently of the page-stamp
	   implementation, including the +17 normal 16x16 quadrant and VRAM wrap. */
	for (Uint32 mode : { 1u, 5u })
		for (Uint32 depth : { 2u, 4u })
			for (Uint32 size : { 0u, 1u })
				for (Uint32 base : { 0u, 0x1000u, 0x7000u })
				{
					SnesBGInfoT info = {};
					info.uChrAddr = base;
					info.uBitDepth = depth;
					info.uChrSize = size;
					info.uScrAddr = 0x7C00;
					info.uScrSize = 3;
					bool used[0x8000] = {};
					for (Uint32 i = 0; i < 4096; ++i)
						used[(info.uScrAddr + i) & 0x7FFF] = true;
					for (Uint32 tile = 0; tile < 1024; ++tile)
						for (Uint32 quadrant : { 0u, 1u, 16u, 17u })
						{
							Uint32 physical = tile + (size ? quadrant : 0u);
							if (mode == 5) physical &= 1023;
							for (Uint32 word = 0; word < depth * 4; ++word)
								used[(base + physical * depth * 4 + word) & 0x7FFF] = true;
						}
					SnesPPUVramStampT stamps;
					for (Uint32 address = 0; address < 0x8000; ++address)
					{
						Uint32 before = stamps.GetBGGeneration(0, mode, &info);
						stamps.Invalidate(address, 1);
						Uint32 after = stamps.GetBGGeneration(0, mode, &info);
						if (used[address] && after == before)
						{
							Check(false, "every fetched VRAM word changes the stamp");
							return;
						}
					}
				}
}

static void CheckFineBGLineDependencies()
{
	SnesBGInfoT info = {};
	info.uScrAddr = 0x5000;
	info.uScrSize = 0;
	info.uChrAddr = 0x0000;
	info.uBitDepth = 4;
	info.uChrSize = 0;
	SnesRenderTileT tiles[33] = {};
	for (Uint32 i = 0; i < 33; ++i)
		tiles[i].uTile = 0;

	SnesPPUVramStampT stamps;
	Uint32 before = stamps.GetBGLineGeneration(0, 0u, 1u | ((Uint32)info.uChrSize << 7), 1u, &info, tiles, 33u);
	stamps.Invalidate(0x5400, 1);
	Check(before == stamps.GetBGLineGeneration(0, 0u, 1u | ((Uint32)info.uChrSize << 7), 1u, &info, tiles, 33u),
		"unrelated tilemap row preserves BG line");
	stamps.Invalidate(0x5000, 1);
	Check(before != stamps.GetBGLineGeneration(0, 0u, 1u | ((Uint32)info.uChrSize << 7), 1u, &info, tiles, 33u),
		"used tilemap block invalidates BG line");

	SnesPPUVramStampT chrStamps;
	before = chrStamps.GetBGLineGeneration(0, 0u, 1u | ((Uint32)info.uChrSize << 7), 1u, &info, tiles, 33u);
	chrStamps.Invalidate(0x0100, 1);
	Check(before == chrStamps.GetBGLineGeneration(0, 0u, 1u | ((Uint32)info.uChrSize << 7), 1u, &info, tiles, 33u),
		"unreferenced CHR tile preserves BG line");
	chrStamps.Invalidate(0x0000, 1);
	Check(before != chrStamps.GetBGLineGeneration(0, 0u, 1u | ((Uint32)info.uChrSize << 7), 1u, &info, tiles, 33u),
		"referenced CHR tile invalidates BG line");

	/* Normal 16x16 Mode 1 can select character +16/+17 without masking the
	   10-bit map number first.  Keep that physical addressing exact. */
	SnesPPUVramStampT largeStamps;
	info.uChrSize = 1;
	for (Uint32 i = 0; i < 33; ++i)
		tiles[i].uTile = 0x400u + 17u;
	before = largeStamps.GetBGLineGeneration(0, 0u, 1u | ((Uint32)info.uChrSize << 7), 1u, &info, tiles, 33u);
	Uint32 physical = (info.uChrAddr + (0x400u + 17u) * 16u) & 0x7FFFu;
	largeStamps.Invalidate(physical, 1);
	Check(before != largeStamps.GetBGLineGeneration(0, 0u, 1u | ((Uint32)info.uChrSize << 7), 1u, &info, tiles, 33u),
		"Mode 1 large-tile physical CHR address is not prematurely masked");
}

/* Compare the optimized CHR chunk arithmetic against the old per-physical-
   tile formula, with a clean tilemap stamp and arbitrary physical epochs. */
static void CheckFastStampEquivalence()
{
	Uint32 rng = 0x12345678u;
	for (Uint32 mode : { 1u, 5u })
		for (Uint32 depth : { 2u, 4u, 8u })
			for (Uint32 size : { 0u, 1u })
				for (Uint32 base : { 0u, 0x1000u, 0x7000u, 0x7FE0u, 0x7FFFu })
				{
					SnesPPUVramStampT stamps;
					SnesBGInfoT info = {};
					info.uBitDepth = depth;
					info.uChrSize = size;
					info.uChrAddr = base;
					info.uScrAddr = 0x4000u;
					SnesRenderTileT tiles[33] = {};
					for (Uint32 run = 0; run < 32; ++run)
					{
						for (Uint32 k = 0; k < 1024; ++k)
						{
							rng = rng * 1664525u + 1013904223u;
							stamps.uChunks32[k] = rng >> 16;
						}
						stamps.uChunks32[0x4000u >> 5] = 0;
						Uint32 expected = 0;
						Uint32 count = (mode == 5 && size) ? 2u : 1u;
						for (Uint32 k = 0; k < 33; ++k)
						{
							rng = rng * 1664525u + 1013904223u;
							tiles[k].uTile = (Uint16)((rng >> 16) % 1041u);
							if (k && (k % 3u == 0)) tiles[k].uTile = tiles[k-1].uTile;
							for (Uint32 j = 0; j < count; ++j)
							{
								Uint32 tile = count == 2 ? ((tiles[k].uTile + j) & 1023u) : tiles[k].uTile;
								Uint32 chunk = ((base + tile * depth * 4u) & 0x7FFFu) >> 5;
								if (stamps.uChunks32[chunk] > expected) expected = stamps.uChunks32[chunk];
							}
						}
						Check(stamps.GetBGLineGenerationSlow(0, 0, 0, mode,
							&info, tiles, 33u, 0, 0) == expected,
							"fast CHR chunk stamp matches reference");
					}
				}
}

static void CheckMemoConfig()
{
	SnesPPUVramStampT stamps;
	SnesBGInfoT info = {};
	info.uScrAddr = 0x5000;
	info.uBitDepth = 4;
	SnesRenderTileT tiles[33] = {};
	stamps.Invalidate(32, 1);
	Uint32 oldStamp = stamps.GetBGLineGeneration(0, 0, 1, 1, &info, tiles, 33);
	info.uChrSize = 1;
	/* Same world-state bits and epoch, but changed tile-size register. */
	for (Uint32 k = 0; k < 33; ++k) tiles[k].uTile = 2;
	Check(stamps.GetBGLineGeneration(0, 0, 0x81, 1, &info, tiles, 33) > oldStamp,
		"tile-size config must reject old dependency memo");
}

int main()
{
	SnesBGInfoT info = {};
	info.uScrAddr = 0x5000;
	info.uBitDepth = 4;
	SnesPPUVramStampT stamps;
	Uint32 before = stamps.GetBGGeneration(0, 5, &info);
	stamps.Invalidate(0x6000, 128);
	Check(before == stamps.GetBGGeneration(0, 5, &info), "unrelated upload keeps BG valid");
	stamps.Invalidate(0x5000, 1);
	Check(before != stamps.GetBGGeneration(0, 5, &info), "tilemap upload invalidates BG");
	before = stamps.GetBGGeneration(0, 5, &info);
	stamps.Invalidate(0xFFFF, 2);
	Check(before != stamps.GetBGGeneration(0, 5, &info), "physical address wrap reaches CHR");
	Uint32 epoch = stamps.uEpoch;
	stamps.Invalidate(0, 0);
	Check(epoch == stamps.uEpoch, "empty invalidation preserves epoch");
	stamps.Invalidate(0x1234, 0x10000);
	for (Uint32 page = 0; page < 32; ++page)
		Check(stamps.uPages[page] == stamps.uEpoch, "full upload touches every page");
	stamps.uEpoch = 0xFFFFFFFFu;
	Check(stamps.Invalidate(0x6000, 1), "counter wrap requires cache clear");
	Check(stamps.GetBGGeneration(0, 5, &info) == 0, "wrap discards old page epochs");
	info.uChrAddr = 0x6000;
	Check(stamps.GetBGGeneration(0, 5, &info) == 1, "changed dependency region is re-evaluated");
	CheckFetchCoverage();
	CheckFineBGLineDependencies();
	CheckFastStampEquivalence();
	CheckMemoConfig();
	std::puts(failures ? "vramstamp_test: FAIL" : "vramstamp_test: PASS");
	return failures ? 1 : 0;
}
