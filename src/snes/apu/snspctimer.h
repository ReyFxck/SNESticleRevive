/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 * Declares the snspctimer interface for SNES audio processing.
 */

#ifndef _SNSPCTIMER_H
#define _SNSPCTIMER_H

typedef struct SNSpcTimer_t
{
	Int32	iCycleSync;

	Int32	nElapsedCycles;		// stage-2 counter * tick + free-running prescaler phase
	Int32	iDivisor;			// legacy state field: target period in master clocks

	Uint32	uCyclesPerTick;		// clock cycles per tick (low-counter)
	Uint8	uCompare;			// low-counter compare
	Uint8	uUpCounter;			// 4-bit hi-counter value
	Bool	bEnabled;
	Uint8 uTestGate; // former padding; 0 = normal, 0xA5 = globally gated
} SNSpcTimerT;

void SNSpcTimerReset(SNSpcTimerT *pTimer, Uint32 uCyclesPerTick);
void SNSpcTimerSetEnable(SNSpcTimerT *pTimer, Int32 nCycles, Bool bEnable);
void SNSpcTimerSetTestGate(SNSpcTimerT *pTimer, Int32 nCycles, Bool bAllow);
void SNSpcTimerSetTimer(SNSpcTimerT *pTimer, Uint8 uValue);
void SNSpcTimerSync(SNSpcTimerT *pTimer, Int32 nCycles);
Uint8 SNSpcTimerGetCounter(SNSpcTimerT *pTimer, Int32 nCycles);

#endif
