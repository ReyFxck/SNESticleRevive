#pragma once

#include "types.h"

class CScreen;

#ifdef __cplusplus
extern "C" {
#endif

void MainLoopModalPrintf(Int32 Time, const Char *pFormat, ...);
void MainLoopStatusPrintf(Int32 Time, const Char *pFormat, ...);
void ScrPrintf(const Char *pFormat, ...);

#ifdef __cplusplus
}
#endif

void _MainLoopSetScreen(CScreen *pScreen);
void _UICycle(int dir);
void _MainLoopCycleScreen(int dir);
void MainLoopShutdown();
