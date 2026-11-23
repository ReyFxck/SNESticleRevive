/* Conservative VRAM dependency stamps for decoded Mode 1/5 BG lines. */
#ifndef _SNPPUVRAMSTAMP_H
#define _SNPPUVRAMSTAMP_H

#include <string.h>
#include "types.h"
#include "snppurender.h"

#ifndef SNPPU_BG_CACHE_ROWMEMO
#define SNPPU_BG_CACHE_ROWMEMO 1
#endif

struct SnesPPUVramStampT
{
	struct RegionT
	{
		Uint32 uEpoch, uKey, uStamp;
	};
#if SNPPU_BG_CACHE_ROWMEMO
	/* Mode 1/5 uses the same tile dependency set for the eight fine-Y
	   scanlines inside one tile row.  Cache the dependency generation while
	   VRAM's epoch is unchanged so hits do not re-walk all 33 tiles. */
	struct LineMemoT
	{
		Uint32 uEpoch;
		Uint32 uState;
		Uint32 uInfo0;
		Uint32 uInfo1;
		Uint32 uStamp;
	};
#endif
	Uint32 uEpoch;
	Uint32 uPages[32]; // 1024 words per page; legacy coarse dependency tests
	Uint32 uBlocks[2048]; // 16 words per block; exact/debug dependency tests
	Uint32 uChunks32[1024]; // 32 words; fast conservative BG-line validation
	RegionT Regions[4];
#if SNPPU_BG_CACHE_ROWMEMO
	LineMemoT LineMemo[4];
#endif

	SnesPPUVramStampT()
	{
		memset(this, 0, sizeof(*this));
		uEpoch = 1;
	}

	/* Returns TRUE on counter wrap, when callers must discard old entries. */
	Bool Invalidate(Uint32 uWordAddress, Uint32 nWords)
	{
		if (!nWords) return FALSE;
		Bool bWrapped = ++uEpoch == 0;
		if (bWrapped)
		{
			memset(uPages, 0, sizeof(uPages));
			memset(uBlocks, 0, sizeof(uBlocks));
			memset(uChunks32, 0, sizeof(uChunks32));
			memset(Regions, 0, sizeof(Regions));
#if SNPPU_BG_CACHE_ROWMEMO
			memset(LineMemo, 0, sizeof(LineMemo));
#endif
			uEpoch = 1;
		}
		if (nWords >= 0x8000u)
		{
			for (Uint32 i = 0; i < 32; ++i) uPages[i] = uEpoch;
			for (Uint32 i = 0; i < 2048; ++i) uBlocks[i] = uEpoch;
			for (Uint32 i = 0; i < 1024; ++i) uChunks32[i] = uEpoch;
			return bWrapped;
		}
		while (nWords)
		{
			Uint32 uAddress = uWordAddress & 0x7FFFu;
			Uint32 nStep = 16u - (uAddress & 15u);
			uPages[uAddress >> 10] = uEpoch;
			uBlocks[uAddress >> 4] = uEpoch;
			uChunks32[uAddress >> 5] = uEpoch;
			if (nStep > nWords) nStep = nWords;
			uWordAddress = (uAddress + nStep) & 0x7FFFu;
			nWords -= nStep;
		}
		return bWrapped;
	}

