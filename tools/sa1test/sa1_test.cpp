/*
 * Host-side SA-1 phase-2 regression tests.
 */

#include <stdio.h>
#include <string.h>
#include <vector>

#include "types.h"
#include "snsa1.h"
#include "sntiming.h"

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
	sa1.WriteRegister(0x222A, 0xFF);
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
	sa1.WriteRegister(0x2226, 0x80);
	sa1.WriteRegister(0x2227, 0x80);
	sa1.WriteRegister(0x2228, 0x00);
	sa1.WriteRegister(0x2229, 0xFF);
	sa1.WriteRegister(0x222A, 0xFF);
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


static void TestWriteProtection(void)
{
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	SNSA1 sa1;
	sa1.SetMemory(NULL, 0, bwram, sizeof(bwram));

	// With both write-enable bits clear, BWPA=0 protects the first $100 bytes.
	sa1.WriteRegister(0x2228, 0x00);
	sa1.WriteBWRAMDirect(0x400080, 0x11);
	CHECK(bwram[0x0080] == 0x00, "BWPA must protect first 256 bytes when SWEN/CWEN are clear");
	sa1.WriteBWRAMDirect(0x400100, 0x22);
	CHECK(bwram[0x0100] == 0x22, "BWPA must leave bytes at/above protection boundary writable");

	// Either write-enable bit disables BWPA protection for both bus masters.
	sa1.WriteRegister(0x2226, 0x80);
	sa1.WriteBWRAMDirect(0x400080, 0x33);
	CHECK(bwram[0x0080] == 0x33, "SWEN must allow S-CPU writes inside BWPA");
	sa1.WriteRegister(0x2226, 0x00);
	sa1.WriteRegister(0x2227, 0x80);
	SNCPUWrite8(sa1.GetCpu(), 0x400081, 0x44);
	CHECK(bwram[0x0081] == 0x44, "CWEN must allow SA-1 writes inside BWPA");

	// SIWP/CIWP bits are write enables, not protection bits.
	sa1.WriteRegister(0x2229, 0xFD); // page 1 disabled, page 2 enabled
	sa1.WriteIRAM(0x0100, 0x55);
	CHECK(sa1.ReadIRAM(0x0100) == 0x00, "SIWP clear bit must block selected S-CPU I-RAM page");
	sa1.WriteIRAM(0x0200, 0x66);
	CHECK(sa1.ReadIRAM(0x0200) == 0x66, "SIWP set bit must enable selected S-CPU I-RAM page");

	sa1.WriteRegister(0x222A, 0xFB); // page 2 disabled, page 3 enabled
	SNCPUWrite8(sa1.GetCpu(), 0x000200, 0x77);
	CHECK(sa1.ReadIRAM(0x0200) == 0x66, "CIWP clear bit must block selected SA-1 I-RAM page");
	SNCPUWrite8(sa1.GetCpu(), 0x000300, 0x88);
	CHECK(sa1.ReadIRAM(0x0300) == 0x88, "CIWP set bit must enable selected SA-1 I-RAM page");
}
static void TestBitmapModes(void)
{
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	SNSA1 sa1;
	sa1.SetMemory(NULL, 0, bwram, sizeof(bwram));
	sa1.WriteRegister(0x2227, 0x80);
	sa1.WriteRegister(0x2228, 0x00);
	sa1.WriteRegister(0x2225, 0x80);

	// 4bpp: two virtual pixels share one BW-RAM byte.
	sa1.WriteRegister(0x223F, 0x00);
	SNCPUWrite8(sa1.GetCpu(), 0x006200, 0x0A);
	SNCPUWrite8(sa1.GetCpu(), 0x006201, 0x05);
	CHECK(bwram[0x0100] == 0x5A, "4bpp bitmap must pack two pixels per byte");
	CHECK(SNCPURead8(sa1.GetCpu(), 0x006200) == 0x0A, "4bpp bitmap pixel 0 read");
	CHECK(SNCPURead8(sa1.GetCpu(), 0x006201) == 0x05, "4bpp bitmap pixel 1 read");

	// 2bpp: four virtual pixels share one BW-RAM byte.
	memset(bwram, 0, sizeof(bwram));
	sa1.WriteRegister(0x223F, 0x80);
	SNCPUWrite8(sa1.GetCpu(), 0x006400, 1);
	SNCPUWrite8(sa1.GetCpu(), 0x006401, 2);
	SNCPUWrite8(sa1.GetCpu(), 0x006402, 3);
	SNCPUWrite8(sa1.GetCpu(), 0x006403, 0);
	CHECK(bwram[0x0100] == 0x39, "2bpp bitmap must pack four pixels per byte");
}



