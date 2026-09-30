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

	/* The high half of DSPADDR is readable as a mirror, but writes to
	   DSPDATA ($F3) must be suppressed when DSPADDR.7 is set. */
	Check("DSPADDR $00 writable", SNSpcIO::CanWriteDspPort(0x00), TRUE);
	Check("DSPADDR $7F writable", SNSpcIO::CanWriteDspPort(0x7F), TRUE);
	Check("DSPADDR $80 write ignored", SNSpcIO::CanWriteDspPort(0x80), FALSE);
	Check("DSPADDR $FF write ignored", SNSpcIO::CanWriteDspPort(0xFF), FALSE);

	/* Hardware timer pipeline: stage1 toggles every base period and stage2
	   advances only on the gated 1->0 edge. */
	{
		SNSpcTimerT timer;
		SNSpcTimerReset(&timer, 10);
		SNSpcTimerSetTimer(&timer, 2);
		SNSpcTimerSetEnable(&timer, 0, TRUE);
		SNSpcTimerSync(&timer, 10);
		Check("timer first half has no output",
			SNSpcTimerGetCounter(&timer, 10), 0);
		SNSpcTimerSync(&timer, 20);
		Check("timer first falling edge stage2 only",
			SNSpcTimerGetCounter(&timer, 20), 0);
		SNSpcTimerSync(&timer, 40);
		Check("timer target raises stage3",
			SNSpcTimerGetCounter(&timer, 40), 1);
		Check("timer read clears stage3",
			SNSpcTimerGetCounter(&timer, 40), 0);
	}

	/* TEST global disable can itself create the stage1 falling edge. */
	{
		SNSpcTimerT timer;
		SNSpcTimerReset(&timer, 10);
		SNSpcTimerSetTimer(&timer, 1);
		SNSpcTimerSetEnable(&timer, 0, TRUE);
		SNSpcTimerSync(&timer, 10); /* stage1/line high */
		SNSpcTimerSetGlobalGate(&timer, 10, TRUE, TRUE);
		Check("timer TEST disable falling edge",
			SNSpcTimerGetCounter(&timer, 10), 1);
	}

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
