#ifndef _SNPPUCHRCACHE_H
#define _SNPPUCHRCACHE_H

#include <string.h>
#include "types.h"

/*
 * Cache fisico de CHR 4bpp exclusivo de OBJ.
 *
 * BG segue sempre o decoder direto: o antigo cache custava mais do que
 * economizava e foi removido, inclusive sua tabela 2bpp. A unica tabela
 * restante cobre os 2048 tiles 4bpp possiveis na VRAM. Cada linha guarda
 * oito indices de cor sem paleta; CGRAM e prioridade nao invalidam pixels.
 * A orientacao canonica e' sem H-flip e o flip acontece somente na saida.
 *
 * Diferente dos caches experimentais de linha, um hit nao le a fonte da
 * VRAM, nao calcula hash e nao disputa uma entrada de 512 slots. Escritas em
 * $2118/$2119 invalidam diretamente os tiles fisicos afetados.
 */

#define SNPPU_CHR4_TILE_WORDS 16u
#define SNPPU_CHR4_TILE_COUNT 2048u
#define SNPPU_VRAM_WORD_MASK  0x7FFFu

struct SnesPPUChrCacheT
{
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
	memset(pCache->uValid4, 0, sizeof(pCache->uValid4));
}

_INLINE Uint32 SnesPPUChrCacheInvalidateRange(SnesPPUChrCacheT *pCache,
	Uint32 uWordAddress, Uint32 nWords)
{
	Uint32 nValidTiles = 0;

	if (!nWords)
		return 0;

	if (nWords >= 0x8000u)
	{
		SnesPPUChrCacheInvalidateAll(pCache);
		return SNPPU_CHR4_TILE_COUNT;
	}

	/* Avanca por limites de tile OBJ 4bpp: no maximo 2049 iteracoes mesmo
	   em uma transferencia que cruza $7FFF->$0000, sem laco por byte. */
	while (nWords)
	{
		Uint32 uAddress = uWordAddress & SNPPU_VRAM_WORD_MASK;
		Uint32 uTile4 = uAddress >> 4;
		Uint32 nStep = 16u - (uAddress & 15u);

		if (pCache->uValid4[uTile4])
		{
			pCache->uValid4[uTile4] = 0;
			nValidTiles++;
		}

		if (nStep > nWords)
			nStep = nWords;
		uWordAddress = (uAddress + nStep) & SNPPU_VRAM_WORD_MASK;
		nWords -= nStep;
	}

	return nValidTiles;
}

/* Implementado em snppurender8.cpp; chamado pelo caminho de escrita da PPU. */
void SnesPPUInvalidateChrCache(Uint32 uWordAddress, Uint32 nWords);

#endif // _SNPPUCHRCACHE_H
