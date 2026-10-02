/* Optional target-side timing; disabled runtime probes do not read Count. */
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
   Mix. CPU includes register traps (and their PPU/APU work), MDMA/HDMA
   include their register side effects, and GIF waits belong to their caller.
   These rows must not be added together as mutually exclusive costs. */
typedef struct SnesTargetProfileFrameT
{
	Uint32 Core, PPU, OBJ, SPC, Mix, Audio, GIFWait;
	Uint32 BGFetch, BGDraw, Output;
	Uint32 CPU, MDMA, HDMA, RenderBegin, RenderEnd;
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
extern Bool g_SnesTargetProfileEnabled;
_INLINE Bool SnesTargetProfileIsEnabled() { return g_SnesTargetProfileEnabled; }
_INLINE void SnesTargetProfileSetEnabled(Bool enabled) { g_SnesTargetProfileEnabled = enabled; }
#define SNTARGET_BEGIN(name) \
	Bool name##_enabled = g_SnesTargetProfileEnabled; \
	Uint32 name = name##_enabled ? ProfCtrGetCycle() : 0
#define SNTARGET_END(field, name) \
	do { if (name##_enabled) \
		g_SnesTargetProfile.field += ProfCtrGetCycle() - (name); } while (0)
#else
_INLINE Bool SnesTargetProfileIsEnabled() { return FALSE; }
_INLINE void SnesTargetProfileSetEnabled(Bool enabled) { (void)enabled; }
#define SNTARGET_BEGIN(name)
#define SNTARGET_END(field, name)
#endif

#endif
