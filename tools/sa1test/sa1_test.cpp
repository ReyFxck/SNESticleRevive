/*
 * Host-side SA-1 phase-2 regression tests.
 */

#include <stdio.h>
#include <string.h>
#include <vector>

#include "types.h"
#include "snsa1.h"

extern "C" {
#include "sncpu_c.h"
}

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
	CHECK(sa1.GetCpu()->Regs.rS.w == 0x01FF, "independent CPU reset stack");
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
	CHECK(sa1.GetCpu()->Regs.rPC == 0x1234, "SA-1 PC must start at $2203/$2204");
	CHECK(sa1.GetState()->LastResetVector == 0x1234, "latched reset vector");
	CHECK(sa1.GetState()->ResetEpoch == 1, "reset epoch");
}

static void TestIRAMAndBWRAM(void)
{
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	SNSA1 sa1;
	sa1.SetMemory(NULL, 0, bwram, sizeof(bwram));
	sa1.WriteIRAM(0x0123, 0xA5);
	CHECK(sa1.ReadIRAM(0x0123) == 0xA5, "I-RAM round trip");
	sa1.WriteIRAM(0x0923, 0x5A);
	CHECK(sa1.ReadIRAM(0x0123) == 0x5A, "I-RAM 2 KiB mirror");

	sa1.WriteRegister(0x2224, 0x03);
	sa1.WriteBWRAMWindow(0x6123, 0xCC);
	CHECK(bwram[0x6123] == 0xCC, "$2224 S-CPU BW-RAM window");

	sa1.WriteRegister(0x2225, 0x02);
	SNCPUWrite8(sa1.GetCpu(), 0x006055, 0x9A);
	CHECK(bwram[0x4055] == 0x9A, "$2225 SA-1 CPU BW-RAM window");
	CHECK(SNCPURead8(sa1.GetCpu(), 0x006055) == 0x9A, "SA-1 CPU BW-RAM read");

	sa1.WriteBWRAMDirect(0x430010, 0x77);
	CHECK(bwram[0x0010] == 0x77, "direct BW-RAM mirroring");
}

static void TestMMCMapping(void)
{
	std::vector<Uint8> rom(0x800000, 0xFF);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	for (Uint32 seg = 0; seg < 8; seg++)
	{
		rom[seg * 0x100000] = (Uint8)(0x10 + seg);
		rom[seg * 0x100000 + 0x8000] = (Uint8)(0x40 + seg);
	}

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));

	CHECK(SNCPURead8(sa1.GetCpu(), 0xC00000) == 0x10,
	      "default $2220 maps segment 0 into bank C0");
	CHECK(SNCPURead8(sa1.GetCpu(), 0xD00000) == 0x11,
	      "default $2221 maps segment 1 into bank D0");
	CHECK(SNCPURead8(sa1.GetCpu(), 0x008000) == 0x10,
	      "default bank 00 LoROM window uses segment 0");

	// Without bit 7, the full C-bank map changes but the LoROM window keeps
	// its group-default segment.
	sa1.WriteRegister(0x2220, 0x02);
	CHECK(SNCPURead8(sa1.GetCpu(), 0xC00000) == 0x12,
	      "$2220 must remap C0-CF full-bank view");
	CHECK(SNCPURead8(sa1.GetCpu(), 0x008000) == 0x10,
	      "LoROM window keeps group segment while MMC bit 7 is clear");

	// Bit 7 overrides the LoROM group too.
	sa1.WriteRegister(0x2220, 0x82);
	CHECK(SNCPURead8(sa1.GetCpu(), 0x008000) == 0x12,
	      "MMC bit 7 must remap the matching LoROM window");
}

static void TestInstructionExecution(void)
{
	std::vector<Uint8> rom(0x200000, 0xEA);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	// $00:8000: LDA #$42 ; STA $10 ; STP
	rom[0x0000] = 0xA9;
	rom[0x0001] = 0x42;
	rom[0x0002] = 0x85;
	rom[0x0003] = 0x10;
	rom[0x0004] = 0xDB;

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));
	sa1.WriteRegister(0x2203, 0x00);
	sa1.WriteRegister(0x2204, 0x80);
	sa1.WriteRegister(0x2200, 0x00);
	sa1.StepMasterCycles(256);

	CHECK(sa1.ReadIRAM(0x0010) == 0x42,
	      "independent SA-1 65C816 must execute ROM and write I-RAM");
	CHECK((sa1.GetCpu()->uSignal & SNCPU_SIGNAL_STP) != 0,
	      "SA-1 test program must reach STP");
	CHECK((sa1.GetCpu()->Regs.rPC & 0xFFFF) == 0x8005,
	      "SA-1 PC must advance independently through its program");
	CHECK(sa1.GetState()->ExecutionSlices != 0,
	      "scheduler must execute at least one SA-1 slice");
}

static void TestIRQVector(void)
{
	std::vector<Uint8> rom(0x200000, 0xEA);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	// Reset: CLI ; WAI ; STP
	rom[0x0000] = 0x58;
	rom[0x0001] = 0xCB;
	rom[0x0002] = 0xDB;
	// IRQ handler at $8100: LDA #$66 ; STA $11 ; STP
	rom[0x0100] = 0xA9;
	rom[0x0101] = 0x66;
	rom[0x0102] = 0x85;
	rom[0x0103] = 0x11;
	rom[0x0104] = 0xDB;

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));
	sa1.WriteRegister(0x2203, 0x00);
	sa1.WriteRegister(0x2204, 0x80);
	sa1.WriteRegister(0x2207, 0x00);
	sa1.WriteRegister(0x2208, 0x81);
	sa1.WriteRegister(0x2200, 0x00);
	sa1.StepMasterCycles(64);
	CHECK((sa1.GetCpu()->uSignal & SNCPU_SIGNAL_WAI) != 0,
	      "SA-1 program must enter WAI before IRQ");

	// Enable SA-1 IRQ then request it through CCNT.
	sa1.WriteRegister(0x220A, 0x80);
	sa1.WriteRegister(0x2200, 0x80);
	sa1.StepMasterCycles(256);
	CHECK(sa1.ReadIRAM(0x0011) == 0x66,
	      "SA-1 IRQ must use vector from $2207/$2208");
}

int main(void)
{
	SNCPUSetExecuteFunc(SNCPUExecute_C);

	TestResetDefaults();
	TestResetReleaseAndScheduler();
	TestIRAMAndBWRAM();
	TestMMCMapping();
	TestInstructionExecution();
	TestIRQVector();

	if (g_Failures)
	{
		fprintf(stderr, "%d SA-1 test(s) failed\n", g_Failures);
		return 1;
	}
	printf("sa1_test: OK\n");
	return 0;
}
