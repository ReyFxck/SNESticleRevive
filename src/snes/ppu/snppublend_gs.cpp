/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snppublend gs behavior for SNES picture processing.
 */

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "prof.h"
#include "snmask.h"
#include "rendersurface.h"
#include "snppurender.h"
#include "snppublend_gs.h"
#include "snppucolor.h"
#include "sndbglog.h"

#include <tamtypes.h>
extern "C" {

#include <kernel.h>
#include "ps2dma.h"
#include "gpfifo.h"
#include "gpprim.h"
#include "gs.h"
#include "gslist.h"
#include "ps2mem.h"
#include "gskit_backend.h"
}

/* RenderInfo occupies the beginning of the 16 KiB EE scratchpad. Keep a
   second, DMA-owned copy of BlendInfo after it: the CPU may then compose the
   next scanline while GIF is still consuming the previous one. */
#define SNPPU_DMA_BLENDINFO_OFFSET (6 * 1024)
#define SNPPU_DMA_BLENDINFO_ADDR \
	(PS2MEM_SCRATCHPAD + SNPPU_DMA_BLENDINFO_OFFSET)

/* Native 512-dot lines are finalized by the EE and staged in scratchpad.
   The GS only transfers/presents them; no VIF/VU synchronization is needed
   for this branch-heavy indexed-pixel workload. */
#define SNPPU_DMA_HIRES_OFFSET (8 * 1024)
#define SNPPU_DMA_HIRES_ADDR \
	(PS2MEM_SCRATCHPAD + SNPPU_DMA_HIRES_OFFSET)
#define SNPPU_DMA_HIRES_BYTES (512 * sizeof(Uint16))

/* Normal direct lines borrow the hires staging area until a hires/math
   boundary flushes them. No pending batch survives End() into the mixer. */
#define SNPPU_DIRECT_BATCH_LINES 8u
typedef char SNPPUDirectBatchLayoutCheck[
	(SNPPU_DMA_HIRES_OFFSET + SNPPU_DIRECT_BATCH_LINES * 256u <=
	 PS2MEM_SNES_LOOKUP_OFFSET) ? 1 : -1];

typedef char SNPPUScratchLayoutCheck[
	(sizeof(SnesRender8pInfoT) <= SNPPU_DMA_BLENDINFO_OFFSET &&
	 SNPPU_DMA_BLENDINFO_OFFSET + sizeof(SNPPUBlendInfoT) <=
		SNPPU_DMA_HIRES_OFFSET &&
	 SNPPU_DMA_HIRES_OFFSET + SNPPU_DMA_HIRES_BYTES <=
		PS2MEM_SNES_LOOKUP_OFFSET &&
	 PS2MEM_SNES_LOOKUP_OFFSET + PS2MEM_SNES_LOOKUP_SIZE <= 16 * 1024)
		? 1 : -1];

#if SNDBG_LOG
#define SNPPU_GS_DIAG_SAMPLES 8
struct SNPPUGSDiagT
{
	Uint32 Session;
	Uint32 Frames;
	Uint32 Lines;
	Uint32 SyncCalls;
	Uint32 SyncCycles;
	Uint32 CopyCycles;
	Uint32 KickCycles;
	Uint32 CopyBytes;
	Uint32 PaletteUploads;
	Uint32 PaletteFullUploads;
	Uint32 PaletteSparseUploads;
	Uint32 PaletteTransferBytes;
	Uint32 IntensityLines;
	Uint32 DirectMainLines;
	Uint32 FixedSubLines;
	Uint32 StageMismatch;
	Uint32 CopyMismatch;
	Uint32 SourceHash;
	Uint32 StageHash;
	Uint32 Expected[SNPPU_GS_DIAG_SAMPLES];
	Bool   HasExpected;
};

static SNPPUGSDiagT _SNPPUGSDiag;

static void _SNPPUGSDiagEnsureSession(void)
{
	if (_SNPPUGSDiag.Session != g_DbgSessionId)
	{
		memset(&_SNPPUGSDiag, 0, sizeof(_SNPPUGSDiag));
		_SNPPUGSDiag.Session = g_DbgSessionId;
	}
}

#if SNDBG_DEEP
static Uint32 _SNPPUGSSample(const SNPPUBlendInfoT *pInfo,
	                          Uint32 *pSamples)
{
	const Uint32 *pWords = (const Uint32 *)pInfo;
	const Uint32 nWords = sizeof(*pInfo) / sizeof(Uint32);
	Uint32 h = 2166136261u;
	Uint32 i;

	for (i = 0; i < SNPPU_GS_DIAG_SAMPLES; i++)
	{
		Uint32 uIndex = (nWords - 1) * i / (SNPPU_GS_DIAG_SAMPLES - 1);
		Uint32 uValue = pWords[uIndex];
		if (pSamples)
			pSamples[i] = uValue;
		h ^= uValue;
		h *= 16777619u;
	}
	return h;
}

static void _SNPPUGSValidateStage(const SNPPUBlendInfoT *pInfo)
{
	Uint32 uSamples[SNPPU_GS_DIAG_SAMPLES];
	Uint32 i;

	if (!_SNPPUGSDiag.HasExpected)
		return;

	_SNPPUGSSample(pInfo, uSamples);
	for (i = 0; i < SNPPU_GS_DIAG_SAMPLES; i++)
	{
		if (uSamples[i] != _SNPPUGSDiag.Expected[i])
		{
			_SNPPUGSDiag.StageMismatch++;
			break;
		}
	}
}
#endif
#endif

#define SNPPUBLEND_PAL32 (TRUE)

/* GPPrimUploadTexture sends a complete 16x16 PSMCT32 CSM1 palette (1 KiB).
   Keep a complete DMA source here as well.  The old eight-word objects made
   the GIF DMA read 992 bytes beyond each array; those bytes happened to be
   ignored for valid attribute indices, but the out-of-bounds transfer was
   neither deterministic nor safe on the EE's real memory/cache path.  The
   unused CSM1 entries are intentionally zero-filled by static initialization. */
static Uint32 _SNPPUBlend_AttribMainPal[16 * 16] _ALIGN(64) =
{                   // HSM
    0x00000000,     // 000
    0x80000000,     // 001
    0x00000000,     // 010
    0x80000000,     // 011
    0x00000000,     // 100
    0x40000000,     // 101
    0x00000000,     // 110
    0x40000000,     // 111
};

static Uint32 _SNPPUBlend_AttribSubPal[16 * 16] _ALIGN(64) =
{                   // HSM
    0x00000000,     // 000
    0x00000000,     // 001
    0x80000000,     // 010
    0x80000000,     // 011
    0x00000000,     // 100
    0x00000000,     // 101
    0x40000000,     // 110
    0x40000000,     // 111
};

typedef char SNPPUAttribPaletteSizeCheck[
	(sizeof(_SNPPUBlend_AttribMainPal) == 16 * 16 * sizeof(Uint32) &&
	 sizeof(_SNPPUBlend_AttribSubPal) == 16 * 16 * sizeof(Uint32)) ? 1 : -1];

