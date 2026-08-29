


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "console.h"
#include "snppu.h"
#include "snppurender.h"
#include "snppuchrcache.h"
#include "snppumode7.h"
#include "rendersurface.h"
#include "snmask.h"
#include "prof.h"
#include "sndbglog.h"
#if CODE_PLATFORM == CODE_PS2
#include "ps2mem.h"
#define SNPPU_BG_PLANE_LOOKUP \
	((SnesChrLookupT *)PS2MEM_SNES_LOOKUP_ADDR)
#define SNPPU_BG_HFLIP_LOOKUP \
	((Uint8 (*)[256])(PS2MEM_SNES_LOOKUP_ADDR + \
		sizeof(_SnesPPU_PlaneLookup)))
#else
#define SNPPU_BG_PLANE_LOOKUP (_SnesPPU_PlaneLookup)
#define SNPPU_BG_HFLIP_LOOKUP (_SnesPPU_HFlipLookup)
#endif

#define SNPPU_BGPLANE_SIZE 48
#define SNPPURENDER_CHR64 (TRUE)

static void _FetchMode7(Uint8 *pLine, SnesPPU *pPPU, Int32 iLine, SNMaskT *pPriority, SNMaskT *pOpaque);

#if SNPPU_OBJ_CACHE || SNPPU_BG_CACHE
static SnesPPUChrCacheT _SnesPPU_ChrCache _ALIGN(64);
#endif

void SnesPPUInvalidateChrCache(Uint32 uWordAddress, Uint32 nWords)
{
#if SNPPU_OBJ_CACHE || SNPPU_BG_CACHE
	Uint32 nInvalidated = SnesPPUChrCacheInvalidateRange(
		&_SnesPPU_ChrCache, uWordAddress, nWords);
#if SNDBG_LOG
	g_DbgChrCacheInvalidations += nInvalidated;
#else
	(void)nInvalidated;
#endif
#else
	(void)uWordAddress;
	(void)nWords;
#endif
}

#if  !SNPPURENDER_CHR64

static Uint32 _SnesPPU_Tile2PalLookup[16]=
{
0x00000000 + 0x04040404 * 0,
0x00000000 + 0x04040404 * 1,
0x00000000 + 0x04040404 * 2,
0x00000000 + 0x04040404 * 3,
0x00000000 + 0x04040404 * 4,
0x00000000 + 0x04040404 * 5,
0x00000000 + 0x04040404 * 6,
0x00000000 + 0x04040404 * 7,
0x00000000 + 0x04040404 * 0,
0x00000000 + 0x04040404 * 1,
0x00000000 + 0x04040404 * 2,
0x00000000 + 0x04040404 * 3,
0x00000000 + 0x04040404 * 4,
0x00000000 + 0x04040404 * 5,
0x00000000 + 0x04040404 * 6,
0x00000000 + 0x04040404 * 7,
};



static Uint32 _SnesPPU_Tile4PalLookup[16]=
{
0x00000000 + 0x10101010 * 0,
0x00000000 + 0x10101010 * 1,
0x00000000 + 0x10101010 * 2,
0x00000000 + 0x10101010 * 3,
0x00000000 + 0x10101010 * 4,
0x00000000 + 0x10101010 * 5,
0x00000000 + 0x10101010 * 6,
0x00000000 + 0x10101010 * 7,
0x00000000 + 0x10101010 * 0,
0x00000000 + 0x10101010 * 1,
0x00000000 + 0x10101010 * 2,
0x00000000 + 0x10101010 * 3,
0x00000000 + 0x10101010 * 4,
0x00000000 + 0x10101010 * 5,
0x00000000 + 0x10101010 * 6,
0x00000000 + 0x10101010 * 7
};

#else

static Uint64 _SnesPPU_Tile2PalLookup64[4][16]=
{
	{
		0x00000000 + 0x0404040404040404 * 0,
		0x00000000 + 0x0404040404040404 * 1,
		0x00000000 + 0x0404040404040404 * 2,
		0x00000000 + 0x0404040404040404 * 3,
		0x00000000 + 0x0404040404040404 * 4,
		0x00000000 + 0x0404040404040404 * 5,
		0x00000000 + 0x0404040404040404 * 6,
		0x00000000 + 0x0404040404040404 * 7,
		0x00000000 + 0x0404040404040404 * 0,
		0x00000000 + 0x0404040404040404 * 1,
		0x00000000 + 0x0404040404040404 * 2,
		0x00000000 + 0x0404040404040404 * 3,
		0x00000000 + 0x0404040404040404 * 4,
		0x00000000 + 0x0404040404040404 * 5,
		0x00000000 + 0x0404040404040404 * 6,
		0x00000000 + 0x0404040404040404 * 7,
	},
	{
		0x2020202020202020 + 0x0404040404040404 * 0,
		0x2020202020202020 + 0x0404040404040404 * 1,
		0x2020202020202020 + 0x0404040404040404 * 2,
		0x2020202020202020 + 0x0404040404040404 * 3,
		0x2020202020202020 + 0x0404040404040404 * 4,
		0x2020202020202020 + 0x0404040404040404 * 5,
		0x2020202020202020 + 0x0404040404040404 * 6,
		0x2020202020202020 + 0x0404040404040404 * 7,
		0x2020202020202020 + 0x0404040404040404 * 0,
		0x2020202020202020 + 0x0404040404040404 * 1,
		0x2020202020202020 + 0x0404040404040404 * 2,
		0x2020202020202020 + 0x0404040404040404 * 3,
		0x2020202020202020 + 0x0404040404040404 * 4,
		0x2020202020202020 + 0x0404040404040404 * 5,
		0x2020202020202020 + 0x0404040404040404 * 6,
		0x2020202020202020 + 0x0404040404040404 * 7,
	},
	{
		0x4040404040404040 + 0x0404040404040404 * 0,
		0x4040404040404040 + 0x0404040404040404 * 1,
		0x4040404040404040 + 0x0404040404040404 * 2,
		0x4040404040404040 + 0x0404040404040404 * 3,
		0x4040404040404040 + 0x0404040404040404 * 4,
		0x4040404040404040 + 0x0404040404040404 * 5,
		0x4040404040404040 + 0x0404040404040404 * 6,
		0x4040404040404040 + 0x0404040404040404 * 7,
		0x4040404040404040 + 0x0404040404040404 * 0,
		0x4040404040404040 + 0x0404040404040404 * 1,
		0x4040404040404040 + 0x0404040404040404 * 2,
		0x4040404040404040 + 0x0404040404040404 * 3,
		0x4040404040404040 + 0x0404040404040404 * 4,
		0x4040404040404040 + 0x0404040404040404 * 5,
		0x4040404040404040 + 0x0404040404040404 * 6,
		0x4040404040404040 + 0x0404040404040404 * 7,
	},
	{
		0x6060606060606060 + 0x0404040404040404 * 0,
		0x6060606060606060 + 0x0404040404040404 * 1,
		0x6060606060606060 + 0x0404040404040404 * 2,
		0x6060606060606060 + 0x0404040404040404 * 3,
		0x6060606060606060 + 0x0404040404040404 * 4,
		0x6060606060606060 + 0x0404040404040404 * 5,
		0x6060606060606060 + 0x0404040404040404 * 6,
		0x6060606060606060 + 0x0404040404040404 * 7,
		0x6060606060606060 + 0x0404040404040404 * 0,
		0x6060606060606060 + 0x0404040404040404 * 1,
		0x6060606060606060 + 0x0404040404040404 * 2,
		0x6060606060606060 + 0x0404040404040404 * 3,
		0x6060606060606060 + 0x0404040404040404 * 4,
		0x6060606060606060 + 0x0404040404040404 * 5,
		0x6060606060606060 + 0x0404040404040404 * 6,
		0x6060606060606060 + 0x0404040404040404 * 7,
	},
};



static Uint64 _SnesPPU_Tile4PalLookup64[16]=
{
0x00000000 + 0x1010101010101010 * 0,
0x00000000 + 0x1010101010101010 * 1,
0x00000000 + 0x1010101010101010 * 2,
0x00000000 + 0x1010101010101010 * 3,
0x00000000 + 0x1010101010101010 * 4,
0x00000000 + 0x1010101010101010 * 5,
0x00000000 + 0x1010101010101010 * 6,
0x00000000 + 0x1010101010101010 * 7,
0x00000000 + 0x1010101010101010 * 0,
0x00000000 + 0x1010101010101010 * 1,
0x00000000 + 0x1010101010101010 * 2,
0x00000000 + 0x1010101010101010 * 3,
0x00000000 + 0x1010101010101010 * 4,
0x00000000 + 0x1010101010101010 * 5,
0x00000000 + 0x1010101010101010 * 6,
0x00000000 + 0x1010101010101010 * 7
};

#endif


static Uint32 _SnesPPU_Obj4PalLookup[8]=
{
	0x80808080 + 0x10101010 * 0,
	0x80808080 + 0x10101010 * 1,
	0x80808080 + 0x10101010 * 2,
	0x80808080 + 0x10101010 * 3,
	0x80808080 + 0x10101010 * 4,
	0x80808080 + 0x10101010 * 5,
	0x80808080 + 0x10101010 * 6,
	0x80808080 + 0x10101010 * 7
};




struct SNPPUBg8FlipT
{
	Uint32  uFlipXOR;
	Uint8   *pFlipLookup;
	SnesChrLookupT *pLookup;
	Uint32	pad;
};