static void TestSCPUBridge(void)
{
	SNSA1 sa1;

	sa1.WriteRegister(0x220C, 0x34);
	sa1.WriteRegister(0x220D, 0x12);
	sa1.WriteRegister(0x220E, 0x78);
	sa1.WriteRegister(0x220F, 0x56);
	sa1.WriteRegister(0x2209, 0x50);
	CHECK(sa1.SCPUUseNMIVector(), "$2209 bit 4 must select SA-1 S-CPU NMI vector");
	CHECK(sa1.SCPUUseIRQVector(), "$2209 bit 6 must select SA-1 S-CPU IRQ vector");
	CHECK(sa1.GetSCPUNMIVector() == 0x1234, "$220C/$220D S-CPU NMI vector");
	CHECK(sa1.GetSCPUIRQVector() == 0x5678, "$220E/$220F S-CPU IRQ vector");

	sa1.WriteRegister(0x2201, 0x80);
	sa1.WriteRegister(0x2209, 0x80);
	CHECK(sa1.SCPUIRQPending(), "S-CPU IRQ request+enable must assert bridge");
	sa1.WriteRegister(0x2202, 0x80);
	CHECK(!sa1.SCPUIRQPending(), "$2202 must clear S-CPU IRQ bridge");
}

static void TestCharConvertType2(void)
{
	SNSA1 sa1;
	// CC2, 2bpp, destination I-RAM $0600.
	sa1.WriteRegister(0x2230, 0xA0);
	sa1.WriteRegister(0x2231, 0x02);
	sa1.WriteRegister(0x2235, 0x00);
	sa1.WriteRegister(0x2236, 0x06);
	sa1.WriteRegister(0x2237, 0x00);

	const Uint8 pixels0[8] = {0,1,2,3,0,1,2,3};
	for (int i = 0; i < 8; i++)
		sa1.WriteRegister((Uint16)(0x2240 + i), pixels0[i]);
	CHECK(sa1.ReadIRAM(0x0600) == 0x55, "CC2 plane 0 packing");
	CHECK(sa1.ReadIRAM(0x0601) == 0x33, "CC2 plane 1 packing");

	for (int i = 0; i < 8; i++)
		sa1.WriteRegister((Uint16)(0x2248 + i), 0x03);
	CHECK(sa1.ReadIRAM(0x0602) == 0xFF, "CC2 second line plane 0");
	CHECK(sa1.ReadIRAM(0x0603) == 0xFF, "CC2 second line plane 1");
}

static void TestCharConvertType1(void)
{
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	// One 2bpp tile, every pixel value = 1. Packed source is 01,01,...,
	// so planar output must be plane0=$FF, plane1=$00 on every row.
	for (int y = 0; y < 8; y++)
	{
		bwram[y * 2 + 0] = 0x55;
		bwram[y * 2 + 1] = 0x55;
	}

	SNSA1 sa1;
	sa1.SetMemory(NULL, 0, bwram, sizeof(bwram));
	sa1.WriteRegister(0x2201, 0x20);
	sa1.WriteRegister(0x2230, 0xB0);
	sa1.WriteRegister(0x2231, 0x02);
	sa1.WriteRegister(0x2232, 0x00);
	sa1.WriteRegister(0x2233, 0x00);
	sa1.WriteRegister(0x2234, 0x00);
	sa1.WriteRegister(0x2235, 0x00);
	sa1.WriteRegister(0x2236, 0x01);

	CHECK(sa1.IsCC1Active(), "CC1 must arm when DDA high is written");
	CHECK((sa1.ReadRegister(0x2300) & 0x20) != 0, "CC1 must latch S-CPU CHDMA flag");
	CHECK(sa1.SCPUIRQPending(), "CC1 flag + SIE must assert S-CPU IRQ bridge");
	CHECK(sa1.ReadCC1Byte(0x400000) == 0xFF, "CC1 first planar byte");
	CHECK(sa1.ReadCC1Byte(0x400001) == 0x00, "CC1 second planar byte");
	CHECK(sa1.ReadIRAM(0x0100) == 0xFF && sa1.ReadIRAM(0x0101) == 0x00,
	      "CC1 must buffer converted row in I-RAM");
	CHECK(sa1.ReadIRAM(0x0102) == 0xFF && sa1.ReadIRAM(0x0103) == 0x00,
	      "CC1 must convert all 8 rows");

	sa1.WriteRegister(0x2231, 0x82);
	CHECK(!sa1.IsCC1Active(), "CDMA CHDEND must stop CC1");
}

