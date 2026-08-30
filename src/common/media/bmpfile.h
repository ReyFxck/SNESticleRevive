/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the bmpfile interface for shared media decoding.
 */

#ifndef _BMPFILE_H
#define _BMPFILE_H

class CSurface;
struct PaletteT;

Bool BMPWriteFile(Char *pFileName, CSurface *pSurface, PaletteT *pPalette);
Bool BMPReadFile(Char *pFileName, CSurface *pSurface);

#endif