static void _PlanarTo3(Uint8 *pDest, const SNMaskT *pSrc0,
	const SNMaskT *pSrc1, const SNMaskT *pSrc2)
{
	Uint32 nBytes = 256 / 8;
	const Uint8 *pPlane0 = (const Uint8 *)pSrc0;
	const Uint8 *pPlane1 = (const Uint8 *)pSrc1;
	const Uint8 *pPlane2 = (const Uint8 *)pSrc2;
	SnesChrLookupT *pPlaneLookup =
		(SnesChrLookupT *)PS2MEM_SNES_LOOKUP_ADDR;
	SnesChrLookup64T *pLookup64 =
		(SnesChrLookup64T *)&pPlaneLookup[1];
	Uint64 *pDest64 = (Uint64 *)pDest;

	while (nBytes > 0)
	{
		Uint64 uData;

		uData  = (*pLookup64)[pPlane0[0]] << 0;
		uData |= (*pLookup64)[pPlane1[0]] << 1;
		uData |= (*pLookup64)[pPlane2[0]] << 2;

		pPlane0++;
		pPlane1++;
		pPlane2++;

		pDest64[0] = uData;
		pDest64+=1;

		nBytes--;
	}

}

void SNPPUBlendGS::MarkPaletteEntryDirty(Uint32 uAddr)
{
	Uint32 uWord = (uAddr >> 5) & 7;
	Uint32 uBit = 1u << (uAddr & 31);

	if (!(m_uPaletteDirty[uWord] & uBit))
	{
		m_uPaletteDirty[uWord] |= uBit;
		m_nPaletteDirty++;
	}
	m_bPaletteDirty = TRUE;
}

void SNPPUBlendGS::MarkPaletteAllDirty()
{
	Int32 iWord;

	for (iWord = 0; iWord < 8; iWord++)
		m_uPaletteDirty[iWord] = 0xFFFFFFFFu;
	m_nPaletteDirty = 256;
	m_bPaletteDirty = TRUE;
}

Uint32 SNPPUBlendGS::CopyDirtyPalette(PaletteT *pDest,
	                                  const PaletteT *pSource)
{
	Uint32 uCopiedBytes = 0;
	Int32 iWord;

	if (!m_bPaletteDirty)
		return 0;

	/* A complete CGRAM upload is cheaper as one burst.  HDMA gradients, on
	   the other hand, commonly alter only one or two entries per scanline;
	   copying the whole 1 KiB CLUT there was pure EE work. */
	if (m_nPaletteDirty >= 64)
	{
		memcpy(pDest, pSource, sizeof(*pDest));
		uCopiedBytes = sizeof(*pDest);
	}
	else
	{
		for (iWord = 0; iWord < 8; iWord++)
		{
			Uint32 uBits = m_uPaletteDirty[iWord];
			while (uBits)
			{
				Uint32 uBit = (Uint32)__builtin_ctz(uBits);
				Uint32 uAddr = (Uint32)iWord * 32 + uBit;
#if SNPPUBLEND_PAL32
				pDest->Color32[uAddr] = pSource->Color32[uAddr];
				uCopiedBytes += sizeof(pDest->Color32[0]);
#else
				pDest->Color16[uAddr] = pSource->Color16[uAddr];
				uCopiedBytes += sizeof(pDest->Color16[0]);
#endif
				uBits &= uBits - 1;
			}
		}
	}

	memset(m_uPaletteDirty, 0, sizeof(m_uPaletteDirty));
	m_nPaletteDirty = 0;
	m_bPaletteDirty = FALSE;
	return uCopiedBytes;
}

Uint64 SNPPUBlendGS::GetDirtyPaletteGroups() const
{
	Uint64 uGroups = 0;
	Uint32 iWord;

	/* GIF image payloads are qword-sized. Group four adjacent PSMCT32
	   entries so every sparse source and destination is 16-byte aligned. */
	for (iWord = 0; iWord < 8; iWord++)
	{
		Uint32 uBits = m_uPaletteDirty[iWord];
		while (uBits)
		{
			Uint32 uBit = (Uint32)__builtin_ctz(uBits);
			Uint32 uAddr = iWord * 32u + uBit;
			uGroups |= (Uint64)1u << (uAddr >> 2);
			uBits &= uBits - 1u;
		}
	}
	return uGroups;
}

#if SNPPUBLEND_PAL32

void SNPPUBlendGS::UpdatePaletteEntry(SNPPUBlendInfoT *pInfo, Uint32 uAddr, Uint32 uData, Uint32 uIntensity)
{
    PaletteT *pPal = pInfo->Pal;

	uData = SNPPUColorConvert15to32(uData & 0x7FFF);

	if (uAddr > 0)
	{
		uData |= 0x80000000;
	}

	// swap 8 and 0x10 of addr
	uAddr = (uAddr & ~0x18) | ((uAddr & 0x10) >> 1) | ((uAddr & 0x08) << 1);

	if (pPal->Color32[uAddr] != uData)
	{
		pPal->Color32[uAddr] = uData;
		MarkPaletteEntryDirty(uAddr);
	}
}

void SNPPUBlendGS::UpdatePalette(SNPPUBlendInfoT *pInfo, Uint16 *pCGRam, Uint32 uIntensity)
{
	Int32 iEntry;
    PaletteT *pPal = pInfo->Pal;

	PROF_ENTER("SNPPUBlendUpdatePalette");

	pPal->Color32[0] = SNPPUColorConvert15to32(pCGRam[0]);
	for (iEntry=1; iEntry < 256; iEntry++)
	{
		Uint32 uAddr = iEntry;

		uAddr = (uAddr & ~0x18) | ((uAddr & 0x10) >> 1) | ((uAddr & 0x08) << 1);

		// set palette entry (with alpha set)
		pPal->Color32[uAddr] = SNPPUColorConvert15to32(pCGRam[iEntry]) | 0x80000000;
	}
	MarkPaletteAllDirty();

	PROF_LEAVE("SNPPUBlendUpdatePalette");
}

#else

static Uint32 SNPPUColorConvert15to32(SnesColor16T uColor16)
{
	Uint32 uColor32;
	Uint32 uR, uG, uB;

	uR = ((uColor16 >>  0) & 0x1F);
	uG = ((uColor16 >>  5) & 0x1F);
	uB = ((uColor16 >>  10) & 0x1F);

	// convert snes16->generic32
	uColor32 =  uR <<  (0  + 3);
	uColor32|=  uG <<  (8  + 3);
	uColor32|=  uB <<  (16 + 3);
	return uColor32;
}

void SNPPUBlendGS::UpdatePaletteEntry(SNPPUBlendInfoT *pInfo, Uint32 uAddr, Uint32 uData, Uint32 uIntensity)
{
    PaletteT *pPal = pInfo->Pal;
	if (uAddr > 0)
	{
		uData |= 0x8000;
	}
	if (pPal->Color16[uAddr] != uData)
	{
		pPal->Color16[uAddr] = uData;
		MarkPaletteEntryDirty(uAddr);
	}
}

void SNPPUBlendGS::UpdatePalette(SNPPUBlendInfoT *pInfo, Uint16 *pCGRam, Uint32 uIntensity)
{
	Int32 iEntry;
    PaletteT *pPal = pInfo->Pal;

	PROF_ENTER("SNPPUBlendUpdatePalette");

	pPal->Color16[0] = pCGRam[0];
	for (iEntry=1; iEntry < 256; iEntry++)
	{
		// set palette entry (with alpha set)
		pPal->Color16[iEntry] = pCGRam[iEntry] | 0x8000;
	}
	MarkPaletteAllDirty();

	PROF_LEAVE("SNPPUBlendUpdatePalette");
}

#endif

