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

	// The first gameplay flip establishes continuity.
	scheduler.AfterFlip(5000, TRUE);
	Check("healthy frame needs no catchup", scheduler.TakeCatchupFrames(), 0);
	scheduler.AfterFlip(7000, TRUE);
	Check("two VBlanks request one hidden frame",
	      scheduler.TakeCatchupFrames(), 1);
	scheduler.AfterFlip(9000, TRUE);
	Check("steady two-VBlank load keeps one hidden frame",
	      scheduler.TakeCatchupFrames(), 1);
	scheduler.AfterFlip(12000, TRUE);
	Check("three VBlanks request two hidden frames",
	      scheduler.TakeCatchupFrames(), 2);
	scheduler.AfterFlip(16000, TRUE);
	Check("four VBlanks reach catchup cap",
	      scheduler.TakeCatchupFrames(), 3);

	// One healthy presentation immediately stops catch-up work.
	scheduler.AfterFlip(17000, TRUE);
	Check("healthy cadence clears catchup", scheduler.TakeCatchupFrames(), 0);

	// A debugger/I/O-sized discontinuity is not treated as video debt.
	scheduler.AfterFlip(23000, TRUE);
	Check("long discontinuity is ignored", scheduler.TakeCatchupFrames(), 0);

	scheduler.CancelRecovery();
	Check("cancel leaves renderer enabled", scheduler.TakeCatchupFrames(), 0);

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
