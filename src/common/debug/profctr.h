/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the profctr interface for shared debugging support.
 */

#ifndef _PROFCTR_H
#define _PROFCTR_H

#include "profclock.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void ProfCtrInit();
void ProfCtrShutdown();
void ProfCtrReset();

//#define ProfCtrGetCycle() (0)

static inline Uint32 ProfCtrGetCycle()
{
#if CODE_PLATFORM == CODE_PS2
    Uint32 uCycle;
	__asm__ volatile ("mfc0  %0, $9" : "=r" (uCycle) : );
    return uCycle;
#elif defined(__i386__) || defined(__x86_64__)
	/* Diagnostic ROM Lab builds need section ratios as well as state hashes.
	   RDTSC is cheap enough to sample the scanline hot path; only deltas are
	   consumed, so the low-word wrap has the same semantics as EE Count. */
	Uint32 uLow, uHigh;
	__asm__ volatile ("rdtsc" : "=a" (uLow), "=d" (uHigh));
	(void)uHigh;
	return uLow;
#elif defined(__aarch64__)
	Uint64 uCycle;
	__asm__ volatile ("mrs %0, cntvct_el0" : "=r" (uCycle));
	return (Uint32)uCycle;
#else
	/* Unsupported host: correctness tools still work without timing data. */
	return 0;
#endif
}

static inline Uint32 ProfCtrGetCounter0()
{
#if CODE_PLATFORM == CODE_PS2
    Uint32 uCount;
	__asm__ volatile ("mfpc  %0, 0" : "=r" (uCount) : );
    return uCount;
#else
	return 0;
#endif
}

static inline Uint32 ProfCtrGetCounter1()
{
#if CODE_PLATFORM == CODE_PS2
    Uint32 uCount;
	__asm__ volatile ("mfpc  %0, 1" : "=r" (uCount) : );
    return uCount;
#else
	return 0;
#endif
}

#define PROFCTR_CYCLEMULTIPLY 1

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
