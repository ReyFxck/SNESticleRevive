/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the file interface for shared base utilities.
 */

#ifndef _FILE_H
#define _FILE_H

Bool FileReadMem(Char *pFilePath, void *pMem, Uint32 nBytes);
Bool FileWriteMem(Char *pFilePath, void *pMem, Uint32 nBytes);

Bool FileExists(Char *pFilePath);

#endif
