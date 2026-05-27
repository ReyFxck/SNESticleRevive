#pragma once

#include "types.h"

namespace Emu { class Rom; }

void _MainLoopGetName(Char *pName, const Char *pPath);
int _MainLoopReadBinaryData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pRomFile);
int _MainLoopReadGZData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pRomFile);
int _MainLoopReadZipData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pZipFile, char *pFileName);
Bool _MainLoopLoadRomData(Emu::Rom *pRom, Uint8 *pRomData, Int32 nRomBytes);
Bool _MainLoopLoadBios(Emu::Rom *pRom, const Char *pFilePath);
Bool _MainLoopLoadSnesPalette(const char *pFileName);
void _MainLoopUnloadRom();
Bool _MainLoopExecuteFile(const char *pFileName, Bool bLoadSRAM);
void _MainLoopSetSampleRate(Uint32 uSampleRate);
