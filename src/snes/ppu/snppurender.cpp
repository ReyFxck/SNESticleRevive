/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snppurender behavior for SNES picture processing.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "console.h"
#include "snppu.h"
#include "snppurender.h"
#include "snppucolor.h"
#include "snppuchrcache.h"
#include "snppuhirlinecache.h"
#include "rendersurface.h"
#include "snmask.h"
#include "snmaskop.h"
#include "prof.h"
#include "sndbglog.h"
#if CODE_PLATFORM == CODE_PS2
#include "ps2mem.h"
#include "ps2dma.h"
#endif

#define SNPPURENDER_INFOSCRATCHPAD ((CODE_PLATFORM == CODE_PS2) && TRUE)
#ifndef SNPPU_BG_CACHE
#define SNPPU_BG_CACHE (TRUE)
#endif

/*

render order

mode 01, bgmode8=0

bg4lo
bg3lo
obj0
bg4hi
bg3hi
obj1
bg2lo
bg1lo
obj2
bg2hi
bg1hi
obj3

mode 01, bgmode8=1

bg4lo
bg3lo
obj0
bg4hi
obj1
bg2lo
bg1lo
obj2
bg2hi
bg1hi
obj3
bg3hi

*/

Uint8 _tm = 0x3F;
Uint8 _tmw = 0x3F;
Uint8 _ts = 0x3F;
Uint8 _tsw = 0x3F;

#if CODE_PLATFORM == CODE_PS2 && SNPPU_BG_CACHE
struct SnesPPUHiresLineCacheEntryT
{
	SnesPPUHiresLineCacheKeyT Key;
	/* A changing scene must not copy 1 KiB into a cache entry on every miss.
	   One complete cache line of metadata keeps pixels 64-byte aligned and
	   admits the large payload only after the exact key repeats. */
	Uint32 uReady;
	Uint8 uAdmissionPad[60];
	Uint16 uPixels[SNPPU_HIRES_LINE_PIXELS];
};

static SnesPPUHiresLineCacheEntryT
	_SnesPPU_HiresLineCache[SNPPU_HIRES_LINE_CACHE_LINES] _ALIGN(64);
static Uint32 _SnesPPU_OutputGeneration = 1;

typedef char SnesPPUHiresLineEntrySizeCheck[
	(sizeof(SnesPPUHiresLineCacheEntryT) == 18 * 64) ? 1 : -1];

static _INLINE void _SnesPPUBuildHiresLineState(
	SnesPPUHiresLineStateT *pState, const SnesPPURegsT *pRegs)
{
	memset(pState, 0, sizeof(*pState));
	pState->uScroll[0] = pRegs->bg1hofs.w;
	pState->uScroll[1] = pRegs->bg1vofs.w;
	pState->uScroll[2] = pRegs->bg2hofs.w;
	pState->uScroll[3] = pRegs->bg2vofs.w;
	pState->uScroll[4] = pRegs->bg3hofs.w;
	pState->uScroll[5] = pRegs->bg3vofs.w;
	pState->uScroll[6] = pRegs->bg4hofs.w;
	pState->uScroll[7] = pRegs->bg4vofs.w;
	pState->uOAMPriority = pRegs->oampri.w;
	pState->uColorData = pRegs->coldata;
	pState->uInidisp = pRegs->inidisp;
	pState->uObsel = pRegs->obsel;
	pState->uBGMode = pRegs->bgmode;
	pState->uMosaic = pRegs->mosaic;
	pState->uBG1SC = pRegs->bg1sc;
	pState->uBG2SC = pRegs->bg2sc;
	pState->uBG3SC = pRegs->bg3sc;
	pState->uBG4SC = pRegs->bg4sc;
	pState->uBG12NBA = pRegs->bg12nba;
	pState->uBG34NBA = pRegs->bg34nba;
	pState->uW12Sel = pRegs->w12sel;
	pState->uW34Sel = pRegs->w34sel;
	pState->uWObjSel = pRegs->wobjsel;
	pState->uWH0 = pRegs->wh0;
	pState->uWH1 = pRegs->wh1;
	pState->uWH2 = pRegs->wh2;
	pState->uWH3 = pRegs->wh3;
	pState->uWBGLog = pRegs->wbglog;
	pState->uWObjLog = pRegs->wobjlog;
	pState->uTM = pRegs->tm;
	pState->uTS = pRegs->ts;
	pState->uTMW = pRegs->tmw;
	pState->uTSW = pRegs->tsw;
	pState->uCGWSel = pRegs->cgwsel;
	pState->uCGADSub = pRegs->cgadsub;
	pState->uSetIni = pRegs->setini;
	pState->uField = SnesPPUHiresLineFieldKey(
		(Uint8)pRegs->setini, (Uint8)pRegs->stat78);
	pState->uRenderTM = _tm;
	pState->uRenderTS = _ts;
	pState->uRenderTMW = _tmw;
	pState->uRenderTSW = _tsw;
}
#endif

