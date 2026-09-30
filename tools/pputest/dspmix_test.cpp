/*
 * Host-side S-DSP mixer math regression tests.
 */
#include <cstdio>
#include "types.h"
#include "snspcmath.h"

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

	/* Keep PMON/OUTX sample-domain math exactly aligned with the existing
	   Revive linear interpolation path until Gaussian interpolation is moved
	   into the mixer as a separate accuracy/performance change. */
	Check("linear frac zero",
		SNSpcDspInterpolateLinear(1000, 3000, 0), 999);
	Check("linear midpoint",
		SNSpcDspInterpolateLinear(1000, 3000, 0x8000), 1999);
	Check("voice output envelope",
		SNSpcDspVoiceOutput(1000, 3000, 0x8000, 127), 1982);
	Check("voice output clears bit0",
		SNSpcDspVoiceOutput(101, 101, 0, 127) & 1, 0);

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