static SNPPUBg8FlipT _FlipTable8[4]=
{
	{0, SNPPU_BG_HFLIP_LOOKUP[1], &SNPPU_BG_PLANE_LOOKUP[0]},
	{0, SNPPU_BG_HFLIP_LOOKUP[0], &SNPPU_BG_PLANE_LOOKUP[1]},
	{7, SNPPU_BG_HFLIP_LOOKUP[1], &SNPPU_BG_PLANE_LOOKUP[0]},
	{7, SNPPU_BG_HFLIP_LOOKUP[0], &SNPPU_BG_PLANE_LOOKUP[1]}
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////



static void _MosaicBG8(Uint8 *pLine, Int32 nPixels, Uint32 uMosaic)
{
	Int32 nMosaic;

	PROF_ENTER("MosiacBG8");

	switch (uMosaic)
	{
	case 2:
		while (nPixels > 0)
		{
			Uint8 uData0, uData1, uData2, uData3;
			uData0 = pLine[0];
			uData1 = pLine[2];
			uData2 = pLine[4];
			uData3 = pLine[6];
			pLine[1] = uData0;
			pLine[3] = uData1;
			pLine[5] = uData2;
			pLine[7] = uData3;
			pLine+=8;
			nPixels-=8;
		}
		break;


	default:
		while (nPixels > 0)
		{
			Uint8 uData;

			uData = pLine[0];
			nMosaic = uMosaic;

			while (nPixels > 0 && nMosaic > 0)
			{
				pLine[0] = uData;
				pLine++;
				nPixels--;
				nMosaic--;
			}
		}
		break;

	}

	PROF_LEAVE("MosiacBG8");
}


static void _MosaicBGPlanar(Uint8 *pLine, Int32 nTotalPixels, Uint32 uMosaic)
{
	Int32 nMosaic;
	Int32 nPixels=0;

	PROF_ENTER("MosiacBGPlanar");

	switch (uMosaic)
	{
	case 2:
		while (nTotalPixels > 0)
		{
			Uint8 uData;
			uData = pLine[0];
			uData &= 0x55;
			uData|= uData << 1;
			pLine[0] = uData;
			nTotalPixels -= 8;
			pLine++;
		}
		break;

	default:
		while (nPixels < nTotalPixels)
		{
			Uint8 uData;

			uData = (pLine[nPixels/8]>>(nPixels&7)) & 1;
			nMosaic = uMosaic;

			if (uData)
			{
				while (nPixels < nTotalPixels && nMosaic > 0)
				{
					pLine[nPixels/8] |= 1<<(nPixels&7);
					nPixels++;
					nMosaic--;
				}
			} else
			{
				while (nPixels < nTotalPixels && nMosaic > 0)
				{
					pLine[nPixels/8] &= ~(1<<(nPixels&7));
					nPixels++;
					nMosaic--;
				}
			}
		}
	}
	PROF_LEAVE("MosiacBGPlanar");
}




// render 8


void _ClearLine8(Uint8 *pLine8, Uint8 *pLineP, Int32 nPixels, Uint8 uColor, Uint32 uBGMask)
{
	if (nPixels > 0)
	{
		/* As duas saidas sao planos de bytes. O Makefile desliga a
		   transformacao automatica de loops em memset, entao a chamada
		   explicita usa a rotina otimizada do PS2SDK. */
		memset(pLine8, uColor, (size_t)nPixels);
		memset(pLineP, (Uint8)uBGMask, (size_t)nPixels);
	}
}

void _ClearLine8(Uint8 *pLine8, Int32 nPixels, Uint8 uColor)
{
	if (nPixels > 0)
		memset(pLine8, uColor, (size_t)nPixels);
}

#if !SNPPURENDER_CHR64

static void _FetchCHR2(Uint16 *pVram, Uint32 uBaseAddr, SnesRenderTileT *pTiles, Int32 nTiles, Uint32 uScrollY, Uint8 *pDest, Uint8 *pMask)
{
	SNPPUBg8FlipT *pFlip;

	PROF_ENTER("_FetchCHR2");
	while (nTiles > 0)
	{
		SnesPPUTile2T *pTile2;
		Uint32 uTileAddr;
		Uint32 uPlane0, uPlane1;
		Uint32 uTile0, uTile1;
		Uint32 uMask;
		SnesChrLookupT *pLookup;
		Uint8 *pHFlip;

		pFlip = &_FlipTable8[pTiles->uFlip];

		// calculate tile address
		uTileAddr = (uBaseAddr + pTiles->uTile * 8) & 0x7FFF;

		// get pointer to tile data (y flipped)
		pTile2 = (SnesPPUTile2T *)(pVram + uTileAddr + (uScrollY ^ pFlip->uFlipXOR));

		// get tile plane bits
		uPlane0 = pTile2->uPlane01[0][0];
		uPlane1 = pTile2->uPlane01[0][1];

		pLookup = pFlip->pLookup;
		pHFlip  = pFlip->pFlipLookup;

		uMask = uPlane0 | uPlane1;
		uMask = pHFlip[uMask];

		pMask[ 0] = uMask;
		pMask[SNPPU_BGPLANE_SIZE] = (pTiles->uPal & 8) ? uMask : 0;
		pMask++;

		// get palette bits
		uTile0  =
		uTile1  = _SnesPPU_Tile2PalLookup[pTiles->uPal];

		// decode tile
		uTile0 |= (*pLookup)[uPlane0][0] << 0;
		uTile1 |= (*pLookup)[uPlane0][1] << 0;

		uTile0 |= (*pLookup)[uPlane1][0] << 1;
		uTile1 |= (*pLookup)[uPlane1][1] << 1;

		// store tile data
		((Uint32 *)pDest)[0] = uTile0;
		((Uint32 *)pDest)[1] = uTile1;

		pDest+=8;
		pTiles++;
		nTiles--;
	}
	PROF_LEAVE("_FetchCHR2");

}







static void _FetchCHR4(Uint16 *pVram, Uint32 uBaseAddr, SnesRenderTileT *pTiles, Int32 nTiles, Uint32 uScrollY, Uint8 *pDest, Uint8 *pMask)
{
	SNPPUBg8FlipT *pFlip;

	PROF_ENTER("_FetchCHR4");

	while (nTiles > 0)
	{
		SnesPPUTile4T *pTile4;
		Uint32 uTileAddr;
		Uint32 uPlane0, uPlane1, uPlane2, uPlane3;
		Uint32 uTile0, uTile1;
		Uint32 uMask;
		SnesChrLookupT *pLookup;
		Uint8 *pHFlip;

		pFlip = &_FlipTable8[pTiles->uFlip];

		// calculate tile address
		uTileAddr = (uBaseAddr + pTiles->uTile * 16) & 0x7FFF;

		// get pointer to tile data (y flipped)
		pTile4 = (SnesPPUTile4T *)(pVram + uTileAddr + (uScrollY ^ pFlip->uFlipXOR));

		// get tile plane bits
		uPlane0 = pTile4->uPlane01[0][0];
		uPlane1 = pTile4->uPlane01[0][1];
		uPlane2 = pTile4->uPlane23[0][0];
		uPlane3 = pTile4->uPlane23[0][1];

		pLookup = pFlip->pLookup;
		pHFlip  = pFlip->pFlipLookup;


		// create mask
		uMask = uPlane0 | uPlane1 | uPlane2 | uPlane3;
		uMask = pHFlip[uMask];

		pMask[ 0] = uMask;
		pMask[SNPPU_BGPLANE_SIZE] = (pTiles->uPal & 8) ? uMask : 0;
		pMask++;

		// get palette bits
		uTile0  =
		uTile1  = _SnesPPU_Tile4PalLookup[pTiles->uPal];

		// decode tile
		uTile0 |= (*pLookup)[uPlane0][0] << 0;
		uTile1 |= (*pLookup)[uPlane0][1] << 0;

		uTile0 |= (*pLookup)[uPlane1][0] << 1;
		uTile1 |= (*pLookup)[uPlane1][1] << 1;

		uTile0 |= (*pLookup)[uPlane2][0] << 2;
		uTile1 |= (*pLookup)[uPlane2][1] << 2;

		uTile0 |= (*pLookup)[uPlane3][0] << 3;
		uTile1 |= (*pLookup)[uPlane3][1] << 3;

		// store tile data
		((Uint32 *)pDest)[0] = uTile0;
		((Uint32 *)pDest)[1] = uTile1;

		pDest+=8;
		pTiles++;
		nTiles--;
	}
	PROF_LEAVE("_FetchCHR4");
}




static void _FetchCHR(Uint8 *pLine, SnesPPU *pPPU, SnesBGInfoT *pBGInfo, struct SnesRenderTileT *pTiles, Int32 nTiles, Int32 iLine,Uint8 *pMask)
{
	Uint32 uScrollY;

	if (pBGInfo->uMosaic > 0)
	{
		iLine /= pBGInfo->uMosaic + 1;
		iLine *= pBGInfo->uMosaic + 1;
	}

	uScrollY = pBGInfo->uScrollY + iLine;
	switch (pBGInfo->uBitDepth)
	{
	case 2:
		// fetch chr (2-bit)
		_FetchCHR2(pPPU->GetVramPtr(0), pBGInfo->uChrAddr, pTiles, nTiles, uScrollY & 7, pLine, pMask);
		break;
	case 4:
		// fetch chr (4-bit)
		_FetchCHR4(pPPU->GetVramPtr(0), pBGInfo->uChrAddr, pTiles, nTiles, uScrollY & 7, pLine, pMask);
		break;
	}

	if (pBGInfo->uMosaic > 0)
	{
		_MosaicBG8(pLine + (pBGInfo->uScrollX & 7), 256, pBGInfo->uMosaic + 1);
	}

}


#else


//
//
//


static void _FetchCHR2_64(const Uint16 *pVram, Uint32 uBaseAddr, const SnesRenderTileT *pTiles, Int32 nTiles, Uint32 uScrollY, Uint8 *pDest, Uint8 *pMask, Uint64 *pPalLookup)
{
	const SNPPUBg8FlipT *pFlip;
	#if SNDBG_LOG
	Uint32 uPreviousRowKey = 0xFFFFFFFFu;
	#endif

	PROF_ENTER("_FetchCHR2_64");
	while (nTiles > 0)
	{
		Uint32 uTileAddr, uRowAddr;
		Uint64 uTile0;
		Uint32 uMask;

		pFlip = &_FlipTable8[pTiles->uFlip];

		// calculate tile address
		uTileAddr = (uBaseAddr + pTiles->uTile * 8) & 0x7FFF;

		// get pointer to tile data (y flipped)
		uRowAddr = uTileAddr +
			((uScrollY + pTiles->uOffsetY) ^ pFlip->uFlipXOR);

		#if SNDBG_LOG
		{
			Uint32 uRowKey = uRowAddr | ((pTiles->uFlip & 1u) << 15);
			if (uRowKey == uPreviousRowKey) g_DbgBGChrRepeatRows++;
			uPreviousRowKey = uRowKey;
		}
		#endif

		#if SNPPU_BG_CACHE
		if (SnesPPUChrCacheLookup2(&_SnesPPU_ChrCache, uRowAddr,
			(pTiles->uFlip & 1) != 0, &uTile0, &uMask))
		{
			#if SNDBG_LOG
			g_DbgBGCacheHits++;
			#endif
		}
		else
		{
			const SnesPPUTile2T *pTile2 =
				(const SnesPPUTile2T *)(pVram + uRowAddr);
			const SnesChrLookup64T *pLookup =
				(const SnesChrLookup64T *)&SNPPU_BG_PLANE_LOOKUP[0];
			Uint32 uPlane0 = pTile2->uPlane01[0][0];
			Uint32 uPlane1 = pTile2->uPlane01[0][1];

			#if SNDBG_LOG
			g_DbgBGCacheMisses++;
			#endif
			uMask = SNPPU_BG_HFLIP_LOOKUP[1][uPlane0 | uPlane1];
			uTile0  = (*pLookup)[uPlane0] << 0;
			uTile0 |= (*pLookup)[uPlane1] << 1;
			SnesPPUChrCacheStore2(&_SnesPPU_ChrCache, uRowAddr,
				uTile0, uMask);
			if (pTiles->uFlip & 1)
				SnesPPUChrCacheFlipRow(&uTile0, &uMask);
		}
		#else
		{
			const SnesPPUTile2T *pTile2 =
				(const SnesPPUTile2T *)(pVram + uRowAddr);
			const SnesChrLookup64T *pLookup =
				(const SnesChrLookup64T *)pFlip->pLookup;
			Uint8 *pHFlip = pFlip->pFlipLookup;
			Uint32 uPlane0 = pTile2->uPlane01[0][0];
			Uint32 uPlane1 = pTile2->uPlane01[0][1];

			uMask = pHFlip[uPlane0 | uPlane1];
			uTile0  = (*pLookup)[uPlane0] << 0;
			uTile0 |= (*pLookup)[uPlane1] << 1;
		}
		#endif

		#if SNDBG_LOG
		if (!uMask) g_DbgBGChrBlankRows++;
		#endif

		// A paleta fica fora do cache: a mesma arte serve a qualquer CGRAM.
		uTile0 |= pPalLookup[pTiles->uPal];

		pMask[ 0] = uMask;
		pMask[SNPPU_BGPLANE_SIZE] = (pTiles->uPal & 8) ? uMask : 0;
		pMask++;

		// store tile data
		((Uint64 *)pDest)[0] = uTile0;

		pDest+=8;
		pTiles++;
		nTiles--;
	}
	PROF_LEAVE("_FetchCHR2_64");

}









static void _FetchCHR4_64(const Uint16 *pVram, Uint32 uBaseAddr, const SnesRenderTileT *pTiles, Int32 nTiles, Uint32 uScrollY, Uint8 *pDest, Uint8 *pMask)
{
	const SNPPUBg8FlipT *pFlip;
	#if SNDBG_LOG
	Uint32 uPreviousRowKey = 0xFFFFFFFFu;
	#endif

	PROF_ENTER("_FetchCHR4_64");

	while (nTiles > 0)
	{
		Uint32 uTileAddr, uRowAddr;
		Uint64 uTile0;
		Uint32 uMask;

		pFlip = &_FlipTable8[pTiles->uFlip];

		// calculate tile address
		uTileAddr = (uBaseAddr + pTiles->uTile * 16) & 0x7FFF;

		// get pointer to tile data (y flipped)
		uRowAddr = uTileAddr +
			((uScrollY + pTiles->uOffsetY) ^ pFlip->uFlipXOR);

		#if SNDBG_LOG
		{
			Uint32 uRowKey = uRowAddr | ((pTiles->uFlip & 1u) << 15);
			if (uRowKey == uPreviousRowKey) g_DbgBGChrRepeatRows++;
			uPreviousRowKey = uRowKey;
		}
		#endif

		#if SNPPU_BG_CACHE
		if (SnesPPUChrCacheLookup4(&_SnesPPU_ChrCache, uRowAddr,
			(pTiles->uFlip & 1) != 0, &uTile0, &uMask))
		{
			#if SNDBG_LOG
			g_DbgBGCacheHits++;
			#endif
		}
		else
		{
			const SnesPPUTile4T *pTile4 =
				(const SnesPPUTile4T *)(pVram + uRowAddr);
			const SnesChrLookup64T *pLookup =
				(const SnesChrLookup64T *)&SNPPU_BG_PLANE_LOOKUP[0];
			Uint32 uPlane0 = pTile4->uPlane01[0][0];
			Uint32 uPlane1 = pTile4->uPlane01[0][1];
			Uint32 uPlane2 = pTile4->uPlane23[0][0];
			Uint32 uPlane3 = pTile4->uPlane23[0][1];

			#if SNDBG_LOG
			g_DbgBGCacheMisses++;
			#endif
			uMask = SNPPU_BG_HFLIP_LOOKUP[1]
				[uPlane0 | uPlane1 | uPlane2 | uPlane3];
			uTile0  = (*pLookup)[uPlane0] << 0;
			uTile0 |= (*pLookup)[uPlane1] << 1;
			uTile0 |= (*pLookup)[uPlane2] << 2;
			uTile0 |= (*pLookup)[uPlane3] << 3;
			SnesPPUChrCacheStore4(&_SnesPPU_ChrCache, uRowAddr,
				uTile0, uMask);
			if (pTiles->uFlip & 1)
				SnesPPUChrCacheFlipRow(&uTile0, &uMask);
		}
		#else
		{
			const SnesPPUTile4T *pTile4 =
				(const SnesPPUTile4T *)(pVram + uRowAddr);
			const SnesChrLookup64T *pLookup =
				(const SnesChrLookup64T *)pFlip->pLookup;
			Uint8 *pHFlip = pFlip->pFlipLookup;
			Uint32 uPlane0 = pTile4->uPlane01[0][0];
			Uint32 uPlane1 = pTile4->uPlane01[0][1];
			Uint32 uPlane2 = pTile4->uPlane23[0][0];
			Uint32 uPlane3 = pTile4->uPlane23[0][1];

			uMask = pHFlip[uPlane0 | uPlane1 | uPlane2 | uPlane3];
			uTile0  = (*pLookup)[uPlane0] << 0;
			uTile0 |= (*pLookup)[uPlane1] << 1;
			uTile0 |= (*pLookup)[uPlane2] << 2;
			uTile0 |= (*pLookup)[uPlane3] << 3;
		}
		#endif

		#if SNDBG_LOG
		if (!uMask) g_DbgBGChrBlankRows++;
		#endif

		// Paleta fora da entrada para maximizar o reaproveitamento seguro.
		uTile0 |= _SnesPPU_Tile4PalLookup64[pTiles->uPal];

		// store mask
		pMask[ 0] = uMask;
		pMask[SNPPU_BGPLANE_SIZE] = (pTiles->uPal & 8) ? uMask : 0;
		pMask++;

		// store tile data
		((Uint64 *)pDest)[0] = uTile0;

		pDest+=8;
		pTiles++;
		nTiles--;
	}
	PROF_LEAVE("_FetchCHR4_64");
}


static void _FetchCHR8_64(const Uint16 *pVram, Uint32 uBaseAddr, const SnesRenderTileT *pTiles, Int32 nTiles, Uint32 uScrollY, Uint8 *pDest, Uint8 *pMask)
{
	SNPPUBg8FlipT *pFlip;

	PROF_ENTER("_FetchCHR8_64");

	while (nTiles > 0)
	{
		SnesPPUTile8T *pTile8;
		Uint32 uTileAddr;
		Uint32 uPlane0, uPlane1, uPlane2, uPlane3;
		Uint32 uPlane4, uPlane5, uPlane6, uPlane7;
		Uint64 uTile0;
		Uint32 uMask;
		SnesChrLookup64T *pLookup;
		Uint8 *pHFlip;

		pFlip = &_FlipTable8[pTiles->uFlip];

		// calculate tile address
		uTileAddr = (uBaseAddr + pTiles->uTile * 32) & 0x7FFF;

		// get pointer to tile data (y flipped)
		pTile8 = (SnesPPUTile8T *)(pVram + uTileAddr + ((uScrollY+pTiles->uOffsetY) ^ pFlip->uFlipXOR));

		// get tile plane bits
		uPlane0 = pTile8->uPlane01[0][0];
		uPlane1 = pTile8->uPlane01[0][1];
		uPlane2 = pTile8->uPlane23[0][0];
		uPlane3 = pTile8->uPlane23[0][1];
		uPlane4 = pTile8->uPlane45[0][0];
		uPlane5 = pTile8->uPlane45[0][1];
		uPlane6 = pTile8->uPlane67[0][0];
		uPlane7 = pTile8->uPlane67[0][1];

		pLookup = (SnesChrLookup64T *)pFlip->pLookup;
		pHFlip  = pFlip->pFlipLookup;

		// create mask
		uMask = uPlane0 | uPlane1 | uPlane2 | uPlane3 | uPlane4 | uPlane5 | uPlane6 | uPlane7;
		uMask = pHFlip[uMask];

		// decode tile
		uTile0  = (*pLookup)[uPlane0] << 0;
		uTile0 |= (*pLookup)[uPlane1] << 1;
		uTile0 |= (*pLookup)[uPlane2] << 2;
		uTile0 |= (*pLookup)[uPlane3] << 3;
		uTile0 |= (*pLookup)[uPlane4] << 4;
		uTile0 |= (*pLookup)[uPlane5] << 5;
		uTile0 |= (*pLookup)[uPlane6] << 6;
		uTile0 |= (*pLookup)[uPlane7] << 7;

		pMask[ 0] = uMask;
		pMask[SNPPU_BGPLANE_SIZE] = (pTiles->uPal & 8) ? uMask : 0;
		pMask++;

		// store tile data
		((Uint64 *)pDest)[0] = uTile0;

		pDest+=8;
		pTiles++;
		nTiles--;
	}
	PROF_LEAVE("_FetchCHR8_64");
}






static void _FetchCHR_64(Uint8 *pLine, SnesPPU *pPPU, SnesBGInfoT *pBGInfo, struct SnesRenderTileT *pTiles, Int32 nTiles, Int32 iLine,Uint8 *pMask, Bool bOffset)
{
	Uint32 uScrollY = 0;

	if (pBGInfo->uMosaic > 0)
	{
		iLine /= pBGInfo->uMosaic + 1;
		iLine *= pBGInfo->uMosaic + 1;
	}

	if (!bOffset)
	{
		uScrollY = pBGInfo->uScrollY + iLine;
	}

	switch (pBGInfo->uBitDepth)
	{
	case 2:
		// fetch chr (2-bit)
		_FetchCHR2_64(pPPU->GetVramPtr(0), pBGInfo->uChrAddr, pTiles, nTiles, uScrollY & 7, pLine, pMask, _SnesPPU_Tile2PalLookup64[pBGInfo->uPalBase]);
		break;
	case 4:
		// fetch chr (4-bit)
		_FetchCHR4_64(pPPU->GetVramPtr(0), pBGInfo->uChrAddr, pTiles, nTiles, uScrollY & 7, pLine, pMask);
		break;
	case 8:
		// fetch chr (8-bit)
		_FetchCHR8_64(pPPU->GetVramPtr(0), pBGInfo->uChrAddr, pTiles, nTiles, uScrollY & 7, pLine, pMask);
		break;
	}

	if (pBGInfo->uMosaic > 0)
	{
		_MosaicBG8(pLine + (pBGInfo->uScrollX & 7), 256, pBGInfo->uMosaic + 1);
	}
}

#endif

//#endif


#if CODE_PLATFORM == CODE_PS2



static void _RenderBGData_O(Uint8 *pLine8, Uint8 *pSrc8, SNMaskT *pBGMask, Uint32 uScrollX, Int32 nTiles)
{
	Uint16 *pMaskData;
	SnesChrLookup64T *pLookup64 =
		(SnesChrLookup64T *)&SNPPU_BG_PLANE_LOOKUP[1];

	pSrc8    += (uScrollX & 7);
	pMaskData = (Uint16 *)pBGMask->uMask8;

	__asm__ (
		"mtsab      %0,0     \n"
		: : "r" (pSrc8)
		);

	while (nTiles > 0)
	{
		Uint32 uMask;

		// fetch mask half-worde for 16 pixels
		uMask = *pMaskData;
		pMaskData++;

		// write 0 to output
		__asm__ __volatile__ (
			"sq        $0,0x00(%0)     \n"
			:
			: "r" (pLine8)
			: "memory"
			);

		if (uMask)
		{
			/* GCC 15 r5900 TI-mode split: keep 128-bit value in fixed
			 * MMI register and emit `sq` inside asm block (snmaskop.h
			 * pattern). C-level Uint128 store would only emit 1 sd. */
			if (uMask!=0xFFFF)
			{
				Uint64 uMask0,uMask1;

				uMask0 = (*pLookup64)[uMask & 0xFF];
				uMask1 = (*pLookup64)[uMask >> 8];

				__asm__ __volatile__ (
					"lq         $8, 0x00(%2)     \n"
					"lq         $9, 0x10(%2)     \n"
					"qfsrv      $8, $9, $8       \n"
					"pcpyld     $10, %1, %0      \n"
					"pceqb      $10, $10, $0     \n"
					"por        $8, $8, $10      \n"
					"pxor       $8, $8, $10      \n"
					"sq         $8, 0x00(%3)     \n"
					:
					: "r" (uMask0), "r" (uMask1), "r" (pSrc8), "r" (pLine8)
					: "$8", "$9", "$10", "memory"
					);
			} else
			{
				__asm__ __volatile__ (
					"lq         $8, 0x00(%0)     \n"
					"lq         $9, 0x10(%0)     \n"
					"qfsrv      $8, $9, $8       \n"
					"sq         $8, 0x00(%1)     \n"
					:
					: "r" (pSrc8), "r" (pLine8)
					: "$8", "$9", "memory"
					);
			}
		}

		pSrc8+=16;
		pLine8+=16;
		nTiles-=2;
	}
}

static void _RenderBGData(Uint8 *pLine8, Uint8 *pSrc8, SNMaskT *pBGMask, Uint32 uScrollX, Int32 nTiles)
{
	Uint16 *pMaskData;
	SnesChrLookup64T *pLookup64 =
		(SnesChrLookup64T *)&SNPPU_BG_PLANE_LOOKUP[1];

	pSrc8    += (uScrollX & 7);
	pMaskData = (Uint16 *)pBGMask->uMask8;

    __asm__ (
    	"mtsab      %0,0     \n"
    	: : "r" (pSrc8)
     );

	while (nTiles > 0)
	{
		Uint32 uMask;

		// fetch mask half-worde for 16 pixels
		uMask = *pMaskData;
        pMaskData++;

		if (uMask)
		{
			/* GCC 15 r5900 TI-mode split: see _RenderBGData_O above. */
			if (uMask!=0xFFFF)
			{
                Uint64 uMask0,uMask1;

                uMask0 = (*pLookup64)[uMask & 0xFF];
                uMask1 = (*pLookup64)[uMask >> 8];

    	        __asm__ __volatile__ (
    	            "lq         $8,  0x00(%2)    \n"
    	            "lq         $9,  0x10(%2)    \n"
    	            "qfsrv      $8,  $9, $8      \n"
    	            "lq         $11, 0x00(%3)    \n"
    	            "pcpyld     $10, %1, %0      \n"
    	            "pceqb      $10, $10, $0     \n"
    	            "pand       $11, $11, $10    \n"
    	            "por        $8,  $8, $10     \n"
    	            "pxor       $8,  $8, $10     \n"
    	            "por        $8,  $8, $11     \n"
    	            "sq         $8,  0x00(%3)    \n"
    	            :
    	            : "r" (uMask0), "r" (uMask1), "r" (pSrc8), "r" (pLine8)
    	            : "$8", "$9", "$10", "$11", "memory"
    	         );
			} else
			{
    	        __asm__ __volatile__ (
    	            "lq         $8, 0x00(%0)     \n"
    	            "lq         $9, 0x10(%0)     \n"
    	            "qfsrv      $8, $9, $8       \n"
    	            "sq         $8, 0x00(%1)     \n"
    	            :
    	            : "r" (pSrc8), "r" (pLine8)
    	            : "$8", "$9", "memory"
    	         );
			}
		}

		pSrc8+=16;
		pLine8+=16;
		nTiles-=2;
	}
}


#else

#define RENDERBGPIXEL(_y,_x)\
	if (uMask&(1<<(_x))) pLine8[_x] = (_y) >> (_x*8);

static void _RenderBGData(Uint8 *pLine8, Uint8 *pSrc8, SNMaskT *pBGMask, Uint32 uScrollX, Int32 nTiles)
{
	Uint8 *pMaskData;
	Uint32 uShift, uInvShift;

	//memcpy(pLine8, pSrc8 + (uScrollX &7), 256);

	pSrc8    += (uScrollX & 4);
	uShift    = (uScrollX & 3) << 3;
	uInvShift = 32 - uShift;

	pMaskData = pBGMask->uMask8;
	if (uShift == 0)
	{
		while (nTiles > 0)
		{
			Uint32 uMask;

			// fetch mask byte
			uMask = *pMaskData++;

			if (uMask)
			{
				Uint32 t0,t2;
				t0 = ((Uint32 *)pSrc8)[0];
				t2 = ((Uint32 *)pSrc8)[1];
				pSrc8+=8;

				if (uMask==0xFF)
				{
					((Uint32 *)pLine8)[0] = t0;
					((Uint32 *)pLine8)[1] = t2;
					pLine8+=8;
				} else
				{
					if (uMask&(1<<0)) pLine8[0] = t0 >> (0*8);
					if (uMask&(1<<1)) pLine8[1] = t0 >> (1*8);
					if (uMask&(1<<2)) pLine8[2] = t0 >> (2*8);
					if (uMask&(1<<3)) pLine8[3] = t0 >> (3*8);
					if (uMask&(1<<4)) pLine8[4] = t2 >> (0*8);
					if (uMask&(1<<5)) pLine8[5] = t2 >> (1*8);
					if (uMask&(1<<6)) pLine8[6] = t2 >> (2*8);
					if (uMask&(1<<7)) pLine8[7] = t2 >> (3*8);
					pLine8+=8;
				}
			}  else
			{
				pSrc8+=8;
				pLine8+=8;
			}

			nTiles--;
		}
	} else
	{

		while (nTiles > 0)
		{
			Uint32 uMask;
			Uint32 t0,t1,t2,t3;

			// fetch mask byte
			uMask = *pMaskData++;

			if (uMask)
			{
				t0 = ((Uint32 *)pSrc8)[0];
				t1 =
				t2 = ((Uint32 *)pSrc8)[1];
				t3 = ((Uint32 *)pSrc8)[2];
				pSrc8+=8;

				t0 >>= uShift;
				t1 <<= uInvShift;
				t0|=t1;

				t2 >>= uShift;
				t3 <<= uInvShift;
				t2|=t3;

				if (uMask==0xFF)
				{
					((Uint32 *)pLine8)[0] = t0;
					((Uint32 *)pLine8)[1] = t2; pLine8+=8;
				} else
				{
					if (uMask&(1<<0)) pLine8[0] = t0 >> (0*8);
					if (uMask&(1<<1)) pLine8[1] = t0 >> (1*8);
					if (uMask&(1<<2)) pLine8[2] = t0 >> (2*8);
					if (uMask&(1<<3)) pLine8[3] = t0 >> (3*8);
					if (uMask&(1<<4)) pLine8[4] = t2 >> (0*8);
					if (uMask&(1<<5)) pLine8[5] = t2 >> (1*8);
					if (uMask&(1<<6)) pLine8[6] = t2 >> (2*8);
					if (uMask&(1<<7)) pLine8[7] = t2 >> (3*8);
					pLine8+=8;
				}
			}  else
			{
				pSrc8+=8;
				pLine8+=8;
			}

			nTiles--;
		}
	}
}


static void _RenderBGData_O(Uint8 *pLine8, Uint8 *pSrc8, SNMaskT *pBGMask, Uint32 uScrollX, Int32 nTiles)
{
	memset(pLine8, 0, 256);
	_RenderBGData(pLine8, pSrc8, pBGMask, uScrollX, nTiles);
}


#endif



static void _RenderBG8(Uint8 *pLine8, SNMaskT *pLine, SNMaskT *pBGPlane, SNMaskT *pWindow, Uint32 uBitDepth, SNMaskT *pAddSubMask, Uint8 bAddSubMask, SNMaskT *pBGPri, SNMaskT *pExtraMask, Uint32 uPriority, Bool &bRendered, Uint32 uScrollX)
{
	if (uBitDepth!=0)
	{
		SNMaskT BGMask;		// bits of BG to render
		SNMaskT BGPri;      // bits of BG with high priority

		if (!pBGPri) pBGPri = &BGPri;

		// BGMask = mask & ~window
		// BGMask are the opaque pixels of the background that are not within the clip window
		if (pWindow)
		{
			SNMaskANDN(&BGMask, &pBGPlane[SNPPU_BGPLANE_OPAQUE], pWindow);
		} else
		{
			SNMaskCopy(&BGMask, &pBGPlane[SNPPU_BGPLANE_OPAQUE]);
		}

		if (pExtraMask)
		{
			// hide bg pixels
			SNMaskANDN(&BGMask, &BGMask, pExtraMask);
		}

		// BGPri  = mask  & ~window & bghipriority
		// these are the high priority pixels that need to be rendered
		// they overwrite the lo and hi priority pixels that have already been rendered
		SNMaskAND(pBGPri, &BGMask, &pBGPlane[SNPPU_BGPLANE_PRI]);

		switch (uPriority)
		{
		case 0:
			// lo = 00
			// hi = 01
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER0], pBGPri, true);
			break;
		case 1:
			// lo = 00 (masked behind bg3hi unless bg2hi)
			SNMaskANDN(&BGMask, &BGMask, &pLine[SNPPU_BGPLANE_LAYER0]);
			SNMaskOR(&BGMask, &BGMask, pBGPri);

			// lo = 00
			// hi = 01
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER0], pBGPri, true);
			break;
		case 2:
			// lo = 10
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER0], &BGMask, false);
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER1], &BGMask, true);
			// hi = 11
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER0], pBGPri, true);
			break;
		case 3:
			{
				SNMaskT temp;
				// temp = bg2hi
				SNMaskAND(&temp, &pLine[SNPPU_BGPLANE_LAYER0], &pLine[SNPPU_BGPLANE_LAYER1]);
				// lo = 00 (masked behind bg2hi unless bg1hi)
				SNMaskANDN(&BGMask, &BGMask, &temp);
				SNMaskOR(&BGMask, &BGMask, pBGPri);
			}

			// lo = 10
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER1], &BGMask, true);
			// hi = 11
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER0], &BGMask, false);
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER0], pBGPri, true);
			break;

		case 4:
			// lo = 00
			// hi = 10
			SNMaskClear(&pLine[SNPPU_BGPLANE_LAYER0]);
			SNMaskCopy(&pLine[SNPPU_BGPLANE_LAYER1], pBGPri);
			break;

		case 5:
			// lo = 01
			SNMaskANDN(&BGMask, &BGMask, &pLine[SNPPU_BGPLANE_LAYER1]);
			SNMaskOR(&BGMask, &BGMask, pBGPri);

			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER0], &BGMask, true);

			// hi = 11
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER1], &BGMask, false);
			SNMaskBool(&pLine[SNPPU_BGPLANE_LAYER1], pBGPri, true);
			break;

		case 7:
			// lo = 10
			//SNMaskSet(&pLine[SNPPU_BGPLANE_LAYER0]);
			//SNMaskClear(&pLine[SNPPU_BGPLANE_LAYER1]);
			//SNMaskClear(&pLine[SNPPU_BGPLANE_LAYER0]);
			//SNMaskCopy(&pLine[SNPPU_BGPLANE_LAYER1], pBGPri);
