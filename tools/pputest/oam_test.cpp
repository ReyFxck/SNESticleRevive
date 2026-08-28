#include <cstdio>
#include <cstring>

#include "types.h"
#include "sndma.h"
#include "snppu.h"

class TestRender : public ISnesPPURender
{
public:
	Uint32 uSingleCalls;
	Uint32 uRangeCalls;
	Uint32 uFirstAddress;
	Uint32 uLastAddress;
	Uint32 nLastWords;
	Uint32 uCGRAMCalls;
	Uint32 uLastCGRAMAddress;
	Uint16 uLastCGRAMData;

	TestRender()
	{
		m_UpdateFlags = 0;
		m_pPPU = NULL;
		ClearStats();
	}

	void BeginRender(CRenderSurface *) {}
	void EndRender() {}
	void RenderLine(Int32) {}
	void UpdateVRAM(Uint32 uAddress)
	{
		if (!uSingleCalls) uFirstAddress = uAddress;
		uLastAddress = uAddress;
		uSingleCalls++;
	}
	void UpdateVRAMRange(Uint32 uAddress, Uint32 nWords)
	{
		uFirstAddress = uAddress;
		uLastAddress = uAddress;
		nLastWords = nWords;
		uRangeCalls++;
	}
	void UpdateCGRAM(Uint32 uAddress, Uint16 uData)
	{
		uCGRAMCalls++;
		uLastCGRAMAddress = uAddress;
		uLastCGRAMData = uData;
	}
	void ClearStats()
	{
		uSingleCalls = 0;
		uRangeCalls = 0;
		uFirstAddress = 0;
		uLastAddress = 0;
		nLastWords = 0;
		uCGRAMCalls = 0;
		uLastCGRAMAddress = 0;
		uLastCGRAMData = 0;
	}
};

static int g_Failures;

static void Check(const char *pName, int nGot, int nExpected)
{
	if (nGot != nExpected)
	{
		std::printf("FAIL %s: %d != %d\n", pName, nGot, nExpected);
		g_Failures++;
	}
}

static void CheckVRAMBlockEquivalence()
{
	static const Uint8 vmainValues[] =
	{
		0x00, 0x01, 0x02, 0x04, 0x08, 0x0C,
		0x80, 0x81, 0x82, 0x84, 0x88, 0x8C
	};
	static const Uint16 startValues[] =
	{
		0x0000, 0x001F, 0x00FF, 0x1234, 0x7FFE, 0x7FFF
	};
	Uint8 data[517];
	Uint32 seed = 0x65816;
	int i, j, n;

	for (i = 0; i < (int)sizeof(data); i++)
	{
		seed = seed * 1664525u + 1013904223u;
		data[i] = (Uint8)(seed >> 24);
	}

	for (i = 0; i < (int)(sizeof(vmainValues) / sizeof(vmainValues[0])); i++)
	for (j = 0; j < (int)(sizeof(startValues) / sizeof(startValues[0])); j++)
	for (n = 1; n <= 517; n += (n < 17 ? 1 : 37))
	{
		SnesPPU reference;
		SnesPPU candidate;
		TestRender referenceRender;
		TestRender candidateRender;
		int k;

		reference.SetPPURender(&referenceRender);
		referenceRender.SetPPU(&reference);
		candidate.SetPPURender(&candidateRender);
		candidateRender.SetPPU(&candidate);
		reference.Reset();
		candidate.Reset();

		reference.Write8(0x2115, vmainValues[i]);
		candidate.Write8(0x2115, vmainValues[i]);
		reference.Write8(0x2116, (Uint8)startValues[j]);
		candidate.Write8(0x2116, (Uint8)startValues[j]);
		reference.Write8(0x2117, (Uint8)(startValues[j] >> 8));
		candidate.Write8(0x2117, (Uint8)(startValues[j] >> 8));

		for (k = 0; k < n; k++)
		{
			if (k & 1)
				reference.WriteVMDATAH(data[k]);
			else
				reference.WriteVMDATAL(data[k]);
		}
		candidate.WriteVMDATABlock(data, n);

		if (std::memcmp(reference.GetVramPtr(0), candidate.GetVramPtr(0),
		                SNESPPU_VRAM_NUMWORDS * sizeof(Uint16)) != 0 ||
		    reference.GetRegs()->vmaddr.w != candidate.GetRegs()->vmaddr.w ||
		    reference.GetRegs()->vmreadlatch.w != candidate.GetRegs()->vmreadlatch.w)
		{
			std::printf("FAIL VRAM block equivalence: vmain=%02X start=%04X bytes=%d addr=%04X/%04X latch=%04X/%04X\n",
			            (unsigned)vmainValues[i], (unsigned)startValues[j], n,
			            (unsigned)reference.GetRegs()->vmaddr.w,
			            (unsigned)candidate.GetRegs()->vmaddr.w,
			            (unsigned)reference.GetRegs()->vmreadlatch.w,
			            (unsigned)candidate.GetRegs()->vmreadlatch.w);
			g_Failures++;
			return;
		}
	}
}

