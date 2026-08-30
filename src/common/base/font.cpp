/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements font behavior for shared base utilities.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "types.h"
#include "font.h"
#include "surface.h"
extern "C" {
#include "gs.h"
#include "gpprim.h"
#include "gskit_backend.h"
#include "gpfifo.h"

};

#define FIXED4(_x) ((Int32)((_x)*16.0f))
#define FIXED7(_x) ((Int32)((_x)*128.0f))

/* Integer pixel-doubling for crisp text.
 *
 * Higher modes use a non-integer logical transform (notably 2.5x in X).
 * Sampling the glyph atlas through it makes glyph shapes vary with screen
 * position. Draw glyphs directly in framebuffer space at an exact integer
 * scale: 1x in native 240p and 2x in the 480-line framebuffer modes.
 *
 * Layout still lives in the 256x240 logical space, so advances and
 * FontGetStrWidth convert the physical glyph size back to logical with
 * the SAME helper (_FontAdvLogical) -> centering/columns stay aligned. */
static inline Int32 _FontDrawScale(void)
{
    /* Round upward so integer glyph scaling remains crisp. */
    int n = (int)(GPPrimGetScaleY() + 0.999f);
    return (n < 1) ? 1 : n;
}
#define FONT_DRAW_SCALE (_FontDrawScale())

static inline Int32 _FontAdvLogical(Int32 physW)
{
    float sx = GPPrimGetScaleX();
    if (sx <= 0.0f) sx = 1.0f;
    return (Int32)(((float)physW) / sx + 0.5f);
}

extern unsigned char       _FontData_ui[];
extern const FontMapEntryT _FontMap_ui[];
extern const int           _FontMap_ui_count;
extern const int           _FontTex_ui_w;
extern const int           _FontTex_ui_h;
extern const int           _Font_ui_spacew;
extern const int           _Font_ui_lineh;
extern const int           _Font_ui_maxw;

struct FontStateT
{
    Uint32 uColor;
	Float32 vz;
	FontT   *pFont;
};

static FontStateT _Font_State;
static FontT _Font_Default;
static FontT _Font_Fixed;
static FontT *_Font_pFonts[4];

static Int32 _FontDrawChar(FontCharT *pFontChar, float fX, float fY, float z1, Uint32 uColor, int c)
{
	Uint32 u0,v0,u1,v1;
    Uint32 x0,y0,x1,y1;
    Int32 width, height;
    float sx, sy, px0, py0, px1, py1;

    width  = pFontChar->u1 - pFontChar->u0;
    height = pFontChar->v1 - pFontChar->v0;

    sx = GPPrimGetScaleX(); if (sx <= 0.0f) sx = 1.0f;
    sy = GPPrimGetScaleY(); if (sy <= 0.0f) sy = 1.0f;

    px0 = fX * sx + GPPrimGetOffsetX();
    py0 = fY * sy + GPPrimGetOffsetY();

    /*
     * At native 240p, place the source coordinates on texel centres and
     * finish the destination one GS subpixel after the final pixel centre.
     * This preserves every column of a 1:1 glyph without drawing a second
     * sprite over its right edge. UV and XY use 10.4/12.4 fixed point.
     */
    if (FONT_DRAW_SCALE == 1 && sx == 1.0f && sy == 1.0f)
    {
        u0 = (pFontChar->u0 << 4) + 8;  /* first texel centre */
        v0 = (pFontChar->v0 << 4) + 8;
        u1 = (pFontChar->u1 << 4) - 6;  /* 0.375 before the edge */
        v1 = (pFontChar->v1 << 4) - 6;

        px1 = px0 + (float)width  - 0.9375f;
        py1 = py0 + (float)height - 0.9375f;
    }
    else
    {
        /* Higher modes retain exact integer pixel scaling. */
        u0 = (pFontChar->u0 << 4);
        v0 = (pFontChar->v0 << 4);
        u1 = (pFontChar->u1 << 4);
        v1 = (pFontChar->v1 << 4);

        px1 = px0 + (float)(width  * FONT_DRAW_SCALE);
        py1 = py0 + (float)(height * FONT_DRAW_SCALE);
    }

    x0 = ((Uint32)FIXED4(px0)) & 0xFFFF;
    y0 = ((Uint32)FIXED4(py0)) & 0xFFFF;
    x1 = ((Uint32)FIXED4(px1)) & 0xFFFF;
    y1 = ((Uint32)FIXED4(py1)) & 0xFFFF;

	GPPrimTexRectAbs(x0, y0, u0, v0, x1, y1, u1, v1, 10, uColor, 1);

    /* advance in LOGICAL units: physical glyph width + 2px gap */
    return _FontAdvLogical(width * FONT_DRAW_SCALE + 2);

}

