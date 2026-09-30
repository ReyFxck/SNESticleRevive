/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Small sample-domain helpers shared by the SPC DSP mixer and host tests.
 */
#ifndef _SNSPCMATH_H
#define _SNSPCMATH_H

#include "types.h"

/* S-DSP pitch modulation:
   pitch += (previous_voice_output >> 5) * pitch >> 10
   The internal pitch latch is 15-bit after modulation. */
static _INLINE Uint32 SNSpcDspApplyPitchMod(Uint32 uPitch, Int32 iPreviousOutput)
{
	Int32 iPitch = (Int32)(uPitch & 0x3FFFu);
	Int32 iModulated =
		iPitch + (((iPreviousOutput >> 5) * iPitch) >> 10);

	if (iModulated < 0)
		iModulated = 0;
	if (iModulated > 0x7FFF)
		iModulated = 0x7FFF;
	return (Uint32)iModulated;
}

/* Match the existing Revive mixer interpolation exactly. */
static _INLINE Int32 SNSpcDspInterpolateLinear(
	Int16 iSample0, Int16 iSample1, Uint16 uFrac)
{
	Int32 iFrac0 = (Int32)(uFrac >> 1);
	Int32 iFrac1 = iFrac0 ^ 0x7FFF;
	Int32 iSample =
		(Int32)iSample0 * iFrac1 + (Int32)iSample1 * iFrac0;
	return iSample >> 15;
}

/* Voice output before per-channel volume. This is the value used by PMON and
   represented by OUTX in the hardware pipeline. Revive's envelope is 7-bit. */
static _INLINE Int16 SNSpcDspVoiceOutput(
	Int16 iSample0, Int16 iSample1, Uint16 uFrac, Uint8 uEnvelope)
{
	Int32 iSample = SNSpcDspInterpolateLinear(
		iSample0, iSample1, uFrac);
	iSample = (iSample * (Int32)uEnvelope) >> 7;

	if (iSample > 0x7FFF)
		iSample = 0x7FFF;
	if (iSample < -0x8000)
		iSample = -0x8000;

	/* The S-DSP voice output clears bit 0 before PMON/OUTX. */
	iSample &= ~1;
	return (Int16)iSample;
}

#endif
