/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snspcbrr behavior for SNES audio processing.
 */

#include "types.h"
#include "snspcbrr.h"
#include "prof.h"

/* Preserve the existing Revive BRR arithmetic, including the 16-bit wrap after doubling. Handle the documented
   invalid-range sign-only rule before the predictor. Decode and filter in one pass.
   Copy the eight source bytes first: the old decoder also consumed all input
   before writing the output (including overlapping buffers). */
static __inline Int32 _BRRNibble(Uint32 n, Uint32 range)
{
	return (Int32)(((n ^ 8u) - 8u) << range) >> 1;
}

static __inline Int16 _BRRFilterSample(Int32 sample, Int32 prev0, Int32 prev1,
	Int32 coeff0, Int32 coeff1)
{
	sample += ((prev0 * coeff0) >> 9) + ((prev1 * coeff1) >> 9);
	if (sample > 32767) sample = 32767;
	if (sample < -32768) sample = -32768;
	return (Int16)(sample * 2);
}

Uint8 SNSpcBRRDecode(const Uint8 *pBRRBlock, Int16 *pOut, Int32 iPrev0, Int32 iPrev1)
{
	const Uint8 header = pBRRBlock[0];
	Uint32 range = header >> 4;
	Uint8 data[8];
	Int32 i, coeff0, coeff1;
	if (range > 12)
	{
		/* Ranges 13..15 produce 0 or -2048 before prediction.
		   Normalize sign-only nibbles once to keep the inner loop unchanged. */
		range = 9;
		for (i = 0; i < 8; ++i) data[i] = pBRRBlock[i + 1] & 0x88;
	}
	else
		for (i = 0; i < 8; ++i) data[i] = pBRRBlock[i + 1];

	switch ((header >> 2) & 3)
	{
	case 0:
		/* No predictor or saturation needed: scaled nibbles fit in 16 bits. */
		for (i = 0; i < 8; ++i)
		{
			pOut[2*i] = (Int16)(_BRRNibble(data[i] >> 4, range) * 2);
			pOut[2*i+1] = (Int16)(_BRRNibble(data[i] & 15, range) * 2);
		}
		return header & 3;
	case 1: coeff0 = 240; coeff1 = 0; break;
	case 2: coeff0 = 488; coeff1 = -240; break;
	default: coeff0 = 460; coeff1 = -208; break;
	}

	/* The first-order filter has no second history tap. Specialize it once
	   per block, avoiding the second multiply and history rotation. */
	if (coeff1 == 0)
	{
		for (i = 0; i < 8; ++i)
		{
			iPrev0 = _BRRFilterSample(_BRRNibble(data[i] >> 4, range), iPrev0, 0, 240, 0);
			pOut[2*i] = (Int16)iPrev0;
			iPrev0 = _BRRFilterSample(_BRRNibble(data[i] & 15, range), iPrev0, 0, 240, 0);
			pOut[2*i+1] = (Int16)iPrev0;
		}
	}
	else
	{
		for (i = 0; i < 8; ++i)
		{
			Int16 first = _BRRFilterSample(_BRRNibble(data[i] >> 4, range), iPrev0, iPrev1, coeff0, coeff1);
			Int16 second = _BRRFilterSample(_BRRNibble(data[i] & 15, range), first, iPrev0, coeff0, coeff1);
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
