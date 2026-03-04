#ifndef _MEMCARD_H
#define _MEMCARD_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

void MemCardInit(void);
void MemCardShutdown(void);
int  MemCardCreateSave(char *pDir, char *pTitle, Bool bForceWrite);
Bool MemCardCheckNewCard(void);
Bool MemCardReadFile(char *pPath, Uint8 *pData, Uint32 nBytes);
Bool MemCardWriteFile(char *pPath, Uint8 *pData, Uint32 nBytes);

#ifdef __cplusplus
}
#endif

#endif
