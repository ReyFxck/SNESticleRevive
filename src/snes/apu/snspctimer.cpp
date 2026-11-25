/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snspctimer behavior for SNES audio processing.
 */

#include "types.h"
#include "snspctimer.h"

void SNSpcTimerReset(SNSpcTimerT *pTimer, Uint32 uCyclesPerTick)
{
	pTimer->bEnabled		= FALSE;
	pTimer->uUpCounter		= 0;
	pTimer->uCompare		= 0;
	pTimer->uCyclesPerTick	= uCyclesPerTick;
	pTimer->iDivisor = pTimer->uCyclesPerTick * 0x100;
	pTimer->iCycleSync		= 0;
	pTimer->nElapsedCycles	= 0;
}

void SNSpcTimerSync(SNSpcTimerT *pTimer, Int32 nCycles)
{
	/* The prescaler runs even while disabled. Keep its phase and the 8-bit
	   target counter packed in the old elapsed field (save-state layout). */
	const Uint32 elapsed = (Uint32)nCycles - (Uint32)pTimer->iCycleSync;
	if (!elapsed || elapsed >= 0x80000000u) return;
	pTimer->iCycleSync = nCycles;
	const Uint32 tick = pTimer->uCyclesPerTick;
	if (!tick) return;
	const Uint32 old = (Uint32)pTimer->nElapsedCycles;
	Uint32 stage = (old / tick) & 255u;
	const Uint32 total = old % tick + elapsed;
	Uint32 pulses = total / tick;
	const Uint32 phase = total % tick;
	if (pTimer->bEnabled && pulses)
	{
		/* A changed target below the current counter must wait for wrap.
		   Equality is tested on each increment, never by >= comparison. */
		Uint32 distance = ((Uint32)pTimer->uCompare - stage) & 255u;
		if (!distance) distance = 256;
		if (pulses < distance)
			stage = (stage + pulses) & 255u;
		else
		{
			pulses -= distance;
			const Uint32 period = pTimer->uCompare ? pTimer->uCompare : 256u;
			pTimer->uUpCounter = (Uint8)((pTimer->uUpCounter + 1u + pulses / period) & 15u);
			stage = pulses % period;
		}
	}
	pTimer->nElapsedCycles = (Int32)(stage * tick + phase);
}

void SNSpcTimerSetEnable(SNSpcTimerT *pTimer, Int32 nCycles, Bool bEnable)
{
	bEnable = bEnable != 0;
	SNSpcTimerSync(pTimer, nCycles);
	if (bEnable && !pTimer->bEnabled)
	{
		// Clear target and output counters, not the free-running prescaler.
		pTimer->nElapsedCycles %= pTimer->uCyclesPerTick;
		pTimer->uUpCounter = 0;
	}
	pTimer->bEnabled = bEnable;
}

void SNSpcTimerSetTimer(SNSpcTimerT *pTimer, Uint8 uValue)
{
	pTimer->uCompare = uValue;

	if (uValue!=0)
	{
		pTimer->iDivisor = pTimer->uCyclesPerTick * pTimer->uCompare;
	} else
	{
		pTimer->iDivisor = pTimer->uCyclesPerTick * 0x100;
	}
}

Uint8 SNSpcTimerGetCounter(SNSpcTimerT *pTimer, Int32 iCycle)
{
	Uint8 uUpCounter;

	SNSpcTimerSync(pTimer, iCycle);

	// get up counter
	uUpCounter = pTimer->uUpCounter;

	// reset up counter
	pTimer->uUpCounter = 0;

	// return
	return uUpCounter & 0xF;
}
