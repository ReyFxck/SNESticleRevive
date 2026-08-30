/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements emurom behavior for the emulator application layer.
 */

#include <stdlib.h>
#include "types.h"
#include "emurom.h"

using namespace Emu;

Rom::Rom()
{
    m_bLoaded = FALSE;
}

Rom::~Rom()
{
}

Rom::LoadErrorE Rom::LoadRom(class CDataIO *pFileIO, Uint8 *pBuffer, Uint32 nBufferBytes)
{
	return LOADERROR_INVALID;
}

void Rom::Unload()
{
}

Uint32	Rom::GetNumRomRegions()
{
	return 0;
}

char   *Rom::GetRomRegionName(Uint32 eRegion)
{
	return NULL;
}

Uint32 	Rom::GetRomRegionSize(Uint32 eRegion)
{
	return 0;
}

Uint32 Rom::GetNumExts()
{
	return 0;
}

char *Rom::GetExtName(Uint32 uExt)
{
	return NULL;
}
