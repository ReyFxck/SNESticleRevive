/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the audframeschedule interface for shared rendering and audio buffers.
 */

#ifndef _AUDFRAMESCHEDULE_H
#define _AUDFRAMESCHEDULE_H

#include "types.h"

/*
 * Distribui uma taxa de amostragem por quadros em blocos alinhados.
 *
 * Para 32000 Hz / 60 quadros / quantum 4, a sequencia e' 532, 532, 536.
 * Ao fim de 60 quadros a soma e' exatamente 32000. A fase representa apenas
 * arredondamento fracionario; ela nunca acumula atraso do ring de audio.
 */
_INLINE Int32 AudFrameScheduleNext(Uint32 *pPhase, Uint32 uSampleRate,
	Uint32 uFrameRate, Uint32 uQuantum)
{
	Uint32 uDenominator;
	Uint32 uSamples;

	if (!pPhase || !uSampleRate || !uFrameRate || !uQuantum)
		return 0;

	uDenominator = uFrameRate * uQuantum;
	*pPhase += uSampleRate;
	uSamples = (*pPhase / uDenominator) * uQuantum;
	*pPhase -= uSamples * uFrameRate;
	return (Int32)uSamples;
}

/* Same scheduler with an exact rational video rate (numerator/denominator).
   This avoids treating SNES NTSC/PAL as exactly 60/50 Hz and lets fractional
   drift remain in the phase accumulator instead of becoming an audio glitch. */
_INLINE Int32 AudFrameScheduleNextRatio(Uint64 *pPhase, Uint32 uSampleRate,
	Uint32 uFrameRateNumerator, Uint32 uFrameRateDenominator,
	Uint32 uQuantum)
{
	Uint64 uStep;
	Uint64 uDenominator;
	Uint64 uSamples;

	if (!pPhase || !uSampleRate || !uFrameRateNumerator ||
	    !uFrameRateDenominator || !uQuantum)
		return 0;

	uStep = (Uint64)uSampleRate * (Uint64)uFrameRateDenominator;
	uDenominator = (Uint64)uFrameRateNumerator * (Uint64)uQuantum;
	*pPhase += uStep;
	uSamples = (*pPhase / uDenominator) * uQuantum;
	*pPhase -= uSamples * (Uint64)uFrameRateNumerator;
	return (Int32)uSamples;
}

#endif // _AUDFRAMESCHEDULE_H