void SnesPPUInvalidateOutputCache()
{
#if CODE_PLATFORM == CODE_PS2 && SNPPU_BG_CACHE
	_SnesPPU_OutputGeneration++;
	if (_SnesPPU_OutputGeneration == 0)
	{
		memset(_SnesPPU_HiresLineCache, 0,
			sizeof(_SnesPPU_HiresLineCache));
		_SnesPPU_OutputGeneration = 1;
	}
#endif
}

#if CODE_PLATFORM == CODE_PS2
#define PS2_RENDERINFOADDR  (PS2MEM_SCRATCHPAD +  0*1024)
#endif

SnesChrLookupT _SnesPPU_PlaneLookup[2] _ALIGN(32);
Uint8 _SnesPPU_HFlipLookup[2][256] _ALIGN(32);

static Bool _SnesPPU_bInitialized=FALSE;

static Uint8 _HFlipBits(Uint8 Bits)
{
	Uint8 FlipBits=0;

	for (int n=0; n<8; n++)
		if (Bits&(1<<n)) FlipBits|=(0x80 >> n);

	return FlipBits;
}

static void _BuildPlaneLookup()
{
	Uint32 i, iBit;

	for (i=0; i < 256; i++)
	{
		Uint8 *pBits;

		pBits = (Uint8 *)&_SnesPPU_PlaneLookup[0][i];
		for (iBit=0; iBit < 8; iBit++)
		{
			pBits[iBit] = ((i<<iBit) & 0x80) ? 1 : 0;
		}

		pBits = (Uint8 *)&_SnesPPU_PlaneLookup[1][i];
		for (iBit=0; iBit < 8; iBit++)
		{
			pBits[iBit] = ((i>>iBit) & 0x01) ? 1 : 0;
		}
	}

	for (i=0; i < 256; i++)
	{
		_SnesPPU_HFlipLookup[0][i] = i;
		_SnesPPU_HFlipLookup[1][i] = _HFlipBits(i);
	}

	#if CODE_PLATFORM == CODE_PS2
	/* Estes lookups somam exatamente 4,5 KiB e permanecem acima do buffer
	   temporario do mixer SPC. O caminho BG passa a le-los no scratchpad,
	   deixando o pequeno D-cache da EE disponivel para VRAM e estado PPU. */
	memcpy((void *)PS2MEM_SNES_LOOKUP_ADDR,
		_SnesPPU_PlaneLookup, sizeof(_SnesPPU_PlaneLookup));
	memcpy((void *)(PS2MEM_SNES_LOOKUP_ADDR +
			sizeof(_SnesPPU_PlaneLookup)),
		_SnesPPU_HFlipLookup, sizeof(_SnesPPU_HFlipLookup));
	#endif
}