//			SNMaskCopy(&pLine[SNPPU_BGPLANE_LAYER0], &BGMask);

	// contra 3 = happy
			SNMaskClear(&pLine[SNPPU_BGPLANE_LAYER0]);
			SNMaskCopy(&pLine[SNPPU_BGPLANE_LAYER1], pBGPri);
	// bgpri = 0 sadv
//			SNMaskCopy(&pLine[SNPPU_BGPLANE_LAYER0], &BGMask);
//			SNMaskClear(&pLine[SNPPU_BGPLANE_LAYER1]);
			break;
		}

		if (pAddSubMask)
		{
			// set or reset bits of AddSubMask based on pixels that were rendered
			SNMaskBool(pAddSubMask, &BGMask, bAddSubMask ? true : false);
		}


		if (!bRendered)
		{
			// render it
			PROF_ENTER("_RenderBGData_O");
			_RenderBGData_O(pLine8, (Uint8 *)pBGPlane, &BGMask, uScrollX, 32);
			PROF_LEAVE("_RenderBGData_O");
			bRendered = TRUE;
		} else
		{
			// render it
			PROF_ENTER("_RenderBGData");
			_RenderBGData(pLine8, (Uint8 *)pBGPlane, &BGMask, uScrollX, 32);
			PROF_LEAVE("_RenderBGData");
		}

	}
}



