/* Check target-clock units and rendered/source cadence independently of FPS
   or wall-clock timing on the host running this test. */
#include <cstdio>
#include "sntargetprofile.h"

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
	const Uint64 second = SNTARGET_COUNT_HZ;
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
	std::puts("targetprofile_test: PASS (clock units, source/render cadence, wrap, wide totals)");
	return 0;
}
