/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Exercises diag test behavior in the pputest regression suite.
 */

#include <cstdio>
#include <cstring>

#include "types.h"
#include "sndbglog.h"

static int g_Failures;

static void Check(const char *pName, Uint32 uGot, Uint32 uExpected)
{
	if (uGot != uExpected)
	{
		std::printf("FAIL %s: %u != %u\n", pName,
			(unsigned)uGot, (unsigned)uExpected);
		g_Failures++;
	}
}

int main()
{
	const Uint32 uNtscBudget = SnesDbgFrameBudget(60u);
	const Uint32 uPalBudget = SnesDbgFrameBudget(50u);
	const Uint32 uNtscLimit =
		(Uint32)(((Uint64)uNtscBudget * SNDBG_SLOW_PERCENT) / 100u);

	Check("schema", std::strcmp(SNDBG_SCHEMA, "snesdiag-v1") == 0, TRUE);
	Check("rolling window", SNDBG_FRAME_PERIOD, 120u);
	Check("NTSC budget", uNtscBudget, 2457600u);
	Check("PAL budget", uPalBudget, 2949120u);
	Check("zero Hz fallback", SnesDbgFrameBudget(0), uNtscBudget);
	Check("at 105 percent is not slow",
		SnesDbgFrameIsSlow(uNtscLimit, uNtscBudget), FALSE);
	Check("over 105 percent is slow",
		SnesDbgFrameIsSlow(uNtscLimit + 1u, uNtscBudget), TRUE);
	Check("capture flags do not overlap",
		SNDBG_CAPTURE_MANUAL | SNDBG_CAPTURE_SLOW_FRAME |
		SNDBG_CAPTURE_PPU_QUEUE | SNDBG_CAPTURE_DMA_WRAP |
		SNDBG_CAPTURE_OBJ | SNDBG_CAPTURE_FRAMESKIP |
		SNDBG_CAPTURE_AUDIO | SNDBG_CAPTURE_CHIP, 0xFFu);

	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
