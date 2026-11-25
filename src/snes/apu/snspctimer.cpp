/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snspctimer behavior for SNES audio processing.
 */

#include "types.h"
#include "snspctimer.h"

typedef char SNSpcTimerLegacySize[(sizeof(SNSpcTimerT) == 20) ? 1 : -1];
static const Uint8 kTestGated = 0xA5;

void SNSpcTimerReset(SNSpcTimerT *pTimer, Uint32 uCyclesPerTick)
{
	pTimer->bEnabled		= FALSE;
	pTimer->uTestGate = 0;
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
    const Uint32 oldQuotient = old / tick;
    Uint32 stage = oldQuotient & 255u;
    const Uint32 oldPhase = old - oldQuotient * tick;
    const Uint32 total = oldPhase + elapsed;
    // Most polling accesses occur before the next prescaler edge.
    if (total < tick)
    {
        pTimer->nElapsedCycles = (Int32)(stage * tick + total);
        return;
    }
    Uint32 pulses = total / tick;
    const Uint32 phase = total - pulses * tick;
	if (pTimer->bEnabled && pTimer->uTestGate != kTestGated && pulses)
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

/* TEST bits 3/0 gate the prescaler output, independent of CONTROL's
   enable latch. Do not reset the target or output counter on gate writes.
   The half-period level is derived from the packed prescaler phase. */
void SNSpcTimerSetTestGate(SNSpcTimerT *pTimer, Int32 nCycles, Bool bAllow)
{
    SNSpcTimerSync(pTimer, nCycles);
    const Uint32 tick = pTimer->uCyclesPerTick;
    if (!bAllow && pTimer->uTestGate != kTestGated && pTimer->bEnabled && tick)
    {
        Uint32 stage = ((Uint32)pTimer->nElapsedCycles / tick) & 255u;
        const Uint32 phase = (Uint32)pTimer->nElapsedCycles % tick;
        // Gating a high prescaler output produces a falling edge.
        if (phase >= tick / 2u)
        {
            stage = (stage + 1u) & 255u;
            if (stage == pTimer->uCompare)
            {
                stage = 0;
                pTimer->uUpCounter = (pTimer->uUpCounter + 1u) & 15u;
            }
            pTimer->nElapsedCycles = (Int32)(stage * tick + phase);
        }
    }
    pTimer->uTestGate = bAllow ? 0 : kTestGated;
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
