/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the pathext interface for shared base utilities.
 */

#ifndef _PATHEXT_H
#define _PATHEXT_H

typedef Uint32 PathExtTypeE;

int PathExtAdd(PathExtTypeE Type, char *pExt);
Bool PathExtResolve(char *pPath, PathExtTypeE *pType, Bool bTruncatePath);
char *PathExtGet(char *pPath);

#endif