static void _GPFifoUploadTextureTracked(int TBP, int TBW, int xofs,
	int yofs, int pxlfmt, void *tex, int wpxls, int hpxls,
	Uint64 **ppTrxPos, SNPPUDmaListT *pDirectList = NULL)
{
    int numq;

    numq = wpxls * hpxls;
    switch (pxlfmt)
    {
    case 0x00: numq = (numq >> 2) + ((numq & 0x03) != 0 ? 1 : 0); break;
    case 0x02: numq = (numq >> 3) + ((numq & 0x07) != 0 ? 1 : 0); break;
    case 0x13: numq = (numq >> 4) + ((numq & 0x0f) != 0 ? 1 : 0); break;
    case 0x14: numq = (numq >> 5) + ((numq & 0x1f) != 0 ? 1 : 0); break;
    default:   numq = 0;
    }

    GSGifTagOpenAD();

    GSGifRegAD(GS_REG_BITBLTBUF,GS_SET_BITBLTBUF( 0, (TBW/64), pxlfmt,  (TBP/256), (TBW/64), pxlfmt));
	if (ppTrxPos)
		*ppTrxPos = (Uint64 *)GSListGetUncachedPtr();
    GSGifRegAD(GS_REG_TRXPOS,GS_SET_TRXPOS(0,0,xofs,yofs,0));
	if (pDirectList)
		pDirectList->pDirectTrxReg = (Uint64 *)GSListGetUncachedPtr();
    GSGifRegAD(GS_REG_TRXREG,GS_SET_TRXREG(wpxls, hpxls));
    GSGifRegAD(GS_REG_TRXDIR,GS_SET_TRXDIR(0));

    GSGifTagCloseAD();

    // image gif tag
	if (pDirectList)
		pDirectList->pDirectImageCount = (Uint16 *)GSListGetUncachedPtr();
    GSGifTagImage(numq);

    // close last dma cnt
    GSDmaCntClose();

    // dma image data
	if (pDirectList)
		pDirectList->pDirectRefCount = (Uint16 *)GSListGetUncachedPtr();
    GSDmaRef((Uint128 *)tex, numq);

    // start new dma cnt
    GSDmaCntOpen();
}

static void _GPFifoUploadTexture(int TBP, int TBW, int xofs, int yofs,
	int pxlfmt, void *tex, int wpxls, int hpxls)
{
	_GPFifoUploadTextureTracked(TBP, TBW, xofs, yofs, pxlfmt, tex,
		wpxls, hpxls, NULL);
}

Uint128 *SNPPUBlendGS::BuildSparsePaletteList(PaletteT *pPalette,
	Uint64 uDirtyGroups, SNPPUDmaListT *pRenderList)
{
	/* Raster palette writes often touch the same CLUT groups on consecutive
	   lines. The wrapper describes addresses and geometry, not color values:
	   CopyDirtyPalette has already refreshed its DMA-owned source after the
	   GIF wait. Retain this exact chain while all three dependencies match. */
	if (m_bSparsePaletteListReady && m_uSparsePaletteGroups == uDirtyGroups &&
	    m_pSparsePaletteSource == pPalette && m_pSparseRenderList == pRenderList)
		return m_SparsePaletteDmaList;

	/* On a wrapper miss, write through the uncached alias after GIF sync.
	   The list must be visible to DMAC without flushing the EE's
	   complete 8 KiB data cache. */
	Uint128 *pBuild = (Uint128 *)PS2MEM_UNCACHED(m_SparsePaletteDmaList);
	Uint32 iRow;

	GSListBegin(pBuild, SNPPU_SPARSE_PALETTE_LIST_QWORDS, NULL);
	GSDmaCntOpen();

	for (iRow = 0; iRow < 16u; iRow++)
	{
		Uint32 uRowGroups = (Uint32)((uDirtyGroups >> (iRow * 4u)) & 0x0Fu);
		while (uRowGroups)
		{
			Uint32 iFirst = (Uint32)__builtin_ctz(uRowGroups);
			Uint32 nGroups = 1;
			Uint32 uRunMask = 1u << iFirst;

			/* Merge adjacent qwords on the same 16-entry CLUT row. The staged
			   neighbors already contain the last uploaded values, so sending a
			   complete run is exact even when only one entry in it changed. */
			while (iFirst + nGroups < 4u &&
			       (uRowGroups & (1u << (iFirst + nGroups))))
			{
				uRunMask |= 1u << (iFirst + nGroups);
				nGroups++;
			}

			Uint32 uPaletteAddr = iRow * 16u + iFirst * 4u;
			_GPFifoUploadTexture(
				pRenderList->uPalAddr * 0x100,
				1, (int)(iFirst * 4u), (int)iRow,
				GS_PSMCT32,
				(void *)(((Uint32)&pPalette->Color32[uPaletteAddr]) |
					0x80000000),
				(int)(nGroups * 4u), 1);
			uRowGroups &= ~uRunMask;
		}
	}

	GSDmaCntClose();
	/* Continue directly into the immutable render chain. Its END tag also
	   terminates this wrapper, so sparse CLUT updates cost no second kick. */
	GSDmaNext(pRenderList->Data);
	GSListEnd();
	__asm__ __volatile__ ("sync.l");
	m_uSparsePaletteGroups = uDirtyGroups;
	m_pSparsePaletteSource = pPalette;
	m_pSparseRenderList = pRenderList;
	m_bSparsePaletteListReady = TRUE;
	return m_SparsePaletteDmaList;
}

static void _SNPPURenderTexLineWH(Int32 iDestLine, Int32 iSrcLine,
	Uint32 RGBA, int abe, Int32 iDestWidth, Int32 iSrcWidth,
	SNPPUDmaListT *pDirectList = NULL)
{
    int x1,x2,y1,y2;
    int u1,u2,v1,v2;

    x1  = 0;
    x2  = iDestWidth << 4;
    y1  = (iDestLine + 0) << 4;
    y2  = (iDestLine + 1) << 4;

    u1  = 0;
    u2  = iSrcWidth << 4;
    v1  = (iSrcLine + 0) << 4;
    v2  = (iSrcLine + 1) << 4;

    x1+=0x8000;
    y1+=0x8000;
    x2+=0x8000;
    y2+=0x8000;

	if (pDirectList)
	{
		/* REGLIST: one qword tag, then six 64-bit register values. */
		Uint64 *pTag = (Uint64 *)GSListGetUncachedPtr();
		pDirectList->pDirectUVEnd = pTag + 6;
		pDirectList->pDirectXYZEnd = pTag + 7;
	}
    GSGifTagOpen(GIF_SET_TAG(1, 1, 0, 0, 1, 6), 0xF535310);

	GSGifReg(GS_SET_PRIM(0x06, 0, 1, 0, abe, 0, 1, 0, 0));
	GSGifReg(RGBA);
	GSGifReg(GS_SET_UV(u1, v1));
	GSGifReg(GS_SET_XYZ(x1,y1,0));
	GSGifReg(GS_SET_UV(u2, v2));
	GSGifReg(GS_SET_XYZ(x2,y2,0));

    GSGifTagClose();
}

static void _SNPPURenderTexLine(Int32 iDestLine, Int32 iSrcLine,
	Uint32 RGBA, int abe)
{
	_SNPPURenderTexLineWH(iDestLine, iSrcLine, RGBA, abe, 256, 256);
}

static void _SNPPURenderLineW(Int32 iDestLine, int abe, Int32 iWidth)
{
    int x1,x2,y1,y2;

    x1  = 0;
    x2  = iWidth << 4;
    y1  = (iDestLine + 0) << 4;
    y2  = (iDestLine + 1) << 4;

    x1+=0x8000;
    x2+=0x8000;
    y1+=0x8000;
    y2+=0x8000;

    GSGifTagOpen(GIF_SET_TAG(1, 1, 0, 0, 1, 4), 0xF550);

	GSGifReg(GS_SET_PRIM(0x06, 0, 0, 0, abe, 0, 1, 0, 0));
	GSGifReg(GS_SET_XYZ(x1,y1,0));
	GSGifReg(GS_SET_XYZ(x2,y2,0));
	GSGifReg(0);

    GSGifTagClose();
}