static _INLINE void _DecodeOBJRow4(SnesChrLookupT *pLookup, Uint8 *pHFlip,
	Uint32 uPlane0, Uint32 uPlane1, Uint32 uPlane2, Uint32 uPlane3,
	Uint32 *pTile0, Uint32 *pTile1, Uint32 *pOpaque)
{
	Uint32 uTile0 = (*pLookup)[uPlane0][0] << 0;
	Uint32 uTile1 = (*pLookup)[uPlane0][1] << 0;

	uTile0 |= (*pLookup)[uPlane1][0] << 1;
	uTile1 |= (*pLookup)[uPlane1][1] << 1;
	uTile0 |= (*pLookup)[uPlane2][0] << 2;
	uTile1 |= (*pLookup)[uPlane2][1] << 2;
	uTile0 |= (*pLookup)[uPlane3][0] << 3;
	uTile1 |= (*pLookup)[uPlane3][1] << 3;

	*pTile0 = uTile0;
	*pTile1 = uTile1;
	*pOpaque = pHFlip[uPlane0 | uPlane1 | uPlane2 | uPlane3];
}


static Int32 _FetchOBJ(SnesRenderObjT *pObjBase, Uint8 *pObjList, Int32 nObjList, SnesRenderObj8T *pObjLine, Int32 MaxObj8Line, Int32 iLine, Uint32 uBaseAddr, Uint32 uNameSelect, Uint16 *pVram)
{
	Int32 nObjLine = 0;
#if SNDBG_LOG && SNPPU_OBJ_CACHE
	Uint32 uCacheHits = 0;
	Uint32 uCacheMisses = 0;
#endif
#if SNDBG_DEEP
	Bool bTrace = g_DbgCaptureActive &&
		(iLine == 112 || iLine == 160);
	if (bTrace)
		DLog("[snes-obj-trace] f=%u line=%d selected=%d base/name=%04X/%04X",
			(unsigned)g_DbgCaptureFrameNo, (int)iLine, (int)nObjList,
			(unsigned)uBaseAddr, (unsigned)uNameSelect);
#endif

	// A busca reversa e' intencional: o PPU primeiro seleciona ate' 32 OBJ
	// na ordem iniciada por OAMPRI e depois busca ate' 34 tiles na ordem
	// inversa. _RenderOBJ8 percorre o resultado de volta para preservar a
	// prioridade. Nao inverter este laco como "correcao" de time-over.
	while (--nObjList >= 0)
	{
		SnesRenderObjT *pObj;
		Int32 ObjX, ObjY;
		Uint32 uSize;

        // get pointer to obj
		pObj = pObjBase + pObjList[nObjList];

		uSize = pObj->uWidth;

		// get obj position
		ObjX = pObj->uPosX;
		ObjX<<=32-9;
		ObjX>>=32-9;
		ObjY = iLine - pObj->uPosY;
		ObjY^= pObj->uVXOR;
		ObjY&= pObj->uHeight - 1;

		Uint32 uTileAddr;
		Uint32 uTile0, uTile1, uOpaque;
#if !SNPPU_OBJ_CACHE
		SnesChrLookupT *pLookup;
		Uint8 *pHFlip;
#endif
		Uint32 uTile;
		const Uint32 uPalette = _SnesPPU_Obj4PalLookup[pObj->uPal];

#if !SNPPU_OBJ_CACHE
		if (pObj->bHFlip)
		{
			pLookup = &SNPPU_BG_PLANE_LOOKUP[1];
			pHFlip  = SNPPU_BG_HFLIP_LOOKUP[0];
		} else
		{
			pLookup = &SNPPU_BG_PLANE_LOOKUP[0];
			pHFlip  = SNPPU_BG_HFLIP_LOOKUP[1];
		}
#endif

		// SNES OBJ: dentro de um sprite, a COLUNA (nibble baixo) e a LINHA
		// (nibble alto) do numero do tile avancam SEPARADAMENTE, com wrap
		// dentro da tabela de 256 tiles. O bit 8 seleciona a segunda tabela,
		// cujo deslocamento completo vem de OBSEL; ele nao faz parte de uTile.
		// Ex.: um 16x16 no tile $0F usa $0F,$00,$1F,$10. O codigo antigo
		// somava linear (+chry*0x10 e pTile4++), estourando os nibbles e
		// embaralhando sprites grandes que cruzam essas fronteiras (bug
		// classico em Final Fight 2 e afins).
		{
		Bool bSecondTable = (pObj->uTile & 0x100) != 0;
		Uint32 uRow  = ((pObj->uTile >> 4) + (ObjY >> 3)) & 0x0F;
		Uint32 uCol0 = pObj->uTile & 0x0F;
		Uint32 uYoff = ObjY & 7;
		Int32 iTileX;
		Int32 nTileCount;
		Int32 iCol;
		Int32 iColStep;

#if SNDBG_DEEP
		if (bTrace)
		{
			Uint32 uTraceHash = 2166136261u;
			Uint32 uFirstAddr = 0;
			Uint32 uLastAddr = 0;
			Int32 iTraceTile;

			for (iTraceTile = 0; iTraceTile < (pObj->uWidth >> 3);
			     iTraceTile++)
			{
				Int32 iSource = _SnesPPUOBJSourceColumn(iTraceTile,
					pObj->uWidth, pObj->bHFlip);
				Uint32 uTraceTile = (uRow << 4) |
					((uCol0 + iSource) & 0x0F);
				Uint32 uTraceAddr = uBaseAddr + uTraceTile * 16;
				Uint32 uWord0;
				Uint32 uWord8;

				if (bSecondTable)
					uTraceAddr += uNameSelect;
				uTraceAddr = (uTraceAddr & 0x7FFF) + uYoff;
				if (!iTraceTile)
					uFirstAddr = uTraceAddr;
				uLastAddr = uTraceAddr;
				uWord0 = pVram[uTraceAddr];
				uWord8 = pVram[uTraceAddr + 8];
				uTraceHash = (uTraceHash ^ uWord0) * 16777619u;
				uTraceHash = (uTraceHash ^ uWord8) * 16777619u;
			}
			DLog("[snes-obj-trace] f=%u line=%d idx=%u xy=%d/%u wh=%u/%u tile/row/yoff=%03X/%X/%u pal/pri/h/vx=%u/%u/%u/%u addr=%04X..%04X vhash=%08X",
				(unsigned)g_DbgCaptureFrameNo, (int)iLine,
				(unsigned)pObjList[nObjList], (int)ObjX,
				(unsigned)pObj->uPosY, (unsigned)pObj->uWidth,
				(unsigned)pObj->uHeight, (unsigned)pObj->uTile,
				(unsigned)uRow, (unsigned)uYoff, (unsigned)pObj->uPal,
				(unsigned)pObj->uPri, (unsigned)pObj->bHFlip,
				(unsigned)pObj->uVXOR, (unsigned)uFirstAddr,
				(unsigned)uLastAddr, (unsigned)uTraceHash);
		}
#endif
		_SnesPPUOBJCountedTileRange(pObj->uPosX, ObjX, pObj->uWidth,
			&iTileX, &nTileCount);
		ObjX += iTileX << 3;
		uSize = nTileCount << 3;
		if (pObj->bHFlip)
		{
			iCol = (pObj->uWidth >> 3) - 1 - iTileX;
			iColStep = -1;
		} else
		{
			iCol = iTileX;
			iColStep = 1;
		}

		while (uSize > 0)
		{
			uTile = (uRow << 4) | ((uCol0 + iCol) & 0x0F);
			uTileAddr = uBaseAddr + uTile * 16;
			if (bSecondTable)
				uTileAddr += uNameSelect;
			Uint32 uRowAddr = (uTileAddr & 0x7FFF) + uYoff;

#if SNPPU_OBJ_CACHE
			{
				Uint64 uRowData;

				if (SnesPPUChrCacheLookup4(&_SnesPPU_ChrCache,
					uRowAddr, pObj->bHFlip, &uRowData, &uOpaque))
				{
#if SNDBG_LOG
					uCacheHits++;
#endif
				}
				else
				{
					const SnesPPUTile4T *pTile4 =
						(const SnesPPUTile4T *)(pVram + uRowAddr);
					Uint32 uPlane0 = pTile4->uPlane01[0][0];
					Uint32 uPlane1 = pTile4->uPlane01[0][1];
					Uint32 uPlane2 = pTile4->uPlane23[0][0];
					Uint32 uPlane3 = pTile4->uPlane23[0][1];
#if SNDBG_LOG
					uCacheMisses++;
#endif
					_DecodeOBJRow4(&SNPPU_BG_PLANE_LOOKUP[0],
						SNPPU_BG_HFLIP_LOOKUP[1], uPlane0, uPlane1,
						uPlane2, uPlane3, &uTile0, &uTile1, &uOpaque);
					uRowData = (Uint64)uTile0 | ((Uint64)uTile1 << 32);
					SnesPPUChrCacheStore4(&_SnesPPU_ChrCache,
						uRowAddr, uRowData, uOpaque);
					if (pObj->bHFlip)
						SnesPPUChrCacheFlipRow(&uRowData, &uOpaque);
				}
				uTile0 = (Uint32)uRowData;
				uTile1 = (Uint32)(uRowData >> 32);
			}
#else
			{
				const SnesPPUTile4T *pTile4 =
					(const SnesPPUTile4T *)(pVram + uRowAddr);
				Uint32 uPlane0 = pTile4->uPlane01[0][0];
				Uint32 uPlane1 = pTile4->uPlane01[0][1];
				Uint32 uPlane2 = pTile4->uPlane23[0][0];
				Uint32 uPlane3 = pTile4->uPlane23[0][1];
				_DecodeOBJRow4(pLookup, pHFlip, uPlane0, uPlane1,
					uPlane2, uPlane3, &uTile0, &uTile1, &uOpaque);
			}
#endif
			uTile0 |= uPalette;
			uTile1 |= uPalette;

			// store tile data
			((Uint32 *)pObjLine->uData)[0] = uTile0;
			((Uint32 *)pObjLine->uData)[1] = uTile1;
			pObjLine->uData[SNPPU_BGPLANE_OPAQUE] = (Uint8)uOpaque;

			// store objline
			pObjLine->uPri  = pObj->uPri;
			pObjLine->uPal  = pObj->uPal;
			pObjLine->iPosX = ObjX;
			pObjLine++;
			nObjLine++;
			if (nObjLine >= MaxObj8Line) goto FetchOBJDone;

			/* OBJ fetch always advances left-to-right. H-flip mirrors the
			   source column, not the fetch position; this matters when the
			   hardware stops at the 34-tile scanline limit. */
			ObjX += 8;
			uSize -= 8;
			iCol += iColStep;
		}
		}

		// next obj
//		pObjList++;
//		nObjList--;
	}

FetchOBJDone:
#if SNDBG_LOG && SNPPU_OBJ_CACHE
	g_DbgObjCacheHits += uCacheHits;
	g_DbgObjCacheMisses += uCacheMisses;
#endif
	return nObjLine;
}

