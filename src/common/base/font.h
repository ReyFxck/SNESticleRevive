
#ifndef _FONT_H
#define _FONT_H

#include "texture.h"

struct FontCharT
{
	Uint8 u0, v0;
	Uint8 u1, v1;
};

struct FontT 
{
	TextureT 	Texture;
    ClutT       Clut;

    Int32       nChars;
	FontCharT	CharMap[256];

	Int32	 	uCharX, uCharY;
    Int32       uFixedWidth;
};

//#define FONT_WIDTH 8
//#define FONT_HEIGHT 16

void FontInit(Uint32 uVramAddr);
void FontShutdown();

void FontSetFont(Int32 iFont, FontT *pFont);
void FontSelect(Int32 iFont);
void FontPrintf(Float32 vx, Float32 vy, const Char *pFormat, ...);
void FontPuts(Float32 vx, Float32 vy, const Char *pStr);
void FontColor4f(Float32 r, Float32 g, Float32 b, Float32 a);

Int32 FontGetHeight();
Int32 FontGetWidth();
Int32 FontGetStrWidth(const Char *pStr);

void FontNew(FontT *pFont);
void FontDelete(FontT *pFont);
void FontParseChars(FontT *pFont, class CSurface *pSurface, const Char *pCharList);
void FontMake(FontT *pFont, class CSurface *pSurface, Uint32 uVramAddr, const Char *pCharList);

/* Explicit glyph-map font (e.g. m6x11): each entry maps an ASCII code to
   a rectangle in the atlas.  Avoids FontParseChars' fragile gap-detection
   (which would split glyphs that contain a fully-transparent column, like
   the double-quote) and gives every glyph a uniform height. */
typedef struct
{
    Uint16 c;
    Uint16 u, v, w, h;
} FontMapEntryT;

void FontMakeFromMap(FontT *pFont, class CSurface *pSurface, Uint32 uVramAddr,
                     const FontMapEntryT *pMap, Int32 nCount,
                     Int32 uSpaceW, Int32 uLineH);

#endif

