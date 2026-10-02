/* Check target-clock units and rendered/source cadence independently of FPS
   or wall-clock timing on the host running this test. */
#include <cstdio>
#include "types.h"
/* A controlled Count source proves that Off never samples the timer. */
#define _PROFCTR_H
#define SNES_TARGET_PROFILE 1
static Uint32 countReads, fakeCount;
static Uint32 ProfCtrGetCycle() { countReads++; return fakeCount; }
#include "sntargetprofile.h"
SnesTargetProfileFrameT g_SnesTargetProfile = {};
Bool g_SnesTargetProfileEnabled = FALSE;

static int failures;
static void Check(const char *name, Uint32 got, Uint32 expected)
{
	if (got != expected)
	{
		std::printf("FAIL %s: %u != %u\n", name, got, expected);
		failures++;
	}
}

int main()
{
	for (int i = 0; i < 10000; i++)
	{
		SNTARGET_BEGIN(off);
		SNTARGET_END(CPU, off);
	}
	Check("disabled timer reads", countReads, 0);
	Check("disabled accumulation", g_SnesTargetProfile.CPU, 0);
	SnesTargetProfileSetEnabled(TRUE);
	fakeCount = 0xfffffff0u;
	SNTARGET_BEGIN(on);
	fakeCount = 0x20;
	SNTARGET_END(CPU, on);
	Check("enabled timer reads", countReads, 2);
	Check("enabled wrap delta", g_SnesTargetProfile.CPU, 0x30);
	SnesTargetProfileSetEnabled(FALSE);
	SNTARGET_BEGIN(offAgain);
	SNTARGET_END(CPU, offAgain);
	Check("disabled again", countReads, 2);
	const Uint64 second = SNTARGET_COUNT_HZ;
	/* Raw Count fixtures must not be derived from the implementation's
	   clock constant: that would let a bus-clock mixup pass again. */
	Check("raw 60 Hz Count cadence", SnesTargetProfileRate10(60, 294912000u), 600);
	Check("raw 30 Hz Count cadence", SnesTargetProfileRate10(30, 294912000u), 300);
	Check("raw one VBlank", SnesTargetProfileMillis10(4915200u, 1), 166);
	Check("raw 20 ms work", SnesTargetProfileMillis10(5898240u, 1), 200);
	Check("NTSC source", SnesTargetProfileRate10(120, second * 2), 600);
	Check("PAL source with NTSC presentation", SnesTargetProfileRate10(100, second * 2), 500);
	Check("30 rendered / 60 executed", SnesTargetProfileRate10(60, second * 2), 300);
	Check("42 executed", SnesTargetProfileRate10(84, second * 2), 420);
	Check("no executed frame", SnesTargetProfileRate10(0, second), 0);
	Check("no time", SnesTargetProfileRate10(60, 0), 0);
	Check("one frame budget", SnesTargetProfileMillis10(second, 60), 166);
	Check("30 FPS frame budget", SnesTargetProfileMillis10(second, 30), 333);
	Check("one millisecond", SnesTargetProfileMillis10(second * 60 / 1000, 60), 10);
	Check("no iterations", SnesTargetProfileMillis10(second, 0), 0);
	/* EE Count wraps; deltas are unsigned before being widened to totals. */
	Uint32 start = 0xF0000000u;
	Uint32 end = start + (Uint32)(second * 2);
	Check("count wrap cadence", SnesTargetProfileRate10(120, (Uint32)(end - start)), 600);
	Check("window total wider than 32 bits", SnesTargetProfileMillis10(second * 60, 60), 10000);
	if (failures) return 1;
	std::puts("targetprofile_test: PASS (runtime off/on, clock units, cadence, wrap, wide totals)");
	return 0;
}
