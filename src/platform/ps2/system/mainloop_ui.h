#pragma once

#include "types.h"

class CScreen;

void MainLoopModalPrintf(Int32 Time, const Char *pFormat, ...);
void MainLoopStatusPrintf(Int32 Time, const Char *pFormat, ...);
void ScrPrintf(const Char *pFormat, ...);

void _MainLoopSetScreen(CScreen *pScreen);
void _UICycle(int dir);
void _MainLoopCycleScreen(int dir);
void MainLoopShutdown();
