#pragma once
#include "types.h"

/* FPS is independent of the detailed SNES timers and also works for NES. */
struct MainLoopFPSSnapshotT
{
    Uint32 SourceRate10, DrawRate10, PresentRate10;
    Bool Ready;
};
extern MainLoopFPSSnapshotT g_MainLoopFPS;
enum MainLoopFPSModeE
{
    MAINLOOP_FPS_OFF = 0,
    MAINLOOP_FPS_SIMPLE = 1,
    MAINLOOP_FPS_DETAILED = 2,
    MAINLOOP_FPS_PROFILER = 3
};
static inline MainLoopFPSModeE MainLoopFPSNormalizeMode(int mode)
{
    return mode >= MAINLOOP_FPS_OFF && mode <= MAINLOOP_FPS_PROFILER
        ? (MainLoopFPSModeE)mode : MAINLOOP_FPS_OFF;
}
static inline MainLoopFPSModeE MainLoopFPSCycleMode(int mode, int dir)
{
    return (MainLoopFPSModeE)((MainLoopFPSNormalizeMode(mode) +
                              (dir < 0 ? 3 : 1)) % 4);
}
Bool MainLoopFPSIsEnabled();
MainLoopFPSModeE MainLoopFPSGetMode();
void MainLoopFPSSetMode(int mode);

enum { MAINLOOP_FPS_COLOR_COUNT = 6 };
static inline int MainLoopFPSNormalizeColor(int color)
{ return color >= 0 && color < MAINLOOP_FPS_COLOR_COUNT ? color : 0; }
static inline int MainLoopFPSCycleColor(int color, int dir)
{ return (MainLoopFPSNormalizeColor(color) + (dir < 0 ? MAINLOOP_FPS_COLOR_COUNT - 1 : 1)) % MAINLOOP_FPS_COLOR_COUNT; }
int MainLoopFPSGetColor();
void MainLoopFPSSetColor(int color);

struct MainLoopDiagFrontT { Bool Ready; Uint32 Work10, MaxWork10, Flip10, Prep10, Submit10; };
extern MainLoopDiagFrontT g_MainLoopDiagFront;
