#pragma once

int _MainLoopInstallCallback(char *pDestName, char *pSrcName, int Position, int Total);
void _DumpMemory();
void _GetExploitDir(char *pStr);
void _AddTitleDB(char *pPath);
typedef int (*CopyProgressCallBackT)(char *pDestName, char *pSrcName, int Position, int Total);
int InstallFiles(char *pDestPath, char *pSrcPath, char **ppInstallFiles, CopyProgressCallBackT pCallBack);
int CopyFile(char *pDest, char *pSrc, CopyProgressCallBackT pCallBack);
