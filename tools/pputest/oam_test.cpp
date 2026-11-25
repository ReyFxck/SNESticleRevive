/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 * Exercises oam test behavior in the pputest regression suite.
 */

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
	Uint16 uInternalOAMAddress;
	Uint8 uInternalCGRAMAddress;

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
	Uint16 GetInternalOAMAddress(Uint32, Uint32)
	{
		return uInternalOAMAddress;
	}
	Uint8 GetInternalCGRAMAddress(Uint32, Uint32)
	{
		return uInternalCGRAMAddress;
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
		uInternalOAMAddress = 0xFFFF;
		uInternalCGRAMAddress = 0;
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
		// The block path is only valid during accessible VRAM periods.
		reference.Write8(0x2100, 0x80);
		candidate.Write8(0x2100, 0x80);

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

static void CheckUnchangedVRAMWrites()
{
    SnesPPU ppu; TestRender render;
    ppu.SetPPURender(&render); render.SetPPU(&ppu);
    ppu.Reset();
    ppu.Write8(0x2100,0x80); // forced blank: DMA may access VRAM
    const Uint8 bytes[] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77};
    const Uint8 changed[] = {0x11,0x22,0x33,0x44,0x99,0x66,0x77};
    ppu.Write8(0x2115,0x80);
    ppu.Write8(0x2116,0xFE); ppu.Write8(0x2117,0x7F);
    ppu.WriteVMDATABlock(bytes,sizeof(bytes));
    ppu.Write8(0x2116,0xFE); ppu.Write8(0x2117,0x7F);
    render.ClearStats();
    Uint16 latch=ppu.GetRegs()->vmreadlatch.w;
    ppu.WriteVMDATABlock(bytes,sizeof(bytes));
    Check("unchanged DMA avoids range invalidation",render.uRangeCalls,0);
    Check("unchanged odd tail avoids invalidation",render.uSingleCalls,0);
    Check("unchanged DMA advances VMADDR",ppu.GetRegs()->vmaddr.w,0x8001);
    Check("unchanged DMA preserves latch",ppu.GetRegs()->vmreadlatch.w,latch);
    ppu.Write8(0x2116,0xFE); ppu.Write8(0x2117,0x7F);
    render.ClearStats();
    ppu.WriteVMDATABlock(changed,sizeof(changed));
    Check("changed word after wrap invalidation count",render.uRangeCalls,1);
    Check("changed word after wrap starts at zero",render.uFirstAddress,0);
    Check("unchanged prefix not invalidated",render.nLastWords,1);
    Check("changed word written",ppu.GetVramPtr(0)[0],0x6699);
    ppu.Write8(0x2116,0); ppu.Write8(0x2117,0);
    render.ClearStats();
    ppu.WriteVMDATALH(0x99,0x66);
    Check("unchanged pair avoids invalidation",render.uSingleCalls,0);
    ppu.Write8(0x2116,0); ppu.Write8(0x2117,0);
    ppu.WriteVMDATALH(0x99,0xAB);
    Check("high-only change invalidates once",render.uSingleCalls,1);
    Check("high-only change written",ppu.GetVramPtr(0)[0],0xAB99);
    ppu.Write8(0x2116,0); ppu.Write8(0x2117,0);
    render.ClearStats();
    ppu.WriteVMDATAL(0x99); ppu.WriteVMDATAH(0xAB);
    Check("unchanged individual ports",render.uSingleCalls,0);
    Check("unchanged individual port advances address",ppu.GetRegs()->vmaddr.w,1);
}

/* Exercise every first-changed word in a typical DMA, with alignment and
   physical wrap. Match both the memory and the invalidation boundaries. */