void _DrawMask(Uint32 *pDest, SNMaskT *pMask, Int32 nPixels)
{
	Int32 iPixel;

	for (iPixel=0; iPixel < nPixels; iPixel++)
	{
		Uint8 uMask;

		uMask =	pMask->uMask8[iPixel >> 3] & (1<<(iPixel&7));

		pDest[iPixel] = uMask ? 0 : 0xFFFFFFF;
	}
}

void _DrawMask2(Uint32 *pDest, SNMaskT *pMask1, SNMaskT *pMask2, Int32 nPixels)
{
	Int32 iPixel;
	static Uint32 Lookup[4]= { 0x0, 0xFF, 0xFF00, 0xFFFFFF};

	for (iPixel=0; iPixel < nPixels; iPixel++)
	{
		Uint8 uMask1, uMask2;
		Uint32 uColor;

		uMask1 =	pMask1->uMask8[iPixel >> 3] & (1<<(iPixel&7));
		uMask2 =	pMask2->uMask8[iPixel >> 3] & (1<<(iPixel&7));

		uColor=0;
		if (uMask1) uColor+=1;
		if (uMask2) uColor+=2;

		pDest[iPixel] = Lookup[uColor];
	}
}

void SnesPPURender::RenderLine(Int32 iLine)
{
	if (m_pTarget)
	{
		switch (m_pTarget->GetFormat()->uBitDepth)
		{
		case 16:
			RenderLine16(iLine);
			break;
		case 32:
			RenderLine32(iLine, 0);
			break;

		}
	}
}

void SnesPPURender::RenderLine16(Int32 iLine)
{
}

void SnesPPURender::UpdateCGRAM(Uint32 uAddr, Uint16 uData)
{
	SnesPPUInvalidateOutputCache();

	/* A video-skipped frame still updates emulated CGRAM in SnesPPU.  The
	   following rendered frame begins with UPDATE_ALL and uploads the complete
	   palette, so touching GS/CLUT state here would be pure duplicate work. */
	if (m_pTarget && m_pRenderInfo && m_pBlend)
	{
		m_pBlend->UpdatePaletteEntry(&m_pRenderInfo->BlendInfo, uAddr, uData, m_pPPU->GetIntensity());
	}
}

void SnesPPURender::UpdateOAM()
{
	SnesPPUInvalidateOutputCache();
	SetUpdateFlags(SNESPPURENDER_UPDATE_OBJ);
}

/* $2133.3 pseudo-hires alternates sub/main physical dots at 512-dot
   resolution. Revive's PS2 carrier is 256 wide, so collapse each pair in
   native SNES BGR555. This preserves the intended CRT transparency while
   avoiding a second full-width framebuffer. The alternating-dot behavior follows the public SNES PPU model;
   the 256-wide collapse is Revive's PS2 presentation choice. */
static _INLINE Uint16 _SnesPPUAveragePseudoHires15(Uint16 uMain, Uint16 uSub)
{
	Uint32 r = ((uMain & 0x001Fu) + (uSub & 0x001Fu)) >> 1;
	Uint32 g = (((uMain >> 5) & 0x001Fu) + ((uSub >> 5) & 0x001Fu)) >> 1;
	Uint32 b = (((uMain >> 10) & 0x001Fu) + ((uSub >> 10) & 0x001Fu)) >> 1;
	return (Uint16)(r | (g << 5) | (b << 10));
}

static void _SnesPPUBuildPseudoHiresLine(
	Uint16 *pOut, const SNPPUBlendInfoT *pInfo, const Uint16 *pCGRAM)
{
	Int32 i;
	for (i = 0; i < 256; ++i)
	{
		Uint16 uMain = (Uint16)(pCGRAM[pInfo->uMain8[i]] & 0x7FFFu);
		Uint16 uSub  = (Uint16)(pCGRAM[pInfo->uSub8[i]] & 0x7FFFu);
		pOut[i] = _SnesPPUAveragePseudoHires15(uMain, uSub);
	}
}

