/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snppuchrcache interface for SNES picture processing.
 */

#ifndef _SNPPUCHRCACHE_H
#define _SNPPUCHRCACHE_H

#include <string.h>
#include "types.h"

/*
 * Cache fisico de CHR 2bpp/4bpp compartilhado por BG e OBJ.
 *
 * O cache e indexado pela posicao fisica na VRAM, nao pelo numero logico do
 * tilemap. Isso permite reutilizar a mesma linha entre BGs, paletas e fases
 * hires sem confundir BGNBA. Escritas em $2118/$2119 invalidam diretamente
 * os tiles 2bpp e 4bpp fisicos afetados.
 */

#define SNPPU_CHR2_TILE_WORDS 8u
#define SNPPU_CHR2_TILE_COUNT 4096u
#define SNPPU_CHR4_TILE_WORDS 16u
#define SNPPU_CHR4_TILE_COUNT 2048u
#define SNPPU_VRAM_WORD_MASK  0x7FFFu

struct SnesPPUChrCacheT
{
	/* Physical VRAM row cache shared by BG and OBJ.  Entries contain only
	   decoded color indices; palette/priority stay outside so the same row
	   survives CGRAM and tilemap changes. */
	Uint64 uData2[SNPPU_CHR2_TILE_COUNT][8];
	Uint8  uOpaque2[SNPPU_CHR2_TILE_COUNT][8];
	Uint8  uValid2[SNPPU_CHR2_TILE_COUNT];

	Uint64 uData4[SNPPU_CHR4_TILE_COUNT][8];
	Uint8  uOpaque4[SNPPU_CHR4_TILE_COUNT][8];
	Uint8  uValid4[SNPPU_CHR4_TILE_COUNT];
};

_INLINE Uint64 SnesPPUChrCacheReverseBytes(Uint64 uData)
{
	uData = ((uData & 0x00FF00FF00FF00FFULL) << 8) |
	        ((uData & 0xFF00FF00FF00FF00ULL) >> 8);
	uData = ((uData & 0x0000FFFF0000FFFFULL) << 16) |
	        ((uData & 0xFFFF0000FFFF0000ULL) >> 16);
	return (uData << 32) | (uData >> 32);
}

_INLINE Uint8 SnesPPUChrCacheReverseMask(Uint8 uMask)
{
	uMask = (Uint8)(((uMask & 0x55u) << 1) | ((uMask & 0xAAu) >> 1));
	uMask = (Uint8)(((uMask & 0x33u) << 2) | ((uMask & 0xCCu) >> 2));
	return (Uint8)((uMask << 4) | (uMask >> 4));
}

_INLINE void SnesPPUChrCacheFlipRow(Uint64 *pData, Uint32 *pOpaque)
{
	*pData = SnesPPUChrCacheReverseBytes(*pData);
	*pOpaque = SnesPPUChrCacheReverseMask((Uint8)*pOpaque);
}

_INLINE Bool SnesPPUChrCacheLookup2(const SnesPPUChrCacheT *pCache,
	Uint32 uRowAddress, Bool bHFlip, Uint64 *pData, Uint32 *pOpaque)
{
	Uint32 uAddress = uRowAddress & SNPPU_VRAM_WORD_MASK;
	Uint32 uTile = uAddress >> 3;
	Uint32 uRow = uAddress & 7u;

	if (!(pCache->uValid2[uTile] & (1u << uRow)))
		return FALSE;

	*pData = pCache->uData2[uTile][uRow];
	*pOpaque = pCache->uOpaque2[uTile][uRow];
	if (bHFlip)
		SnesPPUChrCacheFlipRow(pData, pOpaque);
	return TRUE;
}

_INLINE void SnesPPUChrCacheStore2(SnesPPUChrCacheT *pCache,
	Uint32 uRowAddress, Uint64 uData, Uint32 uOpaque)
{
	Uint32 uAddress = uRowAddress & SNPPU_VRAM_WORD_MASK;
	Uint32 uTile = uAddress >> 3;
	Uint32 uRow = uAddress & 7u;

	pCache->uData2[uTile][uRow] = uData;
	pCache->uOpaque2[uTile][uRow] = (Uint8)uOpaque;
	pCache->uValid2[uTile] |= (Uint8)(1u << uRow);
}