static void TestNormalDMA(void)
{
	std::vector<Uint8> rom(0x200000, 0x00);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));
	rom[0] = 0x11; rom[1] = 0x22; rom[2] = 0x33; rom[3] = 0x44;
	rom[0x0100] = 0xDB; // reset fixture at $8100: STP

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));
	sa1.WriteRegister(0x2229, 0xFF);
	sa1.WriteRegister(0x222A, 0xFF);
	sa1.WriteRegister(0x2203, 0x00);
	sa1.WriteRegister(0x2204, 0x81);
	sa1.WriteRegister(0x2200, 0x00);
	sa1.StepMasterCycles(16);

	// ROM -> I-RAM, trigger on DDAH ($2236). DMA starts asynchronously and
	// consumes one SA-1 tick per byte for this device pair.
	sa1.WriteRegister(0x2230, 0x80);
	sa1.WriteRegister(0x2232, 0x00);
	sa1.WriteRegister(0x2233, 0x00);
	sa1.WriteRegister(0x2234, 0xC0);
	sa1.WriteRegister(0x2238, 4);
	sa1.WriteRegister(0x2239, 0);
	sa1.WriteRegister(0x2235, 0x00);
	sa1.WriteRegister(0x2236, 0x04);
	CHECK(sa1.IsDMARunning(), "normal DMA must arm instead of completing inside the register write");
	CHECK(sa1.ReadIRAM(0x0400) == 0x00, "normal DMA must not be instantaneous");

	sa1.StepMasterCycles(4); // 2 SA-1 ticks -> 2 bytes
	CHECK(sa1.ReadIRAM(0x0400) == 0x11 &&
	      sa1.ReadIRAM(0x0401) == 0x22 &&
	      sa1.ReadIRAM(0x0402) == 0x00,
	      "ROM->I-RAM DMA must advance according to scheduled ticks");
	CHECK(sa1.ReadRegister(0x2238) == 2,
	      "normal DMA terminal counter must update while transfer is active");

	sa1.StepMasterCycles(4);
	CHECK(sa1.ReadIRAM(0x0403) == 0x44 && !sa1.IsDMARunning(),
	      "ROM->I-RAM DMA must finish after the required ticks");
	CHECK((sa1.ReadRegister(0x2301) & 0x20) != 0,
	      "normal DMA completion must latch SA-1 DMA IRQ status");
	CHECK(sa1.ReadRegister(0x2232) == 0x04 &&
	      sa1.ReadRegister(0x2235) == 0x04,
	      "DMA source/destination registers must expose post-transfer addresses");

	// Clear DMA IRQ before the next transfer.
	sa1.WriteRegister(0x220B, 0x20);

	// BW-RAM -> I-RAM costs two SA-1 ticks per byte.
	bwram[0x0100] = 0xA1; bwram[0x0101] = 0xB2;
	sa1.WriteRegister(0x2230, 0x81);
	sa1.WriteRegister(0x2232, 0x00);
	sa1.WriteRegister(0x2233, 0x01);
	sa1.WriteRegister(0x2234, 0x00);
	sa1.WriteRegister(0x2238, 2);
	sa1.WriteRegister(0x2239, 0);
	sa1.WriteRegister(0x2235, 0x20);
	sa1.WriteRegister(0x2236, 0x04);
	sa1.StepMasterCycles(4); // 2 ticks -> one byte
	CHECK(sa1.ReadIRAM(0x0420) == 0xA1 &&
	      sa1.ReadIRAM(0x0421) == 0x00,
	      "BW-RAM->I-RAM DMA must use the slower BW-RAM timing");
	sa1.StepMasterCycles(4);
	CHECK(sa1.ReadIRAM(0x0421) == 0xB2 && !sa1.IsDMARunning(),
	      "BW-RAM->I-RAM DMA must complete incrementally");

	// I-RAM -> BW-RAM also costs two ticks per byte.
	sa1.WriteIRAM(0x0500, 0x5A);
	sa1.WriteIRAM(0x0501, 0xC3);
	sa1.WriteRegister(0x2230, 0x86);
	sa1.WriteRegister(0x2232, 0x00);
	sa1.WriteRegister(0x2233, 0x05);
	sa1.WriteRegister(0x2234, 0x00);
	sa1.WriteRegister(0x2238, 2);
	sa1.WriteRegister(0x2239, 0);
	sa1.WriteRegister(0x2235, 0x00);
	sa1.WriteRegister(0x2236, 0x03);
	sa1.WriteRegister(0x2237, 0x00);
	sa1.StepMasterCycles(8);
	CHECK(bwram[0x0300] == 0x5A && bwram[0x0301] == 0xC3,
	      "normal DMA must copy I-RAM to BW-RAM on scheduled time");

	// BWRAM -> BWRAM is an invalid device pair: addresses/DTC advance, but
	// memory is untouched and no scheduled DMA stall is created.
	bwram[0x0400] = 0x9A;
	sa1.WriteRegister(0x220B, 0x20);
	sa1.WriteRegister(0x2230, 0x85);
	sa1.WriteRegister(0x2232, 0x00);
	sa1.WriteRegister(0x2233, 0x04);
	sa1.WriteRegister(0x2234, 0x00);
	sa1.WriteRegister(0x2238, 1);
	sa1.WriteRegister(0x2239, 0);
	sa1.WriteRegister(0x2235, 0x10);
	sa1.WriteRegister(0x2236, 0x04);
	sa1.WriteRegister(0x2237, 0x00);
	CHECK(!sa1.IsDMARunning(), "invalid DMA pair must complete without scheduled bus work");
	CHECK(bwram[0x0410] == 0x00, "invalid DMA pair must not write destination memory");
	CHECK(sa1.ReadRegister(0x2232) == 0x01 &&
	      sa1.ReadRegister(0x2235) == 0x11 &&
	      sa1.ReadRegister(0x2238) == 0x00,
	      "invalid DMA pair must still consume DTC and advance DSA/DDA");
}
static void TestArithmetic(void)
{
	SNSA1 sa1;

	// signed -3 * 5 = -15, exposed as a 40-bit result.
	sa1.WriteRegister(0x2250, 0x00);
	sa1.WriteRegister(0x2251, 0xFD);
	sa1.WriteRegister(0x2252, 0xFF);
	sa1.WriteRegister(0x2253, 0x05);
	sa1.WriteRegister(0x2254, 0x00);
	CHECK(sa1.ReadRegister(0x2306) == 0xF1 &&
	      sa1.ReadRegister(0x2307) == 0xFF &&
	      sa1.ReadRegister(0x230A) == 0xFF,
	      "signed multiplication must produce 40-bit two's-complement result");

	// signed dividend / unsigned divisor uses a non-negative remainder:
	// -10 = (-4 * 3) + 2.
	sa1.WriteRegister(0x2250, 0x01);
	sa1.WriteRegister(0x2251, 0xF6);
	sa1.WriteRegister(0x2252, 0xFF);
	sa1.WriteRegister(0x2253, 0x03);
	sa1.WriteRegister(0x2254, 0x00);
	CHECK(sa1.ReadRegister(0x2306) == 0xFC &&
	      sa1.ReadRegister(0x2307) == 0xFF &&
	      sa1.ReadRegister(0x2308) == 0x02 &&
	      sa1.ReadRegister(0x2309) == 0x00,
	      "division must expose floor quotient with non-negative remainder");

	// Negative division uses a non-negative remainder: -11 = (-4 * 3) + 1.
	sa1.WriteRegister(0x2250, 0x01);
	sa1.WriteRegister(0x2251, 0xF5);
	sa1.WriteRegister(0x2252, 0xFF);
	sa1.WriteRegister(0x2253, 0x03);
	sa1.WriteRegister(0x2254, 0x00);
	CHECK(sa1.ReadRegister(0x2306) == 0xFC &&
	      sa1.ReadRegister(0x2307) == 0xFF &&
	      sa1.ReadRegister(0x2308) == 0x01 &&
	      sa1.ReadRegister(0x2309) == 0x00,
	      "division must use a positive remainder for negative dividends");
	CHECK(sa1.ReadRegister(0x2251) == 0 && sa1.ReadRegister(0x2253) == 0,
	      "division must destroy MA and MB");

	// Division by zero produces a zero result and still destroys operands.
	sa1.WriteRegister(0x2251, 0x34); sa1.WriteRegister(0x2252, 0x12);
	sa1.WriteRegister(0x2253, 0x00); sa1.WriteRegister(0x2254, 0x00);
	CHECK(sa1.ReadRegister(0x2306) == 0 && sa1.ReadRegister(0x2309) == 0,
	      "division by zero must return zero");

	// cumulative mode: 2*3 + 4*5 = 26.
	sa1.WriteRegister(0x2250, 0x02);
	sa1.WriteRegister(0x2251, 2); sa1.WriteRegister(0x2252, 0);
	sa1.WriteRegister(0x2253, 3); sa1.WriteRegister(0x2254, 0);
	CHECK(sa1.ReadRegister(0x2253) == 0,
	      "cumulative multiply must destroy MB");
	sa1.WriteRegister(0x2251, 4); sa1.WriteRegister(0x2252, 0);
	sa1.WriteRegister(0x2253, 5); sa1.WriteRegister(0x2254, 0);
	CHECK(sa1.ReadRegister(0x2306) == 26 && sa1.ReadRegister(0x2307) == 0,
	      "cumulative arithmetic must retain the 40-bit sum");
}

