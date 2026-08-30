/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the memcard interface for PlayStation 2 memory-card access.
 */

#ifndef _MEMCARD_H
#define _MEMCARD_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MemCardStatusE
{
	MEMCARD_STATUS_ERROR       = -1,
	MEMCARD_STATUS_NOT_PRESENT = 0,
	MEMCARD_STATUS_READY       = 1,
	MEMCARD_STATUS_UNFORMATTED = 2
} MemCardStatusE;

void MemCardInit(void);
void MemCardShutdown(void);
int  MemCardCreateSave(char *pDir, char *pTitle, Bool bForceWrite);
Bool MemCardCheckNewCard(void);
MemCardStatusE MemCardGetStatus(Int32 iPort);
Bool MemCardFormat(Int32 iPort);
Bool MemCardReadFile(char *pPath, Uint8 *pData, Uint32 nBytes);
Bool MemCardWriteFile(char *pPath, Uint8 *pData, Uint32 nBytes);

#ifdef __cplusplus
}
#endif

#endif
