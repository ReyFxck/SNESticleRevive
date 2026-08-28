#include <cstdio>

#include "types.h"
#include "mainloop_safe_frameskip.h"

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
	MainLoopSafeFrameskipScheduler scheduler;

	// Four menu flips calibrate a stable 1000-cycle host period.
	scheduler.AfterFlip(1000, FALSE);
	scheduler.AfterFlip(2000, FALSE);
	scheduler.AfterFlip(3000, FALSE);
	scheduler.AfterFlip(4000, FALSE);
	Check("calibrated period", scheduler.GetPeriod(), 1000);

	// Two gameplay flips establish continuity; 1.6 periods misses VBlank.
	scheduler.AfterFlip(5000, TRUE);
	Check("healthy frame renders", scheduler.Take(), FALSE);
	scheduler.AfterFlip(6600, TRUE);
	Check("miss requests one skip", scheduler.Take(), TRUE);
	Check("skip forces following render", scheduler.Take(), FALSE);

	// Another miss can request recovery only after that real rendered frame.
	scheduler.AfterFlip(8200, TRUE);
	Check("second miss can recover", scheduler.Take(), TRUE);
	scheduler.AfterFlip(9200, TRUE);
	Check("healthy cadence clears debt", scheduler.Take(), FALSE);

	// A debugger/I/O-sized discontinuity is not treated as video debt.
	scheduler.AfterFlip(15000, TRUE);
	Check("long discontinuity is ignored", scheduler.Take(), FALSE);

	scheduler.CancelRecovery();
	Check("cancel leaves renderer enabled", scheduler.Take(), FALSE);

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