Int32 FontGetStrWidth(const Char *pStr)
{
    Int32 iWidth = 0;
	FontT *pFont;
	pFont = _Font_State.pFont;
	if (!pFont) return 0;

    while (*pStr)
    {
		if (*pStr != ' ')
		{
            FontCharT *pFontChar = &pFont->CharMap[(unsigned char)*pStr];
			iWidth += _FontAdvLogical((pFontChar->u1 - pFontChar->u0) * FONT_DRAW_SCALE + 2);
		} else
        {
            // space
		    iWidth += _FontAdvLogical(pFont->uCharX * FONT_DRAW_SCALE);
        }

        pStr++;
    }
	return iWidth;
}

static void _FontDrawStr(FontT *pFont, Float32 vx, Float32 vy, Float32 vz, const Char *pStr, Uint32 uColor)
{
    TextureT *pTexture = &pFont->Texture;
    if (!pTexture) return;

	// Set texture/clut regs.  NEAREST keeps the m5x7 bitmap crisp.
	GPPrimSetTex(pTexture->uVramAddr, pTexture->uWidth, pTexture->uWidthLog2, pTexture->uHeightLog2, pTexture->eFormat, 0, 0, 0, 0);

	while (*pStr)
	{
		if (*pStr != ' ')
		{
            {
                FontCharT *pFontChar = &pFont->CharMap[(unsigned char)*pStr];
                Int32 iWidth;

                iWidth =_FontDrawChar(  pFontChar, vx, vy, vz, uColor,	*pStr);

                if (pFont->uFixedWidth) iWidth = _FontAdvLogical((Int32)pFont->uFixedWidth * FONT_DRAW_SCALE);
			    vx += iWidth;
            }
		} else
        {
		    vx += _FontAdvLogical(pFont->uCharX * FONT_DRAW_SCALE);
        }
		pStr++;
	}
}

static void _FontSetCharMap(FontT *pFont, Uint8 uChar, Uint32 u, Uint32 v, Uint32 w, Uint32 h)
{
	pFont->CharMap[uChar].u0 = u;
	pFont->CharMap[uChar].v0 = v;
	pFont->CharMap[uChar].u1 = u + w;
	pFont->CharMap[uChar].v1 = v + h;
}

static void _FontSetCharSize(FontT *pFont, Uint32 w, Uint32 h)
{
	pFont->uCharX = w;
	pFont->uCharY = h;
}

/*
static void _FontBiosMake(FontT *pFont)
{
	int x, y;

	TextureNew(&pFont->Texture, vixar_width, vixar_height, GS_PSMT8);
    TextureSetAddr(&pFont->Texture, FONT_TEX);

	_FontSetCharSize(pFont, 8, 12);

	for (y=0; y<8; y++) {
		for (x=0; x<16; x++)
		{
            Uint8 uChar  = y * 16 + x;
			_FontSetCharMap(pFont, uChar, x * 16, y * 16, vixarmet[uChar], 16);
		}
	}

	gp_uploadTexture(&thegp, FONT_TEX, 256, 0, 0, GS_PSMT8, vixar_image, vixar_width, vixar_height);
	gp_uploadTexture(&thegp, FONT_CLUT, 256, 0, 0, GS_PSMCT16, vixar_clut, 256, 1);
	gp_hardflush(&thegp);
}
  */

