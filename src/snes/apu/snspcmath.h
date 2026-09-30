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

/*
 * 512 coefficients of the SNES S-DSP Gaussian interpolator.
 *
 * Generated from the analytical Gaussian-window construction and normalized
 * in four mirrored quadrants to a sum of 2048. The resulting integer table
 * matches the hardware-oriented tables used by modern S-DSP references.
 *
 * Keeping this in read-only memory is deliberate: the PS2 scratchpad remains
 * reserved for the hot mixer buffers and SNES renderer lookup split.
 */
static const Int16 _SNSpcDspGaussian[512] =
{
	   0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
	   1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    2,    2,    2,    2,    2,
	   2,    2,    3,    3,    3,    3,    3,    4,    4,    4,    4,    4,    5,    5,    5,    5,
	   6,    6,    6,    6,    7,    7,    7,    8,    8,    8,    9,    9,    9,   10,   10,   10,
	  11,   11,   11,   12,   12,   13,   13,   14,   14,   15,   15,   15,   16,   16,   17,   17,
	  18,   19,   19,   20,   20,   21,   21,   22,   23,   23,   24,   24,   25,   26,   27,   27,
	  28,   29,   29,   30,   31,   32,   32,   33,   34,   35,   36,   36,   37,   38,   39,   40,
	  41,   42,   43,   44,   45,   46,   47,   48,   49,   50,   51,   52,   53,   54,   55,   56,
	  58,   59,   60,   61,   62,   64,   65,   66,   67,   69,   70,   71,   73,   74,   76,   77,
	  78,   80,   81,   83,   84,   86,   87,   89,   90,   92,   94,   95,   97,   99,  100,  102,
	 104,  106,  107,  109,  111,  113,  115,  117,  118,  120,  122,  124,  126,  128,  130,  132,
	 134,  137,  139,  141,  143,  145,  147,  150,  152,  154,  156,  159,  161,  163,  166,  168,
	 171,  173,  175,  178,  180,  183,  186,  188,  191,  193,  196,  199,  201,  204,  207,  210,
	 212,  215,  218,  221,  224,  227,  230,  233,  236,  239,  242,  245,  248,  251,  254,  257,
	 260,  263,  267,  270,  273,  276,  280,  283,  286,  290,  293,  297,  300,  304,  307,  311,
	 314,  318,  321,  325,  328,  332,  336,  339,  343,  347,  351,  354,  358,  362,  366,  370,
	 374,  378,  381,  385,  389,  393,  397,  401,  405,  410,  414,  418,  422,  426,  430,  434,
	 439,  443,  447,  451,  456,  460,  464,  469,  473,  477,  482,  486,  491,  495,  499,  504,
	 508,  513,  517,  522,  527,  531,  536,  540,  545,  550,  554,  559,  563,  568,  573,  577,
	 582,  587,  592,  596,  601,  606,  611,  615,  620,  625,  630,  635,  640,  644,  649,  654,
	 659,  664,  669,  674,  678,  683,  688,  693,  698,  703,  708,  713,  718,  723,  728,  732,
	 737,  742,  747,  752,  757,  762,  767,  772,  777,  782,  787,  792,  797,  802,  806,  811,
	 816,  821,  826,  831,  836,  841,  846,  851,  855,  860,  865,  870,  875,  880,  884,  889,
	 894,  899,  904,  908,  913,  918,  923,  927,  932,  937,  941,  946,  951,  955,  960,  965,
	 969,  974,  978,  983,  988,  992,  997, 1001, 1005, 1010, 1014, 1019, 1023, 1027, 1032, 1036,
	1040, 1045, 1049, 1053, 1057, 1061, 1066, 1070, 1074, 1078, 1082, 1086, 1090, 1094, 1098, 1102,
	1106, 1109, 1113, 1117, 1121, 1125, 1128, 1132, 1136, 1139, 1143, 1146, 1150, 1153, 1157, 1160,
	1164, 1167, 1170, 1174, 1177, 1180, 1183, 1186, 1190, 1193, 1196, 1199, 1202, 1205, 1207, 1210,
	1213, 1216, 1219, 1221, 1224, 1227, 1229, 1232, 1234, 1237, 1239, 1241, 1244, 1246, 1248, 1251,
	1253, 1255, 1257, 1259, 1261, 1263, 1265, 1267, 1269, 1270, 1272, 1274, 1275, 1277, 1279, 1280,
	1282, 1283, 1284, 1286, 1287, 1288, 1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1297, 1298,
	1299, 1300, 1300, 1301, 1302, 1302, 1303, 1303, 1303, 1304, 1304, 1304, 1304, 1304, 1305, 1305
};

static _INLINE Int32 SNSpcDspClamp16(Int32 iValue)
{
	if (iValue > 0x7FFF) return 0x7FFF;
	if (iValue < -0x8000) return -0x8000;
	return iValue;
}

/*
 * Hardware S-DSP four-tap Gaussian interpolation.
 *
 * pSample points at the current decoded BRR sample. The existing Revive BRR
 * block layout guarantees pSample[-1], [0], [1], [2] are valid because it
 * carries two samples across block boundaries and fetches the next block
 * before the phase reaches sample 14.
 *
 * The 16-bit wrap after the first three taps is an S-DSP quirk and must occur
 * before the fourth tap. Output bit 0 is always cleared by the DSP.
 */
static _INLINE Int16 SNSpcDspInterpolateGaussian(
	const Int16 *pSample, Uint16 uFrac)
{
	Uint32 uOffset = ((Uint32)uFrac >> 8) & 0xFFu;
	const Int16 *pForward = _SNSpcDspGaussian + 255 - uOffset;
	const Int16 *pReverse = _SNSpcDspGaussian + uOffset;
	Int32 iOut;

	iOut  = ((Int32)pForward[0]   * (Int32)pSample[-1]) >> 11;
	iOut += ((Int32)pForward[256] * (Int32)pSample[0])  >> 11;
	iOut += ((Int32)pReverse[256] * (Int32)pSample[1])  >> 11;

	/* Exact S-DSP intermediate 16-bit wrap. */
	iOut = (Int16)iOut;

	iOut += ((Int32)pReverse[0] * (Int32)pSample[2]) >> 11;
	iOut = SNSpcDspClamp16(iOut);
	return (Int16)(iOut & ~1);
}

/* Voice output before per-channel volume. This is the value used by PMON and
   represented by OUTX in the hardware pipeline. Revive's envelope is 7-bit. */
/* Legacy Revive main/echo voice mix input. Keep this distinct from
   SNSpcDspVoiceOutput(): the latter applies S-DSP clamp/bit0 behavior for
   PMON/OUTX, while the historical mixer used the raw envelope product. */
static _INLINE Int32 SNSpcDspEnvelopeMixSample(
	Int16 iInterpolatedSample, Uint8 uEnvelope)
{
	return ((Int32)iInterpolatedSample * (Int32)uEnvelope) >> 7;
}

static _INLINE Int16 SNSpcDspVoiceOutput(
	Int16 iInterpolatedSample, Uint8 uEnvelope)
{
	Int32 iSample =
		((Int32)iInterpolatedSample * (Int32)uEnvelope) >> 7;

	iSample = SNSpcDspClamp16(iSample);

	/* The S-DSP voice output clears bit 0 before PMON/OUTX. */
	iSample &= ~1;
	return (Int16)iSample;
}

#endif