static void TestVariableLengthBit(void)
{
	std::vector<Uint8> rom(0x200000, 0x00);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));
	rom[0x0000] = 0x34;
	rom[0x0001] = 0x12;
	rom[0x0002] = 0x78;
	rom[0x0003] = 0x56;

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));
	sa1.WriteRegister(0x2259, 0x00);
	sa1.WriteRegister(0x225A, 0x80);
	sa1.WriteRegister(0x225B, 0x00);
	CHECK(sa1.ReadRegister(0x230C) == 0x34 && sa1.ReadRegister(0x230D) == 0x12,
	      "VBR address writes must expose an unshifted word");

	// Auto mode does not advance until VDPH is read.
	sa1.WriteRegister(0x2258, 0x84);
	CHECK(sa1.ReadRegister(0x230C) == 0x34, "VBR auto mode first low byte is unshifted");
	CHECK(sa1.ReadRegister(0x230D) == 0x12, "VBR auto mode first high byte is unshifted");
	CHECK(sa1.ReadRegister(0x230C) == 0x23 && sa1.ReadRegister(0x230D) == 0x81,
	      "VBR auto mode must advance by four bits after high-port read");

	// Four more bits cross one byte: VA increments by one and bit offset returns to 0.
	CHECK(sa1.ReadRegister(0x230C) == 0x12 && sa1.ReadRegister(0x230D) == 0x78,
	      "VBR auto mode must carry bit position into the byte address");

	// VBR must never expose SA-1 MMIO through $230C/$230D.
	sa1.WriteRegister(0x2259, 0x00);
	sa1.WriteRegister(0x225A, 0x22);
	sa1.WriteRegister(0x225B, 0x00);
	CHECK(sa1.ReadRegister(0x230C) == 0x00,
	      "VBR reads from MMIO space must not recurse into SA-1 registers");
}
static void TestNMI(void)
{
	std::vector<Uint8> rom(0x200000, 0xEA);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	// Reset: WAI ; STP. NMI handler at $8200 writes $88 to I-RAM $12.
	rom[0x0000] = 0xCB;
	rom[0x0001] = 0xDB;
	rom[0x0200] = 0xA9;
	rom[0x0201] = 0x88;
	rom[0x0202] = 0x85;
	rom[0x0203] = 0x12;
	rom[0x0204] = 0xDB;

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));
	sa1.WriteRegister(0x2203, 0x00); sa1.WriteRegister(0x2204, 0x80);
	sa1.WriteRegister(0x2205, 0x00); sa1.WriteRegister(0x2206, 0x82);
	sa1.WriteRegister(0x222A, 0xFF);
	sa1.WriteRegister(0x2200, 0x00);
	sa1.StepMasterCycles(64);
	CHECK((sa1.GetCpu()->uSignal & SNCPU_SIGNAL_WAI) != 0, "NMI fixture must enter WAI");
	sa1.WriteRegister(0x220A, 0x10);
	sa1.WriteRegister(0x2200, 0x10);
	sa1.StepMasterCycles(128);
	CHECK(sa1.ReadIRAM(0x0012) == 0x88, "SA-1 NMI must use $2205/$2206 vector");
}