static void _ClearLinePlanar(SNMaskT *pPlanes, Int32 nPlanes)
{
	Int32 iPlane;
	for (iPlane=0; iPlane < nPlanes; iPlane++)
	{
		SNMaskClear(&pPlanes[iPlane]);
	}
}



void SnesPPURender::RenderLine8(Int32 iLine, SnesRender8pInfoT *pRenderInfo)
{
	SNMaskT BG3Pri;
	Bool bBG3Pri;
	SnesBGInfoT	BGInfo[4];
	SnesRenderObj8T ObjLine[SNPPU_MAXOBJCHR];
	Int32 nObjLine;
	const SnesPPURegsT *pRegs = m_pPPU->GetRegs();
	const Uint8 uBGMode = pRegs->bgmode & 7;
	Uint8 tm = pRegs->tm & _tm;
	Uint8 tmw = pRegs->tmw & _tmw;
	Uint32 cgadsub =  (pRegs->cgadsub & 0x3F);
	Uint8 ts = (pRegs->ts & _ts);
	Uint8 tsw = pRegs->tsw & _tsw;
	Uint8 uFetchLayers;
	Bool bRendered;

	SNMaskT *pMain = pRenderInfo->Main;
	SNMaskT *pSub = pRenderInfo->Sub;
	SNMaskT *pBGWindow = pRenderInfo->BGWindow;
    SNMaskT *pMainAddSubMask = &pRenderInfo->MainAddSubMask;
	SNMaskT *pSubAddSubMask = &pRenderInfo->SubAddSubMask;
    Uint8  *pMain8 = pRenderInfo->BlendInfo.uMain8;
    Uint8  *pSub8 = pRenderInfo->BlendInfo.uSub8;

	/* Fixed-color math never samples the sub screen.  With no CGADSUB target,
	   color math cannot affect a pixel at all.  Apply both facts before
	   tile/OBJ fetch so disabled layers do no invisible EE work. */
	if (!(pRegs->cgwsel & 0x02) || cgadsub == 0)
		ts = 0;
	uFetchLayers = tm | ts;

#if SNDBG_LOG
	g_DbgBGActiveLayers +=
		((uFetchLayers & SNESPPU_MASK_BG1) ? 1u : 0u) +
		((uFetchLayers & SNESPPU_MASK_BG2) ? 1u : 0u) +
		((uFetchLayers & SNESPPU_MASK_BG3) ? 1u : 0u) +
		((uFetchLayers & SNESPPU_MASK_BG4) ? 1u : 0u);
	Uint32 _tBGInfo = ProfCtrGetCycle();
#endif

	PROF_ENTER("DecodeBGInfo");
	if (uBGMode == 7)
	{
		/* _FetchMode7 le os registradores diretamente. Depois da busca, o
		   compositor generico usa somente estes campos de BG1. */
		BGInfo[0].uBitDepth = 8;
		BGInfo[0].uScrollX = 0;
		BGInfo[0].Priority = 7;
		BGInfo[1].uBitDepth = 0;
		BGInfo[2].uBitDepth = 0;
		BGInfo[3].uBitDepth = 0;
	}
	else
	{
		DecodeBGInfo(BGInfo);
	}
	PROF_LEAVE("DecodeBGInfo");
#if SNDBG_LOG
	g_TmgCycBGInfo += ProfCtrGetCycle() - _tBGInfo;
#endif

	// fetch obj chr for visible objs
	PROF_ENTER("FetchOBJ");
#if SNDBG_LOG
	Uint32 _tObjA = ProfCtrGetCycle();
#endif
	if (uFetchLayers & SNESPPU_MASK_OBJ)
		nObjLine = _FetchOBJ(m_Objs, m_ObjLine[iLine], m_nObjLine[iLine],
			ObjLine, SNPPU_MAXOBJCHR, iLine, (pRegs->obsel & 7) << 13,
			_SnesPPUOBJNameSelect(pRegs->obsel), m_pPPU->GetVramPtr(0));
	else
		nObjLine = 0;
#if SNDBG_LOG
	{
		Uint32 _dObjFetch = ProfCtrGetCycle() - _tObjA;
		g_TmgCycObj += _dObjFetch;
		g_TmgCycObjFetch += _dObjFetch;
	}
	{
		Bool _objEnabled = ((tm | ts) & SNESPPU_MASK_OBJ) != 0;
		#if SNDBG_DEEP
		Int32 _i;
		Uint32 _opaque = 0;
		for (_i = 0; _i < nObjLine; _i++)
			if (ObjLine[_i].uData[SNPPU_BGPLANE_OPAQUE] != 0) _opaque++;
		#endif

		if (_objEnabled) g_DbgObjEnabledLines++;
		g_DbgObjOamRefs += m_nObjLine[iLine];
		g_DbgObjTiles += nObjLine;
		#if SNDBG_DEEP
		g_DbgObjOpaqueTiles += _opaque;
		if (_objEnabled && nObjLine > 0 && _opaque == 0) g_DbgObjEmptyLines++;
		#endif
		if (m_nObjLine[iLine] >= SNPPU_MAXOBJ) g_DbgObjRangeLimitLines++;
		if (nObjLine >= SNPPU_MAXOBJCHR) g_DbgObjLimitLines++;
		g_DbgObjOBSEL = pRegs->obsel;
		g_DbgObjTM = pRegs->tm;
		g_DbgObjTS = pRegs->ts;
		g_DbgObjPriority = pRegs->oampri.w;
	}
#endif
	PROF_LEAVE("FetchOBJ");

	if ((pRegs->bgmode&7)!=7)
	{
		Int32 iBG;
		Uint8 uBGFlags[4];
		Uint16 *pOffset = NULL;
		Uint32 uOffsetOR = 0;

		if (((pRegs->bgmode&7)==2 || (pRegs->bgmode&7)==4) &&
		    (uFetchLayers & (SNESPPU_MASK_BG1 | SNESPPU_MASK_BG2)))
		{
			pOffset = pRenderInfo->BGOffset;

			// fetch offsets
#if SNDBG_LOG
			Uint32 _tBGOffset = ProfCtrGetCycle();
#endif
			uOffsetOR  = FetchOffset(&BGInfo[2], pOffset, iLine, pRenderInfo->uBGVramAddr[2], (pRegs->bgmode&7)==2 ? TRUE : FALSE);
#if SNDBG_LOG
			g_TmgCycBGOffset += ProfCtrGetCycle() - _tBGOffset;
#endif
		}

#if SNDBG_LOG
		Uint32 _tBGMap = ProfCtrGetCycle();
#endif
		for (iBG=0; iBG <= 3; iBG++)
		{
			if (!(uFetchLayers & (1 << iBG)))
			{
				uBGFlags[iBG] = 0;
				continue;
			}

			// is offset enabled for this BG layer?
			if (uOffsetOR & (0x2000 << iBG))
			{
				// fetch BGline with offset
				PROF_ENTER("FetchBGOffset");
				uBGFlags[iBG] = FetchBGOffset(&BGInfo[iBG], pRenderInfo->Tiles[iBG], 33, iLine, pOffset, (0x2000 << iBG), ((pRegs->bgmode&7)==4 ? TRUE : FALSE));
				PROF_LEAVE("FetchBGOffset");

				// invalidate cache
				pRenderInfo->uBGVramAddr[iBG] = 0xFFFFFFFF;
			} else
			{
				// fetch line without offset
				PROF_ENTER("FetchBG");
				uBGFlags[iBG] = FetchBG(&BGInfo[iBG], pRenderInfo->Tiles[iBG], 33, iLine, pRenderInfo->uBGVramAddr[iBG]);
				PROF_LEAVE("FetchBG");
			}
		}
#if SNDBG_LOG
		g_TmgCycBGMap += ProfCtrGetCycle() - _tBGMap;
		Uint32 _tBGChr = ProfCtrGetCycle();
#endif

		PROF_ENTER("BGCHR");
		for (iBG=0; iBG <= 3; iBG++)
		{
			if (uBGFlags[iBG] & SNPPU_BGFLAGS_FETCHCHR)
			{
				Uint8 TempMask[2][SNPPU_BGPLANE_SIZE];
#if SNDBG_LOG
				g_DbgBGChrRows += 33;
#endif

				// fetch bg tile data
				#if SNPPURENDER_CHR64
				_FetchCHR_64((Uint8 *)pRenderInfo->BGPlanes[iBG], m_pPPU, &BGInfo[iBG], pRenderInfo->Tiles[iBG], 33, iLine, TempMask[0], (uBGFlags[iBG]&SNPPU_BGFLAGS_OFFSET));
				#else
				_FetchCHR((Uint8 *)pRenderInfo->BGPlanes[iBG], m_pPPU, &BGInfo[iBG], pRenderInfo->Tiles[iBG], 33, iLine, TempMask[0], (uBGFlags[iBG]&SNPPU_BGFLAGS_OFFSET));
				#endif

				// shift mask based on h-scroll of BG
				SNMaskSHL(&pRenderInfo->BGPlanes[iBG][SNPPU_BGPLANE_OPAQUE], TempMask[0], (BGInfo[iBG].uScrollX & 7));
				SNMaskSHL(&pRenderInfo->BGPlanes[iBG][SNPPU_BGPLANE_PRI], TempMask[1], (BGInfo[iBG].uScrollX & 7));

				if (BGInfo[iBG].uMosaic > 0)
				{
					_MosaicBGPlanar((Uint8 *)&pRenderInfo->BGPlanes[iBG][SNPPU_BGPLANE_OPAQUE], 256, BGInfo[iBG].uMosaic + 1);
					_MosaicBGPlanar((Uint8 *)&pRenderInfo->BGPlanes[iBG][SNPPU_BGPLANE_PRI], 256, BGInfo[iBG].uMosaic + 1);
				}
			}
		}
		PROF_LEAVE("BGCHR");
#if SNDBG_LOG
		g_TmgCycBGChr += ProfCtrGetCycle() - _tBGChr;
#endif
	} else
	{
		// mode 7
		PROF_ENTER("BGMODE7");
#if SNDBG_LOG
		Uint32 _tM7 = ProfCtrGetCycle();
#endif
		if (uFetchLayers & (SNESPPU_MASK_BG1 | SNESPPU_MASK_BG2))
			_FetchMode7((Uint8 *)pRenderInfo->BGPlanes[0], m_pPPU, iLine,
				&pRenderInfo->BGPlanes[0][SNPPU_BGPLANE_PRI],
				&pRenderInfo->BGPlanes[0][SNPPU_BGPLANE_OPAQUE]);
#if SNDBG_LOG
		g_TmgCycM7 += ProfCtrGetCycle() - _tM7;
#endif
		PROF_LEAVE("BGMODE7");

		// only draw BG 1
		if (tm & 0x3)
		{
			tm&=~0xF;
			tm|=SNESPPU_MASK_BG1;
		}

		if (ts & 0x3)
		{
			ts&=~0xF;
			ts|=SNESPPU_MASK_BG1;
		}
	}

	PROF_ENTER("RenderBG");
#if SNDBG_LOG
	Uint32 _tBGMain = ProfCtrGetCycle();
#endif

	if (cgadsub & 0x20)
	{
		SNMaskSet(pMainAddSubMask);
	} else
	{
		SNMaskClear(pMainAddSubMask);
	}

	bBG3Pri = (pRegs->bgmode&8) && (tm & SNESPPU_MASK_BG3) && ((pRegs->bgmode&7)==1);

	// clear main screen
	SNMaskClear(&pMain[SNPPU_BGPLANE_PLANE7]);
	SNMaskClear(&pMain[SNPPU_BGPLANE_LAYER0]);
	SNMaskClear(&pMain[SNPPU_BGPLANE_LAYER1]);
	bRendered=FALSE;

	// render bg layers to main screen
	if (tm & SNESPPU_MASK_BG4)
		_RenderBG8(pMain8, pMain, pRenderInfo->BGPlanes[3], (tmw&SNESPPU_MASK_BG4) ? &pBGWindow[3] : NULL, BGInfo[3].uBitDepth,  pMainAddSubMask, cgadsub & SNESPPU_MASK_BG4, NULL, NULL, BGInfo[3].Priority, bRendered, BGInfo[3].uScrollX );
	if (tm & SNESPPU_MASK_BG3)
		_RenderBG8(pMain8, pMain, pRenderInfo->BGPlanes[2], (tmw&SNESPPU_MASK_BG3) ? &pBGWindow[2] : NULL, BGInfo[2].uBitDepth,  pMainAddSubMask, cgadsub & SNESPPU_MASK_BG3, &BG3Pri, NULL, BGInfo[2].Priority, bRendered, BGInfo[2].uScrollX);
	if (tm & SNESPPU_MASK_BG2)
		_RenderBG8(pMain8, pMain, pRenderInfo->BGPlanes[1], (tmw&SNESPPU_MASK_BG2) ? &pBGWindow[1] : NULL, BGInfo[1].uBitDepth,  pMainAddSubMask, cgadsub & SNESPPU_MASK_BG2, NULL, bBG3Pri ? &BG3Pri : NULL, BGInfo[1].Priority, bRendered, BGInfo[1].uScrollX);
	if (tm & SNESPPU_MASK_BG1)
		_RenderBG8(pMain8, pMain, pRenderInfo->BGPlanes[0], (tmw&SNESPPU_MASK_BG1) ? &pBGWindow[0] : NULL, BGInfo[0].uBitDepth,  pMainAddSubMask, cgadsub & SNESPPU_MASK_BG1, NULL, bBG3Pri ? &BG3Pri : NULL, BGInfo[0].Priority, bRendered, BGInfo[0].uScrollX);
	if (!bRendered)
		_ClearLinePlanar((SNMaskT *)pMain8, 8);
#if SNDBG_LOG
	g_TmgCycBGMain += ProfCtrGetCycle() - _tBGMain;
#endif
	if (tm & SNESPPU_MASK_OBJ)
	{
#if SNDBG_LOG
		Uint32 _tObjB = ProfCtrGetCycle();
#endif
		_SnesPPURenderOBJ8(pMain8, pMain, ObjLine, nObjLine,  (tmw&SNESPPU_MASK_OBJ) ? &pBGWindow[4] : NULL, bBG3Pri ? &BG3Pri : NULL,
			pMainAddSubMask, (cgadsub & 0x10) ? 1 : 0);
#if SNDBG_LOG
		{
			Uint32 _dObjDraw = ProfCtrGetCycle() - _tObjB;
			g_TmgCycObj += _dObjDraw;
			g_TmgCycObjDraw += _dObjDraw;
		}
#endif
	}

#if CODE_PLATFORM == CODE_PS2
	/* O caminho direto do GS, escolhido logo depois por RenderLine(), usa
	   apenas a tela principal. Evite montar uma subtela que sera descartada. */
	if (cgadsub == 0 && (pRegs->cgwsel & 0xC0) == 0 &&
	    m_pPPU->GetIntensity() == 15)
	{
		PROF_LEAVE("RenderBG");
		return;
	}
#endif

#if SNDBG_LOG
	Uint32 _tBGSub = ProfCtrGetCycle();
#endif
	if (pRegs->cgwsel & 0x02)
	{
		// coloradd/sub subscreen
		SNMaskClear(pSubAddSubMask);		// the bg of the subscreen is not 1/2'd
	} else
	{
		// coloradd/sub fixed color only
		SNMaskSet(pSubAddSubMask);           //all pixels are 1/2!
	}

	// do fancy BG3 priority stuff?
	bBG3Pri = (pRegs->bgmode&8) && (ts & SNESPPU_MASK_BG3) && ((pRegs->bgmode&7)==1);;

	// clear Sub screen
	SNMaskClear(&pSub[SNPPU_BGPLANE_PLANE7]);
	SNMaskClear(&pSub[SNPPU_BGPLANE_LAYER0]);
	SNMaskClear(&pSub[SNPPU_BGPLANE_LAYER1]);
	bRendered = FALSE;

	// render bg layers to sub screen
	if (ts & SNESPPU_MASK_BG4)
		_RenderBG8(pSub8, pSub, pRenderInfo->BGPlanes[3], (tsw&SNESPPU_MASK_BG4) ? &pBGWindow[3] : NULL, BGInfo[3].uBitDepth, pSubAddSubMask, 1, NULL, NULL, BGInfo[3].Priority, bRendered, BGInfo[3].uScrollX);
	if (ts & SNESPPU_MASK_BG3)
		_RenderBG8(pSub8, pSub, pRenderInfo->BGPlanes[2], (tsw&SNESPPU_MASK_BG3) ? &pBGWindow[2] : NULL, BGInfo[2].uBitDepth, pSubAddSubMask, 1, &BG3Pri, NULL, BGInfo[2].Priority, bRendered, BGInfo[2].uScrollX);
	if (ts & SNESPPU_MASK_BG2)
		_RenderBG8(pSub8, pSub, pRenderInfo->BGPlanes[1], (tsw&SNESPPU_MASK_BG2) ? &pBGWindow[1] : NULL, BGInfo[1].uBitDepth, pSubAddSubMask, 1, NULL, bBG3Pri ? &BG3Pri : NULL, BGInfo[1].Priority, bRendered, BGInfo[1].uScrollX);
	if (ts & SNESPPU_MASK_BG1)
		_RenderBG8(pSub8, pSub, pRenderInfo->BGPlanes[0], (tsw&SNESPPU_MASK_BG1) ? &pBGWindow[0] : NULL, BGInfo[0].uBitDepth, pSubAddSubMask, 1, NULL, bBG3Pri ? &BG3Pri : NULL, BGInfo[0].Priority, bRendered, BGInfo[0].uScrollX);
	if (!bRendered)
		_ClearLinePlanar((SNMaskT *)pSub8, 8);
#if SNDBG_LOG
	g_TmgCycBGSub += ProfCtrGetCycle() - _tBGSub;
#endif
	if (ts & SNESPPU_MASK_OBJ)
	{
#if SNDBG_LOG
		Uint32 _tObjC = ProfCtrGetCycle();
#endif
		_SnesPPURenderOBJ8(pSub8, pSub, ObjLine, nObjLine,  (tsw&SNESPPU_MASK_OBJ) ? &pBGWindow[4] : NULL, bBG3Pri ? &BG3Pri : NULL,
			pSubAddSubMask, 4|1);
#if SNDBG_LOG
		{
			Uint32 _dObjDraw = ProfCtrGetCycle() - _tObjC;
			g_TmgCycObj += _dObjDraw;
			g_TmgCycObjDraw += _dObjDraw;
		}
#endif
	}

	PROF_LEAVE("RenderBG");
}


