/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snspctimer interface for SNES audio processing.
 */

#ifndef _SNSPCTIMER_H
#define _SNSPCTIMER_H

typedef struct SNSpcTimer_t
{
	Int32	iCycleSync;

	/* Keep this legacy layout stable for save states.  The fields now model
	   the hardware timer pipeline instead of one pre-multiplied divisor. */
	Int32	nElapsedCycles;		// stage0 remainder in master-cycle units
	Int32	iDivisor;			// stage2 8-bit counter (stored in low byte)

	Uint32	uCyclesPerTick;		// stage0 period (128 or 16 SMP cycles)
	Uint8	uCompare;			// target; zero means 256 by 8-bit wrap
	/* low nibble = stage3; bits 4/5 = stage1/line; bits 6/7 = global gates */
	Uint8	uUpCounter;
	Bool	bEnabled;
} SNSpcTimerT;

void SNSpcTimerReset(SNSpcTimerT *pTimer, Uint32 uCyclesPerTick);
void SNSpcTimerSetEnable(SNSpcTimerT *pTimer, Int32 nCycles, Bool bEnable);
void SNSpcTimerSetTimer(SNSpcTimerT *pTimer, Uint8 uValue);
void SNSpcTimerSetGlobalGate(SNSpcTimerT *pTimer, Int32 nCycles,
	Bool bTimersEnable, Bool bTimersDisable);
void SNSpcTimerSync(SNSpcTimerT *pTimer, Int32 nCycles);
Uint8 SNSpcTimerGetCounter(SNSpcTimerT *pTimer, Int32 nCycles);

#endif
