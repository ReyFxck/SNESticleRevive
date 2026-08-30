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

#define SNSPCBRR_CLAMP FALSE

/*
ldl		t0    // load 16 nibbles
ldr		t0		   // T0 = OPMN KLIJ GHEF CDAB
pextlb  t1,t0,r0   // t1 = OP00 MN00 KL00 IJ00 GH00 EF00 CD00 AB00
psllh   t2,t1,4    // t2 = P000 N000 L000 J000 H000 F000 D000 B000
psrah   t1,t1,12   // t1 = OOOO MMMM KKKK IIII GGGG EEEE CCCC AAAA
psrah   t2,t2,12   // t2 = PPPP NNNN LLLL JJJJ HHHH FFFF DDDD BBBB
pcpyld  t3,t2,t2   // t3 = HHHH FFFF DDDD BBBB HHHH FFFF DDDD BBBB
pcpyud  t4,t1,t1   // t4 = OOOO MMMM KKKK IIII OOOO MMMM KKKK IIII
pinth   t0,t3,t0   // t0 = HHHH GGGG FFFF EEEE DDDD CCCC BBBB AAAA
pinth   t2,t4,t2   // t2 = PPPP OOOO NNNN MMMM LLLL KKKK JJJJ IIII
*/

static Int32 _SNSpcDsp_FilterParm[4][2]=
{
	{0x100 *  0 / 16, 0x100 *   0 / 16},
	{0x100 * 15 / 16, 0x100 *   0 / 16},
	{0x100 * 61 / 32, 0x100 * -15 / 16},
	{0x100 * 115/ 64, 0x100 * -13 / 16}
};

// BRR decode
#include "console.h"

#if 4
static void _SNSpcBRRFilter3(Int16 *pOut, Int16 *pIn, Int32 nSamples, Int32 eFilterType, Int32 iPrev0, Int32 iPrev1)
{
	Int32 iFilter0, iFilter1;
	Int32 iMin = -0x8000;
	Int32 iMax = 0x7FFF;

	iFilter0 = _SNSpcDsp_FilterParm[eFilterType][0];
	iFilter1 = _SNSpcDsp_FilterParm[eFilterType][1];

	while (nSamples > 0)
	{
		Int32 iSample;

		iSample  = *pIn;
		iSample>>=1;

		iSample += ((iPrev0 * iFilter0)>>9) + ((iPrev1 * iFilter1) >> 9);

		// clamp sample to 16-bit range
		if (iSample >  iMax) iSample = iMax;
		if (iSample <  iMin) iSample = iMin;
		iSample<<=1;

		// write sample
		*pOut = iSample;

		// rotate sample queue
		iPrev1 = iPrev0;
		iPrev0 = *pOut;

		pOut++;
		pIn++;
		nSamples--;
	}
}

#endif

Uint8 SNSpcBRRDecode(Uint8 *pBRRBlock, Int16 *pOut, Int32 iPrev0, Int32 iPrev1)
{
	Uint8 uHeader;
	Uint32 uRange;
	Int16 Decode[16];
	Int16 *pDecode;
	Int32 iByte;

	uHeader = *pBRRBlock++;
	uRange = uHeader >> 4;

	// adjust range if invalid
	if (uRange > 12)
	{
		uRange-=4;
	}

	// decode 16 samples to temp buffer
	pDecode = Decode;
	for (iByte=0; iByte< 8; iByte++)
	{
		Int32 iData = *pBRRBlock++;
		pDecode[0] = ((iData << 24) >> 28) << uRange; // upper nibble
		pDecode[1] = ((iData << 28) >> 28) << uRange; // lower nibble
		pDecode+=2;
	}

	// apply filter

	_SNSpcBRRFilter3(pOut, Decode, 16, (uHeader >> 2) & 3, iPrev0, iPrev1);

	// return end and loop bits
	return uHeader & 3;
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
