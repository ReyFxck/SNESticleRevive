#pragma once
#include "types.h"

/* FPS is independent of the detailed SNES timers and also works for NES. */
struct MainLoopFPSSnapshotT
{
    Uint32 SourceRate10, DrawRate10, PresentRate10;
    Bool Ready;
};
extern MainLoopFPSSnapshotT g_MainLoopFPS;
Bool MainLoopFPSIsEnabled();
void MainLoopFPSSetEnabled(Bool enabled);