void RenderLine8Mode7(Int32 iLine,  SnesRender8pInfoT *pRenderInfo)
{

}










#if 0
static void _FetchMode7Priority(Uint8 *pPriority, Uint8 *pLine, Int32 nPixels)
{
	Uint8 uPriority =0;

	while (nPixels > 0)
	{
		Uint8 uData;
		uData = pLine[0];

		uPriority >>= 1;
		uPriority  |= uData & 0x80;

		if (!(nPixels & 7))
		{
			*pPriority++ = uPriority;
			uPriority = 0;
		}

		pLine[0] = uData & 0x7F;

		pLine++;
		nPixels--;
	}
}
#else

static void _FetchMode7Priority(Uint8 *pPriority, Uint8 *pLine, Int32 nPixels)
{
	Uint64 uMask64;
	Uint64 *pLine64 = (Uint64 *)pLine;

	uMask64 = 0x8080808080808080;

	while (nPixels > 0)
	{
		Uint64 uData64;
		Uint64 uPriority = 0;
		Uint64 uPri64;

		// fetch 8 pixels
		uData64 = pLine64[0];

		// get priority bits
		uPri64 = uData64 & uMask64;

		uPriority|= (uPri64 >> ( 0x00 + 7)) << 0;
		uPriority|= (uPri64 >> ( 0x08 + 7)) << 1;
		uPriority|= (uPri64 >> ( 0x10 + 7)) << 2;
		uPriority|= (uPri64 >> ( 0x18 + 7)) << 3;
		uPriority|= (uPri64 >> ( 0x20 + 7)) << 4;
		uPriority|= (uPri64 >> ( 0x28 + 7)) << 5;
		uPriority|= (uPri64 >> ( 0x30 + 7)) << 6;
		uPriority|= (uPri64 >> ( 0x38 + 7)) << 7;

		// remove priority bits
		uData64 &= ~uMask64;

		// store priority
		pPriority[0] = (Uint8)uPriority;
		pPriority++;

		// store line data
		pLine64[0] = uData64;
		pLine64++;

		nPixels-=8;
	}
}