static void _SNPPURenderLine(Int32 iDestLine, int abe)
{
	_SNPPURenderLineW(iDestLine, abe, 256);
}

void SNPPUBlendGS::Begin(CRenderSurface *pTarget)
{
	FlushDirectLines();
#if SNDBG_LOG
	_SNPPUGSDiagEnsureSession();
	/* End() ja esperou a ultima chain. O mixer de audio tambem usa o
	   scratchpad entre quadros, portanto uma expectativa antiga nao deve ser
	   comparada com o primeiro scanline do quadro seguinte. */
	_SNPPUGSDiag.HasExpected = FALSE;
#endif
    m_pTarget = pTarget;
	if (!m_pTarget)
	{
		return;
	}

	/* The audio mixer may reuse scratchpad between frames.  Refresh the
	   staged CLUT on the first rendered line even when CGRAM did not change. */
	MarkPaletteAllDirty();

    /* These two attribute CLUTs never change and live in a VRAM range
       reserved exclusively for the SNES blender.  Upload them once for the
       lifetime of the renderer instead of spending two transfers per frame.
       The dynamic uploads inside _SNPPUBlendBuildList still use REF tags so
       they pick up the current staged scanline on every kick.

       The legacy _GPFifoUploadTexture took TBP in bytes (it divides
       by 256 internally to encode BITBLTBUF.DBP); GPPrimUploadTexture
       takes TBP in 256-byte units (it multiplies by 256 internally).
       m_DmaList.uAttribMainPal / uAttribSubPal are already stored in
       TBP units, so drop the * 0x100 that converted to bytes for the
       legacy call.

       Both source arrays are complete 16 x 16 PSMCT32 tiles.  Attribute
       indices are only 0..7; the remaining entries are deterministic zeroes. */
    if (!m_bAttribPalettesUploaded)
    {
        GPPrimUploadTexture(
             m_DmaList.uAttribMainPal,
             64, 0, 0,
             GS_PSMCT32,
             _SNPPUBlend_AttribMainPal,
             16,
             16);

        GPPrimUploadTexture(
             m_DmaList.uAttribSubPal,
             64, 0, 0,
             GS_PSMCT32,
             _SNPPUBlend_AttribSubPal,
             16,
             16);
        m_bAttribPalettesUploaded = TRUE;
    }

    GSGifTagOpenAD();

	GSGifRegAD(GS_REG_TEXCLUT,256/64);

	GSGifRegAD(GS_REG_TEXA,GS_SET_TEXA(0x00,0,0x80));

    // clamp_1
	GSGifRegAD(GS_REG_CLAMP_1,GS_SET_CLAMP(0, 0, 0, 0, 0, 0));

    // tex1_1
    GSGifRegAD(GS_REG_TEX1_1, 0x000);

    GSGifTagCloseAD();

    GPFifoPause();
}

void SNPPUBlendGS::End()
{
	if (!m_pTarget)
	{
		return;
	}

	FlushDirectLines();
	// wait for previous dma to finish
#if SNDBG_LOG
	{
		Uint32 uStart = ProfCtrGetCycle();
		DmaSyncGIF();
		_SNPPUGSDiag.SyncCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.SyncCalls++;
		#if SNDBG_DEEP
		_SNPPUGSValidateStage(
			(const SNPPUBlendInfoT *)SNPPU_DMA_BLENDINFO_ADDR);
		#endif
		_SNPPUGSDiag.HasExpected = FALSE;
	}
#else
    DmaSyncGIF();
#endif

    GPFifoResume();

    GSGifTagOpenAD();
    // reset frame register
	GSGifRegAD(GS_REG_FRAME_1, GS_GetFrameReg());
	GSGifRegAD(GS_REG_XYOFFSET_1, GS_GetOffsetReg());
    GSGifTagCloseAD();

    /* The blender chain has just rendered into _OutTex via raw GIF
       DMA (FRAME_1 = uOutAddr). The next gsKit textured prim that
       samples _OutTex (PolyTexture(&_OutTex) + PolyRect in
       MainLoopRender) needs an explicit TEXFLUSH before sampling, or
       the GS hardware texture cache will keep serving the stale
       texels it cached on the previous frame. Without this, on
       hardware and on emulators, the visible output is whatever was
       in the texture cache before the blender ran - typically a
       mostly-black screen, with a brief correct frame whenever some
       other path (e.g. menu font upload via GPPrimUploadTexture, on
       L2+R2 or on menu redraw) happens to call
       GSK_InvalidateTextureCache for an unrelated reason. Hooking
       the invalidate here closes that race so every gsKit sample of
       _OutTex sees the fresh blender output. */
    GSK_InvalidateTextureCache();

#if SNDBG_LOG
	_SNPPUGSDiagEnsureSession();
	_SNPPUGSDiag.Frames++;
	if (_SNPPUGSDiag.Frames >= SNDBG_FRAME_PERIOD)
	{
		Uint32 uLines = _SNPPUGSDiag.Lines ? _SNPPUGSDiag.Lines : 1;
		Uint32 uSync = _SNPPUGSDiag.SyncCalls ? _SNPPUGSDiag.SyncCalls : 1;
		DLog("[snes-gs] frames/lines=%u/%u avgcyc sync/copy/kick=%u/%u/%u avg-copy-bytes=%u pal-uploads/full/sparse=%u/%u/%u pal-gif-bytes=%u intensity-lines=%u direct-main-lines=%u fixed-sub-lines=%u",
			(unsigned)_SNPPUGSDiag.Frames, (unsigned)_SNPPUGSDiag.Lines,
			(unsigned)(_SNPPUGSDiag.SyncCycles / uSync),
			(unsigned)(_SNPPUGSDiag.CopyCycles / uLines),
			(unsigned)(_SNPPUGSDiag.KickCycles / uLines),
			(unsigned)(_SNPPUGSDiag.CopyBytes / uLines),
			(unsigned)_SNPPUGSDiag.PaletteUploads,
			(unsigned)_SNPPUGSDiag.PaletteFullUploads,
			(unsigned)_SNPPUGSDiag.PaletteSparseUploads,
			(unsigned)_SNPPUGSDiag.PaletteTransferBytes,
			(unsigned)_SNPPUGSDiag.IntensityLines,
			(unsigned)_SNPPUGSDiag.DirectMainLines,
			(unsigned)_SNPPUGSDiag.FixedSubLines);
		#if SNDBG_DEEP
		DLog("[snes-gs-deep] mismatch stage/copy=%u/%u",
			(unsigned)_SNPPUGSDiag.StageMismatch,
			(unsigned)_SNPPUGSDiag.CopyMismatch);
		DLog("[snes-gs-deep] sampled cpu/stage hash=%08X/%08X blendbytes=%u renderbytes=%u stage=%08X",
			(unsigned)_SNPPUGSDiag.SourceHash,
			(unsigned)_SNPPUGSDiag.StageHash,
			(unsigned)sizeof(SNPPUBlendInfoT),
			(unsigned)sizeof(SnesRender8pInfoT),
			(unsigned)SNPPU_DMA_BLENDINFO_ADDR);
		#endif
		memset(&_SNPPUGSDiag, 0, sizeof(_SNPPUGSDiag));
		_SNPPUGSDiag.Session = g_DbgSessionId;
	}
#endif

    m_pTarget = NULL;
}

