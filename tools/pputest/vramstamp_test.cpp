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
	Uint32 before = stamps.GetBGLineGeneration(0, 1, &info, tiles, 33);
	stamps.Invalidate(0x5400, 1);
	Check(before == stamps.GetBGLineGeneration(0, 1, &info, tiles, 33),
		"unrelated tilemap row preserves BG line");
	stamps.Invalidate(0x5000, 1);
	Check(before != stamps.GetBGLineGeneration(0, 1, &info, tiles, 33),
		"used tilemap block invalidates BG line");

	SnesPPUVramStampT chrStamps;
	before = chrStamps.GetBGLineGeneration(0, 1, &info, tiles, 33);
	chrStamps.Invalidate(0x0100, 1);
	Check(before == chrStamps.GetBGLineGeneration(0, 1, &info, tiles, 33),
		"unreferenced CHR tile preserves BG line");
	chrStamps.Invalidate(0x0000, 1);
	Check(before != chrStamps.GetBGLineGeneration(0, 1, &info, tiles, 33),
		"referenced CHR tile invalidates BG line");

	/* Normal 16x16 Mode 1 can select character +16/+17 without masking the
	   10-bit map number first.  Keep that physical addressing exact. */
	SnesPPUVramStampT largeStamps;
	info.uChrSize = 1;
	for (Uint32 i = 0; i < 33; ++i)
		tiles[i].uTile = 0x400u + 17u;
	before = largeStamps.GetBGLineGeneration(0, 1, &info, tiles, 33);
	Uint32 physical = (info.uChrAddr + (0x400u + 17u) * 16u) & 0x7FFFu;
	largeStamps.Invalidate(physical, 1);
	Check(before != largeStamps.GetBGLineGeneration(0, 1, &info, tiles, 33),
		"Mode 1 large-tile physical CHR address is not prematurely masked");
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
	std::puts(failures ? "vramstamp_test: FAIL" : "vramstamp_test: PASS");
	return failures ? 1 : 0;
}