static void CheckVRAMWidePrefix()
{
    static SnesPPU ppu;
    TestRender render;
    ppu.SetPPURender(&render); render.SetPPU(&ppu);
    Uint8 storage[72] _ALIGN(16);
    const Uint16 starts[] = {0x0040, 0x0041, 0x7FF8, 0x7FFF};
    unsigned cases = 0;
    for (unsigned off=0; off<4; ++off)
    for (unsigned st=0; st<sizeof(starts)/sizeof(starts[0]); ++st)
    for (unsigned changed=0; changed<=32; ++changed)
    {
        Uint8 *data=storage+off;
        for (unsigned i=0;i<64;++i) data[i]=(Uint8)(i*79u+13u);
        ppu.Reset(); ppu.Write8(0x2100,0x80); ppu.Write8(0x2115,0x80);
        ppu.Write8(0x2116,(Uint8)starts[st]); ppu.Write8(0x2117,starts[st]>>8);
        ppu.WriteVMDATABlock(data,64);
        if (changed<32) data[changed*2+1]^=0x80;
        ppu.Write8(0x2116,(Uint8)starts[st]); ppu.Write8(0x2117,starts[st]>>8);
        render.ClearStats(); ppu.WriteVMDATABlock(data,64);
        Check("wide prefix range calls",render.uRangeCalls,changed<32?1:0);
        if(changed<32) {
            Check("wide prefix first changed",render.uFirstAddress,(starts[st]+changed)&0x7FFF);
            Check("wide prefix suffix length",render.nLastWords,32-changed);
        }
        Check("wide prefix vmaddr",ppu.GetRegs()->vmaddr.w,(Uint16)(starts[st]+32));
        for(unsigned i=0;i<32;++i)
            Check("wide prefix word",*ppu.GetVramPtr(starts[st]+i),(Uint16)(data[2*i]|(data[2*i+1]<<8)));
        ++cases;
    }
    std::printf("VRAM wide prefix: %u cases\n",cases);
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

static void CheckActiveDisplayMemoryPorts()
{
	SnesPPU ppu;
	TestRender render;
	Uint8 *pOAM;

	ppu.SetPPURender(&render);
	render.SetPPU(&ppu);
	ppu.Reset();
	ppu.BeginFrame();
	pOAM = (Uint8 *)ppu.GetOAM();

	/* VRAM writes are blocked during visible display, but the selected data
	   port still performs its documented VMADDR increment. */
	ppu.Write8(0x2115, 0x80);
	ppu.Write8(0x2116, 0x10);
	ppu.Write8(0x2117, 0x00);
	ppu.WriteTimed(0x2119, 0xAB, 1, 500);
	Check("active VRAM write blocked", ppu.GetVramPtr(0x10)[0], 0x0000);
	Check("active VRAM write increments", ppu.GetRegs()->vmaddr.w, 0x11);

	/* VMADDR writes during rendering cannot prefetch real VRAM and therefore
	   seed the read buffer with zero. */
	ppu.GetVramPtr(0x20)[0] = 0xBEEF;
	ppu.WriteTimed(0x2116, 0x20, 1, 500);
	ppu.WriteTimed(0x2117, 0x00, 1, 500);
	Check("active VRAM prefetch blocked", ppu.GetRegs()->vmreadlatch.w, 0x0000);

	/* Forced blank restores ordinary VRAM access immediately. */
	ppu.Write8(0x2100, 0x80);
	ppu.WriteTimed(0x2118, 0x34, 1, 500);
	Check("forced blank VRAM write", ppu.GetVramPtr(0x20)[0] & 0xFF, 0x34);
	ppu.Write8(0x2100, 0x00);

	/* During the inaccessible CGRAM window, the palette bus address comes
	   from the renderer rather than CGADD. The CPU phase/address still moves. */
	render.uInternalCGRAMAddress = 5;
	ppu.Write8(0x2121, 0x02);
	ppu.WriteTimed(0x2122, 0x78, 1, 500);
	ppu.WriteTimed(0x2122, 0x12, 1, 500);
	Check("active CGRAM redirected value", ppu.GetCG(5), 0x1278);
	Check("active CGRAM requested address untouched", ppu.GetCG(2), 0x0000);
	Check("active CGRAM address advances", ppu.GetRegs()->cgadd.w, 6);

	/* H<88 is an allowed CGRAM access window and therefore uses CGADD. */
	ppu.Write8(0x2121, 0x03);
	ppu.WriteTimed(0x2122, 0x56, 1, 40);
	ppu.WriteTimed(0x2122, 0x34, 1, 40);
	Check("early CGRAM window uses CGADD", ppu.GetCG(3), 0x3456);

	/* In active display OAMDATA uses the sprite engine's OAM address. An even
	   low-table address latches the byte and mirrors it into high OAM. */
	render.uInternalOAMAddress = 0x10;
	ppu.Write8(0x2102, 0x00);
	ppu.Write8(0x2103, 0x00);
	ppu.WriteTimed(0x2104, 0xA5, 1, 1100);
	Check("active OAM high-table redirect", pOAM[0x201], 0xA5);
	Check("active OAM internal address increments", ppu.GetRegs()->oamaddr.w, 1);

	pOAM[0x10] = 0x5C;
	ppu.Write8(0x2102, 0x00);
	Check("active OAM read redirected",
	      ppu.ReadTimed(0x2138, 0, TRUE, 1, 1100), 0x5C);
	Check("active OAM read internal increment", ppu.GetRegs()->oamaddr.w, 1);
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

static void CheckReadRegisterSemantics()
{
	SnesPPU ppu;
	TestRender render;

	ppu.SetPPURender(&render);
	render.SetPPU(&ppu);
	ppu.Reset();

	/* The common 5C78 revision reports version 3. */
	Check("STAT78 version", ppu.Read8(0x213F, 0x00, TRUE) & 0x0F, 0x03);

	/* 16 * ($0100 >> 8) = 16. A PPU1 result is retained by the
	   documented write-only aliases and by STAT77 bit 4. */
	ppu.Write8(0x211B, 0x10);
	ppu.Write8(0x211B, 0x00);
	ppu.Write8(0x211C, 0x00);
	ppu.Write8(0x211C, 0x01);
	Check("Mode7 product low", ppu.Read8(0x2134), 0x10);
	Check("PPU1 write-only alias", ppu.Read8(0x2104, 0xA5), 0x10);
	Check("CPU bus on other write-only port", ppu.Read8(0x2100, 0xA5), 0xA5);
	Check("STAT77 PPU1 bit", ppu.Read8(0x213E), 0x11);
	ppu.SetOBJStatus(TRUE, FALSE);
	Check("STAT77 range-over", ppu.Read8(0x213E) & 0xC0, 0x40);
	ppu.SetOBJStatus(FALSE, TRUE);
	Check("STAT77 sticky time-over", ppu.Read8(0x213E) & 0xC0, 0xC0);
	ppu.BeginFrame();
	Check("STAT77 flags clear at frame", ppu.Read8(0x213E) & 0xC0, 0x00);

	/* Latching keeps the current high/low phase. High counter reads expose
	   only bit 0 and retain bits 1-7 from PPU2. */
	ppu.LatchCounters(0x0100, 0x0180);
	Check("latched H low", ppu.Read8(0x213C), 0x00);
	Check("latched H high PPU2", ppu.Read8(0x213C), 0x01);
	Check("latched V low", ppu.Read8(0x213D), 0x80);
	Check("latched V high PPU2", ppu.Read8(0x213D), 0x81);
	Check("STAT78 latch flag", ppu.Read8(0x213F, 0x00, FALSE) & 0x40, 0x40);
	Check("STAT78 enabled read sees latch", ppu.Read8(0x213F, 0x00, TRUE) & 0x40, 0x40);
	Check("STAT78 enabled read clears latch", ppu.Read8(0x213F, 0x00, FALSE) & 0x40, 0x00);

	/* The low CGRAM byte becomes PPU2 open bus; bit 7 is consequently
	   preserved in the following 15-bit high-byte read. */
	ppu.Write8(0x2121, 0x00);
	ppu.WriteCGDATA(0x80);
	ppu.WriteCGDATA(0x12);
	ppu.Write8(0x2121, 0x00);
	Check("CGRAM read low", ppu.Read8(0x213B), 0x80);
	Check("CGRAM read high PPU2 bit", ppu.Read8(0x213B), 0x92);
}

static void CheckBlockedVRAMAndWindows()
{
    SnesPPU ppu; TestRender render;
    ppu.SetPPURender(&render); render.SetPPU(&ppu);
    const Uint8 bytes[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    for (unsigned mode=0; mode<2; ++mode)
    {
        ppu.Reset(); ppu.BeginFrame(); ppu.Sync(1);
        ppu.Write8(0x2115, mode ? 0x80 : 0x00);
        ppu.GetVramPtr(0x10)[0]=0xCAFE;
        ppu.GetVramPtr(0x11)[0]=0xBEEF;
        ppu.GetVramPtr(0x12)[0]=0xAABB;
        ppu.WriteTimed(0x2116,0x10,2,500); ppu.WriteTimed(0x2117,0,2,500);
        render.ClearStats();
        ppu.WriteTimed(0x2118,0x01,2,500);
        ppu.WriteTimed(0x2119,0x02,2,500);
        Check("blocked timed pair increments once",ppu.GetRegs()->vmaddr.w,0x11);
        ppu.WriteVMDATALH(0x03,0x04);
        Check("blocked bulk pair increments once",ppu.GetRegs()->vmaddr.w,0x12);
        ppu.WriteVMDATABlock(bytes,sizeof(bytes));
        Check("blocked odd block increments",ppu.GetRegs()->vmaddr.w,mode?0x14:0x15);
        Check("blocked writes no invalidation",render.uSingleCalls+render.uRangeCalls,0);
        ppu.EndFrame(); ppu.BeginFrame();
        Check("blocked writes not replayed 0",ppu.GetVramPtr(0x10)[0],0xCAFE);
        Check("blocked writes not replayed 1",ppu.GetVramPtr(0x11)[0],0xBEEF);
        Check("blocked writes not replayed 2",ppu.GetVramPtr(0x12)[0],0xAABB);
        Check("blocked odd tail not stored",ppu.GetVramPtr(0x14)[0],0);
        Check("no deferred invalidation",render.uSingleCalls+render.uRangeCalls,0);
    }
    ppu.Reset(); ppu.BeginFrame(); ppu.Sync(1);
    ppu.Write8(0x2124,0x12); ppu.Write8(0x2125,0x34);
    ppu.Write8(0x212A,0x56); ppu.Write8(0x212B,0x78);
    ppu.WriteTimed(0x2123,0xAB,2,1100);
    Check("active W12SEL",ppu.GetRegs()->w12sel,0xAB);
    Check("active W34SEL",ppu.GetRegs()->w34sel,0x12);
    Check("active WOBJSEL",ppu.GetRegs()->wobjsel,0x34);
    Check("active WBGLOG",ppu.GetRegs()->wbglog,0x56);
    Check("active WOBJLOG",ppu.GetRegs()->wobjlog,0x78);
    ppu.EndFrame(); ppu.BeginFrame();
    ppu.Write8(0x2123,0xCD);
    ppu.Sync(1); ppu.Write8(0x212B,0x03);
    ppu.EndFrame(); ppu.BeginFrame();
    Check("partial window write preserves unrelated register",ppu.GetRegs()->w12sel,0xCD);
    Check("partial window write preserves W34SEL",ppu.GetRegs()->w34sel,0x12);
    Check("WOBJLOG persists",ppu.GetRegs()->wobjlog,0x03);
    ppu.EndFrame(); ppu.Reset();
    Check("PPU reset clears vblank flag",ppu.InVBlank(),FALSE);
}

/* Exercise both direct OAM chunks and every kind of byte-loop fallback.
   Compare final memory/address/priority and expose the write latch by a
   subsequent active-display odd-byte write on the renderer's OAM bus. */
static void CheckOAMBulkEquivalence()
{
    SnesPPU ppu, ref;
    TestRender render, refRender;
    ppu.SetPPURender(&render); render.SetPPU(&ppu);
    ref.SetPPURender(&refRender); refRender.SetPPU(&ref);
    Uint32 random = 0x713ae19u;
    Uint8 data[1088];
    unsigned cases = 0;
    for (unsigned start = 0; start < 1024; ++start)
    for (unsigned variant = 0; variant < 4; ++variant)
    {
        ppu.Reset(); ref.Reset();
        const unsigned rotate = variant & 1u ? 0x80u : 0u;
        const unsigned count = variant == 0 ? 256u : variant == 1 ? 32u :
            variant == 2 ? 544u : 1u + (start * 37u) % 1088u;
        for (unsigned i = 0; i < sizeof(data); ++i)
        {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            data[i] = (Uint8)random;
        }
        // Include already-equal data as well as changed data.
        if (variant == 1) memset(data, 0, sizeof(data));
        ppu.Write8(0x2102, (Uint8)(start >> 1));
        ref.Write8(0x2102, (Uint8)(start >> 1));
        ppu.Write8(0x2103, (Uint8)(rotate | (start >> 9)));
        ref.Write8(0x2103, (Uint8)(rotate | (start >> 9)));
        if (start & 1u) { ppu.ReadOAMDATA(); ref.ReadOAMDATA(); }
        for (unsigned i = 0; i < count; ++i) ref.WriteOAMDATA(data[i]);
        // Test the real MDMA segmentation as well as a single call.
        if (variant == 2)
        {
            ppu.WriteOAMBlock(data, 256);
            ppu.WriteOAMBlock(data + 256, 256);
            ppu.WriteOAMBlock(data + 512, 32);
        }
        else ppu.WriteOAMBlock(data, (Int32)count);
        Check("bulk OAM memory", memcmp(ppu.GetOAM(), ref.GetOAM(), sizeof(SnesOAMT)), 0);
        Check("bulk OAM address", ppu.GetRegs()->oamaddr.w, ref.GetRegs()->oamaddr.w);
        Check("bulk OAM priority", ppu.GetRegs()->oampri.w, ref.GetRegs()->oampri.w);
        ppu.Write8(0x2100, 0x0F); ref.Write8(0x2100, 0x0F);
        render.uInternalOAMAddress = refRender.uInternalOAMAddress = 1;
        ppu.WriteTimed(0x2104, 0x5A, 1, 1024);
        ref.WriteTimed(0x2104, 0x5A, 1, 1024);
        Check("bulk OAM latch", memcmp(ppu.GetOAM(), ref.GetOAM(), sizeof(SnesOAMT)), 0);
        ++cases;
    }
    std::printf("OAM bulk equivalence: %u cases %s\n", cases, g_Failures ? "FAIL" : "PASS");
}

int main(int argc, char **argv)
{
	if (argc == 2 && !std::strcmp(argv[1], "--vram-hotpath"))
	{
        CheckOAMBulkEquivalence();
		CheckVRAMBlockEquivalence();
		CheckUnchangedVRAMWrites();
		CheckVRAMWidePrefix();
		std::puts(g_Failures ? "FAIL VRAM hotpath" : "VRAM hotpath PASS");
		return g_Failures ? 1 : 0;
	}
	SnesPPU ppu;
	TestRender render;
	Uint8 *pOAM;

	CheckOAMBulkEquivalence();
	CheckVRAMBlockEquivalence();
	CheckUnchangedVRAMWrites();
	CheckVRAMWidePrefix();
	CheckScrollAndMode7Latches();
	CheckActiveDisplayMemoryPorts();
	CheckMode4VRAMAddressDataDMA();
	CheckReadRegisterSemantics();
	CheckBlockedVRAMAndWindows();

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
		ppu.Write8(0x2100, 0x80); // VRAM DMA requires forced blank or VBlank.
		render.ClearStats();
		ppu.Write8(0x2115, 0x80); // increment by one after $2119
		ppu.Write8(0x2116, 0x34);
		ppu.Write8(0x2117, 0x12);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM block word 0", ppu.GetVramPtr(0x1234)[0], 0x2211);
		Check("VRAM block word 1", ppu.GetVramPtr(0x1235)[0], 0x4433);
		Check("VRAM block word 2", ppu.GetVramPtr(0x1236)[0], 0x6655);
		Check("VRAM block address", ppu.GetRegs()->vmaddr.w, 0x1237);
		Check("VRAM writes preserve read buffer", ppu.GetRegs()->vmreadlatch.w, 0x0000);
		Check("VRAM block range calls", render.uRangeCalls, 1);
		Check("VRAM block invalidate address", render.uFirstAddress, 0x1234);
		Check("VRAM block invalidate words", render.nLastWords, 3);
	}

	// Physical VRAM wraps every $8000 words while VMADDR remains 16-bit.
	{
		const Uint8 data[] = {0xAA, 0xBB, 0xCC, 0xDD};
		ppu.Reset();
		ppu.Write8(0x2100, 0x80); // VRAM DMA requires forced blank or VBlank.
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
		ppu.Write8(0x2100, 0x80); // VRAM DMA requires forced blank or VBlank.
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
		ppu.Write8(0x2100, 0x80); // VRAM DMA requires forced blank or VBlank.
		render.ClearStats();
		ppu.Write8(0x2115, 0x00); // increment after $2118
		ppu.Write8(0x2116, 0x30);
		ppu.Write8(0x2117, 0x00);
		ppu.WriteVMDATABlock(data, sizeof(data));
		Check("VRAM low-increment low", ppu.GetVramPtr(0x30)[0], 0x0012);
		Check("VRAM low-increment high", ppu.GetVramPtr(0x31)[0], 0x3400);
		Check("VRAM low-increment address", ppu.GetRegs()->vmaddr.w, 0x31);
		Check("VRAM low-increment preserves read buffer", ppu.GetRegs()->vmreadlatch.w, 0x0000);
		Check("VRAM low-increment invalidates both", render.uSingleCalls, 2);
		Check("VRAM low-increment first invalidation", render.uFirstAddress, 0x30);
		Check("VRAM low-increment last invalidation", render.uLastAddress, 0x31);
	}

	// $2139/$213A expose the word prefetch buffer, not VMADDR directly.
	// The incrementing port reloads the current word before incrementing,
	// which is why sequential software performs one dummy access first.
	{
		ppu.Reset();
		ppu.GetVramPtr(0x10)[0] = 0x2211;
		ppu.GetVramPtr(0x11)[0] = 0x4433;
		ppu.Write8(0x2115, 0x80); // increment on high read
		ppu.Write8(0x2116, 0x10);
		ppu.Write8(0x2117, 0x00);
		Check("VRAM prefetched low", ppu.Read8(0x2139), 0x11);
		Check("VRAM prefetched high", ppu.Read8(0x213A), 0x22);
		Check("VRAM read address increments", ppu.GetRegs()->vmaddr.w, 0x11);
		Check("VRAM dummy low repeats", ppu.Read8(0x2139), 0x11);
		Check("VRAM dummy high repeats", ppu.Read8(0x213A), 0x22);
		Check("VRAM next word low", ppu.Read8(0x2139), 0x33);
		Check("VRAM next word high", ppu.Read8(0x213A), 0x44);

		ppu.Write8(0x2115, 0x00); // increment on low read
		ppu.Write8(0x2116, 0x10);
		ppu.Write8(0x2117, 0x00);
		Check("VRAM low-increment first low", ppu.Read8(0x2139), 0x11);
		Check("VRAM low-increment first high", ppu.Read8(0x213A), 0x22);
		Check("VRAM low-increment dummy low", ppu.Read8(0x2139), 0x11);
		Check("VRAM low-increment next high", ppu.Read8(0x213A), 0x44);
	}

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