static void _SNPPUBlendBuildList(SNPPUDmaListT *pList,
	SNPPUBlendInfoT *pInfo, Uint32 uOutAddr, Bool bUploadPalette,
	Bool bApplyIntensity, Bool bDirectMain, Bool bFixedSub)
{
    PaletteT *pPal = pInfo->Pal;

	pList->pFixedColor = NULL;
	pList->pAddSub = NULL;
	pList->pIntensity = NULL;
	pList->pXYOffset = NULL;

    // begin dma list
    GSListBegin(pList->Data, sizeof(pList->Data) / sizeof(Uint128), NULL);

    GSDmaCntOpen();

	if (bUploadPalette)
	{
	#if SNPPUBLEND_PAL32
	// upload as 16x16 psmct32 for use as csm1
    _GPFifoUploadTexture(
         pList->uPalAddr * 0x100,
         1, 0, 0,
         GS_PSMCT32,
         (void *)(((Uint32)pPal) | 0x80000000),
         16,
         16);
	#else
	// upload as 256x1 psmct16 for use as csm2
    _GPFifoUploadTexture(
         pList->uPalAddr * 0x100,
         256, 0, 0,
         GS_PSMCT16,
         (void *)(((Uint32)pPal) | 0x80000000),
		 256,
		 1);
	#endif
	}

    _GPFifoUploadTextureTracked(
         pList->uInputAddr * 0x100,
         256, 0, 0,
         GS_PSMT8,
         (void *)((bDirectMain ? SNPPU_DMA_HIRES_ADDR :
			(Uint32)pInfo->uMain8) | 0x80000000),
         256,
         1, NULL, bDirectMain ? pList : NULL);

	if (bDirectMain)
	{
		/* No effective colour math and no main-screen color clipping: the SNES
		   result is exactly the palette-expanded main screen.  Write it to
		   the output in one primitive instead of constructing temp main/sub
		   colors and two attribute masks that can no longer affect a pixel. */
		GSGifTagOpenAD();
		GSGifRegAD(GS_REG_TEXFLUSH, 0);
		GSGifRegAD(GS_REG_FRAME_1,
			GS_SET_FRAME((uOutAddr/0x20), 512/64, GS_PSMCT16, 0));
#if SNPPUBLEND_PAL32
		GSGifRegAD(GS_REG_TEX0_1,
			GS_SET_TEX0(pList->uInputAddr, 256/64, GS_PSMT8, 8, 3,
				1, 0, pList->uPalAddr, GS_PSMCT32, 0, 0, 1));
#else
		GSGifRegAD(GS_REG_TEX0_1,
			GS_SET_TEX0(pList->uInputAddr, 256/64, GS_PSMT8, 8, 3,
				1, 0, pList->uPalAddr, GS_PSMCT16, 1, 0, 1));
#endif
		pList->pXYOffset = (Uint64 *)GSListGetUncachedPtr();
		GSGifRegAD(GS_REG_XYOFFSET_1, 0);
		GSGifTagCloseAD();

		_SNPPURenderTexLineWH(0, 0, 0x80808080, 0, 512, 256, pList);

		GSDmaCntClose();
		GSDmaEnd();
		GSListEnd();
		return;
	}

	if (!bFixedSub)
	{
		_GPFifoUploadTexture(
			 pList->uInputAddr * 0x100,
			 256, 0, 1,
			 GS_PSMT8,
			 (void *)(((Uint32)pInfo->uSub8) | 0x80000000),
			 256,
			 1);
	}

    _GPFifoUploadTexture(
         pList->uInputAddr * 0x100,
         256, 0, 2,
         GS_PSMT8,
         (void *)(((Uint32)pInfo->uAttrib8) | 0x80000000),
         256,
         1);

    GSGifTagOpenAD();

    // texflush
    GSGifRegAD(GS_REG_TEXFLUSH,0);

    // setup frame register to point to our temporary texture
	GSGifRegAD(GS_REG_FRAME_1, GS_SET_FRAME((pList->uTempAddr/0x20),256/64,GS_PSMCT32,0 ));

	GSGifRegAD(GS_REG_XYOFFSET_1, GS_SET_XYOFFSET(0x8000, 0x8000));

    GSGifTagCloseAD();

    // setup src texture

    GSGifTagOpenAD();
	#if SNPPUBLEND_PAL32
	// use clut psmct32 csm1
	GSGifRegAD(GS_REG_TEX0_1,GS_SET_TEX0(pList->uInputAddr, 256/64, GS_PSMT8, 8, 3,    1, 0, pList->uPalAddr, GS_PSMCT32, 0, 0, 1));
	#else
	// use clut psmct16 csm2
	GSGifRegAD(GS_REG_TEX0_1,GS_SET_TEX0(pList->uInputAddr, 256/64, GS_PSMT8, 8, 3,    1, 0, pList->uPalAddr, GS_PSMCT16, 1, 0, 1));
	#endif
    GSGifRegAD(GS_REG_ALPHA_1,GS_SET_ALPHA(0,1,0,1, 0x80));

    pList->pFixedColor = (Uint64 *)GSListGetUncachedPtr();
    GSGifRegAD(GS_REG_RGBAQ, 0);
    GSGifTagCloseAD();

    // render fixed color32 -> temp32[1]
    _SNPPURenderLine(1, 0);

    // render main8 -> temp32[0]
    _SNPPURenderTexLine(0, 0, 0x80808080, 0);

	/* A fixed-only second operand already occupies temp32[1].  Uploading and
	   drawing an empty sub-screen texture would leave the same pixels. */
	if (!bFixedSub)
		_SNPPURenderTexLine(1, 1, 0x80808080, 1);

    // render attribs

    GSGifTagOpenAD();

	// tex0_1
	GSGifRegAD(GS_REG_TEX0_1,GS_SET_TEX0(pList->uInputAddr, 256/64, GS_PSMT8, 8, 3,    1, 0, pList->uAttribMainPal, GS_PSMCT32, 0, 0, 1));

    // alpha_1: A = Cs, B = Cd, C = As, D = Cd
    // (a - b) * c + d
    GSGifRegAD(GS_REG_ALPHA_1,GS_SET_ALPHA(1,2,0,2, 0x20));

    GSGifTagCloseAD();

    // render attrib main8 -> temp32 line 0
    _SNPPURenderTexLine(0, 2, 0x80808080, 1);

    GSGifTagOpenAD();

	// tex0_1
	GSGifRegAD(GS_REG_TEX0_1,GS_SET_TEX0(pList->uInputAddr, 256/64, GS_PSMT8, 8, 3,    1, 0, pList->uAttribSubPal, GS_PSMCT32, 0, 0, 1));

    // alpha_1: A = Cs, B = Cd, C = As, D = Cd
    // (a - b) * c + d
    GSGifRegAD(GS_REG_ALPHA_1,GS_SET_ALPHA(1,2,0,2, 0x80));

    GSGifTagCloseAD();

    // render attrib sub8 -> temp32 line 0
    _SNPPURenderTexLine(1, 2, 0x80808080, 1);

    // texflush
    GSGifTagOpenAD();
    GSGifRegAD(GS_REG_TEXFLUSH,0);

    // setup frame register to point to our output texture
	GSGifRegAD(GS_REG_FRAME_1, GS_SET_FRAME((uOutAddr/0x20),512/64,GS_PSMCT16,0 ));

	// tex0_1
	GSGifRegAD(GS_REG_TEX0_1,GS_SET_TEX0(pList->uTempAddr, 256/64, GS_PSMCT32, 8, 3,    1, 0, 0, 0, 0, 0, 0));

    // alpha_1: A = Cs, B = Cd, C = As, D = Cd
    // (a - b) * c + d
    pList->pAddSub = (Uint64 *)GSListGetUncachedPtr();
    GSGifRegAD(GS_REG_ALPHA_1,GS_SET_ALPHA(0,2,2,1, 0x80));

    pList->pXYOffset = (Uint64 *)GSListGetUncachedPtr();
	GSGifRegAD(GS_REG_XYOFFSET_1, 0);

    GSGifTagCloseAD();

    // render out32 = main32 * attrib
    _SNPPURenderTexLineWH(0, 0, 0x80808080, 0, 512, 256);

    // render out16 += sub32 * attrib, duplicated horizontally
    _SNPPURenderTexLineWH(0, 1, 0x80808080, 1, 512, 256);

    /* Preserve the GS state left by the legacy chain even when brightness is
       full.  Only the mathematically redundant drawing primitive is omitted. */
    GSGifTagOpenAD();
    GSGifRegAD(GS_REG_ALPHA_1,GS_SET_ALPHA(1,2,0,2, 0x80 ));
    pList->pIntensity = (Uint64 *)GSListGetUncachedPtr();
    GSGifRegAD(GS_REG_RGBAQ, 0);
    GSGifTagCloseAD();

    if (bApplyIntensity)
    {
        // render out32 *= intensity
        _SNPPURenderLineW(0, 1, 512);
    }

    // close current dma cnt
    GSDmaCntClose();

    // add end tag
    GSDmaEnd();

    GSListEnd();
}