static void TestTimerIRQ(void)
{
	std::vector<Uint8> rom(0x200000, 0xEA);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	// Reset: CLI ; WAI ; STP. Timer handler at $8100 writes $99 to I-RAM $13.
	rom[0x0000] = 0x58;
	rom[0x0001] = 0xCB;
	rom[0x0002] = 0xDB;
	rom[0x0100] = 0xA9;
	rom[0x0101] = 0x99;
	rom[0x0102] = 0x85;
	rom[0x0103] = 0x13;
	rom[0x0104] = 0xDB;

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));
	sa1.WriteRegister(0x2203, 0x00); sa1.WriteRegister(0x2204, 0x80);
	sa1.WriteRegister(0x2207, 0x00); sa1.WriteRegister(0x2208, 0x81);
	sa1.WriteRegister(0x222A, 0xFF);
	sa1.WriteRegister(0x2200, 0x00);
	sa1.StepMasterCycles(64);
	CHECK((sa1.GetCpu()->uSignal & SNCPU_SIGNAL_WAI) != 0, "timer fixture must enter WAI");

	sa1.WriteRegister(0x220A, 0x40);
	sa1.WriteRegister(0x2212, 20); // H=20 dots -> 80 master clocks
	sa1.WriteRegister(0x2213, 0);
	sa1.WriteRegister(0x2210, 0x01);
	sa1.StepMasterCycles(128);
	CHECK((sa1.ReadRegister(0x2301) & 0x40) != 0, "H timer must latch timer IRQ status");
	CHECK(sa1.ReadIRAM(0x0013) == 0x99, "timer IRQ must enter the SA-1 IRQ vector");
}


