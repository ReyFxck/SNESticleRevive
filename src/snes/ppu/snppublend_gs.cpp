

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

typedef char SNPPUScratchLayoutCheck[
	(sizeof(SnesRender8pInfoT) <= SNPPU_DMA_BLENDINFO_OFFSET &&
	 SNPPU_DMA_BLENDINFO_OFFSET + sizeof(SNPPUBlendInfoT) <=
		PS2MEM_SNES_LOOKUP_OFFSET &&
	 PS2MEM_SNES_LOOKUP_OFFSET + PS2MEM_SNES_LOOKUP_SIZE <= 16 * 1024)
		? 1 : -1];

#if SNDBG_LOG
#define SNPPU_GS_DIAG_SAMPLES 8
struct SNPPUGSDiagT
{
	Uint32 Frames;
	Uint32 Lines;
	Uint32 SyncCalls;
	Uint32 SyncCycles;
	Uint32 CopyCycles;
	Uint32 KickCycles;
	Uint32 CopyBytes;
	Uint32 PaletteUploads;
	Uint32 IntensityLines;
	Uint32 DirectMainLines;
	Uint32 StageMismatch;
	Uint32 CopyMismatch;
	Uint32 SourceHash;
	Uint32 StageHash;
	Uint32 Expected[SNPPU_GS_DIAG_SAMPLES];
	Bool   HasExpected;
};

static SNPPUGSDiagT _SNPPUGSDiag;

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

static Uint32 _SNPPUBlend_AttribMainPal[8] _ALIGN(16) =
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


static Uint32 _SNPPUBlend_AttribSubPal[8] _ALIGN(16) =
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



