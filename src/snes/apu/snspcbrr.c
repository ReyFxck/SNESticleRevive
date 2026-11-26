/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 * Implements snspcbrr behavior for SNES audio processing.
 */

#include "types.h"
#include "snspcbrr.h"
#include "prof.h"

/* Use the S-DSP intermediate rounding, including the 16-bit wrap after doubling. Handle the documented
   invalid-range sign-only rule before the predictor. Decode and filter in one pass.
   Copy the eight source bytes first: the old decoder also consumed all input
   before writing the output (including overlapping buffers). */
static __inline Int32 _BRRNibble(Uint32 n, Uint32 range)
{
	return (Int32)(((n ^ 8u) - 8u) << range) >> 1;
}

/* The predictor uses separately rounded terms, not a rounded sum of
   fixed-point coefficients. History is in doubled (16-bit) sample units. */
static __inline Int16 _BRRFilterSample(Int32 sample, Int32 prev0, Int32 prev1,
    Uint32 filter)
{
    if (filter == 1)
        sample += (prev0 >> 1) + ((-prev0) >> 5);
    else
    {
        const Int32 halfOld = prev1 >> 1;
        if (filter == 2)
            sample += prev0 + ((-3 * prev0) >> 6) - halfOld + (halfOld >> 4);
        else
            sample += prev0 + ((-13 * prev0) >> 7) - halfOld + ((3 * halfOld) >> 4);
    }
    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;
    return (Int16)(sample * 2);
}

Uint8 SNSpcBRRDecode(const Uint8 *pBRRBlock, Int16 *pOut, Int32 iPrev0, Int32 iPrev1)
{
	const Uint8 header = pBRRBlock[0];
	Uint32 range = header >> 4;
	Uint8 data[8];
	Int32 i;
	const Uint32 filter = (header >> 2) & 3;
	if (range > 12)
	{
		/* Ranges 13..15 produce 0 or -2048 before prediction.
		   Normalize sign-only nibbles once to keep the inner loop unchanged. */
		range = 9;
		for (i = 0; i < 8; ++i) data[i] = pBRRBlock[i + 1] & 0x88;
	}
	else
		for (i = 0; i < 8; ++i) data[i] = pBRRBlock[i + 1];

	switch (filter)
	{
	case 0:
		/* No predictor or saturation needed: scaled nibbles fit in 16 bits. */
		for (i = 0; i < 8; ++i)
		{
			pOut[2*i] = (Int16)(_BRRNibble(data[i] >> 4, range) * 2);
			pOut[2*i+1] = (Int16)(_BRRNibble(data[i] & 15, range) * 2);
		}
		return header & 3;
	default: break;
	}

	/* The first-order filter has no second history tap. */
	if (filter == 1)
	{
		for (i = 0; i < 8; ++i)
		{
			iPrev0 = _BRRFilterSample(_BRRNibble(data[i] >> 4, range), iPrev0, 0, 1);
			pOut[2*i] = (Int16)iPrev0;
			iPrev0 = _BRRFilterSample(_BRRNibble(data[i] & 15, range), iPrev0, 0, 1);
			pOut[2*i+1] = (Int16)iPrev0;
		}
	}
	else
	{
		for (i = 0; i < 8; ++i)
		{
			Int16 first = _BRRFilterSample(_BRRNibble(data[i] >> 4, range), iPrev0, iPrev1, filter);
			Int16 second = _BRRFilterSample(_BRRNibble(data[i] & 15, range), first, iPrev0, filter);
			pOut[2*i] = first;
			pOut[2*i+1] = second;
			iPrev1 = first;
			iPrev0 = second;
		}
	}
	return header & 3;
}

void SNSpcBRRClear(Int16 *pOut, Int16 iPrev)
{
	Int32 nSamples = 16;
	while (nSamples > 0)
	{
		pOut[0]=iPrev;
		pOut[1]=iPrev;
		pOut[2]=iPrev;
		pOut[3]=iPrev;
		pOut+=4;
		nSamples-=4;
	}
}