static void CheckScrollAndMode7Latches()
{
	SnesPPU ppu;
	TestRender render;

	ppu.SetPPURender(&render);
	render.SetPPU(&ppu);

	// Horizontal scroll keeps a separate 3-bit latch and is 10-bit wide.
	ppu.Reset();
	ppu.Write8(0x210F, 0x34);
	ppu.Write8(0x210F, 0xFF);
	Check("BG H scroll latch/mask", ppu.GetRegs()->bg2hofs.w, 0x334);

	// Vertical scroll consumes the shared H/V latch, not the H-only latch.
	ppu.Reset();
	ppu.Write8(0x210F, 0x5A);
	ppu.Write8(0x2110, 0x02);
	Check("BG V shared latch", ppu.GetRegs()->bg2vofs.w, 0x25A);

	// A vertical write changes bits 3-7 used by the next H write, while the
	// low three bits still come from the previous horizontal write.
	ppu.Reset();
	ppu.Write8(0x210F, 0xA5);
	ppu.Write8(0x2110, 0x3C);
	ppu.Write8(0x2111, 0x02);
	Check("BG interleaved H/V latches", ppu.GetRegs()->bg3hofs.w, 0x23D);

	// $210D/$210E update both ordinary BG1 scroll and the independent
	// 13-bit Mode 7 scroll registers.
	ppu.Reset();
	ppu.Write8(0x210D, 0x34);
	ppu.Write8(0x210D, 0x12);
	Check("BG1 H scroll from $210D", ppu.GetRegs()->bg1hofs.w, 0x234);
	Check("Mode 7 H scroll from $210D", ppu.GetRegs()->m7hofs.w, 0x1234);

	// All Mode 7 ports share one byte latch, including BG1 scroll and the
	// matrix/centre registers.
	ppu.Reset();
	ppu.Write8(0x210D, 0x34);
	ppu.Write8(0x211B, 0x12);
	Check("Mode 7 shared latch matrix", ppu.GetRegs()->m7a.w, 0x1234);
	ppu.Write8(0x210E, 0x05);
	Check("Mode 7 shared latch scroll", ppu.GetRegs()->m7vofs.w, 0x0512);
	Check("BG1 V has independent latch", ppu.GetRegs()->bg1vofs.w, 0x0134);
}

static void CheckMode4VRAMAddressDataDMA()
{
	SnesPPU ppu;
	TestRender render;
	/* DMA mode 4 at BBAD=$16 repeats VMADDL, VMADDH, VMDATAL,
	   VMDATAH.  First Samurai uses this command stream for scattered
	   tilemap updates. */
	static const Uint8 commands[] =
	{
		0x34, 0x12, 0xAA, 0xBB,
		0x78, 0x56, 0xCC, 0xDD
	};
	static const Uint8 ports[] = {0x16, 0x17, 0x18, 0x19};
	int i;

	ppu.SetPPURender(&render);
	render.SetPPU(&ppu);
	ppu.Reset();
	ppu.Write8(0x2115, 0x80); // increment by one after $2119
	ppu.Write8(0x2116, 0x55);
	ppu.Write8(0x2117, 0x55);

	for (i = 0; i < (int)sizeof(commands); i++)
		SnesDMAWritePPUPort(&ppu, ports[i & 3], commands[i]);

	Check("mode4 first addressed word", ppu.GetVramPtr(0x1234)[0], 0xBBAA);
	Check("mode4 second addressed word", ppu.GetVramPtr(0x5678)[0], 0xDDCC);
	Check("mode4 stale address untouched", ppu.GetVramPtr(0x5555)[0], 0x0000);
	Check("mode4 final address", ppu.GetRegs()->vmaddr.w, 0x5679);
}