static void TestSaveStateRoundTrip(void)
{
	std::vector<Uint8> rom(0x800000, 0xEA);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));

	// Make segment 2 visibly different after MMC reconstruction.
	rom[0x000000] = 0x10;
	rom[0x200000] = 0x32;

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));
	sa1.WriteRegister(0x2220, 0x82);
	sa1.WriteRegister(0x2229, 0xFF);
	sa1.WriteIRAM(0x0123, 0xA5);
	sa1.WriteRegister(0x2203, 0x00);
	sa1.WriteRegister(0x2204, 0x80);
	sa1.WriteRegister(0x222A, 0xFF);
	sa1.WriteRegister(0x2200, 0x00);
	sa1.StepMasterCycles(32);
	sa1.GetCpu()->Regs.rA.w = 0xBEEF;
	sa1.GetCpu()->Regs.rX.w = 0x1234;

	SA1SaveState snap;
	sa1.SaveState(&snap);

	// Destroy live state and mapping before restore.
	sa1.WriteRegister(0x2220, 0x00);
	sa1.WriteIRAM(0x0123, 0x5A);
	sa1.GetCpu()->Regs.rA.w = 0;
	sa1.GetCpu()->Regs.rX.w = 0;

	sa1.RestoreState(&snap);
	CHECK(sa1.ReadIRAM(0x0123) == 0xA5, "save state must restore SA-1 I-RAM");
	CHECK(sa1.GetCpu()->Regs.rA.w == 0xBEEF &&
	      sa1.GetCpu()->Regs.rX.w == 0x1234,
	      "save state must restore independent 65C816 registers");
	CHECK(sa1.GetState()->Registers[0x020] == 0x82,
	      "save state must restore MMC register state");
	CHECK(SNCPURead8(sa1.GetCpu(), 0x008000) == 0x32,
	      "restore must rebuild SA-1 ROM bank pointers from MMC state");
	CHECK(sa1.GetState()->MasterCycles == snap.State.MasterCycles &&
	      sa1.GetState()->ScheduledCycles == snap.State.ScheduledCycles,
	      "save state must restore scheduler counters");
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
	sa1.WriteRegister(0x222A, 0xFF);
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
	sa1.WriteRegister(0x222A, 0xFF);
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