#if CODE_PLATFORM == CODE_PS2
static _INLINE Uint16 _SnesPPUColor15ToGS16(Uint16 uColor15,
	Uint32 uIntensity)
{
	Uint32 c = uColor15 & 0x7FFFu;

	/* SNES CGRAM and GS PSMCT16 both store R5/G5/B5 in bits 0..14.
	   Do not round-trip through 32-bit RGB for every hires dot. */
	if (uIntensity >= 15u)
		return (Uint16)(c | 0x8000u);
	if (uIntensity == 0u)
		return 0x8000u;

	{
		Uint32 r = (c & 0x1Fu) * uIntensity / 15u;
		Uint32 g = ((c >> 5) & 0x1Fu) * uIntensity / 15u;
		Uint32 b = ((c >> 10) & 0x1Fu) * uIntensity / 15u;
		return (Uint16)(r | (g << 5) | (b << 10) | 0x8000u);
	}
}

static void _SnesPPUBuildNativeHires512(
	Uint16 *pOut, const SNPPUBlendInfoT *pInfo, const Uint16 *pCGRAM,
	Uint32 uIntensity)
{
	Int32 x;

	if (uIntensity >= 15u)
	{
		for (x = 0; x < 256; ++x)
		{
			pOut[(x << 1) + 0] =
				(Uint16)((pCGRAM[pInfo->uSub8[x]] & 0x7FFFu) | 0x8000u);
			pOut[(x << 1) + 1] =
				(Uint16)((pCGRAM[pInfo->uMain8[x]] & 0x7FFFu) | 0x8000u);
		}
	}
	else
	{
		for (x = 0; x < 256; ++x)
		{
			pOut[(x << 1) + 0] = _SnesPPUColor15ToGS16(
				pCGRAM[pInfo->uSub8[x]], uIntensity);
			pOut[(x << 1) + 1] = _SnesPPUColor15ToGS16(
				pCGRAM[pInfo->uMain8[x]], uIntensity);
		}
	}
}
#endif