void FontMake(FontT *pFont, CSurface *pSurface, Uint32 uVramAddr, const Char *pCharList)
{
    TextureT *pTexture = &pFont->Texture;

    FontNew(pFont);
    FontParseChars(pFont, pSurface, pCharList);

    // allocate texture in GS Memory
    TextureNew(pTexture, pSurface->GetWidth(), pSurface->GetHeight(), GS_PSMCT32);
    TextureSetAddr(pTexture, uVramAddr);

    // upload texture data to memory
    TextureUpload(pTexture,  pSurface->GetLinePtr(0));
    GPFifoFlush();
}

void FontMakeFromMap(FontT *pFont, CSurface *pSurface, Uint32 uVramAddr,
                     const FontMapEntryT *pMap, Int32 nCount,
                     Int32 uSpaceW, Int32 uLineH)
{
    TextureT *pTexture = &pFont->Texture;
    Int32 i;

    FontNew(pFont);

    // explicit glyph map (no auto-parse)
    for (i = 0; i < nCount; i++)
        _FontSetCharMap(pFont, (Uint8)pMap[i].c,
                        pMap[i].u, pMap[i].v, pMap[i].w, pMap[i].h);

    _FontSetCharSize(pFont, uSpaceW, uLineH);

    // allocate + upload the atlas (same as FontMake)
    TextureNew(pTexture, pSurface->GetWidth(), pSurface->GetHeight(), GS_PSMCT32);
    TextureSetAddr(pTexture, uVramAddr);
    TextureUpload(pTexture, pSurface->GetLinePtr(0));
    GPFifoFlush();
}

void FontColor4f(Float32 r, Float32 g, Float32 b, Float32 a)
{
    Uint32 uR, uG, uB, uA;

    /* Tint all "white" text amber/gold (user preference).  Any request
       for pure white (1,1,1) is remapped to a warm gold; every other
       colour (cyan headers, green, red, ...) passes through untouched.
       Centralising it here recolours white text on ALL screens at once. */
    if (r >= 1.0f && g >= 1.0f && b >= 1.0f)
    {
        r = 1.00f; g = 0.88f; b = 0.46f;
    }

    uR = FIXED7(r);
    uG = FIXED7(g);
    uB = FIXED7(b);
    uA = FIXED7(a);

	_Font_State.uColor = GS_SET_RGBA(uR, uG, uB, uA);

}

void FontPuts(Float32 vx, Float32 vy, const Char *pStr)
{
	FontT *pFont;
	pFont = _Font_State.pFont;
	if (!pFont) return;

	_FontDrawStr(pFont,
				vx, vy, 15.0f,
				pStr,
				_Font_State.uColor
				);

}

void FontPrintf(Float32 vx, Float32 vy, const Char *pFormat, ...)
{
	static char strbuf[1024];
	va_list args;

	va_start(args, pFormat);
	vsprintf(strbuf, pFormat, args);
	va_end(args);

	FontPuts(vx,vy,strbuf);
}

void FontSelect(Int32 iFont)
{
	_Font_State.pFont = _Font_pFonts[iFont];
}

void FontSetFont(Int32 iFont, FontT *pFont)
{
    _Font_pFonts[iFont] = pFont;
}

Int32 FontGetWidth()
{
	return _Font_State.pFont ? _Font_State.pFont->uCharX : 0;
}

Int32 FontGetHeight()
{
	return _Font_State.pFont ? _Font_State.pFont->uCharY : 0;
}

Uint32 FontGetVramSize()
{
    return (Uint32)_FontTex_ui_w * (Uint32)_FontTex_ui_h * 4U;
}