_INLINE Bool SnesPPUChrCacheLookup4(const SnesPPUChrCacheT *pCache,
	Uint32 uRowAddress, Bool bHFlip, Uint64 *pData, Uint32 *pOpaque)
{
	Uint32 uAddress = uRowAddress & SNPPU_VRAM_WORD_MASK;
	Uint32 uTile = uAddress >> 4;
	Uint32 uRow = uAddress & 7u;

	if (!(pCache->uValid4[uTile] & (1u << uRow)))
		return FALSE;

	*pData = pCache->uData4[uTile][uRow];
	*pOpaque = pCache->uOpaque4[uTile][uRow];
	if (bHFlip)
		SnesPPUChrCacheFlipRow(pData, pOpaque);
	return TRUE;
}

_INLINE void SnesPPUChrCacheStore4(SnesPPUChrCacheT *pCache,
	Uint32 uRowAddress, Uint64 uData, Uint32 uOpaque)
{
	Uint32 uAddress = uRowAddress & SNPPU_VRAM_WORD_MASK;
	Uint32 uTile = uAddress >> 4;
	Uint32 uRow = uAddress & 7u;

	pCache->uData4[uTile][uRow] = uData;
	pCache->uOpaque4[uTile][uRow] = (Uint8)uOpaque;
	pCache->uValid4[uTile] |= (Uint8)(1u << uRow);
}

_INLINE void SnesPPUChrCacheInvalidateAll(SnesPPUChrCacheT *pCache)
{
	memset(pCache->uValid2, 0, sizeof(pCache->uValid2));
	memset(pCache->uValid4, 0, sizeof(pCache->uValid4));
}

_INLINE Uint32 SnesPPUChrCacheInvalidateRange(SnesPPUChrCacheT *pCache,
	Uint32 uWordAddress, Uint32 nWords)
{
	Uint32 nValidTiles = 0;
	Uint32 uStart;
	Uint32 nRemain;

	if (!nWords)
		return 0;

	if (nWords >= 0x8000u)
	{
		SnesPPUChrCacheInvalidateAll(pCache);
		return SNPPU_CHR2_TILE_COUNT + SNPPU_CHR4_TILE_COUNT;
	}

	/* One VRAM write can overlap a 2bpp BG tile and a 4bpp BG/OBJ tile.
	   Walk physical tile boundaries instead of words so even a full DMA
	   costs only O(number of touched tiles). */
	uStart = uWordAddress;
	nRemain = nWords;
	while (nRemain)
	{
		Uint32 uAddress = uStart & SNPPU_VRAM_WORD_MASK;
		Uint32 uTile2 = uAddress >> 3;
		Uint32 nStep = 8u - (uAddress & 7u);
		if (pCache->uValid2[uTile2])
		{
			pCache->uValid2[uTile2] = 0;
			nValidTiles++;
		}
		if (nStep > nRemain) nStep = nRemain;
		uStart = (uAddress + nStep) & SNPPU_VRAM_WORD_MASK;
		nRemain -= nStep;
	}

	uStart = uWordAddress;
	nRemain = nWords;
	while (nRemain)
	{
		Uint32 uAddress = uStart & SNPPU_VRAM_WORD_MASK;
		Uint32 uTile4 = uAddress >> 4;
		Uint32 nStep = 16u - (uAddress & 15u);
		if (pCache->uValid4[uTile4])
		{
			pCache->uValid4[uTile4] = 0;
			nValidTiles++;
		}
		if (nStep > nRemain) nStep = nRemain;
		uStart = (uAddress + nStep) & SNPPU_VRAM_WORD_MASK;
		nRemain -= nStep;
	}

	return nValidTiles;
}

/* Implementado em snppurender8.cpp; chamado pelo caminho de escrita da PPU. */
void SnesPPUInvalidateChrCache(Uint32 uWordAddress, Uint32 nWords);

#endif // _SNPPUCHRCACHE_H