#endif

#if CODE_PLATFORM == CODE_PS2
static void _FetchMode7Opaque(Uint8 *pMask, Uint8 *pLine, Int32 nPixels)
{
	Uint64 uMask64;
	Uint64 *pLine64 = (Uint64 *)pLine;
	Uint64 uZero;
	Uint64 uOne;

	uMask64 = 0x8080808080808080;
	uZero	= 0x0000000000000000;
	uOne    = 0xFFFFFFFFFFFFFFFF;

	while (nPixels > 0)
	{
		Uint64 uData64;
		Uint64 uOpaque = 0;

		// fetch 8 pixels
		uData64 = pLine64[0];
		pLine64++;

		__asm__ (
			"pceqb      %0,%0,$0        \n"   // %0 = FF or 00
			: "+r" (uData64)
			);

		if (uData64==uZero)
		{
			pMask[0] = 0xFF;
			pMask++;
		} else
		if (uData64==uOne)
		{
			pMask[0] = 0x00;
			pMask++;
		} else
		{
			// get priority bits
			uData64 = uData64 & uMask64;

			uOpaque|= (uData64 >> ( 0x00 + 7)) << 0;
			uOpaque|= (uData64 >> ( 0x08 + 7)) << 1;
			uOpaque|= (uData64 >> ( 0x10 + 7)) << 2;
			uOpaque|= (uData64 >> ( 0x18 + 7)) << 3;
			uOpaque|= (uData64 >> ( 0x20 + 7)) << 4;
			uOpaque|= (uData64 >> ( 0x28 + 7)) << 5;
			uOpaque|= (uData64 >> ( 0x30 + 7)) << 6;
			uOpaque|= (uData64 >> ( 0x38 + 7)) << 7;

			// store priority
			pMask[0] = (Uint8)(uOpaque^0xFF);
			pMask++;
		}
		nPixels-=8;
	}
}
#else

