#include <string.h>
#include "types.h"
#include "texture.h"
#include "surface.h"

extern "C" {
#include "gpprim.h"
}

static Uint32 _TextureLog2(Uint32 uVal)
{
    Uint32 n = 0;
    while (uVal > (Uint32)(1U << n))
    {
        n++;
    }
    return n;
}

static Uint32 _TextureCalcBytes(Uint32 uWidth, Uint32 uHeight, TexFormatE eTexFormat)
{
    Uint32 nPixels = uWidth * uHeight;

    switch (eTexFormat)
    {
        case 0x00:
            return nPixels * 4;
        case 0x02:
            return nPixels * 2;
        case 0x13:
            return nPixels;
        case 0x14:
            return (nPixels + 1) >> 1;
        default:
            return 0;
    }
}

void TextureNew(TextureT *pTexture, Uint32 uWidth, Uint32 uHeight, TexFormatE eTexFormat)
{
    Uint32 uWidthPow2;
    Uint32 uHeightPow2;

    memset(pTexture, 0, sizeof(*pTexture));

    pTexture->uWidth      = uWidth;
    pTexture->uHeight     = uHeight;
    pTexture->uWidthLog2  = _TextureLog2(uWidth);
    pTexture->uHeightLog2 = _TextureLog2(uHeight);
    pTexture->eFormat     = eTexFormat;

    uWidthPow2  = (1U << pTexture->uWidthLog2);
    uHeightPow2 = (1U << pTexture->uHeightLog2);

    pTexture->fInvWidth  = 1.0f / (Float32)uWidthPow2;
    pTexture->fInvHeight = 1.0f / (Float32)uHeightPow2;

    pTexture->eFilter   = 0;
    pTexture->uPitch    = uWidthPow2;
    pTexture->nBytes    = _TextureCalcBytes(uWidthPow2, uHeightPow2, eTexFormat);
    pTexture->uVramAddr = 0;
}

void TextureSetAddr(TextureT *pTexture, Uint32 uAddr)
{
    pTexture->uVramAddr = uAddr;
}

void TextureUpload(TextureT *pTexture, Uint8 *pData)
{
    GPPrimUploadTexture(
        pTexture->uVramAddr,
        pTexture->uPitch,
        0,
        0,
        pTexture->eFormat,
        pData,
        pTexture->uWidth,
        pTexture->uHeight
    );
}

void TextureDelete(TextureT *pTexture)
{
    pTexture->uVramAddr = 0;
}

Uint32 TextureGetAddr(TextureT *pTexture)
{
    return pTexture->uVramAddr;
}

void TextureSetFilter(TextureT *pTexture, Uint32 eFilter)
{
    pTexture->eFilter = eFilter;
}
