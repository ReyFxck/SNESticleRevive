/* Target-side timing HUD; independent of deep SNES instruction counters. */
#ifndef _MAINLOOP_TARGET_PROFILE_H
#define _MAINLOOP_TARGET_PROFILE_H

#include "sntargetprofile.h"

#if SNES_TARGET_PROFILE
struct MainLoopTargetProfileSnapshotT
{
	Uint32 SourceRate10, PresentRate10, RenderRate10, WorkMs10;
	Uint32 CoreMs10, PPUMs10, OBJMs10, SPCMs10, MixMs10, AudioMs10;
	Uint32 GIFWaitMs10, PrepMs10, SubmitMs10, FlipMs10;
	Bool Ready;
};
extern MainLoopTargetProfileSnapshotT g_MainLoopTargetProfile;
#endif

#endif
