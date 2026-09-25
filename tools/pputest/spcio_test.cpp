/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Exercises CPU-to-SPC port publication ordering.
 */

#include <cstdio>

#include "types.h"
#include "snspcio.h"

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
	SNSpcIO io;

	io.Reset();
	Check("reset port 2", io.m_Regs.apu_w[2], 0);

	/* A late-phase write must already be visible because the caller catches
	   the SPC up before publishing the latch. */
	io.WriteCpuPort(20, 0, 2, 0x7F);
	Check("late-phase write is immediate", io.m_Regs.apu_w[2], 0x7F);
	io.SyncCpuPorts(1000);
	Check("sync cannot overwrite immediate write", io.m_Regs.apu_w[2], 0x7F);

	io.WriteCpuPort(41, 21, 2, 0x00);
	Check("back-to-back acknowledgement", io.m_Regs.apu_w[2], 0x00);
	io.SyncCpuPorts(2000);
	Check("acknowledgement remains visible", io.m_Regs.apu_w[2], 0x00);

	io.WriteCpuPort(0, 0, 0, 0x12);
	io.WriteCpuPort(0, 0, 1, 0x34);
	io.WriteCpuPort(0, 0, 2, 0x56);
	io.ClearCpuPorts(0x03);
	Check("clear selected port 0", io.m_Regs.apu_w[0], 0x00);
	Check("clear selected port 1", io.m_Regs.apu_w[1], 0x00);
	Check("preserve unselected port 2", io.m_Regs.apu_w[2], 0x56);

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