int main()
{
	SnesPPU ppu;
	TestRender render;
	Uint8 *pOAM;

	CheckVRAMBlockEquivalence();
	CheckScrollAndMode7Latches();
	CheckMode4VRAMAddressDataDMA();

	ppu.SetPPURender(&render);
	render.SetPPU(&ppu);
	ppu.Reset();
	pOAM = (Uint8 *)ppu.GetOAM();

	// CGRAM commits only after the high byte and stores 15-bit colors.
	render.ClearStats();
	ppu.Write8(0x2121, 0x00);
	ppu.WriteCGDATA(0x34);
	Check("CGRAM low byte remains latched", ppu.GetCG(0), 0x0000);
	Check("CGRAM low byte has no host update", render.uCGRAMCalls, 0);
	ppu.WriteCGDATA(0x92);
	Check("CGRAM pair commits 15-bit color", ppu.GetCG(0), 0x1234);
	Check("CGRAM pair host update", render.uCGRAMCalls, 1);
	Check("CGRAM update address", render.uLastCGRAMAddress, 0);
	Check("CGRAM update data", render.uLastCGRAMData, 0x1234);
	ppu.Write8(0x2121, 0x00);
	ppu.WriteCGDATA(0x34);
	ppu.WriteCGDATA(0x12);
	Check("CGRAM unchanged write elided", render.uCGRAMCalls, 1);

	// OAMADDL must not discard the high-table bit selected by OAMADDH.
	ppu.Write8(0x2103, 0x01);
	ppu.Write8(0x2102, 0x12);
	Check("OAMADDL preserves bit 9", ppu.GetRegs()->oamaddr.w, 0x224);
	Check("OAM base preserves bit 9", ppu.GetRegs()->oamaddrlatch.w, 0x224);

	// The base written through $2102/$2103 is not the current address that
	// $2104 advances. Writing either address register reloads the current
	// address from the two last register halves.
	ppu.Write8(0x2103, 0x00);
	ppu.Write8(0x2102, 0x20);
	ppu.WriteOAMDATA(0x11);
	ppu.WriteOAMDATA(0x22);
	Check("OAM current advances away from base", ppu.GetRegs()->oamaddr.w, 0x42);
	Check("OAM base does not advance", ppu.GetRegs()->oamaddrlatch.w, 0x40);
	ppu.Write8(0x2103, 0x80);
	Check("OAMADDH reloads base low bits", ppu.GetRegs()->oamaddr.w, 0x8040);
	Check("OAMADDH updates base", ppu.GetRegs()->oamaddrlatch.w, 0x8040);
	Check("OAMADDH reloads first object", ppu.GetRegs()->oampri.w, 0x10);
	ppu.WriteOAMDATA(0x33);
	ppu.WriteOAMDATA(0x44);
	ppu.EndFrame();
	Check("VBlank reloads OAM base", ppu.GetRegs()->oamaddr.w, 0x8040);
	Check("VBlank reloads first object", ppu.GetRegs()->oampri.w, 0x10);

	// Low OAM commits an even/odd pair only when the odd byte arrives.
	ppu.Write8(0x2103, 0x00);
	ppu.Write8(0x2102, 0x00);
	ppu.WriteOAMDATA(0x12);
	Check("low OAM even byte stays latched", pOAM[0], 0x00);
	Check("low OAM increments after even", ppu.GetRegs()->oamaddr.w, 0x01);
	ppu.WriteOAMDATA(0x34);
	Check("low OAM commits latched byte", pOAM[0], 0x12);
	Check("low OAM commits odd byte", pOAM[1], 0x34);

	ppu.Write8(0x2102, 0x00);
	ppu.WriteOAMDATA(0xAA);
	ppu.Write8(0x2102, 0x01);
	Check("address reset discards unpaired byte", pOAM[0], 0x12);

	// High OAM writes immediately and mirrors every 32 logical bytes.
	ppu.Write8(0x2102, 0x00);
	ppu.Write8(0x2103, 0x01);
	ppu.WriteOAMDATA(0xA5);
	Check("high OAM direct write", ppu.GetOAM()->ObjEx[0], 0xA5);
	ppu.Write8(0x2102, 0x10);
	ppu.WriteOAMDATA(0x5A);
	Check("high OAM mirror", ppu.GetOAM()->ObjEx[0], 0x5A);
	ppu.Write8(0x2102, 0x10);
	Check("high OAM mirrored read", ppu.ReadOAMDATA(), 0x5A);

	// Priority rotation follows the current byte address as the port advances.
	ppu.Write8(0x2102, 0x00);
	ppu.Write8(0x2103, 0x80);
	ppu.WriteOAMDATA(0x01);
	ppu.WriteOAMDATA(0x02);
	ppu.WriteOAMDATA(0x03);
	ppu.WriteOAMDATA(0x04);
	Check("priority address advances", ppu.GetRegs()->oamaddr.w, 0x8004);
	Check("priority first object advances", ppu.GetRegs()->oampri.w, 1);
	ppu.Write8(0x2103, 0x00);
	Check("priority rotation disabled", ppu.GetRegs()->oampri.w, 0);

	// The optimized full-frame OAM DMA must match byte-port semantics.
	{
		SnesPPU reference;
		TestRender referenceRender;
		Uint8 data[sizeof(SnesOAMT)];
		int i;

		reference.SetPPURender(&referenceRender);
		referenceRender.SetPPU(&reference);
		reference.Reset();
		ppu.Reset();
		for (i = 0; i < (int)sizeof(data); i++)
			data[i] = (Uint8)(i * 37 + 11);

		for (i = 0; i < (int)sizeof(data); i++)
			reference.WriteOAMDATA(data[i]);
		ppu.WriteOAMBlock(data, sizeof(data));

		Check("OAM DMA data",
		      std::memcmp(ppu.GetOAM(), reference.GetOAM(), sizeof(SnesOAMT)), 0);
		Check("OAM DMA address", ppu.GetRegs()->oamaddr.w,
		      reference.GetRegs()->oamaddr.w);
		Check("OAM DMA priority", ppu.GetRegs()->oampri.w,
		      reference.GetRegs()->oampri.w);
	}

	// Common mode-1 VRAM DMA writes consecutive low/high byte pairs.
	{
		const Uint8 data[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
		ppu.Reset();
		render.ClearStats();
		ppu.Write8(0x2115, 0x80); // increment by one after $2119
		ppu.Write8(0x2116, 0x34);
		ppu.Write8(0x2117, 0x12);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM block word 0", ppu.GetVramPtr(0x1234)[0], 0x2211);
		Check("VRAM block word 1", ppu.GetVramPtr(0x1235)[0], 0x4433);
		Check("VRAM block word 2", ppu.GetVramPtr(0x1236)[0], 0x6655);
		Check("VRAM block address", ppu.GetRegs()->vmaddr.w, 0x1237);
		Check("VRAM block read latch", ppu.GetRegs()->vmreadlatch.w, 0x1236);
		Check("VRAM block range calls", render.uRangeCalls, 1);
		Check("VRAM block invalidate address", render.uFirstAddress, 0x1234);
		Check("VRAM block invalidate words", render.nLastWords, 3);
	}

	// Physical VRAM wraps every $8000 words while VMADDR remains 16-bit.
	{
		const Uint8 data[] = {0xAA, 0xBB, 0xCC, 0xDD};
		ppu.Reset();
		ppu.Write8(0x2115, 0x80);
		ppu.Write8(0x2116, 0xFF);
		ppu.Write8(0x2117, 0x7F);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM block last word", ppu.GetVramPtr(0x7FFF)[0], 0xBBAA);
		Check("VRAM block wrapped word", ppu.GetVramPtr(0x0000)[0], 0xDDCC);
		Check("VRAM wrapped address", ppu.GetRegs()->vmaddr.w, 0x8001);
	}

	// An odd byte count ends on the low port without a high-port increment.
	{
		const Uint8 data[] = {0xAA, 0xBB, 0xCC};
		ppu.Reset();
		ppu.Write8(0x2115, 0x80);
		ppu.Write8(0x2116, 0x20);
		ppu.Write8(0x2117, 0x00);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM odd pair", ppu.GetVramPtr(0x20)[0], 0xBBAA);
		Check("VRAM odd low byte", ppu.GetVramPtr(0x21)[0], 0x00CC);
		Check("VRAM odd address", ppu.GetRegs()->vmaddr.w, 0x21);
	}

	// Non-linear increment modes retain the exact per-port fallback path.
	{
		const Uint8 data[] = {0x12, 0x34};
		ppu.Reset();
		render.ClearStats();
		ppu.Write8(0x2115, 0x00); // increment after $2118
		ppu.Write8(0x2116, 0x30);
		ppu.Write8(0x2117, 0x00);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM low-increment low", ppu.GetVramPtr(0x30)[0], 0x0012);
		Check("VRAM low-increment high", ppu.GetVramPtr(0x31)[0], 0x3400);
		Check("VRAM low-increment address", ppu.GetRegs()->vmaddr.w, 0x31);
		Check("VRAM low-increment latch", ppu.GetRegs()->vmreadlatch.w, 0x31);
		Check("VRAM low-increment invalidates both", render.uSingleCalls, 2);
		Check("VRAM low-increment first invalidation", render.uFirstAddress, 0x30);
		Check("VRAM low-increment last invalidation", render.uLastAddress, 0x31);
	}

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