static void TestSA1BusMirrorsAndTimerLatch(void)
{
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));
	SNSA1 sa1;
	sa1.SetMemory(NULL, 0, bwram, sizeof(bwram));
	sa1.WriteRegister(0x222A, 0xFF);

	SNCPUWrite8(sa1.GetCpu(), 0x003123, 0x5A);
	CHECK(SNCPURead8(sa1.GetCpu(), 0x000123) == 0x5A,
	      "SA-1 I-RAM must mirror at $3000-$37FF");
	CHECK(SNCPURead8(sa1.GetCpu(), 0x500000) == 0xFF,
	      "$50-$5F must not decode as direct BW-RAM");

	sa1.WriteRegister(0x2211, 0x00);
	sa1.StepMasterCycles(40);
	CHECK(sa1.ReadRegister(0x2302) == 10, "HCR low read must latch H counter in dots");
	sa1.StepMasterCycles(8);
	CHECK(sa1.ReadRegister(0x2303) == 0, "HCR high must use previous latch");
	CHECK(sa1.ReadRegister(0x2304) == 0, "VCR must share HCR latch event");
	CHECK(sa1.ReadRegister(0x2302) == 12, "new HCR low read must refresh latch");

	sa1.SetVideoRegion(TRUE);
	sa1.WriteRegister(0x2211, 0x00);
	sa1.StepMasterCycles(SNES_CYCLESPERLINE * 262);
	CHECK(sa1.GetState()->VCounter == 262,
	      "PAL SA-1 timer must not wrap at NTSC 262-line boundary");
}

static int g_PoisonExecutorCalls = 0;
static Int32 PoisonGlobalExecutor(SNCpuT *pCpu)
{
	g_PoisonExecutorCalls++;
	pCpu->Cycles = 0;
	return 0;
}

static void TestIndependentCBackend(void)
{
	std::vector<Uint8> rom(0x200000, 0xEA);
	Uint8 bwram[0x8000];
	memset(bwram, 0, sizeof(bwram));
	rom[0x0000] = 0xA9; // LDA #$7B
	rom[0x0001] = 0x7B;
	rom[0x0002] = 0x85; // STA $20
	rom[0x0003] = 0x20;
	rom[0x0004] = 0xDB; // STP

	SNSA1 sa1;
	sa1.SetMemory(&rom[0], (Uint32)rom.size(), bwram, sizeof(bwram));
	sa1.WriteRegister(0x222A, 0xFF);
	sa1.WriteRegister(0x2203, 0x00);
	sa1.WriteRegister(0x2204, 0x80);

	g_PoisonExecutorCalls = 0;
	SNCPUSetExecuteFunc(PoisonGlobalExecutor);
	sa1.WriteRegister(0x2200, 0x00);
	sa1.StepMasterCycles(128);
	SNCPUSetExecuteFunc(SNCPUExecute_C);

	CHECK(g_PoisonExecutorCalls == 0,
	      "SA-1 execution must not use the S-CPU global backend selector");
	CHECK(sa1.ReadIRAM(0x0020) == 0x7B,
	      "SA-1 portable C backend must execute independently");
}

int main(void)
{
	SNCPUSetExecuteFunc(SNCPUExecute_C);

	TestResetDefaults();
	TestResetReleaseAndScheduler();
	TestIRAMAndBWRAM();
	TestWriteProtection();
	TestBitmapModes();
	TestSCPUBridge();
	TestCharConvertType2();
	TestCharConvertType1();
	TestNormalDMA();
	TestArithmetic();
	TestVariableLengthBit();
	TestSaveStateRoundTrip();
	TestMMCMapping();
	TestInstructionExecution();
	TestIndependentCBackend();
	TestIRQVector();
	TestNMI();
	TestTimerIRQ();
	TestSA1BusMirrorsAndTimerLatch();

	if (g_Failures)
	{
		fprintf(stderr, "%d SA-1 test(s) failed\n", g_Failures);
		return 1;
	}
	printf("sa1_test: OK\n");
	return 0;
}
