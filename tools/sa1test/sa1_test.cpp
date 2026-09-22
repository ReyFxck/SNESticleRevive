/*
 * Host-side SA-1 phase-1 regression tests.
 */

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "snsa1.h"

static int g_Failures = 0;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", (m), __FILE__, __LINE__); g_Failures++; } } while (0)

static void TestResetDefaults(void)
{
	SNSA1 sa1;
	CHECK(sa1.ReadRegister(0x2200) == 0x20, "$2200 reset default");
	CHECK(sa1.ReadRegister(0x2220) == 0x00, "$2220 bank C default");
	CHECK(sa1.ReadRegister(0x2221) == 0x01, "$2221 bank D default");
	CHECK(sa1.ReadRegister(0x2222) == 0x02, "$2222 bank E default");
	CHECK(sa1.ReadRegister(0x2223) == 0x03, "$2223 bank F default");
	CHECK(sa1.ReadRegister(0x2228) == 0x0F, "$2228 protection default");
	CHECK(sa1.ReadRegister(0x230E) == 0x23, "$230E version code");
	CHECK(!sa1.IsRunning(), "SA-1 held in reset");
}

static void TestResetReleaseAndScheduler(void)
{
	SNSA1 sa1;
	sa1.WriteRegister(0x2203, 0x34);
	sa1.WriteRegister(0x2204, 0x12);
	sa1.StepMasterCycles(1364);
	CHECK(sa1.GetLastSliceCycles() == 0, "reset blocks scheduler credits");
	sa1.WriteRegister(0x2200, 0x00);
	CHECK(sa1.IsRunning(), "reset release starts scheduler");
	CHECK(sa1.GetResetVector() == 0x1234, "reset vector");
	CHECK(sa1.GetState()->LastResetVector == 0x1234, "latched reset vector");
	CHECK(sa1.GetState()->ResetEpoch == 1, "reset epoch");
	sa1.StepMasterCycles(1364);
	CHECK(sa1.GetLastSliceCycles() == 682, "master/2 scheduler ratio");
	sa1.StepMasterCycles(1);
	CHECK(sa1.GetLastSliceCycles() == 0, "odd master carry");
	sa1.StepMasterCycles(1);
	CHECK(sa1.GetLastSliceCycles() == 1, "carried master clock");
}

static void TestIRAM(void)
{
	SNSA1 sa1;
	sa1.WriteIRAM(0x0123, 0xA5);
	CHECK(sa1.ReadIRAM(0x0123) == 0xA5, "I-RAM round trip");
	sa1.WriteIRAM(0x0923, 0x5A);
	CHECK(sa1.ReadIRAM(0x0123) == 0x5A, "I-RAM 2 KiB mirror");
}

static void TestBWRAM(void)
{
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));
	SNSA1 sa1;
	sa1.SetMemory(NULL, 0, bwram, sizeof(bwram));
	sa1.WriteRegister(0x2224, 0x03);
	sa1.WriteBWRAMWindow(0x6123, 0xCC);
	CHECK(bwram[0x6123] == 0xCC, "$2224 BW-RAM window");
	CHECK(sa1.ReadBWRAMWindow(0x6123) == 0xCC, "BW-RAM window read");
	sa1.WriteBWRAMDirect(0x430010, 0x77);
	CHECK(bwram[0x0010] == 0x77, "direct BW-RAM mirroring");
	CHECK(sa1.ReadBWRAMDirect(0x430010) == 0x77, "direct BW-RAM read");
}

static void TestInterruptFlags(void)
{
	SNSA1 sa1;
	sa1.WriteRegister(0x2200, 0x80);
	CHECK((sa1.ReadRegister(0x2301) & 0x80) != 0, "CCNT IRQ status");
	sa1.WriteRegister(0x220B, 0x80);
	CHECK((sa1.ReadRegister(0x2301) & 0x80) == 0, "SA-1 IRQ clear");
	sa1.WriteRegister(0x2209, 0x80);
	CHECK((sa1.ReadRegister(0x2300) & 0x80) != 0, "SCNT IRQ status");
	sa1.WriteRegister(0x2202, 0x80);
	CHECK((sa1.ReadRegister(0x2300) & 0x80) == 0, "S-CPU IRQ clear");
}

int main(void)
{
	TestResetDefaults();
	TestResetReleaseAndScheduler();
	TestIRAM();
	TestBWRAM();
	TestInterruptFlags();
	if (g_Failures) { fprintf(stderr, "%d SA-1 test(s) failed\n", g_Failures); return 1; }
	printf("sa1_test: OK\n");
	return 0;
}
