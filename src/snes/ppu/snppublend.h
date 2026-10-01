/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snppublend interface for SNES picture processing.
 */

#ifndef _SNPPUBLEND_H
#define _SNPPUBLEND_H

#include <string.h>
#include "palette.h"
#include "snppuhires.h"

#if CODE_PLATFORM == CODE_PS2

struct SNPPUBlendInfoT
{
    PaletteT    Pal[1] _ALIGN(16);
	Uint8	    uMain8[256] _ALIGN(64);
	Uint8	    uSub8[256] _ALIGN(16);
	Uint8	    uAttrib8[256] _ALIGN(64);
};
#else
struct SNPPUBlendInfoT
{
    PaletteT    Pal[4] _ALIGN(16);
    Uint32      uMain32[256] _ALIGN(16);
    Uint32      uSub32[256] _ALIGN(16);
    Uint32      uLine32[256] _ALIGN(16);

	Uint8	    uMain8[256] _ALIGN(64);
	Uint8	    uSub8[256] _ALIGN(16);
	Uint8	    uAttrib8[256] _ALIGN(64);
};

#endif

class ISNPPUBlend
{
protected:
    CRenderSurface *m_pTarget;

public:
    virtual void Begin(class CRenderSurface *pTarget)=0;
    virtual void Exec(SNPPUBlendInfoT *pInfo, Int32 iLine,
        Uint32 uFixedColor32, SNMaskT *pColorMask, Bool bAddSub,
        Uint32 uIntensity, Bool bFixedSub=FALSE)=0;
    /* PS2 native-hires fast path. The default keeps non-PS2 backends source
       compatible; only the GS backend consumes the 512-pixel BGR555 line. */
    virtual void ExecHires512(const Uint16 *pLine512, Int32 iLine)
    {
        (void)pLine512;
        (void)iLine;
    }
    /* A backend with writable DMA staging can expand indices directly there.
       Other backends retain the ordinary finalized-line contract. */
    virtual Bool ExecHiresIndexed(const Uint8 *pMain, const Uint8 *pSub,
        const Uint16 *pPalette, Int32 iLine, Uint16 *pCache = NULL)
    {
        Uint32 Line[256] _ALIGN(64);
        SnesPPUBuildHiresOutput32(Line, pMain, pSub, pPalette);
        if (pCache) memcpy(pCache, Line, sizeof(Line));
        ExecHires512((const Uint16 *)Line, iLine);
        return TRUE;
    }
    virtual void Clear(SNPPUBlendInfoT *pInfo, Int32 iLine)=0;
    virtual void End()=0;
    virtual void UpdatePalette(SNPPUBlendInfoT *pInfo, Uint16 *pCGRam, Uint32 uIntensity)=0;
    virtual void UpdatePaletteEntry(SNPPUBlendInfoT *pInfo, Uint32 uAddr, Uint32 uData, Uint32 uIntensity)=0;
};

#endif