#if 1

static void _SNPPUBlendSetParm(SNPPUDmaListT *pList, Int32 iLine,
	Uint32 uFixedColor16, Bool bAddSub, Uint32 uIntensity,
	Bool bDirectMain)
{
	if (bDirectMain)
	{
		*pList->pXYOffset =
			GS_SET_XYOFFSET(0x8000, 0x8000 - (iLine << 4));
		__asm__ __volatile__ ("sync.l");
		return;
	}

    *pList->pFixedColor = SNPPUColorConvert15to32(uFixedColor16);
    *pList->pXYOffset   = GS_SET_XYOFFSET(0x8000, 0x8000 - (iLine<<4)  );
    *pList->pIntensity  = (uIntensity * 0x80 / 15) << 24;
    if (!bAddSub)
    {
        // add
        *pList->pAddSub     = GS_SET_ALPHA(1,2,2,0, 0x80);
    } else
    {
        // sub
        *pList->pAddSub     = GS_SET_ALPHA(1,0,2,2, 0x80);
    }
    __asm__ __volatile__ ("sync.l");
}

#include "gs.h"

SNPPUBlendGS::SNPPUBlendGS(Uint32 uVramAddr, Uint32 uOutAddr)
{
    SNPPUDmaListT *pList = &m_DmaList;

    m_pDmaBlendInfo = NULL;
	memset(m_uPaletteDirty, 0, sizeof(m_uPaletteDirty));
	m_nPaletteDirty = 0;
	MarkPaletteAllDirty();
    m_bAttribPalettesUploaded = FALSE;
    m_bDmaListHasIntensity = FALSE;
	m_bDmaListReady = FALSE;
	m_bDirectDmaListReady = FALSE;
	m_bDmaListFixedSub = FALSE;
	m_pHiresTrxPos = NULL;
	m_bHiresDmaListReady = FALSE;
	m_nDirectLines = 0;
	m_pDirectExecList = NULL;
	m_bSparsePaletteListReady = FALSE;

    pList->uPalAddr        = uVramAddr + 0x000;
    pList->uInputAddr      = uVramAddr + 0x080 ;
    pList->uAttribMainPal  = uVramAddr + 0x180 ;
    pList->uAttribSubPal   = uVramAddr + 0x184 ;
    pList->uTempAddr       = uVramAddr + 0x200 ;

	pList->uOutAddr = uOutAddr;

	/* Keep the direct and colour-math chains resident independently. A
	   scanline can switch between them without rebuilding lists or flushing
	   the EE data cache. Each also has a full-CLUT upload variant. */
	SNPPUDmaListT *pOtherLists[] = {
		&m_DmaListWithPalette, &m_DirectDmaList,
		&m_DirectDmaListWithPalette
	};
	for (Uint32 i = 0; i < 3; i++)
	{
		SNPPUDmaListT *pOther = pOtherLists[i];
		pOther->uPalAddr       = pList->uPalAddr;
		pOther->uInputAddr     = pList->uInputAddr;
		pOther->uAttribMainPal = pList->uAttribMainPal;
		pOther->uAttribSubPal  = pList->uAttribSubPal;
		pOther->uTempAddr      = pList->uTempAddr;
		pOther->uOutAddr       = pList->uOutAddr;
	}

#if SNDBG_LOG
	DLog("[snes-gs-layout] vram blend/out=%X/%X scratch render/stage=%08X/%08X bytes=%u/%u",
		(unsigned)uVramAddr, (unsigned)uOutAddr,
		(unsigned)PS2MEM_SCRATCHPAD, (unsigned)SNPPU_DMA_BLENDINFO_ADDR,
		(unsigned)sizeof(SnesRender8pInfoT),
		(unsigned)sizeof(SNPPUBlendInfoT));
#endif
}