void SnesPPURender::RenderLine32(Int32 iLine, Bool bPlanar)
{
	SnesRender8pInfoT *pRenderInfo;
	SNPPUBlendInfoT *pBlendInfo;
	const SnesPPURegsT *pRegs  = m_pPPU->GetRegs();
	const Uint8 uBGMode = (Uint8)pRegs->bgmode & 7u;
	const Bool bPseudoHiresSimple =
		((pRegs->setini & SNESPPU_SETINI_PSEUDOHIR) != 0) &&
		(uBGMode != 5u && uBGMode != 6u) &&
		((pRegs->cgadsub & 0x3Fu) == 0) &&
		((pRegs->cgwsel & 0xC0u) == 0);
	const Bool bMode56HiresSimple =
		(uBGMode == 5u || uBGMode == 6u) &&
		((pRegs->cgadsub & 0x3Fu) == 0) &&
		((pRegs->cgwsel & 0xC0u) == 0);
#if CODE_PLATFORM == CODE_PS2 && SNPPU_BG_CACHE
	SnesPPUHiresLineCacheEntryT *pHiresLineCache = NULL;
	SnesPPUHiresLineStateT HiresLineState;
	Bool bHiresLineCachePromote = FALSE;
#endif

#if SNDBG_LOG
	{
		Uint8 uMode = (Uint8)pRegs->bgmode & 7u;
		Uint8 uSetIni = (Uint8)pRegs->setini;
		Uint8 uMainSub = ((Uint8)pRegs->tm | (Uint8)pRegs->ts) & 0x1Fu;

		g_DbgPPUModeLines[uMode]++;
		if (g_DbgPPULastMode != 0xFF && g_DbgPPULastMode != uMode)
			g_DbgPPUModeChanges++;
		g_DbgPPULastMode = uMode;

		if ((Uint8)pRegs->inidisp & 0x80u) g_DbgPPUForcedBlankLines++;
		if (((Uint8)pRegs->mosaic & 0x0Fu) &&
		    ((Uint8)pRegs->mosaic >> 4)) g_DbgPPUMosaicLines++;
		if ((uMode == 2u || uMode == 4u) &&
		    (uMainSub & (SNESPPU_MASK_BG1 | SNESPPU_MASK_BG2)))
			g_DbgPPUOffsetLines++;
		if (((Uint8)pRegs->tmw | (Uint8)pRegs->tsw) ||
		    ((Uint8)pRegs->cgwsel & 0xF0u)) g_DbgPPUWindowLines++;
		if ((Uint8)pRegs->cgadsub & 0x3Fu) g_DbgPPUColorMathLines++;
		if (((Uint8)pRegs->cgwsel & 0x01u) &&
		    (uMode == 3u || uMode == 4u || uMode == 7u))
			g_DbgPPUDirectColorLines++;
		if (uSetIni & 0x01u) g_DbgPPUInterlaceLines++;
		if (uSetIni & 0x02u) g_DbgPPUObjInterlaceLines++;
		if (uSetIni & 0x04u) g_DbgPPUOverscanLines++;
		if ((uSetIni & 0x08u) || uMode == 5u || uMode == 6u)
			g_DbgPPUHiresLines++;
		if (uSetIni & 0x40u) g_DbgPPUExtBGLines++;
	}
#endif

#if CODE_PLATFORM == CODE_PS2 && SNPPU_BG_CACHE
	/* The expensive native-hires path is reused only for exact, fully stable
	   Mode 5 lines.  Memory writes advance OutputGeneration; every visual PPU
	   register and the renderer's layer masks are compared byte-for-byte.
	   Sprites remain part of the cached final pixels, and any OAM change makes
	   the entry miss before it can be displayed. */
	if (uBGMode == 5u && bMode56HiresSimple &&
	    !(pRegs->inidisp & 0x80u) && pRegs->mosaic == 0 &&
	    iLine >= 0 && (Uint32)iLine < SNPPU_HIRES_LINE_CACHE_LINES)
	{
		Bool bSameState;

		_SnesPPUBuildHiresLineState(&HiresLineState, pRegs);
		pHiresLineCache = &_SnesPPU_HiresLineCache[iLine];
		bSameState = SnesPPUHiresLineCacheKeyMatches(
			&pHiresLineCache->Key, _SnesPPU_OutputGeneration,
			iLine, &HiresLineState);
		if (pHiresLineCache->uReady && bSameState)
		{
#if SNDBG_LOG
			g_DbgHiresLineCacheHits++;
#endif
			m_pBlend->ExecHires512(pHiresLineCache->uPixels, iLine);
			return;
		}
		/* Two consecutive observations are required before paying the 1 KiB
		   store.  Animation and scrolling therefore miss without thrashing
		   EE memory, while a stable third frame takes the fast path. */
		bHiresLineCachePromote = bSameState;
		pHiresLineCache->uReady = FALSE;
#if SNDBG_LOG
		g_DbgHiresLineCacheMisses++;
#endif
	}
#if SNDBG_LOG
	else if (uBGMode == 5u)
	{
		g_DbgHiresLineCacheBypasses++;
	}
#endif
#endif

	pRenderInfo = m_pRenderInfo;
    pBlendInfo = &pRenderInfo->BlendInfo;

#if CODE_DEBUG && CODE_PLATFORM==CODE_PS2
static Bool bPrint = TRUE;
	if (bPrint)
	{
		printf("BlendInfo: %X\n", (Uint32)&pRenderInfo->BlendInfo);
		printf("Main: %X\n", (Uint32)pRenderInfo->Main);
		printf("Sub: %X\n", (Uint32)pRenderInfo->Sub);
		printf("BGPlanes: %X\n", (Uint32)pRenderInfo->BGPlanes);
		printf("Tiles: %X\n", (Uint32)pRenderInfo->Tiles);
		printf("Size= %X\n", sizeof(pRenderInfo));
		bPrint=FALSE;
	}
#endif

	if (pRegs->inidisp & 0x80)
	{
        m_pBlend->Clear(pBlendInfo, iLine);
	} else
	{
		SNMaskT ColorMask[3];
		Bool bDirectMain = FALSE;

		if (m_UpdateFlags & SNESPPURENDER_UPDATE_PAL)
		{
            m_pBlend->UpdatePalette(pBlendInfo, m_pPPU->GetCGData(), m_pPPU->GetIntensity());

			m_UpdateFlags &= ~SNESPPURENDER_UPDATE_PAL;
		}

		if (m_UpdateFlags & SNESPPURENDER_UPDATE_OBJ)
		{
#if SNDBG_LOG
			Uint32 _tObjUpdate = ProfCtrGetCycle();
#endif
			UpdateOBJ(pRenderInfo->uObjY, pRenderInfo->uObjSize);

            PROF_ENTER("UpdateOBJVisibility");
            UpdateOBJVisibility(pRenderInfo->uObjY, pRenderInfo->uObjSize, pRegs->oampri.w, SNESPPU_OBJ_NUM);
            PROF_LEAVE("UpdateOBJVisibility");
#if SNDBG_LOG
			{
				Uint32 _dObjUpdate = ProfCtrGetCycle() - _tObjUpdate;
				g_TmgCycObj += _dObjUpdate;
				g_TmgCycObjUpdate += _dObjUpdate;
			}
#endif

			m_UpdateFlags &= ~SNESPPURENDER_UPDATE_OBJ;
		}

		/* Tiles and decoded character rows are cached across scanlines. A VRAM
		   upload can replace either the tilemap or the character data without
		   changing scroll/base registers, so both update classes must invalidate
		   the cached VRAM addresses. This is especially visible after pause/map
		   screens and SuperFX text overlays. */
		if (m_UpdateFlags &
		    (SNESPPURENDER_UPDATE_BGSCR | SNESPPURENDER_UPDATE_BGCHR))
		{
            pRenderInfo->uBGVramAddr[0] = 0xFFFFFFFF;
            pRenderInfo->uBGVramAddr[1] = 0xFFFFFFFF;
            pRenderInfo->uBGVramAddr[2] = 0xFFFFFFFF;
            pRenderInfo->uBGVramAddr[3] = 0xFFFFFFFF;

			m_UpdateFlags &= ~(SNESPPURENDER_UPDATE_BGSCR |
			                   SNESPPURENDER_UPDATE_BGCHR);
		}

	    if (m_UpdateFlags & SNESPPURENDER_UPDATE_WINDOW)
        {
		DecodeWindows(pRenderInfo->WindowMask, pRenderInfo->BGWindow);
		m_UpdateFlags &= ~SNESPPURENDER_UPDATE_WINDOW;
        }

		// render line
		RenderLine8(iLine, pRenderInfo);

#if SNDBG_LOG
		Uint32 _tColorMath = ProfCtrGetCycle();
#endif

#if CODE_PLATFORM == CODE_PS2
		/* If no main-screen source is selected by CGADSUB, the sub screen and
		   all add/sub masks are mathematically unable to change the result.
		   With main clipping disabled and brightness at 15, the GS can expand
		   the indexed main line directly into the output texture. */
		bDirectMain = !bPseudoHiresSimple && !bMode56HiresSimple &&
		              (pRegs->cgadsub & 0x3F) == 0 &&
		              (pRegs->cgwsel & 0xC0) == 0 &&
		              m_pPPU->GetIntensity() == 15;
#endif

		// determine color window mask for main screen
		// 0 = disabled (masked)
        // 1 = enabled
		if (!bDirectMain)
		{
			switch ((pRegs->cgwsel >> 6) & 3)
			{
			case 0:	// all the time
				SNMaskSet(&ColorMask[0]);
				break;
			case 1: // inside color window
				SNMaskCopy(&ColorMask[0], &pRenderInfo->BGWindow[SNPPU_BGWINDOW_COLOR]);
				break;
			case 2:	// outside color window
				SNMaskNOT(&ColorMask[0], &pRenderInfo->BGWindow[SNPPU_BGWINDOW_COLOR]);
				break;
			case 3: // confirmed: never.
			default:
				SNMaskClear(&ColorMask[0]);
				break;
			}

			// determine color window mask for sub screen
			// 0 = disabled (masked)
			// 1 = enabled
			switch ((pRegs->cgwsel >> 4) & 3)
			{
			case 0:	// enabled all the time (only when add/sub layers of main screen are opaque)
				SNMaskCopy(&ColorMask[1], &pRenderInfo->MainAddSubMask);
				break;
			case 1: // inside color window
				SNMaskAND(&ColorMask[1], &pRenderInfo->MainAddSubMask, &pRenderInfo->BGWindow[SNPPU_BGWINDOW_COLOR]);
				break;
			case 2:	// outside color window
				SNMaskANDN(&ColorMask[1], &pRenderInfo->MainAddSubMask, &pRenderInfo->BGWindow[SNPPU_BGWINDOW_COLOR]);
				break;
			case 3: // confirmed: never
			default:
				SNMaskClear(&ColorMask[1]);
				break;
			}

			// determine pixels that are subject to 1/2 color add/sub
			// these are the:
			//      layers of the mainscreen that are set in the cgadsub register that are not obscured by color window
			//      ANDed with the enabled pixels of the subscreen (opaque, fixed color, and windowed)
			// Quoth: "in the back color constant area on the sub screen, it does not	become 1/2"
			// there will never be a case where 1/2 is applied to a main or subscreen color that has been masked by color window
			if (pRegs->cgadsub & 0x40)
			{
				// 0 = disabled
				// 1 = 1/2 add sub enabled
				SNMaskAND(&ColorMask[2], &pRenderInfo->SubAddSubMask, &ColorMask[1]);
				SNMaskAND(&ColorMask[2], &ColorMask[2], &ColorMask[0]);
			} else
			{
				// 1/2 disabled
				SNMaskClear(&ColorMask[2]);
			}
		}

		// perform color blending of main+sub
#if SNDBG_LOG
		g_TmgCycColorMath += ProfCtrGetCycle() - _tColorMath;
		Uint32 _tBlend = ProfCtrGetCycle();
#endif
		if (bMode56HiresSimple)
		{
#if CODE_PLATFORM == CODE_PS2
			Uint16 HiresLine[512] _ALIGN(64);
			_SnesPPUBuildNativeHires512(
				HiresLine, pBlendInfo, m_pPPU->GetCGData(),
				m_pPPU->GetIntensity());
			m_pBlend->ExecHires512(HiresLine, iLine);
#if SNPPU_BG_CACHE
			if (pHiresLineCache)
			{
				if (bHiresLineCachePromote)
				{
					memcpy(pHiresLineCache->uPixels, HiresLine,
						sizeof(pHiresLineCache->uPixels));
					/* Publish readiness only after all 512 pixels exist. */
					pHiresLineCache->uReady = TRUE;
				} else
				{
					/* Remember only the cheap candidate key on a first miss. */
					SnesPPUHiresLineCacheSetKey(&pHiresLineCache->Key,
						_SnesPPU_OutputGeneration, iLine,
						&HiresLineState);
				}
			}
#endif
#else
			m_pBlend->Exec(
				pBlendInfo, iLine, pRegs->coldata, ColorMask,
				(pRegs->cgadsub & 0x80), m_pPPU->GetIntensity());
#endif
		}
		else if (bPseudoHiresSimple)
		{
			Uint16 PseudoCGRAM[256] _ALIGN(16);
			Int32 i;
			_SnesPPUBuildPseudoHiresLine(
				PseudoCGRAM, pBlendInfo, m_pPPU->GetCGData());
			for (i = 0; i < 256; ++i)
				pBlendInfo->uMain8[i] = (Uint8)i;

			/* Upload the collapsed line as a transient 256-entry CLUT. Exec
			   snapshots it into the DMA staging area, so restoring the real
			   CGRAM immediately afterward is safe for the next scanline. */
			m_pBlend->UpdatePalette(
				pBlendInfo, PseudoCGRAM, m_pPPU->GetIntensity());
			m_pBlend->Exec(
				pBlendInfo, iLine, 0, NULL, FALSE,
				m_pPPU->GetIntensity());
			/* Do not upload the real CGRAM again on every pseudo-hires line.
			   Mark it dirty instead; the normal path restores it only when a
			   subsequent non-pseudo line actually needs it. */
			m_UpdateFlags |= SNESPPURENDER_UPDATE_PAL;
		}
		else
		{
			m_pBlend->Exec(
				pBlendInfo,
				iLine,
				pRegs->coldata,
				bDirectMain ? NULL : ColorMask,
				(pRegs->cgadsub & 0x80),
				m_pPPU->GetIntensity()
				);
		}
#if SNDBG_LOG
		g_TmgCycBlend += ProfCtrGetCycle() - _tBlend;
#endif
	}
}