void FontInit(Uint32 uVramAddr)
{
    CSurface Surface;

    // UI font (m5x7, Daniel Linssen): embedded RGBA8 atlas + explicit map.
    Surface.Set(_FontData_ui, _FontTex_ui_w, _FontTex_ui_h,
                _FontTex_ui_w * 4, PixelFormatGetByEnum(PIXELFORMAT_RGBA8));

    FontMakeFromMap(&_Font_Default, &Surface, uVramAddr,
                    _FontMap_ui, _FontMap_ui_count,
                    _Font_ui_spacew, _Font_ui_lineh);

    FontMakeFromMap(&_Font_Fixed, &Surface, uVramAddr,
                    _FontMap_ui, _FontMap_ui_count,
                    _Font_ui_spacew, _Font_ui_lineh);

    /* monospace cell = widest glyph so nothing overlaps in fixed mode */
    _Font_Fixed.uFixedWidth = _Font_ui_maxw;

    FontSetFont(0, &_Font_Default);
    FontSetFont(1, &_Font_Fixed);
    FontSetFont(2, &_Font_Default);
    FontSetFont(3, &_Font_Default);

	FontSelect(0);
	FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void FontShutdown()
{
	FontDelete(&_Font_Default);
	FontDelete(&_Font_Fixed);
}

void FontNew(FontT *pFont)
{
    pFont->nChars   = 0;
    pFont->uFixedWidth = 0;
    memset(&pFont->CharMap, 0, sizeof(pFont->CharMap));
}

void FontDelete(FontT *pFont)
{
}

static Bool _FontScanHorizWhite(CSurface *pSurface, Uint32 uStartX, Uint32 uEndX, Uint32 uY)
{
    Uint32 uX;
    Uint32 *pLine;
    pLine = (Uint32 *)pSurface->GetLinePtr(uY);
    if (!pLine) return TRUE;

    for (uX = uStartX; uX < uEndX; uX++)
    {
        if (pLine[uX] & 0xFF000000)
        {
            return FALSE;
        }
    }

    return TRUE;
}

static Bool _FontScanVertWhite(CSurface *pSurface, Uint32 uStartY, Uint32 uEndY, Uint32 uX)
{
    Uint32 uY;
    Uint32 *pLine;

    for (uY = uStartY; uY < uEndY; uY++)
    {
        pLine = (Uint32 *)pSurface->GetLinePtr(uY);
        if (!pLine) return TRUE;

        if (pLine[uX] & 0xFF000000)
        {
            return FALSE;
        }
    }

    return TRUE;
}

const Char *_FontParseLine(FontT *pFont, CSurface *pSurface, const Char *pCharList, Uint32 uStartY, Uint32 uEndY)
{
    Uint32 uX;
    Uint32 uWidth;//, uHeight;

    uWidth = pSurface->GetWidth();

    uX = 0;
    while (uX < uWidth)
    {
        Uint32 uStartX, uEndX;

        // scan for non-white space
        while ((uX < uWidth) && _FontScanVertWhite(pSurface, uStartY, uEndY, uX))
        {
            uX++;
        }

        if (uX < uWidth)
        {

            // get start of char
            uStartX = uX;

            // scan for white space
            while ((uX < uWidth) && !_FontScanVertWhite(pSurface, uStartY, uEndY, uX))
            {
                uX++;
            }

            // get end of char
            uEndX = uX;

            _FontSetCharMap(pFont, *pCharList,  uStartX, uStartY, uEndX - uStartX, uEndY - uStartY);
            pCharList++;

            uX++;
        }

    }

    return pCharList;
}

void FontParseChars(FontT *pFont, CSurface *pSurface, const Char *pCharList)
{
    Uint32 uY;
    Uint32 uWidth, uHeight;
    Uint32 uFontHeight = 0;

    uWidth = pSurface->GetWidth();
    uHeight = pSurface->GetHeight();

    uY=0;
    while (uY < uHeight)
    {
        Uint32 uStartY, uEndY;
        // scan for non-white space
        while ((uY < uHeight) && _FontScanHorizWhite(pSurface, 0, uWidth, uY))
        {
            uY++;
        }

        if (uY < uHeight)
        {

            // get start of line
            uStartY = uY;

            // scan for white space
            while ((uY < uHeight) && !_FontScanHorizWhite(pSurface, 0, uWidth, uY))
            {
                uY++;
            }

            uEndY = uY;

            if (uFontHeight == 0)
            {
                // font height is height of first line
                uFontHeight = uEndY - uStartY;
            }

            pCharList = _FontParseLine(pFont, pSurface, pCharList, uStartY, uEndY);
            uY++;
        }
    }

	_FontSetCharSize(pFont, 5, uFontHeight);

}