static void _FetchMode7Opaque(Uint8 *pMask, Uint8 *pLine, Int32 nPixels)
{
	Uint64 *pLine64 = (Uint64 *)pLine;

	while (nPixels > 0)
	{
		Uint64 uData64;
		Uint64 uOpaque = 0;

		// fetch 8 pixels
		uData64 = pLine64[0];
		pLine64++;

		// on a mips processor this produces several movn instructions
		uOpaque|= ((uData64 >> 0x00) & 0xFF) ? 1 : 0;
		uOpaque|= ((uData64 >> 0x08) & 0xFF) ? 2 : 0;
		uOpaque|= ((uData64 >> 0x10) & 0xFF) ? 4 : 0;
		uOpaque|= ((uData64 >> 0x18) & 0xFF) ? 8 : 0;
		uOpaque|= ((uData64 >> 0x20) & 0xFF) ? 16 : 0;
		uOpaque|= ((uData64 >> 0x28) & 0xFF) ? 32 : 0;
		uOpaque|= ((uData64 >> 0x30) & 0xFF) ? 64 : 0;
		uOpaque|= ((uData64 >> 0x38) & 0xFF) ? 128 : 0;


		// store priority
		pMask[0] = (Uint8)uOpaque;
		pMask++;
		nPixels-=8;
	}
}
#endif
static void _FetchMode7(Uint8 *pLine, SnesPPU *pPPU, Int32 iLine, SNMaskT *pPriority, SNMaskT *pOpaque)
{
	const SnesPPURegsT *pRegs = pPPU->GetRegs();
	SnesPPUMode7LineT Line;
	Uint8 *pVram;

	pVram = (Uint8 *)pPPU->GetVramPtr(0);
	Line = SnesPPUMode7MakeLine(
		pRegs->m7hofs.w, pRegs->m7vofs.w,
		pRegs->m7x.w, pRegs->m7y.w,
		(Int16)pRegs->m7a.w, (Int16)pRegs->m7b.w,
		(Int16)pRegs->m7c.w, (Int16)pRegs->m7d.w,
		pRegs->m7sel, iLine);

	switch (pRegs->m7sel>>6)
	{
	case 0: // screen repetition if outside of screen area
	case 1: // mode 1 also wraps like mode 0
		SnesPPUMode7FetchRepeat(pLine, 256, pVram,
			Line.x, Line.y, Line.dx, Line.dy);
		break;
	case 3: // character 0x00 repetition if outside of screen area
		SnesPPUMode7FetchClamp(pLine, 256, pVram,
			Line.x, Line.y, Line.dx, Line.dy);
		break;
	case 2: // outside of the screen area is the back drop screen in single color
	default:
		SnesPPUMode7FetchBlack(pLine, 256, pVram,
			Line.x, Line.y, Line.dx, Line.dy);
		break;
	}

	if (pPriority)
	{
		if (pRegs->setini & 0x40)
		{
			PROF_ENTER("_FetchMode7Priority");
			_FetchMode7Priority(pPriority->uMask8, pLine, 256);
			PROF_LEAVE("_FetchMode7Priority");
		} else
		{
//			SNMaskClear(pPriority);
			SNMaskSet(pPriority);

		}
	}

	// no transparency?
	_FetchMode7Opaque(pOpaque->uMask8, pLine, 256);
}