void SNPPUBlendGS::Exec(SNPPUBlendInfoT *pInfo, Int32 iLine,
	Uint32 uFixedColor32, SNMaskT *pColorMask, Bool bAddSub,
	Uint32 uIntensity, Bool bFixedSub)
{
	SNPPUBlendInfoT *pDmaInfo =
		(SNPPUBlendInfoT *)SNPPU_DMA_BLENDINFO_ADDR;
	SNPPUDmaListT *pExecList;
	Uint128 *pExecChain;
	Bool bUploadPalette;
	Bool bSparsePalette;
	Bool bApplyIntensity = uIntensity < 15;
	Bool bDirectMain = pColorMask == NULL && !bApplyIntensity;
	Uint32 uPaletteCopyBytes;
	Uint64 uDirtyPaletteGroups;
	Uint32 nDirtyPaletteGroups;

	if (!m_pTarget)
	{
		return;
	}
	bFixedSub = bFixedSub && !bDirectMain;
	if (m_nDirectLines &&
	    (!bDirectMain || m_bPaletteDirty || m_pDmaBlendInfo != pInfo ||
	     iLine != m_iDirectFirstLine + (Int32)m_nDirectLines))
		FlushDirectLines();
	if (bDirectMain && m_nDirectLines)
	{
		/* No GIF chain owns this area: the batch has not been submitted yet.
		   Palette changes and every output-path boundary flush above. */
#if SNDBG_LOG
		Uint32 uStart = ProfCtrGetCycle();
#endif
		memcpy((void *)(SNPPU_DMA_HIRES_ADDR + m_nDirectLines * 256u),
			pInfo->uMain8, 256);
		m_nDirectLines++;
#if SNDBG_LOG
		_SNPPUGSDiag.CopyCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.CopyBytes += 256;
		_SNPPUGSDiag.Lines++;
		_SNPPUGSDiag.DirectMainLines++;
#endif
		if (m_nDirectLines == SNPPU_DIRECT_BATCH_LINES)
			FlushDirectLines();
		return;
	}

    if (pColorMask)
    {
        PROF_ENTER("SNPPUBlendPlanarTo3");
        _PlanarTo3(pInfo->uAttrib8, &pColorMask[0],&pColorMask[1],&pColorMask[2]);
        PROF_LEAVE("SNPPUBlendPlanarTo3");
    }

    // wait for previous dma to finish
    PROF_ENTER("SNPPUGS");
#if SNDBG_LOG
	{
		Uint32 uStart = ProfCtrGetCycle();
		DmaSyncGIF();
		_SNPPUGSDiag.SyncCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.SyncCalls++;
		#if SNDBG_DEEP
		_SNPPUGSValidateStage(pDmaInfo);
		#endif
	}
#else
    DmaSyncGIF();
#endif
    PROF_LEAVE("SNPPUGS");

	if (m_pDmaBlendInfo != pInfo)
	{
		m_pDmaBlendInfo = pInfo;
		m_bDmaListReady = FALSE;
		m_bDirectDmaListReady = FALSE;
	}
	SNPPUDmaListT *pRenderList = bDirectMain ?
		&m_DirectDmaList : &m_DmaList;
	SNPPUDmaListT *pPaletteList = bDirectMain ?
		&m_DirectDmaListWithPalette : &m_DmaListWithPalette;
	if (bDirectMain ? !m_bDirectDmaListReady :
	    (!m_bDmaListReady || m_bDmaListHasIntensity != bApplyIntensity ||
	     m_bDmaListFixedSub != bFixedSub))
    {
		/* The sync above makes it safe to rebuild a list when a fade crosses
		   brightness 15.  REF tags always point at the stable staging copy,
		   never at the scanline buffer that RenderLine8 is about to reuse. */
		_SNPPUBlendBuildList(pRenderList, pDmaInfo,
		                      pRenderList->uOutAddr, FALSE, bApplyIntensity,
		                      bDirectMain, bFixedSub);
		_SNPPUBlendBuildList(pPaletteList, pDmaInfo,
		                      pPaletteList->uOutAddr, TRUE,
		                      bApplyIntensity, bDirectMain, bFixedSub);

        // flush cache
        FlushCache(0);

		if (bDirectMain)
			m_bDirectDmaListReady = TRUE;
		else
		{
			m_bDmaListReady = TRUE;
			m_bDmaListHasIntensity = bApplyIntensity;
			m_bDmaListFixedSub = bFixedSub;
		}
    }

	/* The previous GIF chain is done with the staging area now. Main and
	   attributes change every line; sub is omitted when fixed colour is the
	   only second operand. Snapshot the dirty CLUT groups before the
	   CPU copy clears them: up to eight qword groups use an exact partial GS
	   upload, while larger changes retain the single 1 KiB burst. */
	bUploadPalette = m_bPaletteDirty;
	if (bUploadPalette)
	{
		uDirtyPaletteGroups = GetDirtyPaletteGroups();
		nDirtyPaletteGroups =
			(Uint32)__builtin_popcountll(uDirtyPaletteGroups);
		bSparsePalette = nDirtyPaletteGroups > 0 &&
			nDirtyPaletteGroups <= SNPPU_SPARSE_PALETTE_MAX_GROUPS;
	}
	else
	{
		uDirtyPaletteGroups = 0;
		nDirtyPaletteGroups = 0;
		bSparsePalette = FALSE;
	}
#if SNDBG_LOG
	{
		Uint32 uStart = ProfCtrGetCycle();
		#if SNDBG_DEEP
		Uint32 uSourceHash;
		Uint32 uStageHash;
		#endif
		uPaletteCopyBytes = CopyDirtyPalette(pDmaInfo->Pal, pInfo->Pal);
		memcpy(bDirectMain ? (void *)SNPPU_DMA_HIRES_ADDR :
		       (void *)pDmaInfo->uMain8, pInfo->uMain8, sizeof(pDmaInfo->uMain8));
		if (!bDirectMain)
		{
			if (!bFixedSub)
				memcpy(pDmaInfo->uSub8, pInfo->uSub8,
				       sizeof(pDmaInfo->uSub8));
			memcpy(pDmaInfo->uAttrib8, pInfo->uAttrib8, sizeof(pDmaInfo->uAttrib8));
		}
#if SNDBG_DEEP
		if (bDirectMain || bFixedSub)
		{
			/* Keep full staging validation meaningful in the intrusive build. */
			if (bDirectMain)
				memcpy(pDmaInfo->uMain8, pInfo->uMain8, 256);
			memcpy(pDmaInfo->uSub8, pInfo->uSub8, sizeof(pDmaInfo->uSub8));
			if (bDirectMain)
				memcpy(pDmaInfo->uAttrib8, pInfo->uAttrib8,
				       sizeof(pDmaInfo->uAttrib8));
		}
#endif
		_SNPPUGSDiag.CopyCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.CopyBytes += sizeof(pDmaInfo->uMain8);
		if (!bDirectMain)
		{
			_SNPPUGSDiag.CopyBytes += sizeof(pDmaInfo->uAttrib8);
			if (!bFixedSub)
				_SNPPUGSDiag.CopyBytes += sizeof(pDmaInfo->uSub8);
		}
#if SNDBG_DEEP
		if (bDirectMain)
			_SNPPUGSDiag.CopyBytes += sizeof(pDmaInfo->uMain8) + sizeof(pDmaInfo->uSub8) +
				sizeof(pDmaInfo->uAttrib8);
		else if (bFixedSub)
			_SNPPUGSDiag.CopyBytes += sizeof(pDmaInfo->uSub8);
#endif
		if (bUploadPalette)
		{
			_SNPPUGSDiag.CopyBytes += uPaletteCopyBytes;
			_SNPPUGSDiag.PaletteUploads++;
			if (bSparsePalette)
			{
				_SNPPUGSDiag.PaletteSparseUploads++;
				_SNPPUGSDiag.PaletteTransferBytes +=
					nDirtyPaletteGroups * 16u;
			}
			else
			{
				_SNPPUGSDiag.PaletteFullUploads++;
				_SNPPUGSDiag.PaletteTransferBytes += sizeof(PaletteT);
			}
		}
		#if SNDBG_DEEP
		uSourceHash = _SNPPUGSSample(pInfo, NULL);
		uStageHash = _SNPPUGSSample(pDmaInfo, _SNPPUGSDiag.Expected);
		if (uSourceHash != uStageHash)
			_SNPPUGSDiag.CopyMismatch++;
		_SNPPUGSDiag.SourceHash =
			(_SNPPUGSDiag.SourceHash << 5) ^ uSourceHash ^ (Uint32)iLine;
		_SNPPUGSDiag.StageHash =
			(_SNPPUGSDiag.StageHash << 5) ^ uStageHash ^ (Uint32)iLine;
		_SNPPUGSDiag.HasExpected = TRUE;
		#endif
	}
#else
	uPaletteCopyBytes = CopyDirtyPalette(pDmaInfo->Pal, pInfo->Pal);
	(void)uPaletteCopyBytes;
	memcpy(bDirectMain ? (void *)SNPPU_DMA_HIRES_ADDR :
		       (void *)pDmaInfo->uMain8, pInfo->uMain8, sizeof(pDmaInfo->uMain8));
	if (!bDirectMain)
	{
		if (!bFixedSub)
			memcpy(pDmaInfo->uSub8, pInfo->uSub8,
			       sizeof(pDmaInfo->uSub8));
		memcpy(pDmaInfo->uAttrib8, pInfo->uAttrib8, sizeof(pDmaInfo->uAttrib8));
	}
#endif
	pExecList = (bUploadPalette && !bSparsePalette) ?
		pPaletteList : pRenderList;
	if (bDirectMain)
	{
		m_pDirectExecList = pExecList;
		m_uDirectDirtyGroups = uDirtyPaletteGroups;
		m_bDirectSparsePalette = bSparsePalette;
		m_iDirectFirstLine = iLine;
		m_nDirectLines = 1;
#if SNDBG_LOG
		_SNPPUGSDiag.Lines++;
		_SNPPUGSDiag.DirectMainLines++;
#endif
		return;
	}

    PROF_ENTER("SNPPUBlendExec");

    // set parameters of dma-list
    _SNPPUBlendSetParm(pExecList, iLine, uFixedColor32, bAddSub,
		uIntensity, bDirectMain);
	if (bSparsePalette)
		pExecChain = BuildSparsePaletteList(pDmaInfo->Pal,
			uDirtyPaletteGroups, pExecList);
	else
		pExecChain = pExecList->Data;

    PROF_LEAVE("SNPPUBlendExec");

    // transfer render ilst
#if SNDBG_LOG
	{
		Uint32 uStart = ProfCtrGetCycle();
		DmaExecGIFChain(pExecChain);
		_SNPPUGSDiag.KickCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.Lines++;
		if (bApplyIntensity)
			_SNPPUGSDiag.IntensityLines++;
		if (bDirectMain)
			_SNPPUGSDiag.DirectMainLines++;
		if (bFixedSub)
			_SNPPUGSDiag.FixedSubLines++;
	}
#else
    DmaExecGIFChain(pExecChain);
#endif

}