#if CODE_PLATFORM == CODE_PS2
#include "snppublend_gs.h"
/* TBPs of the blender scratchpad slab and the SNES output texture, both
   allocated via gsKit's VRAM allocator in MainLoopInit() after the
   mode-specific framebuffers. A zero address is a fatal boot-time VRAM
   allocation failure, so MainLoopInit() never reaches this renderer then. */
extern Uint32 _MainLoop_uBlenderTBP;
extern Uint32 _MainLoop_uOutTexTBP;
static SNPPUBlendGS *_Blend;
#else

#include "snppublend_c.h"
static SNPPUBlendC _Blend;
#endif

#if !SNPPURENDER_INFOSCRATCHPAD
static SnesRender8pInfoT _RenderInfo;
#endif

//static SNPPUBlendMM _Blend;

void SnesPPURender::BeginRender(CRenderSurface *pTarget)
{
#if CODE_PLATFORM == CODE_PS2
	if (!_Blend)
	{
		_Blend = new SNPPUBlendGS(_MainLoop_uBlenderTBP,
		                          _MainLoop_uOutTexTBP);
	}
	m_pBlend = _Blend;
#else
	m_pBlend = &_Blend;
#endif

    #if SNPPURENDER_INFOSCRATCHPAD
    m_pRenderInfo = (SnesRender8pInfoT *)PS2_RENDERINFOADDR;
	#else
    m_pRenderInfo = &_RenderInfo;
	#endif

	m_pTarget = pTarget;
	if (pTarget)
	{
		pTarget->Lock();
		pTarget->SetLineOffset(1);
        m_pBlend->Begin(pTarget);
	}

    SetUpdateFlags(SNESPPURENDER_UPDATE_ALL);

    if (!_SnesPPU_bInitialized)
    {
	    _BuildPlaneLookup();
        _SnesPPU_bInitialized = TRUE;
    }
}

void SnesPPURender::EndRender()
{
    #if CODE_PLATFORM == CODE_PS2
	if (m_pTarget)
		DmaSyncSprToRam();
    #endif

	if (m_pTarget)
	{
        m_pBlend->End();
        m_pBlend = NULL;
        m_pTarget->Unlock();
	}

    m_pRenderInfo=NULL;
	m_pTarget=NULL;
}

void SnesPPURender::UpdateVRAM(Uint32 uVramAddr)
{
	UpdateVRAMRange(uVramAddr, 1);
}

void SnesPPURender::UpdateVRAMRange(Uint32 uVramAddr, Uint32 nWords)
{
	SnesPPUInvalidateChrCache(uVramAddr, nWords);

	/* O cache CHR exclusivo de OBJ ja foi invalidado acima. Estes bits
	   atualizam os dados derivados de BG/tilemap na proxima scanline. */
	SetUpdateFlags(SNESPPURENDER_UPDATE_BGSCR |
	               SNESPPURENDER_UPDATE_BGCHR);
}
