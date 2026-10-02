/* Optional target-side timing; normal builds contain none of these probes. */
#ifndef _SNTARGETPROFILE_H
#define _SNTARGETPROFILE_H

#include "types.h"
#include "profclock.h"

#ifndef SNES_TARGET_PROFILE
#define SNES_TARGET_PROFILE 0
#endif

#define SNTARGET_COUNT_HZ PROFCTR_COUNT_HZ

/* Means are per presentation iteration, including hidden emulation frames.
   Nested measurements are inclusive: OBJ belongs to PPU, and audio RPC to
   Mix. GIF waits also belong to the caller that issued them. */
typedef struct SnesTargetProfileFrameT
{
	Uint32 Core, PPU, OBJ, SPC, Mix, Audio, GIFWait;
	Uint32 BGFetch, BGDraw, Output;
	Uint32 SourceFrames, RenderedFrames;
} SnesTargetProfileFrameT;

_INLINE Uint32 SnesTargetProfileRate10(Uint32 nFrames, Uint64 uElapsed)
{
	return uElapsed ? (Uint32)((Uint64)nFrames * SNTARGET_COUNT_HZ * 10u / uElapsed) : 0;
}

_INLINE Uint32 SnesTargetProfileMillis10(Uint64 uCount, Uint32 nIterations)
{
	return nIterations ? (Uint32)(uCount * 10000u / ((Uint64)SNTARGET_COUNT_HZ * nIterations)) : 0;
}

#if SNES_TARGET_PROFILE
#include "profctr.h"
extern SnesTargetProfileFrameT g_SnesTargetProfile;
#define SNTARGET_BEGIN(name) Uint32 name = ProfCtrGetCycle()
#define SNTARGET_END(field, name) \
	(g_SnesTargetProfile.field += ProfCtrGetCycle() - (name))
#else
#define SNTARGET_BEGIN(name)
#define SNTARGET_END(field, name)
#endif

#endif