void SNPPUBlendGS::FlushDirectLines()
{
	if (!m_nDirectLines) return;
	SNPPUDmaListT *pList = m_pDirectExecList;
	Uint32 nLines = m_nDirectLines;
	_SNPPUBlendSetParm(pList, m_iDirectFirstLine, 0, FALSE, 15, TRUE);
	*pList->pDirectTrxReg = GS_SET_TRXREG(256, nLines);
	*pList->pDirectImageCount = (Uint16)(nLines * 16u);
	*pList->pDirectRefCount = (Uint16)(nLines * 16u);
	*pList->pDirectUVEnd = GS_SET_UV(256 << 4, nLines << 4);
	Uint32 uEndY = 0x8000 + (nLines << 4);
	*pList->pDirectXYZEnd = GS_SET_XYZ(0xA000, uEndY, 0);
	Uint128 *pChain = m_bDirectSparsePalette ?
		BuildSparsePaletteList((PaletteT *)SNPPU_DMA_BLENDINFO_ADDR,
			m_uDirectDirtyGroups, pList) : pList->Data;
	__asm__ __volatile__ ("sync.l");
#if SNDBG_LOG
	Uint32 uStart = ProfCtrGetCycle();
#endif
	DmaExecGIFChain(pChain);
#if SNDBG_LOG
	_SNPPUGSDiag.KickCycles += ProfCtrGetCycle() - uStart;
#endif
	m_nDirectLines = 0;
}

static void _SNPPUBlendBuildHiresList(Uint128 *pDmaList,
	Uint32 nDmaQwords, Uint32 uOutAddr, Uint64 **ppTrxPos)
{
	*ppTrxPos = NULL;

	GSListBegin(pDmaList, nDmaQwords, NULL);
	GSDmaCntOpen();
	_GPFifoUploadTextureTracked(
		uOutAddr * 0x100,
		512, 0, 0, GS_PSMCT16,
		(void *)(SNPPU_DMA_HIRES_ADDR | 0x80000000),
		512, 1, ppTrxPos);
	GSDmaCntClose();
	GSDmaEnd();
	GSListEnd();
}

void SNPPUBlendGS::ExecHires512(const Uint16 *pLine512, Int32 iLine)
{
	Uint16 *pStage = (Uint16 *)SNPPU_DMA_HIRES_ADDR;

	if (!m_pTarget || !pLine512)
		return;
	FlushDirectLines();

	/* One GIF transfer per rendered scanline, just like the normal blender.
	   The EE owns SNES semantics and writes final BGR555 pixels; the GS owns
	   only transport/storage. Reusing scratchpad avoids D-cache writeback and
	   lets the next scanline's MIPS work overlap the previous GIF transfer. */
#if SNDBG_LOG
	{
		Uint32 uStart = ProfCtrGetCycle();
		DmaSyncGIF();
		_SNPPUGSDiag.SyncCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.SyncCalls++;
	}
#else
	DmaSyncGIF();
#endif

#if SNDBG_LOG
	Uint32 uStart = ProfCtrGetCycle();
#endif
	memcpy(pStage, pLine512, SNPPU_DMA_HIRES_BYTES);
#if SNDBG_LOG
	_SNPPUGSDiag.CopyCycles += ProfCtrGetCycle() - uStart;
#endif
	SubmitHiresLine(iLine);
}

Bool SNPPUBlendGS::ExecHiresIndexed(const Uint8 *pMain, const Uint8 *pSub,
	const Uint16 *pPalette, Int32 iLine, Uint16 *pCache)
{
	if (!m_pTarget) return FALSE;
	FlushDirectLines();
	/* The previous transfer owns this staging area until GIF completes.
	   Build final pixels directly here instead of stack -> scratchpad copy. */
#if SNDBG_LOG
	Uint32 uStart = ProfCtrGetCycle();
#endif
	DmaSyncGIF();
#if SNDBG_LOG
	_SNPPUGSDiag.SyncCycles += ProfCtrGetCycle() - uStart;
	_SNPPUGSDiag.SyncCalls++;
	uStart = ProfCtrGetCycle();
#endif
	Uint32 *pStage = (Uint32 *)SNPPU_DMA_HIRES_ADDR;
	SnesPPUBuildHiresOutput32(pStage, pMain, pSub, pPalette);
	if (pCache) memcpy(pCache, pStage, SNPPU_DMA_HIRES_BYTES);
#if SNDBG_LOG
	_SNPPUGSDiag.CopyCycles += ProfCtrGetCycle() - uStart;
#endif
	SubmitHiresLine(iLine);
	return TRUE;
}

void SNPPUBlendGS::SubmitHiresLine(Int32 iLine)
{

	/* The upload source, format and destination texture never change.  Build
	   the GIF/DMA chain once, then patch only TRXPOS.DSAY for each line. */
	if (!m_bHiresDmaListReady)
	{
		_SNPPUBlendBuildHiresList(m_HiresDmaList,
			sizeof(m_HiresDmaList) / sizeof(m_HiresDmaList[0]),
			m_DmaList.uOutAddr, &m_pHiresTrxPos);

		/* GSList writes through the cached alias.  Publish the immutable chain
		   once; subsequent line patches use the uncached address captured above. */
		FlushCache(0);
		m_bHiresDmaListReady = TRUE;
	}

	*m_pHiresTrxPos = GS_SET_TRXPOS(0, 0, 0, iLine, 0);
	__asm__ __volatile__ ("sync.l");

#if SNDBG_LOG
	{
		Uint32 uStart = ProfCtrGetCycle();
		DmaExecGIFChain(m_HiresDmaList);
		_SNPPUGSDiag.KickCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.Lines++;
		_SNPPUGSDiag.CopyBytes += SNPPU_DMA_HIRES_BYTES;
	}
#else
	DmaExecGIFChain(m_HiresDmaList);
#endif
}

void SNPPUBlendGS::Clear(SNPPUBlendInfoT *pInfo, Int32 iLine)
{
    // render clear line
    Exec(pInfo, iLine, 0, NULL, 0, 0, FALSE);
}

#endif
