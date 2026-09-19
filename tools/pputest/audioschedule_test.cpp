/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Exercises audioschedule test behavior in the pputest regression suite.
 */

#include <cstdio>

#include "types.h"
#include "audframeschedule.h"

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
	Uint32 uPhase = 0;
	Int32 nTotal = 0;
	Int32 nMin = 99999;
	Int32 nMax = 0;
	Int32 i;

	Check("32k frame 1", AudFrameScheduleNext(&uPhase, 32000, 60, 4), 532);
	Check("32k frame 2", AudFrameScheduleNext(&uPhase, 32000, 60, 4), 532);
	Check("32k frame 3", AudFrameScheduleNext(&uPhase, 32000, 60, 4), 536);
	Check("32k phase cycle", uPhase, 0);

	uPhase = 0;
	for (i = 0; i < 60; i++)
	{
		Int32 n = AudFrameScheduleNext(&uPhase, 32000, 60, 4);
		nTotal += n;
		if (n < nMin) nMin = n;
		if (n > nMax) nMax = n;
	}
	Check("32k samples per second", nTotal, 32000);
	Check("32k minimum", nMin, 532);
	Check("32k maximum", nMax, 536);
	Check("32k final phase", uPhase, 0);

	/* PAL must still produce the same 32 kHz DSP stream over real time:
	   32000 / 50 = exactly 640 samples per emulated frame. */
	uPhase = 0;
	nTotal = 0;
	nMin = 99999;
	nMax = 0;
	for (i = 0; i < 50; i++)
	{
		Int32 n = AudFrameScheduleNext(&uPhase, 32000, 50, 4);
		nTotal += n;
		if (n < nMin) nMin = n;
		if (n > nMax) nMax = n;
	}
	Check("PAL 32k samples per second", nTotal, 32000);
	Check("PAL 32k minimum", nMin, 640);
	Check("PAL 32k maximum", nMax, 640);
	Check("PAL 32k final phase", uPhase, 0);

	/* Hardware-accurate cadence from MesenCE. Rounded 60/50 Hz drifts against
	   the SNES master clock and eventually forces an audio correction. */
	{
		Uint64 uExactPhase = 0;
		nTotal = 0;
		for (i = 0; i < 60; i++)
			nTotal += AudFrameScheduleNextRatio(
				&uExactPhase, 32000, 21477272u, 357366u, 4);
		Check("NTSC exact cadence 60 frames", nTotal, 31944);

		uExactPhase = 0;
		nTotal = 0;
		for (i = 0; i < 50; i++)
			nTotal += AudFrameScheduleNextRatio(
				&uExactPhase, 32000, 21281370u, 425568u, 4);
		Check("PAL exact cadence 50 frames", nTotal, 31992);
	}

	uPhase = 0;
	nTotal = 0;
	for (i = 0; i < 60; i++)
		nTotal += AudFrameScheduleNext(&uPhase, 48000, 60, 4);
	Check("48k samples per second", nTotal, 48000);
	Check("48k exact frame", AudFrameScheduleNext(&uPhase, 48000, 60, 4), 800);

	Check("invalid rate", AudFrameScheduleNext(&uPhase, 0, 60, 4), 0);
	Check("invalid phase", AudFrameScheduleNext(NULL, 32000, 60, 4), 0);

	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
