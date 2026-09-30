/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snspctimer behavior for SNES audio processing.
 */

#include "types.h"
#include "snspctimer.h"

#define SNSPC_TIMER_STAGE3_MASK       0x0F
#define SNSPC_TIMER_STAGE1_BIT        0x10
#define SNSPC_TIMER_LINE_BIT          0x20
#define SNSPC_TIMER_GLOBAL_ENABLE_BIT 0x40
#define SNSPC_TIMER_GLOBAL_DISABLE_BIT 0x80

static void _SNSpcTimerPulseStage2(SNSpcTimerT *pTimer)
{
	Uint8 uStage2;

	if (!pTimer->bEnabled)
		return;

	uStage2 = (Uint8)pTimer->iDivisor;
	uStage2++;

	/* Hardware compares an 8-bit stage2 counter after increment.  A target
	   of zero therefore naturally means 256 clocks by wraparound. */
	if (uStage2 == pTimer->uCompare)
	{
		uStage2 = 0;
		pTimer->uUpCounter =
			(Uint8)((pTimer->uUpCounter & ~SNSPC_TIMER_STAGE3_MASK) |
			(((pTimer->uUpCounter & SNSPC_TIMER_STAGE3_MASK) + 1) & 0x0F));
	}

	pTimer->iDivisor = uStage2;
}

static void _SNSpcTimerSynchronizeStage1(SNSpcTimerT *pTimer)
{
	Bool bStage1 = (pTimer->uUpCounter & SNSPC_TIMER_STAGE1_BIT) ? TRUE : FALSE;
	Bool bOldLine = (pTimer->uUpCounter & SNSPC_TIMER_LINE_BIT) ? TRUE : FALSE;
	Bool bLine = bStage1;

	if (!(pTimer->uUpCounter & SNSPC_TIMER_GLOBAL_ENABLE_BIT))
		bLine = FALSE;
	if (pTimer->uUpCounter & SNSPC_TIMER_GLOBAL_DISABLE_BIT)
		bLine = FALSE;

	/* ares/bsnes timer pipeline: stage2 pulses only on a 1->0 transition of
	   the globally gated stage1 signal. */
	if (bOldLine && !bLine)
		_SNSpcTimerPulseStage2(pTimer);

	if (bLine)
		pTimer->uUpCounter |= SNSPC_TIMER_LINE_BIT;
	else
		pTimer->uUpCounter &= (Uint8)~SNSPC_TIMER_LINE_BIT;
}

void SNSpcTimerReset(SNSpcTimerT *pTimer, Uint32 uCyclesPerTick)
{
	pTimer->bEnabled = FALSE;
	pTimer->uCompare = 0;
	pTimer->uCyclesPerTick = uCyclesPerTick;
	pTimer->iCycleSync = 0;
	pTimer->nElapsedCycles = 0;
	pTimer->iDivisor = 0;

	/* TEST powers up with RAM writes and timers globally enabled. */
	pTimer->uUpCounter = SNSPC_TIMER_GLOBAL_ENABLE_BIT;
}

void SNSpcTimerSync(SNSpcTimerT *pTimer, Int32 nCycles)
{
	Int32 nDelta = nCycles - pTimer->iCycleSync;

	if (nDelta > 0)
	{
		pTimer->nElapsedCycles += nDelta;
		while (pTimer->nElapsedCycles >= (Int32)pTimer->uCyclesPerTick)
		{
			pTimer->nElapsedCycles -= (Int32)pTimer->uCyclesPerTick;
			pTimer->uUpCounter ^= SNSPC_TIMER_STAGE1_BIT;
			_SNSpcTimerSynchronizeStage1(pTimer);
		}
	}

	pTimer->iCycleSync = nCycles;
}

void SNSpcTimerSetEnable(SNSpcTimerT *pTimer, Int32 nCycles, Bool bEnable)
{
	SNSpcTimerSync(pTimer, nCycles);

	if (bEnable && !pTimer->bEnabled)
	{
		/* CONTROL 0->1 resets stage2/stage3, but stage0/stage1 continue. */
		pTimer->iDivisor = 0;
		pTimer->uUpCounter &= (Uint8)~SNSPC_TIMER_STAGE3_MASK;
	}

	pTimer->bEnabled = bEnable ? TRUE : FALSE;
}

void SNSpcTimerSetTimer(SNSpcTimerT *pTimer, Uint8 uValue)
{
	pTimer->uCompare = uValue;
}

void SNSpcTimerSetGlobalGate(SNSpcTimerT *pTimer, Int32 nCycles,
	Bool bTimersEnable, Bool bTimersDisable)
{
	/* Catch the timer up with the old TEST value before changing the line. */
	SNSpcTimerSync(pTimer, nCycles);

	if (bTimersEnable)
		pTimer->uUpCounter |= SNSPC_TIMER_GLOBAL_ENABLE_BIT;
	else
		pTimer->uUpCounter &= (Uint8)~SNSPC_TIMER_GLOBAL_ENABLE_BIT;

	if (bTimersDisable)
		pTimer->uUpCounter |= SNSPC_TIMER_GLOBAL_DISABLE_BIT;
	else
		pTimer->uUpCounter &= (Uint8)~SNSPC_TIMER_GLOBAL_DISABLE_BIT;

	/* A TEST write can itself force the gated stage1 signal high or low. */
	_SNSpcTimerSynchronizeStage1(pTimer);
}

Uint8 SNSpcTimerGetCounter(SNSpcTimerT *pTimer, Int32 iCycle)
{
	Uint8 uCounter;

	SNSpcTimerSync(pTimer, iCycle);
	uCounter = pTimer->uUpCounter & SNSPC_TIMER_STAGE3_MASK;

	/* Reading TnOUT clears only stage3. */
	pTimer->uUpCounter &= (Uint8)~SNSPC_TIMER_STAGE3_MASK;
	return uCounter;
}