static void _PlanarTo3(Uint8 *pDest, SNMaskT *pSrc0, SNMaskT *pSrc1, SNMaskT *pSrc2)
{
	Uint32 nBytes = 256 / 8;
	SnesChrLookupT *pPlaneLookup =
		(SnesChrLookupT *)PS2MEM_SNES_LOOKUP_ADDR;
	SnesChrLookup64T *pLookup64 =
		(SnesChrLookup64T *)&pPlaneLookup[1];
	Uint64 *pDest64 = (Uint64 *)pDest;


	while (nBytes > 0)
	{
		Uint64 uData;

		uData  = (*pLookup64)[pSrc0->uMask8[0]] << 0;	
		uData |= (*pLookup64)[pSrc1->uMask8[0]] << 1;	
		uData |= (*pLookup64)[pSrc2->uMask8[0]] << 2;	


		pSrc0  = (SNMaskT *) (((Uint8 *)pSrc0) + 1);
		pSrc1  = (SNMaskT *) (((Uint8 *)pSrc1) + 1);
		pSrc2  = (SNMaskT *) (((Uint8 *)pSrc2) + 1);

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


static void _GPFifoUploadTexture(int TBP, int TBW, int xofs, int yofs, int pxlfmt, void *tex, int wpxls, int hpxls)
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
    GSGifRegAD(GS_REG_TRXPOS,GS_SET_TRXPOS(0,0,xofs,yofs,0));
    GSGifRegAD(GS_REG_TRXREG,GS_SET_TRXREG(wpxls, hpxls));
    GSGifRegAD(GS_REG_TRXDIR,GS_SET_TRXDIR(0));
    
    GSGifTagCloseAD();
    
    // image gif tag
    GSGifTagImage(numq);

    // close last dma cnt
    GSDmaCntClose();

    // dma image data
    GSDmaRef((Uint128 *)tex, numq);


    // start new dma cnt
    GSDmaCntOpen();
}


static void _SNPPURenderTexLine(Int32 iDestLine, Int32 iSrcLine, Uint32 RGBA, int abe)
{
    int x1,x2,y1,y2;
    int u1,u2,v1,v2;

    x1  =   0 << 4;
    x2  = 256 << 4;
    y1  = (iDestLine + 0) << 4;
    y2  = (iDestLine + 1) << 4;

    u1  =   0 << 4;
    u2  = 256 << 4;
    v1  = (iSrcLine + 0) << 4;
    v2  = (iSrcLine + 1) << 4;

    x1+=0x8000;
    y1+=0x8000;
    x2+=0x8000;
    y2+=0x8000;
    
    GSGifTagOpen(GIF_SET_TAG(1, 1, 0, 0, 1, 6), 0xF535310);
    
	GSGifReg(GS_SET_PRIM(0x06, 0, 1, 0, abe, 0, 1, 0, 0));
	GSGifReg(RGBA);
	GSGifReg(GS_SET_UV(u1, v1));
	GSGifReg(GS_SET_XYZ(x1,y1,0));
	GSGifReg(GS_SET_UV(u2, v2));
	GSGifReg(GS_SET_XYZ(x2,y2,0));
    
    GSGifTagClose();

}


static void _SNPPURenderLine(Int32 iDestLine, int abe)
{
    int x1,x2,y1,y2;

    x1  =   0 << 4;
    x2  = 256 << 4;
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
  


void SNPPUBlendGS::Begin(CRenderSurface *pTarget)
{
#if SNDBG_LOG
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

       Note: only 8 Uint32 of source are valid but the upload size is
       16 x 16 PSMCT32 (1024 bytes). The blender uses CSM1 which
       expects the palette to be laid out in a 16x16 PSMCT32 tile, so
       we keep the same dimensions as the legacy upload. The 992
       bytes past the end of _SNPPUBlend_AttribMainPal are unused by
       the blender (TEXCLUT only reads the first eight entries) so
       the over-read is benign and matches pre-Fase-3 behaviour. */
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
	_SNPPUGSDiag.Frames++;
	if (_SNPPUGSDiag.Frames >= SNDBG_FRAME_PERIOD)
	{
		Uint32 uLines = _SNPPUGSDiag.Lines ? _SNPPUGSDiag.Lines : 1;
		Uint32 uSync = _SNPPUGSDiag.SyncCalls ? _SNPPUGSDiag.SyncCalls : 1;
		DLog("[snes-gs] frames/lines=%u/%u avgcyc sync/copy/kick=%u/%u/%u avg-copy-bytes=%u pal-uploads=%u intensity-lines=%u direct-main-lines=%u",
			(unsigned)_SNPPUGSDiag.Frames, (unsigned)_SNPPUGSDiag.Lines,
			(unsigned)(_SNPPUGSDiag.SyncCycles / uSync),
			(unsigned)(_SNPPUGSDiag.CopyCycles / uLines),
			(unsigned)(_SNPPUGSDiag.KickCycles / uLines),
			(unsigned)(_SNPPUGSDiag.CopyBytes / uLines),
			(unsigned)_SNPPUGSDiag.PaletteUploads,
			(unsigned)_SNPPUGSDiag.IntensityLines,
			(unsigned)_SNPPUGSDiag.DirectMainLines);
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
	}
#endif

    m_pTarget = NULL;
}




static void _SNPPUBlendBuildList(SNPPUDmaListT *pList,
	SNPPUBlendInfoT *pInfo, Uint32 uOutAddr, Bool bUploadPalette,
	Bool bApplyIntensity, Bool bDirectMain)
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


    _GPFifoUploadTexture(
         pList->uInputAddr * 0x100, 
         256, 0, 0, 
         GS_PSMT8, 
         (void *)(((Uint32)pInfo->uMain8) | 0x80000000), 
         256, 
         1);

	if (bDirectMain)
	{
		/* No CGADSUB target and no main-screen color clipping: the SNES
		   result is exactly the palette-expanded main screen.  Write it to
		   the output in one primitive instead of constructing temp main/sub
		   colors and two attribute masks that can no longer affect a pixel. */
		GSGifTagOpenAD();
		GSGifRegAD(GS_REG_TEXFLUSH, 0);
		GSGifRegAD(GS_REG_FRAME_1,
			GS_SET_FRAME((uOutAddr/0x20), 256/64, GS_PSMCT32, 0));
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

		_SNPPURenderTexLine(0, 0, 0x80808080, 0);

		GSDmaCntClose();
		GSDmaEnd();
		GSListEnd();
		return;
	}

    _GPFifoUploadTexture(
         pList->uInputAddr * 0x100, 
         256, 0, 1, 
         GS_PSMT8, 
         (void *)(((Uint32)pInfo->uSub8) | 0x80000000), 
         256, 
         1);

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

    // render sub8 -> temp32[1] (alpha=0 means use fixed color)
    _SNPPURenderTexLine(1, 1, 0x80808080, 1);


    //
    // render attribs
    //



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
	GSGifRegAD(GS_REG_FRAME_1, GS_SET_FRAME((uOutAddr/0x20),256/64,GS_PSMCT32,0 ));

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
    _SNPPURenderTexLine(0, 0, 0x80808080, 0);

    // render out32 += sub32 * attrib
    _SNPPURenderTexLine(0, 1, 0x80808080, 1);

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
        _SNPPURenderLine(0, 1);
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
    SNPPUDmaListT *pPaletteList = &m_DmaListWithPalette;

    m_pDmaBlendInfo = NULL;
	memset(m_uPaletteDirty, 0, sizeof(m_uPaletteDirty));
	m_nPaletteDirty = 0;
	MarkPaletteAllDirty();
    m_bAttribPalettesUploaded = FALSE;
    m_bDmaListHasIntensity = FALSE;
	m_bDmaListDirectMain = FALSE;

    pList->uPalAddr        = uVramAddr + 0x000;
    pList->uInputAddr      = uVramAddr + 0x080 ;
    pList->uAttribMainPal  = uVramAddr + 0x180 ;
    pList->uAttribSubPal   = uVramAddr + 0x184 ;
    pList->uTempAddr       = uVramAddr + 0x200 ;

	pList->uOutAddr = uOutAddr;

	/* Both chains render identically.  The larger one refreshes the CLUT;
	   the normal per-line chain reuses it and avoids 1 KiB of GS traffic. */
	pPaletteList->uPalAddr       = pList->uPalAddr;
	pPaletteList->uInputAddr     = pList->uInputAddr;
	pPaletteList->uAttribMainPal = pList->uAttribMainPal;
	pPaletteList->uAttribSubPal  = pList->uAttribSubPal;
	pPaletteList->uTempAddr      = pList->uTempAddr;
	pPaletteList->uOutAddr       = pList->uOutAddr;

#if SNDBG_LOG
	DLog("[snes-gs-layout] vram blend/out=%X/%X scratch render/stage=%08X/%08X bytes=%u/%u",
		(unsigned)uVramAddr, (unsigned)uOutAddr,
		(unsigned)PS2MEM_SCRATCHPAD, (unsigned)SNPPU_DMA_BLENDINFO_ADDR,
		(unsigned)sizeof(SnesRender8pInfoT),
		(unsigned)sizeof(SNPPUBlendInfoT));
#endif
}

void SNPPUBlendGS::Exec(SNPPUBlendInfoT *pInfo, Int32 iLine, Uint32 uFixedColor32, SNMaskT *pColorMask, Bool bAddSub, Uint32 uIntensity)
{
	SNPPUBlendInfoT *pDmaInfo =
		(SNPPUBlendInfoT *)SNPPU_DMA_BLENDINFO_ADDR;
	SNPPUDmaListT *pExecList;
	Bool bUploadPalette;
	Bool bApplyIntensity = uIntensity < 15;
	Bool bDirectMain = pColorMask == NULL && !bApplyIntensity;
	Uint32 uPaletteCopyBytes;

	if (!m_pTarget)
	{
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

    if (m_pDmaBlendInfo != pInfo ||
        m_bDmaListHasIntensity != bApplyIntensity ||
		m_bDmaListDirectMain != bDirectMain)
    {
		/* The sync above makes it safe to rebuild a list when a fade crosses
		   brightness 15.  REF tags always point at the stable staging copy,
		   never at the scanline buffer that RenderLine8 is about to reuse. */
		_SNPPUBlendBuildList(&m_DmaList, pDmaInfo,
		                      m_DmaList.uOutAddr, FALSE, bApplyIntensity,
		                      bDirectMain);
		_SNPPUBlendBuildList(&m_DmaListWithPalette, pDmaInfo,
		                      m_DmaListWithPalette.uOutAddr, TRUE,
		                      bApplyIntensity, bDirectMain);

        // flush cache
        FlushCache(0);

        m_pDmaBlendInfo = pInfo;
        m_bDmaListHasIntensity = bApplyIntensity;
		m_bDmaListDirectMain = bDirectMain;
    }

	/* The previous GIF chain is done with the staging area now.  Main, sub
	   and attributes change every line; the 1 KiB CLUT is copied and sent
	   only when CGRAM changed (and once after Begin because scratchpad is
	   shared between frames). */
	bUploadPalette = m_bPaletteDirty;
#if SNDBG_LOG
	{
		Uint32 uStart = ProfCtrGetCycle();
		#if SNDBG_DEEP
		Uint32 uSourceHash;
		Uint32 uStageHash;
		#endif
		uPaletteCopyBytes = CopyDirtyPalette(pDmaInfo->Pal, pInfo->Pal);
		memcpy(pDmaInfo->uMain8, pInfo->uMain8, sizeof(pDmaInfo->uMain8));
		if (!bDirectMain)
		{
			memcpy(pDmaInfo->uSub8, pInfo->uSub8, sizeof(pDmaInfo->uSub8));
			memcpy(pDmaInfo->uAttrib8, pInfo->uAttrib8, sizeof(pDmaInfo->uAttrib8));
		}
#if SNDBG_DEEP
		else
		{
			/* Keep full staging validation meaningful in the intrusive build. */
			memcpy(pDmaInfo->uSub8, pInfo->uSub8, sizeof(pDmaInfo->uSub8));
			memcpy(pDmaInfo->uAttrib8, pInfo->uAttrib8, sizeof(pDmaInfo->uAttrib8));
		}
#endif
		_SNPPUGSDiag.CopyCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.CopyBytes += sizeof(pDmaInfo->uMain8);
		if (!bDirectMain)
			_SNPPUGSDiag.CopyBytes += sizeof(pDmaInfo->uSub8) +
				sizeof(pDmaInfo->uAttrib8);
#if SNDBG_DEEP
		else
			_SNPPUGSDiag.CopyBytes += sizeof(pDmaInfo->uSub8) +
				sizeof(pDmaInfo->uAttrib8);
#endif
		if (bUploadPalette)
		{
			_SNPPUGSDiag.CopyBytes += uPaletteCopyBytes;
			_SNPPUGSDiag.PaletteUploads++;
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
	memcpy(pDmaInfo->uMain8, pInfo->uMain8, sizeof(pDmaInfo->uMain8));
	if (!bDirectMain)
	{
		memcpy(pDmaInfo->uSub8, pInfo->uSub8, sizeof(pDmaInfo->uSub8));
		memcpy(pDmaInfo->uAttrib8, pInfo->uAttrib8, sizeof(pDmaInfo->uAttrib8));
	}
#endif
	pExecList = bUploadPalette ? &m_DmaListWithPalette : &m_DmaList;

    PROF_ENTER("SNPPUBlendExec");

    // set parameters of dma-list
    _SNPPUBlendSetParm(pExecList, iLine, uFixedColor32, bAddSub,
		uIntensity, bDirectMain);

    PROF_LEAVE("SNPPUBlendExec");

    // transfer render ilst
#if SNDBG_LOG
	{
		Uint32 uStart = ProfCtrGetCycle();
		DmaExecGIFChain(pExecList->Data);
		_SNPPUGSDiag.KickCycles += ProfCtrGetCycle() - uStart;
		_SNPPUGSDiag.Lines++;
		if (bApplyIntensity)
			_SNPPUGSDiag.IntensityLines++;
		if (bDirectMain)
			_SNPPUGSDiag.DirectMainLines++;
	}
#else
    DmaExecGIFChain(pExecList->Data);
#endif

}




void SNPPUBlendGS::Clear(SNPPUBlendInfoT *pInfo, Int32 iLine)
{
    // render clear line
    Exec(pInfo, iLine, 0, NULL, 0, 0);
}








#endif
