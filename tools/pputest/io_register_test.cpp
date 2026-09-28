/*
 * Host-side coverage for the base 5A22 I/O and DMA register images.
 */

#include <cstdio>

#include "types.h"
#include "sndma.h"
#include "snio.h"

static int g_Failures;

static void Check(const char *pName, unsigned uGot, unsigned uExpected)
{
	if (uGot != uExpected)
	{
		std::printf("FAIL %s: %04X != %04X\n", pName, uGot, uExpected);
		g_Failures++;
	}
}

static void CheckPowerOnRegisters()
{
	SnesIO io;

	Check("RDNMI version", io.m_Regs.rdnmi, 0x02);
	Check("WRIO power-on", io.m_Regs.wrio, 0xFF);
	Check("HTIME power-on", io.m_Regs.htime.w, 0x01FF);
	Check("VTIME power-on", io.m_Regs.vtime.w, 0x01FF);

	io.m_Regs.wrio = 0;
	io.m_Regs.htime.w = 0;
	io.m_Regs.vtime.w = 0;
	io.Reset();
	Check("WRIO reset", io.m_Regs.wrio, 0xFF);
	Check("HTIME reset", io.m_Regs.htime.w, 0x01FF);
	Check("VTIME reset", io.m_Regs.vtime.w, 0x01FF);
}

static void CheckDMARegisterMap()
{
	SnesDMAC dma;
	unsigned uChannel;

	dma.Reset();
	for (uChannel = 0; uChannel < SNESDMAC_CHANNEL_NUM; uChannel++)
	{
		unsigned uOffset;
		for (uOffset = 0; uOffset <= 0x0A; uOffset++)
			Check("DMA power-on register", dma.Read8(uChannel, uOffset, 0x43),
			      0xFF);
		Check("DMA $43xB power-on", dma.Read8(uChannel, 0x0B, 0x43), 0xFF);
		Check("DMA $43xF mirror", dma.Read8(uChannel, 0x0F, 0x43), 0xFF);
		Check("DMA $43xC open bus", dma.Read8(uChannel, 0x0C, 0x43), 0x43);
		Check("DMA $43xD open bus", dma.Read8(uChannel, 0x0D, 0x43), 0x43);
		Check("DMA $43xE open bus", dma.Read8(uChannel, 0x0E, 0x43), 0x43);

		for (uOffset = 0; uOffset <= 0x0B; uOffset++)
			dma.Write8(uChannel, uOffset,
			           (Uint8)(0x10u * uChannel + uOffset));
		for (uOffset = 0; uOffset <= 0x0B; uOffset++)
			Check("DMA register round trip", dma.Read8(uChannel, uOffset, 0x43),
			      0x10u * uChannel + uOffset);
		Check("DMA $43xB/$43xF storage mirror",
		      dma.Read8(uChannel, 0x0F, 0x43), 0x10u * uChannel + 0x0B);
	}
}

int main()
{
	CheckPowerOnRegisters();
	CheckDMARegisterMap();

	if (g_Failures)
	{
		std::printf("%d I/O register test(s) failed\n", g_Failures);
		return 1;
	}

	std::puts("I/O register tests passed");
	return 0;
}
