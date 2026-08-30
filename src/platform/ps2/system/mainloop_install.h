/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the mainloop install interface for the PlayStation 2 application runtime.
 */

#pragma once

int _MainLoopInstallCallback(char *pDestName, char *pSrcName, int Position, int Total);
void _DumpMemory();
void _GetExploitDir(char *pStr);
void _AddTitleDB(char *pPath);
typedef int (*CopyProgressCallBackT)(char *pDestName, char *pSrcName, int Position, int Total);
int InstallFiles(char *pDestPath, char *pSrcPath, char **ppInstallFiles, CopyProgressCallBackT pCallBack);
int CopyFile(char *pDest, char *pSrc, CopyProgressCallBackT pCallBack);
