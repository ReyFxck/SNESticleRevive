/*
 * Host-side S-DSP mixer math regression tests.
 */
#include <cstdio>
#include "types.h"
#include "snspcmath.h"
#include "snspcbrr.h"

static int g_Failures;

static void Check(const char *pName, int nGot, int nExpected)
{
	if (nGot != nExpected)
	{
		std::printf("FAIL %s: %d != %d\n", pName, nGot, nExpected);
		g_Failures++;
	}
}

int main()
{
	/* Ares/higan S-DSP model:
	   pitch += (previous_output >> 5) * pitch >> 10 */
	Check("PMON zero", SNSpcDspApplyPitchMod(0x2000, 0), 0x2000);
	Check("PMON positive half", SNSpcDspApplyPitchMod(0x2000, 0x4000), 0x3000);
	Check("PMON negative half", SNSpcDspApplyPitchMod(0x2000, -0x4000), 0x1000);
	Check("PMON full negative", SNSpcDspApplyPitchMod(0x3FFF, -0x8000), 0);
	Check("PMON masks base pitch", SNSpcDspApplyPitchMod(0xFFFF, 0), 0x3FFF);

	/* Four-tap hardware Gaussian interpolation. The pointer passed to the
	   helper is the current sample; [-1], [0], [1], [2] are the tap window. */
	{
		Int16 samples[4] = { -1000, 2000, -3000, 4000 };
		const Int16 *p = &samples[1];

		Check("gaussian phase 0",
			SNSpcDspInterpolateGaussian(p, 0x0000), 544);
		Check("gaussian phase 1/4",
			SNSpcDspInterpolateGaussian(p, 0x4000), 152);
		Check("gaussian phase 1/2",
			SNSpcDspInterpolateGaussian(p, 0x8000), -394);
		Check("gaussian phase 3/4",
			SNSpcDspInterpolateGaussian(p, 0xC000), -812);
	}

	/* Saturation and the S-DSP's forced-even output bit are observable at
	   the interpolation stage, before envelope and per-voice volume. */
	{
		Int16 positive[4] = { 32767, 32767, 32767, 32767 };
		Int16 negative[4] = { -32768, -32768, -32768, -32768 };
		Check("gaussian positive clamp",
			SNSpcDspInterpolateGaussian(&positive[1], 0xFFFF), 32766);
		Check("gaussian negative clamp",
			SNSpcDspInterpolateGaussian(&negative[1], 0xFFFF), -32768);
	}

	Check("voice output full 11-bit envelope",
		SNSpcDspVoiceOutput(2000, 0x7F0), 1984);
	Check("voice output clears bit0",
		SNSpcDspVoiceOutput(103, 0x7FF) & 1, 0);
	Check("voice output half envelope",
		SNSpcDspVoiceOutput(1000, 0x400), 500);

	Check("DSP counter wraps then decrements",
		SNSpcDspCounterTick(0), 30719);
	Check("DSP counter decrements",
		SNSpcDspCounterTick(30719), 30718);
	Check("DSP rate0 never polls",
		SNSpcDspCounterPoll(0, 0), FALSE);
	Check("DSP rate31 polls every sample",
		SNSpcDspCounterPoll(12345, 31), TRUE);
	Check("DSP rate30 even phase",
		SNSpcDspCounterPoll(30718, 30), TRUE);
	Check("DSP rate30 odd phase",
		SNSpcDspCounterPoll(30719, 30), FALSE);

	/* BRR scale 13-15 has special hardware behavior: negative nybbles
	   become -2048 before the final x2 write, positive nybbles become zero. */
	{
		Uint8 block[9] = { 0xD0, 0x87, 0, 0, 0, 0, 0, 0, 0 };
		Int16 out[16] = {};
		SNSpcBRRDecode(block, out, 0, 0);
		Check("BRR invalid range negative", out[0], -4096);
		Check("BRR invalid range positive", out[1], 0);
	}
	{
		Uint8 block[9] = { 0x00, 0x8F, 0, 0, 0, 0, 0, 0, 0 };
		Int16 out[16] = {};
		SNSpcBRRDecode(block, out, 0, 0);
		Check("BRR scale0 negative eight", out[0], -8);
		Check("BRR scale0 negative one", out[1], -2);
	}

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