	Uint32 GetBGGeneration(Uint32 iBG, Uint32 uMode,
		const SnesBGInfoT *pInfo)
	{
		Uint32 uScreenPage = (pInfo->uScrAddr >> 10) & 31u;
		Uint32 nScreenPages = pInfo->uScrSize == 3u ? 4u :
			(pInfo->uScrSize ? 2u : 1u);
		Uint32 uChrPage = (pInfo->uChrAddr >> 10) & 31u;
		Uint32 nChrPages = pInfo->uBitDepth == 2u ? 8u : 16u;
		/* Normal 16x16 fetch can add 17 to the 10-bit tile number.
		   Native-hires fetch masks each physical half back to 10 bits. */
		if (uMode == 1u && pInfo->uChrSize) ++nChrPages;
		Uint32 uKey = uScreenPage | (nScreenPages << 5) |
			(uChrPage << 8) | (nChrPages << 13);
		RegionT *pRegion = &Regions[iBG];
		if (pRegion->uEpoch != uEpoch || pRegion->uKey != uKey)
		{
			Uint32 uStamp = 0;
			for (Uint32 i = 0; i < nScreenPages; ++i)
			{
				Uint32 uPageStamp = uPages[(uScreenPage + i) & 31u];
				if (uPageStamp > uStamp) uStamp = uPageStamp;
			}
			for (Uint32 i = 0; i < nChrPages; ++i)
			{
				Uint32 uPageStamp = uPages[(uChrPage + i) & 31u];
				if (uPageStamp > uStamp) uStamp = uPageStamp;
			}
			pRegion->uEpoch = uEpoch;
			pRegion->uKey = uKey;
			pRegion->uStamp = uStamp;
		}
		return pRegion->uStamp;
	}
	/* Fast conservative dependency generation for the ordinary Mode 1/5 BG
	   line cache.  The previous implementation re-walked every tilemap word
	   plus every physical CHR tile before *every* cache lookup.  On EE that
	   made a 95%+ line-cache hit cost about as much as decoding the row.

	   Tilemap: a cached 33-cell carrier spans at most two physical 32-word
	   tilemap rows, so validate those row-sized chunks directly.

	   CHR: validate 32-word chunks (two 4bpp tiles / four 2bpp tiles).  This
	   is deliberately conservative: a neighbour in the same chunk can cause
	   an extra miss, but can never leave stale pixels.  Consecutive physical
	   tiles in the same chunk are collapsed to one stamp load.

	   The generation remains the maximum write epoch among dependencies.
	   Because every VRAM invalidation advances the global epoch, a later write
	   to any dependency always produces a generation newer than the cached one. */
	Uint32 GetBGLineGeneration(Uint32 iBG, Uint32 uVramState, Uint32 uMode,
		const SnesBGInfoT *pInfo, const SnesRenderTileT *pTiles, Uint32 nTiles)
	{
#if SNPPU_BG_CACHE_ROWMEMO
		/* Fine X/Fine Y select pixels/rows inside the already selected tile
		   set.  Dependency stamps cover complete physical tiles, so neither
		   changes the set that must be validated.  Any VRAM write increments
		   uEpoch, making this memo automatically conservative. */
		const Uint32 uMemoState = uVramState & ~((7u << 16) | (7u << 24));
		const Uint32 uMemoInfo0 = (pInfo->uScrAddr & 0x7FFFu) |
			((pInfo->uChrAddr & 0x7FFFu) << 15);
		const Uint32 uMemoInfo1 = (uMode & 7u) |
			((Uint32)(pInfo->uScrSize & 3u) << 3) |
			((Uint32)(pInfo->uChrSize & 1u) << 5) |
			((Uint32)(pInfo->uBitDepth & 0x0Fu) << 6);
		LineMemoT *pMemo = &LineMemo[iBG & 3u];
		if (pMemo->uEpoch == uEpoch &&
		    pMemo->uState == uMemoState &&
		    pMemo->uInfo0 == uMemoInfo0 &&
		    pMemo->uInfo1 == uMemoInfo1)
			return pMemo->uStamp;
#endif

		static const Uint16 ScrSizeOffset[4][4] =
		{
			{0x0000, 0x0000, 0x0000, 0x0000},
			{0x0000, 0x0400, 0x0000, 0x0400},
			{0x0000, 0x0000, 0x0400, 0x0400},
			{0x0000, 0x0400, 0x0800, 0x0C00}
		};
		Uint32 uStamp = 0;

		/* Map dependencies. FetchBG walks horizontally and jumps to the next
		   32-word screen row at the boundary.  Process whole horizontal spans
		   instead of checking the stamp once per map word. */
		Uint32 uAddr = uVramState & 0x0FFFu;
		Uint32 nMapWords = (uMode == 1u && pInfo->uChrSize)
			? ((nTiles + 1u) >> 1) : nTiles;
		while (nMapWords)
		{
			Uint32 uScreen = (uAddr >> 10) & 3u;
			Uint32 uPhysical = (pInfo->uScrAddr +
				ScrSizeOffset[pInfo->uScrSize & 3u][uScreen] +
				(uAddr & 0x03FFu)) & 0x7FFFu;
			Uint32 uChunkStamp = uChunks32[uPhysical >> 5];
			if (uChunkStamp > uStamp) uStamp = uChunkStamp;

			Uint32 nStep = 32u - (uAddr & 0x1Fu);
			if (nStep > nMapWords) nStep = nMapWords;
			uAddr += nStep;
			nMapWords -= nStep;
			if (!(uAddr & 0x1Fu))
			{
				uAddr -= 32u;
				uAddr ^= (1u << 10);
			}
		}

		/* Physical CHR dependencies. Mode 1/5 BG character bases are aligned
		   and their 2bpp/4bpp tiles cannot cross a 32-word chunk, so the old
		   generic end-chunk test was pure overhead on this path. */
		const Uint32 uTileShift = pInfo->uBitDepth == 2u ? 3u :
			(pInfo->uBitDepth == 4u ? 4u : 5u);
		const Uint32 nPhysicalTiles =
			(uMode == 5u && pInfo->uBitDepth && pInfo->uChrSize) ? 2u : 1u;
		Uint32 uLastChunk = 0xFFFFFFFFu;
		for (Uint32 i = 0; i < nTiles; ++i)
		{
			const Uint32 uTile = pTiles[i].uTile;
			for (Uint32 j = 0; j < nPhysicalTiles; ++j)
			{
				const Uint32 uPhysicalTile = nPhysicalTiles > 1u
					? ((uTile + j) & 0x03FFu) : uTile;
				const Uint32 uPhysical = (pInfo->uChrAddr +
					(uPhysicalTile << uTileShift)) & 0x7FFFu;
				const Uint32 uChunk = uPhysical >> 5;
				if (uChunk != uLastChunk)
				{
					const Uint32 uChunkStamp = uChunks32[uChunk];
					if (uChunkStamp > uStamp) uStamp = uChunkStamp;
					uLastChunk = uChunk;
				}
			}
		}

#if SNPPU_BG_CACHE_ROWMEMO
		pMemo->uEpoch = uEpoch;
		pMemo->uState = uMemoState;
		pMemo->uInfo0 = uMemoInfo0;
		pMemo->uInfo1 = uMemoInfo1;
		pMemo->uStamp = uStamp;
#endif
		return uStamp;
	}


};

#endif
