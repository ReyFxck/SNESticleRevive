#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "types.h"
#include "snes.h"
#include "rendersurface.h"
#include "console.h"
#include "prof.h"
#include "sntiming.h"
#include "sndebug.h"
#include "sndbglog.h"

// --- diagnostico de TIMING (ver sndbglog.h) ---
#if SNDBG_LOG
static Uint32 g_TmgFrameStart = 0;   // COP0 cycle no inicio do frame
static Uint32 g_TmgWinFrames  = 0;   // frames acumulados na janela
static Uint32 g_TmgWinSumCyc  = 0;   // soma de ciclos/frame na janela
static Uint32 g_TmgWinMaxCyc  = 0;   // pior frame (ciclos) na janela
static Uint32 g_TmgWinSumM7   = 0;   // soma de ciclos do Mode-7 na janela
static Uint32 g_TmgWinSumObj  = 0;   // soma de ciclos de sprites na janela
static Uint32 g_TmgWinSumPPU  = 0;
static Uint32 g_TmgWinSumCPU  = 0;
static Uint32 g_TmgWinSumGSU  = 0;
static Uint32 g_TmgWinSumMDMA = 0;
static Uint32 g_TmgWinSumHDMA = 0;
static Uint32 g_TmgWinSumAPU  = 0;
static Uint32 g_TmgWinSumMix  = 0;
static Uint32 g_TmgWinSumBlend = 0;
static Uint32 g_TmgWinSumPPUSync = 0;
static Uint32 g_TmgWinSumBGInfo = 0;
static Uint32 g_TmgWinSumBGOffset = 0;
static Uint32 g_TmgWinSumBGMap = 0;
static Uint32 g_TmgWinSumBGChr = 0;
static Uint32 g_TmgWinSumBGMain = 0;
static Uint32 g_TmgWinSumBGSub = 0;
static Uint32 g_TmgWinSumColorMath = 0;
static Uint32 g_TmgWinSumObjUpdate = 0;
static Uint32 g_TmgWinSumObjFetch = 0;
static Uint32 g_TmgWinSumObjDraw = 0;
static Uint32 g_TmgWinSumHDMAData = 0;
static Uint32 g_TmgWinSumHDMATable = 0;
static Int32  g_TmgIrqLineMin = 9999;
static Int32  g_TmgIrqLineMax = -1;
static Uint32 g_TmgIrqCount   = 0;   // total de H-IRQs na janela
static Uint32 g_TmgIrqRearms  = 0;   // timer H/V reprogramado na linha ativa
static Uint32 g_TmgIrqInstant = 0;   // novo compare ja ficou atras do feixe
static Uint32 g_TmgFrameNo    = 0;
// acumuladores por frame (externados, alimentados em snppurender8.cpp)
Uint32 g_TmgCycM7  = 0;
Uint32 g_TmgCycObj = 0;
Uint32 g_TmgCycPPU = 0;
Uint32 g_TmgCycCPU = 0;
Uint32 g_TmgCycGSU = 0;
Uint32 g_TmgCycMDMA = 0;
Uint32 g_TmgCycHDMA = 0;
Uint32 g_TmgCycAPU = 0;
Uint32 g_TmgCycMix = 0;
Uint32 g_TmgCycBlend = 0;
Uint32 g_TmgCycPPUSync = 0;
Uint32 g_TmgCycBGInfo = 0;
Uint32 g_TmgCycBGOffset = 0;
Uint32 g_TmgCycBGMap = 0;
Uint32 g_TmgCycBGChr = 0;
Uint32 g_TmgCycBGMain = 0;
Uint32 g_TmgCycBGSub = 0;
Uint32 g_TmgCycColorMath = 0;
Uint32 g_TmgCycObjUpdate = 0;
Uint32 g_TmgCycObjFetch = 0;
Uint32 g_TmgCycObjDraw = 0;
Uint32 g_TmgCycHDMAData = 0;
Uint32 g_TmgCycHDMATable = 0;

Uint32 g_DbgOAMWrites = 0;
Uint32 g_DbgVRAMWrites = 0;
Uint32 g_DbgCGRAMWrites = 0;
Uint32 g_DbgCGRAMCommits = 0;
Uint32 g_DbgCGRAMUnchanged = 0;
Uint32 g_DbgVideoRenderedFrames = 0;
Uint32 g_DbgVideoSkippedFrames = 0;
Uint32 g_DbgObjEnabledLines = 0;
Uint32 g_DbgObjOamRefs = 0;
Uint32 g_DbgObjTiles = 0;
Uint32 g_DbgObjCacheHits = 0;
Uint32 g_DbgObjCacheMisses = 0;
Uint32 g_DbgObjCacheRefreshes = 0;
Uint32 g_DbgBGCacheHits = 0;
Uint32 g_DbgBGCacheMisses = 0;
Uint32 g_DbgBGCacheRefreshes = 0;
Uint32 g_DbgChrCacheInvalidations = 0;
Uint32 g_DbgAudioSamples = 0;
Uint32 g_DbgObjOpaqueTiles = 0;
Uint32 g_DbgObjCandidatePixels = 0;
Uint32 g_DbgObjDrawnPixels = 0;
Uint32 g_DbgObjClippedTiles = 0;
Uint32 g_DbgObjEmptyLines = 0;
Uint32 g_DbgObjRangeLimitLines = 0;
Uint32 g_DbgObjLimitLines = 0;
Uint8  g_DbgObjOBSEL = 0;
Uint8  g_DbgObjTM = 0;
Uint8  g_DbgObjTS = 0;
Uint16 g_DbgObjPriority = 0;
Uint32 g_DbgPPUSyncCalls = 0;
Uint32 g_DbgPPURenderLines = 0;
Uint32 g_DbgDMAStarts = 0;
Uint32 g_DbgDMAReadBytes = 0;
Uint32 g_DbgDMAOAMBytes = 0;
Uint32 g_DbgDMAVRAMBytes = 0;
Uint32 g_DbgDMACGRAMBytes = 0;
Uint32 g_DbgDMAOtherBytes = 0;
Uint32 g_DbgDMAWraps = 0;
Uint32 g_DbgDMAMaxBytes = 0;
Uint32 g_DbgDMAModes[8] = {0,0,0,0,0,0,0,0};
Uint32 g_DbgHDMAScrollBytes = 0;
Uint32 g_DbgHDMACGRAMBytes = 0;
Uint32 g_DbgHDMAWindowColorBytes = 0;
Uint32 g_DbgHDMAOtherBytes = 0;
Uint32 g_DbgPPUQueuedWrites = 0;
Uint32 g_DbgPPUAppliedWrites = 0;
Uint32 g_DbgPPUQueueFull = 0;
Uint32 g_DbgHDMALines = 0;
Uint32 g_DbgHDMAActiveChannels = 0;
Uint32 g_DbgHDMATransferChannels = 0;
Uint32 g_DbgBGActiveLayers = 0;
Uint32 g_DbgBGMapReloads = 0;
Uint32 g_DbgBGChrRows = 0;
Uint32 g_DbgBGChrBlankRows = 0;
Uint32 g_DbgBGChrRepeatRows = 0;
Bool   g_DbgCaptureActive = FALSE;
Uint32 g_DbgCaptureFrameNo = 0;
// contagem de acessos ao DSP por janela (diagnostico de carga)
static Uint32 g_TmgDspRd = 0;
static Uint32 g_TmgDspWr = 0;

#if SNDBG_DEEP
// Bases por frame para detectar o instante em que os sprites colapsam sem
// despejar uma linha de log a cada frame.
static Uint32 g_DbgFrameBaseOAM = 0;
static Uint32 g_DbgFrameBaseVRAM = 0;
static Uint32 g_DbgFrameBaseCGRAM = 0;
static Uint32 g_DbgFrameBaseEnabled = 0;
static Uint32 g_DbgFrameBaseRefs = 0;
static Uint32 g_DbgFrameBaseTiles = 0;
static Uint32 g_DbgFrameBaseOpaque = 0;
static Uint32 g_DbgFrameBaseCandidate = 0;
static Uint32 g_DbgFrameBaseDrawn = 0;
static Uint32 g_DbgPrevObjDrawn = 0;
static Uint32 g_DbgObjEventCooldown = 0;

static Uint32 SnesDbgHash32(const void *pData, Uint32 nBytes)
{
	const Uint8 *p = (const Uint8 *)pData;
	Uint32 h = 2166136261u;
	while (nBytes--)
	{
		h ^= *p++;
		h *= 16777619u;
	}
	return h;
}
#endif

#endif


#define SNES_SYNCPPUEVERYLINE (CODE_DEBUG && 0) 


void SnesSystem::SyncSPC(Int32 uExtra)
{
	Int32 nCycles;

/*#if SNES_DEBUG
    if (g_bStateDebug)
    {
        ConDebug("SyncSPC cpu=%06d spc=%06d\n", 
            SNCPUGetCounter(&m_Cpu, SNCPU_COUNTER_FRAME),
            SNSPCGetCounter(&m_Spc, SNSPC_COUNTER_FRAME)
            );

    }
#endif */     

    Int32 CpuTime = SNCPUGetCounter(&m_Cpu, SNCPU_COUNTER_FRAME);
    Int32 SpcTime = SNSPCGetCounter(&m_Spc, SNSPC_COUNTER_FRAME);

    // get cycle count 
    nCycles = CpuTime - SpcTime - m_Spc.Cycles;
    nCycles += uExtra;
    if (nCycles > (SNSPC_CYCLE * SNES_SPCMINCYCLES))
    {
        //SnesDebug("SNSPCExec: %d\n", nCycles);
        // execute SPC
        PROF_ENTER("SNSpcExecute");
#if SNDBG_LOG
        Uint32 _tAPU = ProfCtrGetCycle();
#endif
        SNSPCExecute(&m_Spc, nCycles);
#if SNDBG_LOG
        g_TmgCycAPU += ProfCtrGetCycle() - _tAPU;
#endif
        PROF_LEAVE("SNSpcExecute");

#if SNSPCIO_WRITEQUEUE
        m_SpcIO.SyncQueueAll();
#endif
    }

//#if SNES_DEBUG
//    if (g_bStateDebug)
//    {
//        ConDebug("DoneSyncSPC cpu=%06d spc=%06d\n", 
//            SNCPUGetCounter(&m_Cpu, SNCPU_COUNTER_FRAME),
//            SNSPCGetCounter(&m_Spc, SNSPC_COUNTER_FRAME)
//            );
//
//    }
//#endif      

    
    
    
    
    /*
    // get cycle count 
    nCycles = SNCPUGetCounter(&m_Cpu, SNCPU_COUNTER_FRAME) - SNSPCGetCounter(&m_Spc, SNSPC_COUNTER_FRAME);
    nCycles += uExtra;

    // get cycle count 
    PROF_ENTER("SNSpcExecute");
    SNSPCExecuteToCycle( &m_Spc, nCycles );
    PROF_LEAVE("SNSpcExecute");

#if SNSPCIO_WRITEQUEUE
    m_SpcIO.SyncQueueAll();
#endif
*/


    /*
	// get cycle count 
	nCycles = SNCPUGetCounter(&m_Cpu, SNCPU_COUNTER_FRAME) - SNSPCGetCounter(&m_Spc, SNSPC_COUNTER_FRAME);
	nCycles += uExtra;
	if (nCycles > (SNSPC_CYCLE * SNES_SPCMINCYCLES))
	{
		//SnesDebug("SNSPCExec: %d\n", nCycles);
		// execute SPC
        PROF_ENTER("SNSpcExecute");
		SNSPCExecute(&m_Spc, nCycles);
        PROF_LEAVE("SNSpcExecute");

		#if SNSPCIO_WRITEQUEUE
		m_SpcIO.SyncQueueAll();
		#endif
	}
    */
}


inline void SnesSystem::SyncPPU()
{
#if SNDBG_LOG
	Uint32 _tSync = ProfCtrGetCycle();
	g_DbgPPUSyncCalls++;
#endif
	// sync ppu to current line
	m_PPU.Sync(m_uLine);
#if SNDBG_LOG
	g_TmgCycPPUSync += ProfCtrGetCycle() - _tSync;
#endif
}

#if SNES_DEBUG
Uint8 SNCPU_TRAPFUNC SnesSystem::Read2000Debug(SNCpuT *pCpu, Uint32 uAddr)
{
    Uint8 uData = Read2000(pCpu, uAddr);
    if (Snes_bDebugIO)
        SnesDebugReadData(uAddr, uData);
    return uData;
}
#endif


Uint8 SNCPU_TRAPFUNC SnesSystem::Read2000(SNCpuT *pCpu, Uint32 uAddr)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	SnesPPURegsT *pPPURegs = (SnesPPURegsT *)pSnes->m_PPU.GetRegs();

	uAddr &= 0xFFFF;

	// S-RTC: relogio em $2800 (leitura)
	if (pSnes->m_bSRTC && uAddr == 0x2800)
		return pSnes->m_SRTC.ReadReg();

	// SuperFX/GSU: registradores em $3000-34FF.  Roteados aqui (dentro do
	// handler do PPU0) porque a granularidade de trap e' 8KB e a pagina
	// $2000-3FFF e' compartilhada com o PPU -- um trap proprio em $3000
	// atropelaria o PPU. O GSU avanca em fatias por scanline, em paralelo
	// aproximado com a CPU principal.
	if (pSnes->m_bSuperFX && uAddr >= 0x3000 && uAddr <= 0x34FF)
	{
		Uint8 v = pSnes->m_GSU.ReadReg((Uint16)uAddr);
		// ler o SFR ($3031) limpa o flag de IRQ do GSU -> baixa a linha de IRQ.
		if (!pSnes->m_GSU.IrqPending())
			SNCPUSignalIRQ(&pSnes->m_Cpu, 0);
		return v;
	}

	// The four CPU/APU communication ports repeat throughout $2140-$217F
	// on real SNES hardware.  Some games deliberately use those mirrors;
	// treating $2144+ as open bus can turn a 16-bit port read into a value
	// which never matches and leave the game in an infinite wait loop.
	if (uAddr >= 0x2140 && uAddr <= 0x217F)
	{
		pSnes->SyncSPC();
		return pSnes->m_SpcIO.m_Regs.apu_r[uAddr & 3];
	}
/*	if (uAddr < 0x2140)
	{
		// ppu read
		return pSnes->m_PPU.Read8(uAddr);
	} else*/
	switch (uAddr)
	{

	case 0x2137: // slhv
		//SnesDebug("readppu_slhv\n");
		pPPURegs->ophct.Reg.w = (SNCPUGetCounter(&pSnes->m_Cpu, SNCPU_COUNTER_LINE) + 14) >> 2;
		pPPURegs->opvct.Reg.w = pSnes->m_uLine & 0x1FF;
		//pPPURegs->opvct.Reg.w |= (pPPURegs->opvct.Reg.w << 8) & 0xFE00;
		//pPPURegs->opvct.Reg.w = 0xE1;
		return 0;

	case 0x2138: // read oam
		#if SNPPU_WRITEQUEUE
		pSnes->SyncPPU();
		#endif
        return pSnes->m_PPU.ReadOAMDATA();

	case 0x213c: // ophct
		return pPPURegs->ophct.Read8();

	case 0x213d: // opvct
		return pPPURegs->opvct.Read8();

	case 0x213e: // stat77
		return pPPURegs->stat77;

	case 0x213f: // stat78
		pPPURegs->ophct.Reset();
		pPPURegs->opvct.Reset();
		return pPPURegs->stat78; // NTSC/PAL bit 4
	case 0x2134: //mpyl
		#if SNPPU_WRITEQUEUE
		pSnes->SyncPPU();
		#endif
		return pPPURegs->mpyl;
	case 0x2135: //mpym
		#if SNPPU_WRITEQUEUE
		pSnes->SyncPPU();
		#endif
		return pPPURegs->mpym;
	case 0x2136: //mpyh
		#if SNPPU_WRITEQUEUE
		pSnes->SyncPPU();
		#endif
		return pPPURegs->mpyh;
	case 0x2139:
		#if SNPPU_WRITEQUEUE
		pSnes->SyncPPU();
		#endif
		return pSnes->m_PPU.ReadVMDATAL();
	case 0x213a:	// vmdatah (video port data hi)
		#if SNPPU_WRITEQUEUE
		pSnes->SyncPPU();
		#endif
		return pSnes->m_PPU.ReadVMDATAH();

	case 0x213b:
		#if SNPPU_WRITEQUEUE
		pSnes->SyncPPU();
		#endif
		return pSnes->m_PPU.ReadCGDATA();

	case 0x2180:	// WMDATA
		{
			Uint8 uData;
			// read from ram
			uData = pSnes->m_Ram[pSnes->m_IO.m_Regs.wmadd];
			// increment memory address
			pSnes->m_IO.m_Regs.wmadd++;
			pSnes->m_IO.m_Regs.wmadd &= 0x1FFFF;
			return uData;
		}

	default:
		#if SNES_DEBUG
        if (Snes_bDebugUnhandledIO)
            SnesDebugRead(uAddr);
		#endif
		break;
	}

	return uAddr >> 8;
}

//#define SNES_SPCWRITE_LATENCY (21)
#define SNES_SPCWRITE_LATENCY (0)

#if SNES_DEBUG
void SNCPU_TRAPFUNC SnesSystem::Write2000Debug(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    if (Snes_bDebugIO)
        SnesDebugWrite(uAddr, uData);
    Write2000(pCpu, uAddr, uData);
}
#endif

void SNCPU_TRAPFUNC SnesSystem::Write2000(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;

	uAddr &= 0xFFFF;

	// S-RTC: relogio em $2801 (escrita)
	if (pSnes->m_bSRTC && uAddr == 0x2801)
	{
		pSnes->m_SRTC.WriteReg(uData);
		return;
	}

	// SuperFX/GSU: registradores em $3000-34FF (ver Read2000). Escrever
	// R15.MSB ($301F) liga GO; a execucao acontece em fatias no fim de cada
	// scanline, evitando bloquear a EE por uma rotina inteira do coprocessador.
	if (pSnes->m_bSuperFX && uAddr >= 0x3000 && uAddr <= 0x34FF)
	{
		pSnes->m_GSU.WriteReg((Uint16)uAddr, uData);
		return;
	}

	// APUIO0-3 are mirrored every four bytes through $217F.  Route every
	// mirror through the same SPC write queue and timing path as $2140-43.
	if (uAddr >= 0x2140 && uAddr <= 0x217F)
	{
		#if SNSPCIO_WRITEQUEUE
		if (!pSnes->m_SpcIO.EnqueueWrite(
		        SNCPUGetCounter(pCpu, SNCPU_COUNTER_FRAME) + SNES_SPCWRITE_LATENCY,
		        uAddr & 3, uData))
		#endif
		{
			pSnes->SyncSPC(SNES_SPCWRITE_LATENCY);
			pSnes->m_SpcIO.m_Regs.apu_w[uAddr & 3] = uData;
		}
		return;
	}

	if (uAddr < 0x2140)
	{
		// enqueue write to ppu, if it fails (full) then force a sync 
		#if SNPPU_WRITEQUEUE
		while (!pSnes->m_PPU.EnqueueWrite(pSnes->m_uLine, uAddr, uData))
		{
			// sync ppu
			pSnes->SyncPPU();
		}
		#else
		// sync ppu before writing to it
		pSnes->SyncPPU();

		// ppu write
		pSnes->m_PPU.Write8(uAddr, uData);
		#endif
	} else
	{
		switch(uAddr)
		{
			case 0x2180:	// WMDATA
				// write directly to ram
				pSnes->m_Ram[pSnes->m_IO.m_Regs.wmadd] = uData;
				// increment memory address
				pSnes->m_IO.m_Regs.wmadd++;
				pSnes->m_IO.m_Regs.wmadd &= 0x1FFFF;
				break;
			case 0x2181:	// WMADDL
				pSnes->m_IO.m_Regs.wmadd &= ~0x0000FF;
				pSnes->m_IO.m_Regs.wmadd |= (uData & 0xFF) << 0;
				break;

			case 0x2182:	// WMADDM
				pSnes->m_IO.m_Regs.wmadd &= ~0x00FF00;
				pSnes->m_IO.m_Regs.wmadd |= (uData & 0xFF) << 8;
				break;

			case 0x2183:	// WMADDH
				pSnes->m_IO.m_Regs.wmadd &= ~0xFF0000;
				pSnes->m_IO.m_Regs.wmadd |= (uData & 0x01) << 16;
				break;

			case 0x2184:	// ?? dk country
				break;

			default:
				#if SNES_DEBUG
                if (Snes_bDebugUnhandledIO)
                    SnesDebugWrite(uAddr, uData);
				#endif
				break;
		}
	}
}

#if CODE_DEBUG
Uint8 _CPUHackMem[0x10000];
#endif


#if SNES_DEBUG
Uint8 SNCPU_TRAPFUNC SnesSystem::Read4000Debug(SNCpuT *pCpu, Uint32 uAddr)
{
    Uint8 uData = Read4000(pCpu, uAddr);
    if (Snes_bDebugIO)
        SnesDebugReadData(uAddr, uData);
    return uData;
}
#endif

Uint8 SNCPU_TRAPFUNC SnesSystem::Read4000(SNCpuT *pCpu, Uint32 uAddr)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	SnesIO *pIO = &pSnes->m_IO;

	uAddr &= 0xFFFF;

	if (uAddr >= 0x4300 && uAddr < 0x4380)
	{
		// read from DMA controller
		return pSnes->m_DMAC.Read8((uAddr>>4) & 7, uAddr & 0xF);
	} else
	if (pSnes->m_bSDD1 && uAddr >= 0x4800 && uAddr <= 0x4807)
	{
		// S-DD1 registradores
		return pSnes->m_SDD1.ReadReg(uAddr);
	} else
	switch (uAddr)
	{
    //
    // 40XX
    //
    case 0x4016:	// serial joystick port
        SNCPUConsumeCycles(&pSnes->m_Cpu, 2);       //access from 4000>41FF is 1.78mhz
        return pIO->ReadSerial0();
    case 0x4017:
        SNCPUConsumeCycles(&pSnes->m_Cpu, 2);       //access from 4000>41FF is 1.78mhz
        return pIO->ReadSerial1();

    //
    // 42XX
    //
    case 0x4202:	// wrmpya (multiplicand-a)
		return 0; // ? pIO->m_Regs.wrmpya
	case 0x4203:	// wrmpyb (multiplicand-b)
		return 0; // ? pIO->m_Regs.wrmpyb = uData;

    case 0x420B:	// mdmaen (DMA enable register)
        return pSnes->m_DMAC.GetMDMAEnable();
    case 0x420C:
        return pSnes->m_DMAC.GetHDMAEnable();

    case 0x4210:	// RDNMI
        {
            Uint8 uData = pIO->m_Regs.rdnmi;

            // clear RDNMI on read
            pIO->m_Regs.rdnmi &= ~0x80;

            // set new nmi signal
            SNCPUSignalNMI(pCpu, pIO->m_Regs.rdnmi & pIO->m_Regs.nmitimen & 0x80);
            return uData;
        }

    case 0x4211:	// TIMEUP 
        {
            Uint8 uData = pIO->m_Regs.timeup;
            pIO->m_Regs.timeup &= ~0x80;
            SNCPUSignalIRQ(pCpu, 0);
            return uData;
        }
    case 0x4212:	// HVBJOY
        {
            /* Aero the Acro-Bat 2 polls VBlank around the frame wrap.
               Line 0 is not part of VBlank even though the PPU does not draw
               it.  Derive bit 7 from the live vertical counter so a stale
               latched status (for example after restoring state) cannot keep
               the game waiting forever. */
            Uint8 uData = pIO->m_Regs.hvbjoy & (Uint8)~0x80;
            if (SNES_LINE_IN_VBLANK(pSnes->m_uLine))
                uData |= 0x80;
            return uData;
        }

    case 0x4213:	// RDIO
        return 0;

	case 0x4214:	// RDDIVL
		return pIO->m_Regs.rddiv.b.l;

	case 0x4215:	// RDDIVH
		return pIO->m_Regs.rddiv.b.h;

	case 0x4216:	// RDMPYL
		return pIO->m_Regs.rdmpy.b.l;

	case 0x4217:	// RDMPYH
		return pIO->m_Regs.rdmpy.b.h;

	case 0x4218:	// JOY1L
		return pIO->m_Regs.joy1.b.l;
	case 0x4219:	// JOY1H
		return pIO->m_Regs.joy1.b.h;

	case 0x421A:	// JOY2L
		return pIO->m_Regs.joy2.b.l;
	case 0x421B:	// JOY2H
		return pIO->m_Regs.joy2.b.h;

	case 0x421C:	// JOY3L
		return pIO->m_Regs.joy3.b.l;
	case 0x421D:	// JOY3H
		return pIO->m_Regs.joy3.b.h;

	case 0x421E:	// JOY4L
		return pIO->m_Regs.joy4.b.l;
	case 0x421F:	// JOY4H
		return pIO->m_Regs.joy4.b.h;

	default:
		break;
	}
	#if SNES_DEBUG
    if (Snes_bDebugUnhandledIO)
	    SnesDebugRead(uAddr);
	#endif
	return uAddr >> 8;
}

#if SNES_DEBUG
void SNCPU_TRAPFUNC SnesSystem::Write4000Debug(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    if (Snes_bDebugIO)
        SnesDebugWrite(uAddr, uData);

    _CPUHackMem[uAddr & 0xFFFF]= uData;

    Write4000(pCpu, uAddr, uData);
}
#endif

void SNCPU_TRAPFUNC SnesSystem::Write4000(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;

	uAddr &= 0xFFFF;

	if (uAddr >= 0x4300 && uAddr < 0x4380)
	{
		// write to DMA controller
		pSnes->m_DMAC.Write8((uAddr>>4) & 7, uAddr & 0xF, uData);
	} else
	if (pSnes->m_bSDD1 && uAddr >= 0x4800 && uAddr <= 0x4807)
	{
		// S-DD1 registradores; $4804-$4807 mudam o mapa de bancos $C0-$FF
		pSnes->m_SDD1.WriteReg(uAddr, uData);
		if (pSnes->m_SDD1.MapDirty())
		{
			pSnes->RemapSDD1();
			pSnes->m_SDD1.ClearMapDirty();
		}
	} else
	{
		SnesIO *pIO = &pSnes->m_IO;

		switch (uAddr)
		{
        case 0x4016:	// reset serial joystick port?
            SNCPUConsumeCycles(&pSnes->m_Cpu, 2);       //access from 4000>41FF is 1.78mhz
            pIO->WriteSerial(uData);
            break;

        case 0x4200:	// nmitimen
        {
			Uint8 uOldNmitimen = pIO->m_Regs.nmitimen;
            pIO->m_Regs.nmitimen = uData;

            // unconfirmed:
            // should nmi be disabled if someone set 0 to nmitimen register?
            // this breaks final fight
            // has nmi enable been set to zero?
            if ( !( uData & 0x80 ))
            {
                // if so, disable 'BLANK NMI' for good
                if ( pIO->m_Regs.rdnmi & 0x80 )
                {
//                    pIO->m_Regs.rdnmi &= ~0x80;
                }
            }

            // have v-en and h-en been set to zero?
            if ( !(uData & (0x20|0x10)) )
            {
                if ( pIO->m_Regs.timeup & 0x80 )
                {
                    // remove timeup bit
                    pIO->m_Regs.timeup &= ~0x80;
                }
                // clear IRQ
                SNCPUSignalIRQ(pCpu, 0);
            }

            // set new nmi signal
            SNCPUSignalNMI(pCpu, pIO->m_Regs.rdnmi & pIO->m_Regs.nmitimen & 0x80);
			if ((uOldNmitimen ^ uData) & 0x30)
				pSnes->RescheduleLineIRQ(TRUE);
            break;
		}

        case 0x4201:	// wrio (programmable i/o port)
            // confirmed:
            // setting this to a value will cause the h/v latching to be controlled
            // by external latching (v-count seemed to stick at E1)
            pIO->m_Regs.wrio = uData;
            break;

		case 0x4202:	// wrmpya (multiplicand-a)
			pIO->m_Regs.wrmpya = uData;
			break;
		case 0x4203:	// wrmpyb (multiplicand-b)
			pIO->m_Regs.wrmpyb = uData;

			// multiply
			pIO->m_Regs.rdmpy.w = pIO->m_Regs.wrmpya * pIO->m_Regs.wrmpyb;
			break;

		case 0x4204:	// wrdivl (multiplier-c low)
			pIO->m_Regs.wrdiv.b.l = uData;
			break;
		case 0x4205:	// wrdivh (multiplier-c high)
			pIO->m_Regs.wrdiv.b.h = uData;
			break;
		case 0x4206:	// wrdivb (divisor-b)
			pIO->m_Regs.wrdivb  = uData;
			if (uData!=0)
			{
				pIO->m_Regs.rddiv.w = pIO->m_Regs.wrdiv.w / pIO->m_Regs.wrdivb;
				pIO->m_Regs.rdmpy.w = pIO->m_Regs.wrdiv.w % pIO->m_Regs.wrdivb;
			} else
			{
                // divide by zero
				pIO->m_Regs.rddiv.w = 0xFFFF;
				pIO->m_Regs.rdmpy.w = pIO->m_Regs.wrdiv.w;
			}
			break;

        case 0x4207:	// htmel (video horizontal IRQ beam position)
            pIO->m_Regs.htime.b.l = uData;
			pSnes->RescheduleLineIRQ(FALSE);
            break;
        case 0x4208:	// htmeh (video horizontal IRQ beam position)
            pIO->m_Regs.htime.b.h = uData & 1;
			pSnes->RescheduleLineIRQ(FALSE);
            break;
        case 0x4209:	// vtmel (video vertical IRQ beam position)
            pIO->m_Regs.vtime.b.l = uData;
			pSnes->RescheduleLineIRQ(FALSE);
            break;
        case 0x420A:	// vtmeh (video vertical IRQ beam position)
            pIO->m_Regs.vtime.b.h = uData & 1;
			pSnes->RescheduleLineIRQ(FALSE);
            break;

		case 0x420B:	// mdmaen (DMA enable register)
			pSnes->m_DMAC.SetMDMAEnable(uData);
			if (uData != 0)
			{
                // we've got DMA, so abort execution
                SNCPUSignalDMA(pCpu, 1);
			}
			break;

		case 0x420C:	// hdmaen (HDMA enable register)
			pSnes->m_DMAC.SetHDMAEnable(uData);
			break;

        case 0x420D:	// memsel (cycle speed register)
            if ((pIO->m_Regs.memsel ^ uData) & 1)
            {
                if (uData & 1)
                {
                    pSnes->SetFastRom();
                } else
                {
                    pSnes->SetSlowRom();
                }
                pIO->m_Regs.memsel = uData;
            }
            break;

		case 0x4211:	// TIMEUP 
			pIO->m_Regs.timeup &= ~0x80;
			SNCPUSignalIRQ(pCpu, 0);
			break;

		case 0x4212:	// HVBJOY
			break;

		case 0x4214:	// RDDIVL
		case 0x4215:	// RDDIVH
		case 0x4216:	// RDMPYL
		case 0x4217:	// RDMPYH 
			break; // mario ??

		default:
			#if SNES_DEBUG
            if (Snes_bDebugUnhandledIO)
			    SnesDebugWrite(uAddr, uData);
			#endif
			break;
		}
	}

}

Uint8 SNCPU_TRAPFUNC SnesSystem::ReadMem(SNCpuT *pCpu, Uint32 uAddr)
{
//	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	#if SNES_DEBUG
    if (Snes_bDebugUnhandledIO)
	SnesDebugRead(uAddr);
	#endif
	return 0;
}


void SNCPU_TRAPFUNC SnesSystem::WriteMem(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
//	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	#if SNES_DEBUG
    if (Snes_bDebugUnhandledIO)
        SnesDebugWrite(uAddr, uData);
	#endif
}

Uint8 SNCPU_TRAPFUNC SnesSystem::ReadSRAM(SNCpuT *pCpu, Uint32 uAddr)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	Uint8 *pSRAM =  pSnes->GetSRAM();
	return pSRAM[uAddr & (pSnes->m_uSramSize-1)];
}


void SNCPU_TRAPFUNC SnesSystem::WriteSRAM(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	Uint8 *pSRAM =  pSnes->GetSRAM();

	pSRAM[uAddr & (pSnes->m_uSramSize-1)] = uData;
}

#ifdef SNES_DSP1

// Decodifica se o endereco do DSP-1 e' o Status Register (SR) ou o Data
// Register (DR).  O bit de selecao DR/SR DEPENDE do mapeamento:
//   - HiROM/$6000:  DR=$6000-$6FFF, SR=$7000-$7FFF  (bit 0x1000)
//   - LoROM/$8000:  DR=$8000-$BFFF, SR=$C000-$FFFF  (bit 0x4000)
// O decode antigo usava (uAddr & 0xE000), que mascara o bit 0x1000 e
// portanto fazia $7000 (SR) cair no caso do DR -> as leituras de status
// do jogo eram servidas como DADOS e AVANCAVAM a FSM do DSP, perdendo
// sincronia (matriz Mode-7 lixo / pista achatada).
static inline Bool _SnesDsp1IsStatus(Uint32 uAddr)
{
	Uint16 a = (Uint16)(uAddr & 0xFFFF);
	if (a < 0x8000)
		return (a & 0x1000) ? TRUE : FALSE;   // $6000=DR  $7000=SR
	else
		return (a & 0x4000) ? TRUE : FALSE;   // $8000=DR  $C000=SR
}

Uint8 SNCPU_TRAPFUNC SnesSystem::ReadDSP1(SNCpuT *pCpu, Uint32 uAddr)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;

	// Guarda anti-crash: se o chip nao esta ligado (ex.: jogo de
	// DSP-3/DSP-4 sem o firmware correspondente), nao ha objeto DSP.
	// Devolve um status "ocupado/sem RQM" e dados 0 em vez de
	// dereferenciar um ponteiro nulo.  (O mapa de memoria normalmente
	// nem mapeia a regiao quando nao ha chip, mas isto e' a rede de
	// seguranca para qualquer caminho residual.)
	if (!pSnes->m_pDsp)
		return _SnesDsp1IsStatus(uAddr) ? 0x80 : 0x00;

	if (_SnesDsp1IsStatus(uAddr))
	{
		Uint8 s = pSnes->m_pDsp->ReadStatus(uAddr);
#if SNDBG_LOG
		g_TmgDspRd++;
#endif
		return s;
	}
	else
	{
		Uint8 d = pSnes->m_pDsp->ReadData(uAddr);
#if SNDBG_LOG
		g_TmgDspRd++;
#endif
		return d;
	}
}


void SNCPU_TRAPFUNC SnesSystem::WriteDSP1(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;

	// Guarda anti-crash: sem chip DSP ligado, ignora a escrita.
	if (!pSnes->m_pDsp)
		return;

	// Escritas vao para o DR; o SR e' somente leitura.
	if (!_SnesDsp1IsStatus(uAddr))
	{
#if SNDBG_LOG
		g_TmgDspWr++;
#endif
		pSnes->m_pDsp->WriteData(uAddr, uData);
	}
}

#endif


Uint8 SNCPU_TRAPFUNC SnesSystem::ReadGSU(SNCpuT *pCpu, Uint32 uAddr)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	return pSnes->m_GSU.ReadReg((Uint16)(uAddr & 0xFFFF));
}

void SNCPU_TRAPFUNC SnesSystem::WriteGSU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	pSnes->m_GSU.WriteReg((Uint16)(uAddr & 0xFFFF), uData);
}


Uint8 SNCPU_TRAPFUNC SnesSystem::ReadOBC1(SNCpuT *pCpu, Uint32 uAddr)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	return pSnes->m_OBC1.Read(uAddr & 0xFFFF);
}

void SNCPU_TRAPFUNC SnesSystem::WriteOBC1(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	pSnes->m_OBC1.Write(uAddr & 0xFFFF, uData);
}


// CX4 (Mega Man X2/X3). O chip le dados direto da ROM (vertices, sprites,
// blocos de DMA); o callback resolve um endereco SNES de 24 bits via a CPU.
Uint8 SnesSystem::CX4ReadMem(void *pCtx, Uint32 uAddr)
{
	return SNCPUPeek8((SNCpuT *)pCtx, uAddr);
}

Uint8 SNCPU_TRAPFUNC SnesSystem::ReadCX4(SNCpuT *pCpu, Uint32 uAddr)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	return pSnes->m_CX4.Read(uAddr & 0xFFFF);
}

void SNCPU_TRAPFUNC SnesSystem::WriteCX4(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	SnesSystem *pSnes = (SnesSystem *)pCpu->pUserData;
	pSnes->m_CX4.Write(uAddr & 0xFFFF, uData);
}


//
//
//








//
//
//

SnesSystem::SnesSystem()
{
	m_pRom			= NULL;

	// setup cpu
	SNCPUNew(&m_Cpu);
	m_Cpu.pUserData = (void *)this;

	m_uSramSize = 0;
	m_bDynamicHVIRQ = FALSE;
	m_bLineIRQActive = FALSE;
	m_bLineIRQReschedule = FALSE;
	m_bLineIRQFired = FALSE;
	m_bLineIRQInstant = FALSE;
	m_nLineIRQCycle = -1;
	m_nLineIRQClock = 0;

	// setup spc
	SNSPCNew(&m_Spc);
	SNSPCSetTrapFunc(&m_Spc, SNSpcIO::Read8Trap, SNSpcIO::Write8Trap);
	m_Spc.pUserData  = (void *)&m_SpcIO;
	m_SpcIO.SetSpc(&m_Spc);
	m_SpcIO.SetSpcDsp(&m_SpcDsp);

	// link dsp with mixer(s)
	m_SpcDsp.SetMixer(0,&m_SpcDspMixer);
	m_SpcDsp.SetMixer(1,&m_SpcDspSilentMixer);
	m_SpcDspMixer.SetDsp(&m_SpcDsp);
	m_SpcDspSilentMixer.SetDsp(&m_SpcDsp);
	m_SpcDsp.SetMem(m_Spc.Mem);

	// setup dma controller
	m_DMAC.SetCPU(&m_Cpu);
	m_DMAC.SetPPU(&m_PPU);
	m_DMAC.SetSDD1(&m_SDD1);

	m_bSDD1 = FALSE;

	// setup ppu
	m_PPURender.SetPPU(&m_PPU);
	m_PPU.SetPPURender(&m_PPURender);

#if SNES_DSP1
	m_pDsp = NULL;
#endif
}

SnesSystem::~SnesSystem()
{
	SetRom(NULL);
	SNCPUDelete(&m_Cpu);
	SNSPCDelete(&m_Spc);
#if SNES_DSP1
	m_pDsp = NULL;
#endif 
}

void SnesSystem::Reset()
{
	SetSlowRom();

	m_PPU.Reset();
	m_DMAC.Reset();
	m_IO.Reset();
	m_SpcIO.Reset();
	m_SpcDsp.Reset();
	m_SpcDspMixer.Reset();
	m_SpcDspSilentMixer.Reset();

#ifdef SNES_DSP1
	if (m_pDsp)
	{
		m_pDsp->Reset();
	}
#endif 

	m_OBC1.Reset();

	m_CX4.Reset();

	m_GSU.Reset();

	m_SDD1.Reset();

	// So' o cartucho com S-RTC deve tocar o relogio do host (time/gmtime).
	// Antes isso rodava no boot de TODO jogo -> se time()/gmtime() falhar no
	// PS2 real, quebraria o boot de qualquer jogo.
	if (m_bSRTC)
		m_SRTC.Reset();

#if CODE_DEBUG
	memset(_CPUHackMem, 0, sizeof(_CPUHackMem));
#endif

	memset(m_Ram, 0, sizeof(m_Ram));
	memset(m_SRam, 0, sizeof(m_SRam));

	// reset cpu
	SNCPUReset(&m_Cpu, true);
	SNSPCReset(&m_Spc, true);

	m_uFrame=0;
	m_uLine =0;
	m_bLineIRQActive = FALSE;
	m_bLineIRQReschedule = FALSE;
	m_bLineIRQFired = FALSE;
	m_bLineIRQInstant = FALSE;
	m_nLineIRQCycle = -1;
	m_nLineIRQClock = 0;
}

void SnesSystem::SoftReset()
{
	// reset cpu
	SNCPUReset(&m_Cpu, false);
	SNSPCReset(&m_Spc, false);
}


void SnesSystem::SetRom(class Emu::Rom *pRom)
{
	SetSnesRom((SnesRom *)pRom);
}

void SnesSystem::SetSnesRom(SnesRom *pRom)
{
	if (m_pRom)
	{
		// disconnect from current rom
		m_pRom = NULL;
	}
#ifdef SNES_DSP1
	m_pDsp = NULL;
	m_DSP1.SetTargetYSubtract(FALSE);
	m_DSP1.SetOriginalDistanceBug(FALSE);
#endif 
	m_bDynamicHVIRQ = FALSE;
	// set rom
	m_pRom = pRom;

	if (m_pRom)
	{
		m_bDynamicHVIRQ =
			(m_pRom->m_Flags & SNROM_FLAG_PILOTWINGS_DYNAMIC_HVIRQ)
				? TRUE : FALSE;
		#ifdef SNES_DSP1
		m_DSP1.SetTargetYSubtract(
			(m_pRom->m_Flags & SNROM_FLAG_DSP1_TARGET_Y_SUBTRACT)
				? TRUE : FALSE);
		m_DSP1.SetOriginalDistanceBug(
			(m_pRom->m_Flags & SNROM_FLAG_DSP1_ORIGINAL_OP28)
				? TRUE : FALSE);
		#endif
		// setup memory mapping for this rom
		MapMem(m_pRom->m_eMapping, m_pRom->m_Flags);
	} 
	else
	{
		// reset mapping
		SNCPUSetBank(&m_Cpu, 0, SNCPU_MEM_SIZE, NULL, TRUE);
		SNCPUSetTrap(&m_Cpu, 0, SNCPU_MEM_SIZE, NULL, NULL);
		m_uSramSize = 0;
	}
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////



Int32 SnesSystem::CalculateLineIRQCycle()
{
	const Bool bH = (m_IO.m_Regs.nmitimen & 0x10) ? TRUE : FALSE;
	const Bool bV = (m_IO.m_Regs.nmitimen & 0x20) ? TRUE : FALSE;

	if (bV)
	{
		if (m_uLine != m_IO.m_Regs.vtime.w)
			return -1;
		return bH ? SNES_HIRQ_CYCLES(m_IO.m_Regs.htime.w)
		          : SNES_VIRQ_CYCLES;
	}

	return bH ? SNES_HIRQ_CYCLES(m_IO.m_Regs.htime.w) : -1;
}


void SnesSystem::RescheduleLineIRQ(Bool bAllowImmediate)
{
	/* Pilotwings changes $4200/$4207-$420A after ExecuteLine() already
	   calculated the timer event. Stop the CPU batch and give its unused
	   clocks back so the new compare can take effect on this scanline. */
	if (!m_bDynamicHVIRQ || !m_bLineIRQActive)
		return;

	m_nLineIRQCycle = CalculateLineIRQCycle();
	m_bLineIRQFired = FALSE;
	m_bLineIRQInstant = bAllowImmediate;
	m_bLineIRQReschedule = TRUE;
#if SNDBG_LOG
	g_TmgIrqRearms++;
#endif
	SNCPUAbort(&m_Cpu);
}


void SnesSystem::ExecuteCPU(Int32 nCycles)
{
    // increment cycle counter
    SNCPUAddCycles( &m_Cpu, nCycles );

    while (m_Cpu.Cycles > 0)
    {
		/* STP ignores IRQ/NMI and remains halted until reset.  Consume the
		   scheduler budget so the PPU and APU keep advancing. */
		if ((m_Cpu.uSignal & SNCPU_SIGNAL_STP) &&
			!(m_Cpu.uSignal & SNCPU_SIGNAL_RESET))
		{
			m_Cpu.Cycles = 0;
			break;
		}

        // process signal
        if (m_Cpu.uSignal & (SNCPU_SIGNAL_IRQ | SNCPU_SIGNAL_NMIEDGE | SNCPU_SIGNAL_RESET | SNCPU_SIGNAL_DMA))
        {
            if (m_Cpu.uSignal & SNCPU_SIGNAL_DMA)
            {
                // sync up PPU before DMA (only necessary for read dmas?)
                SyncPPU();

                PROF_ENTER("ProcessMDMA");
#if SNDBG_LOG
				Uint32 _tMDMA = ProfCtrGetCycle();
#endif
                // so, execute DMA for remainder of CPU time
                // this function automatically subtracts from the CPU cycle count as it transfers each byte
#if 1
                m_DMAC.ProcessMDMA();
#else
                {
                    int n = m_Cpu.Cycles;
                    m_Cpu.Cycles = 100000;
                    m_DMAC.ProcessMDMA();
                    m_Cpu.Cycles += n - 100000;
                }
#endif
#if SNDBG_LOG
				g_TmgCycMDMA += ProfCtrGetCycle() - _tMDMA;
#endif
                PROF_LEAVE("ProcessMDMA");

                // are all MDMAs complete?
                if (m_DMAC.GetMDMAEnable() == 0)
                {
                    // remove DMA signal
                    m_Cpu.uSignal &= ~SNCPU_SIGNAL_DMA;

                    // very interesting
                    #if SNES_DEBUG
                    if (m_Cpu.uSignal != 0)
                    {
                        // this means that a nmi occurred while we were in an dma
                        ConDebug("interesting");
                    }
                    #endif
                } 

                // continue...dont allow CPU to run unless it has cycle time available
                continue;
            } else
            if (m_Cpu.uSignal & SNCPU_SIGNAL_NMIEDGE)
            {
				if (m_Cpu.uNmiDmaDelay)
				{
					/* An NMI captured while MDMA owns the bus is released 24-30
					   master clocks after DMA.  Run ordinary CPU work during that
					   window, as the hardware does, and carry a partial delay into
					   the next scheduler slice when necessary. */
					Int32 nStartCycles = m_Cpu.Cycles;
					if (m_Cpu.uSignal & SNCPU_SIGNAL_WAI)
					{
						Int32 nDelay = m_Cpu.uNmiDmaDelay;
						if (nDelay > m_Cpu.Cycles) nDelay = m_Cpu.Cycles;
						SNCPUConsumeCycles(&m_Cpu, nDelay);
					}
					else
					{
						while (m_Cpu.Cycles > 0 &&
							(nStartCycles - m_Cpu.Cycles) < m_Cpu.uNmiDmaDelay &&
							!(m_Cpu.uSignal & SNCPU_SIGNAL_DMA))
						{
							SNCPUExecuteOne(&m_Cpu);
						}
					}

					Int32 nElapsed = nStartCycles - m_Cpu.Cycles;
					if (nElapsed >= m_Cpu.uNmiDmaDelay)
						m_Cpu.uNmiDmaDelay = 0;
					else if (nElapsed > 0)
						m_Cpu.uNmiDmaDelay -= (Uint8)nElapsed;

					if (m_Cpu.uNmiDmaDelay ||
						(m_Cpu.uSignal & SNCPU_SIGNAL_DMA))
						continue;
				}
				else
				{
					/* The S-CPU sees the vblank NMI edge around H=6 and enters
					   the vector after the instruction then in progress. */
					if (!(m_Cpu.uSignal & SNCPU_SIGNAL_WAI))
					{
						SNCPUExecuteOne(&m_Cpu);
						if (m_Cpu.uSignal & SNCPU_SIGNAL_DMA)
						{
							m_Cpu.uNmiDmaDelay = 24;
							continue;
						}
					}
				}

                SNCPUNMI(&m_Cpu);
                // clear NMI edge signal
                m_Cpu.uSignal&= ~SNCPU_SIGNAL_NMIEDGE;
            } else
            if (m_Cpu.uSignal & SNCPU_SIGNAL_IRQ)
            {
				/* Opcode-based cores cannot observe a timer edge in the middle
				   of an instruction.  A game-specific line-zero V-IRQ may leave
				   a short pending window so the polling opcode(s) retire first. */
				if (SNCPUExecuteIRQDelay(&m_Cpu))
					continue;

                // attempt irq
                // irqs will always be attempted until signal has been cleared
                SNCPUIRQ(&m_Cpu);
            } else
            if (m_Cpu.uSignal & SNCPU_SIGNAL_RESET)
            {
                // perform reset (soft)
                SNCPUReset(&m_Cpu, FALSE);

                m_Cpu.uSignal&= ~SNCPU_SIGNAL_RESET;
            } 
        }			

        assert(m_DMAC.GetMDMAEnable() == 0);

        // run CPU!
        SNCPUExecute(&m_Cpu);
		if (m_bLineIRQActive && m_bLineIRQReschedule)
			break;
    }
}



void SnesSystem::ExecuteWithIRQ(Int32 nCycles, Int32 &nIRQCycles)
{
	if (m_bLineIRQActive)
	{
		Int32 nRemaining = nCycles;

		while (nRemaining > 0)
		{
			Int32 nNow = m_nLineIRQClock;

			/* Toggling H/V IRQ mode after the compare point raises TIMEUP
			   immediately. Merely rewriting HTIME/VTIME to a stale point does
			   not manufacture a second IRQ on this scanline. */
			if (!m_bLineIRQFired && m_nLineIRQCycle >= 0 &&
				m_nLineIRQCycle <= nNow)
			{
				m_bLineIRQFired = TRUE;
				m_nLineIRQCycle = -1;
				if (m_bLineIRQInstant)
				{
					m_IO.m_Regs.timeup |= 0x80;
					SNCPUSetIRQDelay(&m_Cpu, 0);
					SNCPUSignalIRQ(&m_Cpu, 1);
#if SNDBG_LOG
					g_TmgIrqInstant++;
#endif
				}
				continue;
			}

			Int32 nRun = nRemaining;
			Bool bReachIRQ = FALSE;

			if (!m_bLineIRQFired && m_nLineIRQCycle >= 0)
			{
				Int32 nToIRQ = m_nLineIRQCycle - nNow;
				if (nToIRQ > 0 && nToIRQ <= nRun)
				{
					nRun = nToIRQ;
					bReachIRQ = TRUE;
				}
			}

			m_bLineIRQReschedule = FALSE;
			ExecuteCPU(nRun);

			if (m_bLineIRQReschedule)
			{
				/* SNCPUAbort returns the clocks that were reserved but not run.
				   Remove them from both the live budget and every counter before
				   rebuilding the timer split. */
				Int32 nUnspent = m_Cpu.Cycles;
				if (nUnspent < 0)
					nUnspent = 0;
				if (nUnspent > nRun)
					nUnspent = nRun;

				if (nUnspent > 0)
				{
					m_Cpu.Cycles -= nUnspent;
					for (Int32 i = 0; i < SNCPU_COUNTER_NUM; ++i)
						m_Cpu.Counter[i] -= nUnspent;
				}

				Int32 nSpent = nRun - nUnspent;
				if (nSpent < 0)
					nSpent = 0;
				m_nLineIRQClock += nSpent;
				nRemaining -= nSpent;
				m_bLineIRQReschedule = FALSE;
				continue;
			}

			m_nLineIRQClock += nRun;
			nRemaining -= nRun;

			if (bReachIRQ && !m_bLineIRQFired &&
				m_nLineIRQCycle >= 0)
			{
				m_bLineIRQFired = TRUE;
				m_nLineIRQCycle = -1;
				m_IO.m_Regs.timeup |= 0x80;
				SNCPUSetIRQDelay(&m_Cpu, 0);
				SNCPUSignalIRQ(&m_Cpu, 1);
			}
		}

		nIRQCycles -= nCycles;
		return;
	}

    if (nIRQCycles >= 0 && nIRQCycles < nCycles)
    {
        // execute up to h-irq
        ExecuteCPU(nIRQCycles);

        // set irq flag 
        m_IO.m_Regs.timeup |= 0x80;

		/* Aero the Acro-Bat 2 waits for $4212.VBlank to clear while its
		   V-only timer targets line 0.  The real timer edge occurs inside an
		   opcode; entering the handler before that polling opcode retires
		   leaves the game in the black-screen loop after the intro.  Snes9x's
		   opcode core uses the same two-opcode deferral for this title. */
		const char *pTitle = m_pRom ? m_pRom->GetRomTitle() : NULL;
		if (m_uLine == 0 &&
			(m_IO.m_Regs.nmitimen & 0x30) == 0x20 &&
			m_IO.m_Regs.vtime.w == 0 &&
			pTitle &&
			(strcmp(pTitle, "Aero the AcroBat 2") == 0 ||
			 strcmp(pTitle, "AERO THE ACROBAT 2") == 0))
		{
			SNCPUSetIRQDelay(&m_Cpu, 2);
		}
		else
		{
			SNCPUSetIRQDelay(&m_Cpu, 0);
		}
        SNCPUSignalIRQ(&m_Cpu, 1);

        // execute rest of way
        ExecuteCPU(nCycles - nIRQCycles);
    }
    else
    {
        // just execute as normal
        ExecuteCPU(nCycles);
    }

    nIRQCycles -= nCycles;
}



void SnesSystem::ExecuteLine()
{
	SNCPUResetCounter(&m_Cpu, SNCPU_COUNTER_LINE);

    // don't trigger IRQ by default
    int nHIRQCycles = -1;

	// virq enabled?
	if (m_IO.m_Regs.nmitimen & 0x20)
	{
		if (m_uLine == m_IO.m_Regs.vtime.w)
		{
			// hirq enabled?
			if (m_IO.m_Regs.nmitimen & 0x10)
			{
				// calculate cycle time to perform h-irq
				nHIRQCycles = SNES_HIRQ_CYCLES(m_IO.m_Regs.htime.w);

			} else
			{
				// trigger at beginning of line
				nHIRQCycles = SNES_VIRQ_CYCLES;
			}
		}
	} else
	// hirq enabled?
	if (m_IO.m_Regs.nmitimen & 0x10)
	{
		// calculate cycle time to perform h-irq
		nHIRQCycles = SNES_HIRQ_CYCLES(m_IO.m_Regs.htime.w);
	}

	m_bLineIRQActive = m_bDynamicHVIRQ;
	m_bLineIRQReschedule = FALSE;
	m_bLineIRQFired = FALSE;
	m_bLineIRQInstant = TRUE;
	m_nLineIRQCycle = nHIRQCycles;
	m_nLineIRQClock = 0;

#if SNDBG_LOG
	// rastreia a scanline em que um H-IRQ esta agendado (divisao de tela).
	// Se min..max variam muito frame a frame, a divisao "treme".
	if (nHIRQCycles >= 0)
	{
		Int32 ln = (Int32)m_uLine;
		if (ln < g_TmgIrqLineMin) g_TmgIrqLineMin = ln;
		if (ln > g_TmgIrqLineMax) g_TmgIrqLineMax = ln;
		g_TmgIrqCount++;
	}
#endif

	PROF_ENTER("ExecLine");

	SNCPUConsumeCycles(&m_Cpu, SNES_LINECYCLEDELAY);

    // execute CPU during scanline
#if SNDBG_LOG
	Uint32 _tCPU = ProfCtrGetCycle();
#endif
    ExecuteWithIRQ(SNES_CYCLESPERLINE - SNES_HBLANKCYCLES, nHIRQCycles);
#if SNDBG_LOG
	g_TmgCycCPU += ProfCtrGetCycle() - _tCPU;
#endif

	// set h-blank enable flag
	m_IO.m_Regs.hvbjoy|= 0x40;

    // are we not in vblank?
    if ( !(m_IO.m_Regs.hvbjoy & 0x80) )
    {
        // perform HDMA
#if SNDBG_LOG
		Uint32 _tHDMA = ProfCtrGetCycle();
#endif
        m_DMAC.ProcessHDMA(m_uLine);
#if SNDBG_LOG
		g_TmgCycHDMA += ProfCtrGetCycle() - _tHDMA;
#endif
    }

    // execute CPU during h-blank
#if SNDBG_LOG
	_tCPU = ProfCtrGetCycle();
#endif
    ExecuteWithIRQ(SNES_HBLANKCYCLES, nHIRQCycles);
#if SNDBG_LOG
	g_TmgCycCPU += ProfCtrGetCycle() - _tCPU;
#endif

	// O hardware GSU roda em paralelo com o 65816. Este emulador sincroniza
	// os dois uma vez por scanline, como uma aproximacao de baixo custo para
	// o PS2: ~370 instrucoes em 10,7 MHz e ~925 em 21,4 MHz.
	if (m_bSuperFX && m_GSU.IsRunning())
	{
#if SNDBG_LOG
		Uint32 _tGSU = ProfCtrGetCycle();
#endif
		m_GSU.Run(m_GSU.GetLineInstructionBudget());
#if SNDBG_LOG
		g_TmgCycGSU += ProfCtrGetCycle() - _tGSU;
#endif
	}
	if (m_bSuperFX && m_GSU.IrqPending())
		SNCPUSignalIRQ(&m_Cpu, 1);

	// clear h-blank enable flag
	m_IO.m_Regs.hvbjoy&= ~0x40;
	m_bLineIRQActive = FALSE;
	m_bLineIRQReschedule = FALSE;
	m_bLineIRQInstant = FALSE;
	m_nLineIRQCycle = -1;
	m_nLineIRQClock = SNES_CYCLESPERLINE;
	PROF_LEAVE("ExecLine");
}




void SnesSystem::ExecuteFrame(Emu::SysInputT  *pInput, CRenderSurface *pTarget, CMixBuffer *pSound, ModeE eMode)
{
    m_uLine = 0;

	/* The hidden frame-wrap line is line 0, not a continuation of VBlank.
	   Keep the internal HDMA gate in sync with the live $4212 value too. */
	m_IO.m_Regs.hvbjoy &= ~0x80;

#if SNDBG_LOG
	#if SNDBG_DEEP
	g_DbgCaptureFrameNo = g_TmgFrameNo + 1;
	g_DbgCaptureActive =
		(g_DbgCaptureFrameNo % (SNDBG_FRAME_PERIOD * 5u)) == 0;
	if (g_DbgCaptureActive)
	{
		DLog("[snes-capture] begin f=%u (light OBJ/DMA trace)",
			(unsigned)g_DbgCaptureFrameNo);
	}
	#else
	g_DbgCaptureActive = FALSE;
	#endif
	// --- timing: marca inicio do frame ---
	g_TmgFrameStart  = ProfCtrGetCycle();
	g_TmgCycM7  = 0;
	g_TmgCycObj = 0;
	g_TmgCycPPU = 0;
	g_TmgCycCPU = 0;
	g_TmgCycGSU = 0;
	g_TmgCycMDMA = 0;
	g_TmgCycHDMA = 0;
	g_TmgCycAPU = 0;
	g_TmgCycMix = 0;
	g_TmgCycBlend = 0;
	g_TmgCycPPUSync = 0;
	g_TmgCycBGInfo = 0;
	g_TmgCycBGOffset = 0;
	g_TmgCycBGMap = 0;
	g_TmgCycBGChr = 0;
	g_TmgCycBGMain = 0;
	g_TmgCycBGSub = 0;
	g_TmgCycColorMath = 0;
	g_TmgCycObjUpdate = 0;
	g_TmgCycObjFetch = 0;
	g_TmgCycObjDraw = 0;
	g_TmgCycHDMAData = 0;
	g_TmgCycHDMATable = 0;
	#if SNDBG_DEEP
	g_DbgFrameBaseOAM = g_DbgOAMWrites;
	g_DbgFrameBaseVRAM = g_DbgVRAMWrites;
	g_DbgFrameBaseCGRAM = g_DbgCGRAMWrites;
	g_DbgFrameBaseEnabled = g_DbgObjEnabledLines;
	g_DbgFrameBaseRefs = g_DbgObjOamRefs;
	g_DbgFrameBaseTiles = g_DbgObjTiles;
	g_DbgFrameBaseOpaque = g_DbgObjOpaqueTiles;
	g_DbgFrameBaseCandidate = g_DbgObjCandidatePixels;
	g_DbgFrameBaseDrawn = g_DbgObjDrawnPixels;
	#endif
#endif

	m_IO.LatchInput(pInput);

	// reset frame cycle counter
	SNCPUResetCounter(&m_Cpu, SNCPU_COUNTER_FRAME);
    SNCPUResetCounter(&m_Cpu, SNCPU_COUNTER_LINE);
	SNSPCResetCounter(&m_Spc, SNCPU_COUNTER_FRAME);

#if SNES_DEBUG
    if (Snes_bDebugFrame)
        SnesDebug("frame\n");
#endif

	m_DMAC.BeginHDMA();

	m_PPURender.BeginRender(pTarget);
	m_PPU.BeginFrame();
	
	for (m_uLine=0; m_uLine < (224+1); m_uLine++)
	{
		#if SNES_SYNCPPUEVERYLINE
		SyncPPU();
		#endif

		ExecuteLine();
	}

	// sync ppu at end of frame (this ensures all rendering has been completed)
	SyncPPU();

	m_PPU.EndFrame();
	m_PPURender.EndRender();

    //ExecuteLine();
    //ExecuteLine();

	// confirmed:
	// nmi is triggered from hi->lo transition (edge level interrupt)
	// rdnmi flag is set at beginning of vbl
	// rdnmi flag is cleared at end of vbl
	// rdnmi flag is cleared on rdnmi read
	// nmi will trigger immediately if nmitimen flag is set while rdnmi flag is set

    PROF_ENTER("ExecVBLANK");
    // set vbl flag at start of vblank
    m_IO.m_Regs.hvbjoy|= 0x80;

	if (m_IO.m_Regs.nmitimen & 1)
	{
		// set joy enable flag at start of vblank
		m_IO.m_Regs.hvbjoy|= 0x01;
		m_IO.UpdateJoyPads();
	}

    // set 'BLANK NMI' flag at beginning of v-blank
    m_IO.m_Regs.rdnmi |= 0x80;
    SNCPUSignalNMI(&m_Cpu, m_IO.m_Regs.rdnmi & m_IO.m_Regs.nmitimen & 0x80);

    for ( ; m_uLine < 262; m_uLine++)
	{
		ExecuteLine();

		if (m_uLine==225+2) // * 60 = 4410 cycles long (3.10 scanlines)
		{
			// done reading joypad
			m_IO.m_Regs.hvbjoy&= ~0x01;
		}
	}

    // clear 'BLANK NMI' flag at end of v-blank
    m_IO.m_Regs.rdnmi &= ~0x80;
    SNCPUSignalNMI(&m_Cpu, m_IO.m_Regs.rdnmi & m_IO.m_Regs.nmitimen & 0x80);

	// clear vbl flag at end of vblank
	m_IO.m_Regs.hvbjoy&= ~0x80;
	PROF_LEAVE("ExecVBLANK");

	//SNCPUConsumeCycles(&m_Cpu, SNES_CYCLESPERLINE);
	//SNCPUExecute(&m_Cpu, SNES_CYCLESPERLINE);

	SyncPPU();
	SyncSPC();

	// update spc timers
	SNSpcTimerSync(&m_SpcIO.m_Regs.spc_timer[0], SNSPCGetCounter(&m_Spc, SNSPC_COUNTER_TOTAL));
	SNSpcTimerSync(&m_SpcIO.m_Regs.spc_timer[1], SNSPCGetCounter(&m_Spc, SNSPC_COUNTER_TOTAL));
	SNSpcTimerSync(&m_SpcIO.m_Regs.spc_timer[2], SNSPCGetCounter(&m_Spc, SNSPC_COUNTER_TOTAL));

	// mix non-deterministic mixer
	PROF_ENTER("SNSpcDspUpdate");
#if SNDBG_LOG
	Uint32 _tMix = ProfCtrGetCycle();
#endif
	m_SpcDspMixer.Mix(pSound);
#if SNDBG_LOG
	g_TmgCycMix += ProfCtrGetCycle() - _tMix;
#endif
	PROF_LEAVE("SNSpcDspUpdate");

	// ensure that all queued registers have been committed 
	m_SpcDsp.Sync();

	switch (eMode)
	{
		case MODE_INACCURATEDETERMINISTIC:
			m_SpcDsp.UpdateFlags(NULL);
			break;
		case MODE_ACCURATENONDETERMINISTIC:
			// update flags based on real mixer (This should produce most accurate sound, but is not deterministic)
			m_SpcDsp.UpdateFlags(&m_SpcDspMixer);
			break;
		case MODE_ACCURATEDETERMINISTIC:
			// mix silent mixer
			PROF_ENTER("SNSpcDspUpdateSilent");
#if SNDBG_LOG
			_tMix = ProfCtrGetCycle();
#endif
			m_SpcDspSilentMixer.Mix(NULL);
#if SNDBG_LOG
			g_TmgCycMix += ProfCtrGetCycle() - _tMix;
#endif
			PROF_LEAVE("SNSpcDspUpdateSilent");

			// update spc flags based on deterministic mixer
			m_SpcDsp.UpdateFlags(&m_SpcDspSilentMixer);
			break;
	}

#if SNDBG_LOG
	// --- timing: fecha o frame e resume a janela ---
	{
		Uint32 cyc = ProfCtrGetCycle() - g_TmgFrameStart;  // ciclos de emulacao deste frame
		g_TmgWinSumCyc += cyc;
		g_TmgWinSumM7  += g_TmgCycM7;
		g_TmgWinSumObj += g_TmgCycObj;
		g_TmgWinSumPPU += g_TmgCycPPU;
		g_TmgWinSumCPU += g_TmgCycCPU;
		g_TmgWinSumGSU += g_TmgCycGSU;
		g_TmgWinSumMDMA += g_TmgCycMDMA;
		g_TmgWinSumHDMA += g_TmgCycHDMA;
		g_TmgWinSumAPU += g_TmgCycAPU;
		g_TmgWinSumMix += g_TmgCycMix;
		g_TmgWinSumBlend += g_TmgCycBlend;
		g_TmgWinSumPPUSync += g_TmgCycPPUSync;
		g_TmgWinSumBGInfo += g_TmgCycBGInfo;
		g_TmgWinSumBGOffset += g_TmgCycBGOffset;
		g_TmgWinSumBGMap += g_TmgCycBGMap;
		g_TmgWinSumBGChr += g_TmgCycBGChr;
		g_TmgWinSumBGMain += g_TmgCycBGMain;
		g_TmgWinSumBGSub += g_TmgCycBGSub;
		g_TmgWinSumColorMath += g_TmgCycColorMath;
		g_TmgWinSumObjUpdate += g_TmgCycObjUpdate;
		g_TmgWinSumObjFetch += g_TmgCycObjFetch;
		g_TmgWinSumObjDraw += g_TmgCycObjDraw;
		g_TmgWinSumHDMAData += g_TmgCycHDMAData;
		g_TmgWinSumHDMATable += g_TmgCycHDMATable;
		if (cyc > g_TmgWinMaxCyc) g_TmgWinMaxCyc = cyc;
		g_TmgWinFrames++;
		g_TmgFrameNo++;

		#if SNDBG_DEEP
		// Captura pontual quando pixels OBJ caem bruscamente apesar de ainda
		// haver sprites selecionados. Os hashes distinguem OAM/VRAM corrompida
		// de um erro posterior de mascara/prioridade no compositor.
		{
			Uint32 fOAM = g_DbgOAMWrites - g_DbgFrameBaseOAM;
			Uint32 fVRAM = g_DbgVRAMWrites - g_DbgFrameBaseVRAM;
			Uint32 fCGRAM = g_DbgCGRAMWrites - g_DbgFrameBaseCGRAM;
			Uint32 fEnabled = g_DbgObjEnabledLines - g_DbgFrameBaseEnabled;
			Uint32 fRefs = g_DbgObjOamRefs - g_DbgFrameBaseRefs;
			Uint32 fTiles = g_DbgObjTiles - g_DbgFrameBaseTiles;
			Uint32 fOpaque = g_DbgObjOpaqueTiles - g_DbgFrameBaseOpaque;
			Uint32 fCandidate = g_DbgObjCandidatePixels - g_DbgFrameBaseCandidate;
			Uint32 fDrawn = g_DbgObjDrawnPixels - g_DbgFrameBaseDrawn;
			Bool bCollapse = g_DbgPrevObjDrawn >= 512 &&
				fDrawn * 4u < g_DbgPrevObjDrawn && fEnabled && fRefs;
			Bool bEmpty = fEnabled && fRefs >= 64 && fTiles && !fOpaque;

			if (g_DbgObjEventCooldown) g_DbgObjEventCooldown--;
			if ((bCollapse || bEmpty) && !g_DbgObjEventCooldown)
			{
				const SnesPPURegsT *pr = m_PPU.GetRegs();
				Uint32 hOAM = SnesDbgHash32(m_PPU.GetOAM(), sizeof(SnesOAMT));
				Uint32 hVRAM = SnesDbgHash32(m_PPU.GetVramPtr(0), 0x10000);
				Uint32 hCGRAM = SnesDbgHash32(m_PPU.GetCGData(), 512);
				DLog("[snes-obj-event] f=%u reason=%s drawn=%u->%u candidate=%u refs/tiles/opaque=%u/%u/%u writes=%u/%u/%u",
					(unsigned)g_TmgFrameNo, bEmpty ? "empty" : "drop",
					(unsigned)g_DbgPrevObjDrawn, (unsigned)fDrawn,
					(unsigned)fCandidate, (unsigned)fRefs,
					(unsigned)fTiles, (unsigned)fOpaque,
					(unsigned)fOAM, (unsigned)fVRAM, (unsigned)fCGRAM);
				DLog("[snes-state] hash oam/vram/cgram=%08X/%08X/%08X regs mode/obsel/tm/ts=%02X/%02X/%02X/%02X cg=%02X/%02X first=%u bgsc=%02X/%02X/%02X/%02X",
					(unsigned)hOAM, (unsigned)hVRAM, (unsigned)hCGRAM,
					(unsigned)pr->bgmode, (unsigned)pr->obsel,
					(unsigned)pr->tm, (unsigned)pr->ts,
					(unsigned)pr->cgwsel, (unsigned)pr->cgadsub,
					(unsigned)pr->oampri.w, (unsigned)pr->bg1sc,
					(unsigned)pr->bg2sc, (unsigned)pr->bg3sc,
					(unsigned)pr->bg4sc);
				g_DbgObjEventCooldown = 30;
			}
			g_DbgPrevObjDrawn = fDrawn;
		}
		if (g_DbgCaptureActive)
		{
			const SnesPPURegsT *pr = m_PPU.GetRegs();
			DLog("[snes-capture] f=%u regs inidisp/obsel/mode/tm/ts/setini=%02X/%02X/%02X/%02X/%02X/%02X oam=%04X base=%04X first=%u vmain/vmaddr/cgadd=%02X/%04X/%04X",
				(unsigned)g_TmgFrameNo,
				(unsigned)(Uint8)pr->inidisp, (unsigned)(Uint8)pr->obsel,
				(unsigned)(Uint8)pr->bgmode, (unsigned)(Uint8)pr->tm,
				(unsigned)(Uint8)pr->ts, (unsigned)(Uint8)pr->setini,
				(unsigned)pr->oamaddr.w, (unsigned)pr->oamaddrlatch.w,
				(unsigned)pr->oampri.w, (unsigned)(Uint8)pr->vmain,
				(unsigned)pr->vmaddr.w, (unsigned)pr->cgadd.w);
			DLog("[snes-capture] end f=%u", (unsigned)g_TmgFrameNo);
		}
		#endif
		if (g_TmgWinFrames >= SNDBG_FRAME_PERIOD)
		{
			Uint32 sum   = g_TmgWinSumCyc ? g_TmgWinSumCyc : 1;
			Uint32 avg   = g_TmgWinSumCyc / g_TmgWinFrames;
			Uint32 ratio = avg ? (g_TmgWinMaxCyc * 100u / avg) : 0;
			/* CP0 Count avanca a metade do clock de 294.912 MHz da EE. */
			Uint32 fps10 = avg
				? (Uint32)(1474560000ull / (Uint64)avg) : 0;
			Uint32 pM7   = (Uint32)(((Uint64)g_TmgWinSumM7  * 100u) / sum);
			Uint32 pObj  = (Uint32)(((Uint64)g_TmgWinSumObj * 100u) / sum);
			Uint32 pPPU  = (Uint32)(((Uint64)g_TmgWinSumPPU * 100u) / sum);
			Uint32 pCPU  = (Uint32)(((Uint64)g_TmgWinSumCPU * 100u) / sum);
			Uint32 pGSU  = (Uint32)(((Uint64)g_TmgWinSumGSU * 100u) / sum);
			Uint32 pMDMA = (Uint32)(((Uint64)g_TmgWinSumMDMA * 100u) / sum);
			Uint32 pHDMA = (Uint32)(((Uint64)g_TmgWinSumHDMA * 100u) / sum);
			Uint32 pAPU  = (Uint32)(((Uint64)g_TmgWinSumAPU * 100u) / sum);
			Uint32 pMix  = (Uint32)(((Uint64)g_TmgWinSumMix * 100u) / sum);
			Uint32 pBlend = (Uint32)(((Uint64)g_TmgWinSumBlend * 100u) / sum);
			Uint32 pPPUSync = (Uint32)(((Uint64)g_TmgWinSumPPUSync * 100u) / sum);
			Uint32 pBGInfo = (Uint32)(((Uint64)g_TmgWinSumBGInfo * 100u) / sum);
			Uint32 pBGOffset = (Uint32)(((Uint64)g_TmgWinSumBGOffset * 100u) / sum);
			Uint32 pBGMap = (Uint32)(((Uint64)g_TmgWinSumBGMap * 100u) / sum);
			Uint32 pBGChr = (Uint32)(((Uint64)g_TmgWinSumBGChr * 100u) / sum);
			Uint32 pBGMain = (Uint32)(((Uint64)g_TmgWinSumBGMain * 100u) / sum);
			Uint32 pBGSub = (Uint32)(((Uint64)g_TmgWinSumBGSub * 100u) / sum);
			Uint32 pColorMath = (Uint32)(((Uint64)g_TmgWinSumColorMath * 100u) / sum);
			Uint32 pObjUpdate = (Uint32)(((Uint64)g_TmgWinSumObjUpdate * 100u) / sum);
			Uint32 pObjFetch = (Uint32)(((Uint64)g_TmgWinSumObjFetch * 100u) / sum);
			Uint32 pObjDraw = (Uint32)(((Uint64)g_TmgWinSumObjDraw * 100u) / sum);
			Uint32 pHDMAData = (Uint32)(((Uint64)g_TmgWinSumHDMAData * 100u) / sum);
			Uint32 pHDMATable = (Uint32)(((Uint64)g_TmgWinSumHDMATable * 100u) / sum);
			Uint64 uPPUOther = g_TmgWinSumPPU;
			if (uPPUOther > (Uint64)g_TmgWinSumObj + g_TmgWinSumBlend)
				uPPUOther -= (Uint64)g_TmgWinSumObj + g_TmgWinSumBlend;
			else
				uPPUOther = 0;
			Uint32 pPPUOther = (Uint32)((uPPUOther * 100u) / sum);
			const SNGSUDiagT &gd = m_GSU.GetDiag();

			// CPU/APU/PPU sao medidas inclusivas (podem se sobrepor quando um
			// acesso do 65816 sincroniza outro bloco). Ainda assim identificam
			// diretamente qual rotina esta consumindo o tempo da EE.
			DLog("[snes-perf] diag=%s f=%u avg=%u snes=%u.%u fps peak=%u%% cpu=%u%% ppu=%u%% gsu=%u%% apu=%u%% mix=%u%% mdma=%u%% hdma=%u%%",
				SNDBG_DEEP ? "deep" : "general",
				(unsigned)g_TmgFrameNo, (unsigned)avg,
				(unsigned)(fps10 / 10), (unsigned)(fps10 % 10),
				(unsigned)ratio,
				(unsigned)pCPU, (unsigned)pPPU, (unsigned)pGSU,
				(unsigned)pAPU, (unsigned)pMix, (unsigned)pMDMA,
				(unsigned)pHDMA);
			DLog("[snes-render] bg+compose=%u%% mode7=%u%% obj=%u%% blend=%u%% dsp=%u/%u hirq=%d..%d n=%u rearm/instant=%u/%u",
				(unsigned)pPPUOther, (unsigned)pM7, (unsigned)pObj,
				(unsigned)pBlend,
				(unsigned)g_TmgDspRd, (unsigned)g_TmgDspWr,
				(int)g_TmgIrqLineMin, (int)g_TmgIrqLineMax,
				(unsigned)g_TmgIrqCount,
				(unsigned)g_TmgIrqRearms,
				(unsigned)g_TmgIrqInstant);
			DLog("[snes-hot-bg] schema=topgear-r29 pct sync/info/off/map/chr/main/sub/cmath=%u/%u/%u/%u/%u/%u/%u/%u",
				(unsigned)pPPUSync, (unsigned)pBGInfo,
				(unsigned)pBGOffset, (unsigned)pBGMap,
				(unsigned)pBGChr, (unsigned)pBGMain,
				(unsigned)pBGSub, (unsigned)pColorMath);
			DLog("[snes-hot-obj] pct update/fetch/draw=%u/%u/%u hdma data/table=%u/%u",
				(unsigned)pObjUpdate, (unsigned)pObjFetch,
				(unsigned)pObjDraw, (unsigned)pHDMAData,
				(unsigned)pHDMATable);
			DLog("[snes-hot-cyc] avg sync/map/chr/main/sub/cmath/obj-u/obj-f/obj-d/hdma-d/hdma-t=%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u",
				(unsigned)(g_TmgWinSumPPUSync / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumBGMap / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumBGChr / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumBGMain / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumBGSub / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumColorMath / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumObjUpdate / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumObjFetch / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumObjDraw / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumHDMAData / g_TmgWinFrames),
				(unsigned)(g_TmgWinSumHDMATable / g_TmgWinFrames));
			DLog("[snes-raster] ppu queued/applied/full=%u/%u/%u hdma lines/active/xfer=%u/%u/%u bg layers/reloads/chrrows=%u/%u/%u",
				(unsigned)g_DbgPPUQueuedWrites,
				(unsigned)g_DbgPPUAppliedWrites,
				(unsigned)g_DbgPPUQueueFull,
				(unsigned)g_DbgHDMALines,
				(unsigned)g_DbgHDMAActiveChannels,
				(unsigned)g_DbgHDMATransferChannels,
				(unsigned)g_DbgBGActiveLayers,
				(unsigned)g_DbgBGMapReloads,
				(unsigned)g_DbgBGChrRows);
			DLog("[snes-bg-rows] total/blank/repeat=%u/%u/%u",
				(unsigned)g_DbgBGChrRows,
				(unsigned)g_DbgBGChrBlankRows,
				(unsigned)g_DbgBGChrRepeatRows);
			DLog("[snes-obj] ports oam=%u vram=%u cgram=%u | lines=%u refs=%u tiles=%u range/time=%u/%u",
				(unsigned)g_DbgOAMWrites, (unsigned)g_DbgVRAMWrites,
				(unsigned)g_DbgCGRAMWrites, (unsigned)g_DbgObjEnabledLines,
				(unsigned)g_DbgObjOamRefs, (unsigned)g_DbgObjTiles,
				(unsigned)g_DbgObjRangeLimitLines, (unsigned)g_DbgObjLimitLines);
			DLog("[snes-obj-cache] enabled=%u hit/miss=%u/%u",
				(unsigned)SNPPU_OBJ_CACHE, (unsigned)g_DbgObjCacheHits,
				(unsigned)g_DbgObjCacheMisses);
			DLog("[snes-bg-cache] enabled=%u hit/miss=%u/%u",
				(unsigned)SNPPU_BG_CACHE, (unsigned)g_DbgBGCacheHits,
				(unsigned)g_DbgBGCacheMisses);
			DLog("[snes-chr-cache] direct=1 bytes=448512 invalidated-tiles=%u",
				(unsigned)g_DbgChrCacheInvalidations);
			DLog("[snes-audio] samples=%u avg/frame=%u",
				(unsigned)g_DbgAudioSamples,
				(unsigned)(g_DbgAudioSamples / g_TmgWinFrames));
			DLog("[snes-cgram] bytes/commits/unchanged=%u/%u/%u",
				(unsigned)g_DbgCGRAMWrites,
				(unsigned)g_DbgCGRAMCommits,
				(unsigned)g_DbgCGRAMUnchanged);
			DLog("[snes-video] rendered/skipped=%u/%u",
				(unsigned)g_DbgVideoRenderedFrames,
				(unsigned)g_DbgVideoSkippedFrames);
			#if SNDBG_DEEP
			DLog("[snes-obj-deep] opaque/empty=%u/%u pixels candidate/drawn=%u/%u edge-tiles=%u | regs obsel=%02X tm=%02X ts=%02X first=%u",
				(unsigned)g_DbgObjOpaqueTiles, (unsigned)g_DbgObjEmptyLines,
				(unsigned)g_DbgObjCandidatePixels, (unsigned)g_DbgObjDrawnPixels,
				(unsigned)g_DbgObjClippedTiles,
				(unsigned)g_DbgObjOBSEL, (unsigned)g_DbgObjTM,
				(unsigned)g_DbgObjTS, (unsigned)g_DbgObjPriority);
			#else
			DLog("[snes-obj] regs obsel=%02X tm=%02X ts=%02X first=%u (pixel counters require SNES_DIAGNOSTICS=2)",
				(unsigned)g_DbgObjOBSEL, (unsigned)g_DbgObjTM,
				(unsigned)g_DbgObjTS, (unsigned)g_DbgObjPriority);
			#endif
			#if SNDBG_DEEP
			{
				const SnesPPURegsT *pr = m_PPU.GetRegs();
				Uint32 hOAM = SnesDbgHash32(m_PPU.GetOAM(), sizeof(SnesOAMT));
				Uint32 hVRAM = SnesDbgHash32(m_PPU.GetVramPtr(0), 0x10000);
				Uint32 hCGRAM = SnesDbgHash32(m_PPU.GetCGData(), 512);
				DLog("[snes-state] f=%u hash oam/vram/cgram=%08X/%08X/%08X regs inidisp/mode/obsel/tm/ts/setini=%02X/%02X/%02X/%02X/%02X/%02X",
					(unsigned)g_TmgFrameNo, (unsigned)hOAM,
					(unsigned)hVRAM, (unsigned)hCGRAM,
					(unsigned)(Uint8)pr->inidisp,
					(unsigned)(Uint8)pr->bgmode,
					(unsigned)(Uint8)pr->obsel, (unsigned)(Uint8)pr->tm,
					(unsigned)(Uint8)pr->ts, (unsigned)(Uint8)pr->setini);
				DLog("[snes-state] addr oam/base/first=%04X/%04X/%u vmain/vmaddr/cgadd=%02X/%04X/%04X bgsc=%02X/%02X/%02X/%02X bgnba=%02X/%02X",
					(unsigned)pr->oamaddr.w, (unsigned)pr->oamaddrlatch.w,
					(unsigned)pr->oampri.w, (unsigned)(Uint8)pr->vmain,
					(unsigned)pr->vmaddr.w, (unsigned)pr->cgadd.w,
					(unsigned)(Uint8)pr->bg1sc, (unsigned)(Uint8)pr->bg2sc,
					(unsigned)(Uint8)pr->bg3sc, (unsigned)(Uint8)pr->bg4sc,
					(unsigned)(Uint8)pr->bg12nba, (unsigned)(Uint8)pr->bg34nba);
			}
			#endif
			DLog("[snes-sync] ppu calls/lines=%u/%u dma starts=%u read=%u wrap=%u max=%u",
				(unsigned)g_DbgPPUSyncCalls, (unsigned)g_DbgPPURenderLines,
				(unsigned)g_DbgDMAStarts, (unsigned)g_DbgDMAReadBytes,
				(unsigned)g_DbgDMAWraps, (unsigned)g_DbgDMAMaxBytes);
			DLog("[snes-dma] bytes oam/vram/cgram/other=%u/%u/%u/%u modes=%u/%u/%u/%u/%u/%u/%u/%u",
				(unsigned)g_DbgDMAOAMBytes, (unsigned)g_DbgDMAVRAMBytes,
				(unsigned)g_DbgDMACGRAMBytes, (unsigned)g_DbgDMAOtherBytes,
				(unsigned)g_DbgDMAModes[0], (unsigned)g_DbgDMAModes[1],
				(unsigned)g_DbgDMAModes[2], (unsigned)g_DbgDMAModes[3],
				(unsigned)g_DbgDMAModes[4], (unsigned)g_DbgDMAModes[5],
				(unsigned)g_DbgDMAModes[6], (unsigned)g_DbgDMAModes[7]);
			DLog("[snes-hdma] bytes scroll/cgram/window-color/other=%u/%u/%u/%u",
				(unsigned)g_DbgHDMAScrollBytes,
				(unsigned)g_DbgHDMACGRAMBytes,
				(unsigned)g_DbgHDMAWindowColorBytes,
				(unsigned)g_DbgHDMAOtherBytes);
			{
				const SnesPPURegsT *pr = m_PPU.GetRegs();
				DLog("[snes-ppu] mode/tm/ts/cgw/cgad/bright=%02X/%02X/%02X/%02X/%02X/%u scroll bg1=%03X/%03X bg2=%03X/%03X bg3=%03X/%03X bg4=%03X/%03X m7=%04X/%04X latch hv/h/m7=%02X/%02X/%02X",
					(unsigned)(Uint8)pr->bgmode, (unsigned)(Uint8)pr->tm,
					(unsigned)(Uint8)pr->ts, (unsigned)(Uint8)pr->cgwsel,
					(unsigned)(Uint8)pr->cgadsub,
					(unsigned)(pr->inidisp & 0x0F),
					(unsigned)pr->bg1hofs.w, (unsigned)pr->bg1vofs.w,
					(unsigned)pr->bg2hofs.w, (unsigned)pr->bg2vofs.w,
					(unsigned)pr->bg3hofs.w, (unsigned)pr->bg3vofs.w,
					(unsigned)pr->bg4hofs.w, (unsigned)pr->bg4vofs.w,
					(unsigned)pr->m7hofs.w, (unsigned)pr->m7vofs.w,
					(unsigned)(Uint8)pr->bgofslo,
					(unsigned)(Uint8)pr->bghofslo,
					(unsigned)(Uint8)pr->m7latch);
			}
			#if SNDBG_DEEP
			DLog("[snes-gsu] ins=%u start/stop/abort/wd=%u/%u/%u/%u max=%u cur=%u plot/rpix=%u/%u ramw=%u",
				(unsigned)gd.Instructions, (unsigned)gd.Starts,
				(unsigned)gd.Stops, (unsigned)gd.Aborts,
				(unsigned)gd.Watchdogs, (unsigned)gd.MaxJobInstructions,
				(unsigned)gd.CurrentJobInstructions, (unsigned)gd.Plots,
				(unsigned)gd.Rpix, (unsigned)gd.RamWrites);
			DLog("[snes-gsu] cache hit/miss=%u/%u branch=%u/%u jump=%u go=%u",
				(unsigned)gd.CacheHits, (unsigned)gd.CacheMisses,
				(unsigned)gd.BranchesTaken, (unsigned)gd.Branches,
				(unsigned)gd.Jumps, (unsigned)(m_GSU.IsRunning() ? 1 : 0));
			#else
			DLog("[snes-gsu] detail=off start/stop/abort/wd=%u/%u/%u/%u go=%u (use SNES_DIAGNOSTICS=2 for instruction counters)",
				(unsigned)gd.Starts, (unsigned)gd.Stops,
				(unsigned)gd.Aborts, (unsigned)gd.Watchdogs,
				(unsigned)(m_GSU.IsRunning() ? 1 : 0));
			#endif
			g_TmgWinFrames  = 0;
			g_TmgWinSumCyc  = 0;
			g_TmgWinMaxCyc  = 0;
			g_TmgWinSumM7   = 0;
			g_TmgWinSumObj  = 0;
			g_TmgWinSumPPU  = 0;
			g_TmgWinSumCPU  = 0;
			g_TmgWinSumGSU  = 0;
			g_TmgWinSumMDMA = 0;
			g_TmgWinSumHDMA = 0;
			g_TmgWinSumAPU  = 0;
			g_TmgWinSumMix  = 0;
			g_TmgWinSumBlend = 0;
			g_TmgWinSumPPUSync = 0;
			g_TmgWinSumBGInfo = 0;
			g_TmgWinSumBGOffset = 0;
			g_TmgWinSumBGMap = 0;
			g_TmgWinSumBGChr = 0;
			g_TmgWinSumBGMain = 0;
			g_TmgWinSumBGSub = 0;
			g_TmgWinSumColorMath = 0;
			g_TmgWinSumObjUpdate = 0;
			g_TmgWinSumObjFetch = 0;
			g_TmgWinSumObjDraw = 0;
			g_TmgWinSumHDMAData = 0;
			g_TmgWinSumHDMATable = 0;
			g_TmgDspRd      = 0;
			g_TmgDspWr      = 0;
			g_TmgIrqCount   = 0;
			g_TmgIrqRearms  = 0;
			g_TmgIrqInstant = 0;
			g_TmgIrqLineMin = 9999;
			g_TmgIrqLineMax = -1;
			g_DbgOAMWrites = 0;
			g_DbgVRAMWrites = 0;
			g_DbgCGRAMWrites = 0;
			g_DbgCGRAMCommits = 0;
			g_DbgCGRAMUnchanged = 0;
			g_DbgVideoRenderedFrames = 0;
			g_DbgVideoSkippedFrames = 0;
			g_DbgObjEnabledLines = 0;
			g_DbgObjOamRefs = 0;
			g_DbgObjTiles = 0;
			g_DbgObjCacheHits = 0;
			g_DbgObjCacheMisses = 0;
			g_DbgObjCacheRefreshes = 0;
			g_DbgBGCacheHits = 0;
			g_DbgBGCacheMisses = 0;
			g_DbgBGCacheRefreshes = 0;
			g_DbgChrCacheInvalidations = 0;
			g_DbgAudioSamples = 0;
			g_DbgObjOpaqueTiles = 0;
			g_DbgObjCandidatePixels = 0;
			g_DbgObjDrawnPixels = 0;
			g_DbgObjClippedTiles = 0;
			g_DbgObjEmptyLines = 0;
			g_DbgObjRangeLimitLines = 0;
			g_DbgObjLimitLines = 0;
			g_DbgPPUSyncCalls = 0;
			g_DbgPPURenderLines = 0;
			g_DbgDMAStarts = 0;
			g_DbgDMAReadBytes = 0;
			g_DbgDMAOAMBytes = 0;
			g_DbgDMAVRAMBytes = 0;
			g_DbgDMACGRAMBytes = 0;
			g_DbgDMAOtherBytes = 0;
			g_DbgDMAWraps = 0;
			g_DbgDMAMaxBytes = 0;
			memset(g_DbgDMAModes, 0, sizeof(g_DbgDMAModes));
			g_DbgHDMAScrollBytes = 0;
			g_DbgHDMACGRAMBytes = 0;
			g_DbgHDMAWindowColorBytes = 0;
			g_DbgHDMAOtherBytes = 0;
			g_DbgPPUQueuedWrites = 0;
			g_DbgPPUAppliedWrites = 0;
			g_DbgPPUQueueFull = 0;
			g_DbgHDMALines = 0;
			g_DbgHDMAActiveChannels = 0;
			g_DbgHDMATransferChannels = 0;
			g_DbgBGActiveLayers = 0;
			g_DbgBGMapReloads = 0;
			g_DbgBGChrRows = 0;
			g_DbgBGChrBlankRows = 0;
			g_DbgBGChrRepeatRows = 0;
			m_GSU.ClearDiagWindow();
		}
		g_DbgCaptureActive = FALSE;
	}
#endif

	m_uFrame++;
}


Int32 SnesSystem::GetSRAMBytes()
{
    if (m_pRom)
    {
        return m_pRom->GetSRAMBytes();
    } else
    {
        return 0;
    }               
}

Uint8 *SnesSystem::GetSRAMData()
{
    if (m_pRom && (m_pRom->GetSRAMBytes()>0))
    {
        return m_SRam;
    } else
    {
        return 0;
    }               

}

const char *SnesSystem::GetString(StringE eString)
{
	switch(eString)
	{
		case STRING_SHORTNAME:	
			return (char *)"SNES";
		case STRING_FULLNAME:	
			return (char *)"Super Nintendo";
		case STRING_SRAMEXT:	
			return (char *)"srm";
		case STRING_STATEEXT:	
			return (char *)"sns";
		default:
			return NULL;
	}
}



#ifdef SNES_DEBUG

/*static*/
const char *SnesSystem::GetRegName(Uint32 uAddr)
{
    uAddr &= 0xFFFF;

    switch (uAddr)
    {
    case 0x2134: return (char *)"mpyl";
    case 0x2135: return (char *)"mpym";
    case 0x2136: return (char *)"mpyh";
    case 0x2137: return (char *)"slhv";
    case 0x2138: return (char *)"oam";
    case 0x2139: return (char *)"vmdatal";
    case 0x213a: return (char *)"vmdatah";
    case 0x213b: return (char *)"cgdata";
    case 0x213c: return (char *)"ophct";
    case 0x213d: return (char *)"opvct";
    case 0x213e: return (char *)"stat77";
    case 0x213f: return (char *)"stat78";
    case 0x2140: return (char *)"apui00";
    case 0x2141: return (char *)"apui01";
    case 0x2142: return (char *)"apui02";
    case 0x2143: return (char *)"apui03";

    case 0x2180: return (char *)"WMDATA";
    case 0x2181: return (char *)"WMADDL";
    case 0x2182: return (char *)"WMADDM";
    case 0x2183: return (char *)"WMADDH";

    case 0x4016:	return (char *)"ser0";
    case 0x4017:	return (char *)"ser1";

    case 0x4200:	return (char *)"nmitimen";
    case 0x4201:	return (char *)"wrio";
    case 0x4202:	return (char *)"wrmpya";
    case 0x4203:	return (char *)"wrmpyb";
    case 0x4204:	return (char *)"wrdivl";
    case 0x4205:	return (char *)"wrdivh";
    case 0x4206:	return (char *)"wrdivb";
    case 0x4207:	return (char *)"htmel";
    case 0x4208:	return (char *)"htmeh";
    case 0x4209:	return (char *)"vtmel";
    case 0x420A:	return (char *)"vtmeh";
    case 0x420B:	return (char *)"mdmaen";
    case 0x420C:    return (char *)"hdmaen";
    case 0x420D:	return (char *)"memsel";

    case 0x4210:	return (char *)"RDNMI";
    case 0x4211:	return (char *)"TIMEUP";
    case 0x4212:	return (char *)"HVBJOY";
    case 0x4213:	return (char *)"RDIO";
    case 0x4214:	return (char *)"RDDIVL";
    case 0x4215:	return (char *)"RDDIVH";
    case 0x4216:	return (char *)"RDMPYL";
    case 0x4217:	return (char *)"RDMPYH";
    case 0x4218:	return (char *)"JOY1L";
    case 0x4219:	return (char *)"JOY1H";
    case 0x421A:	return (char *)"JOY2L";
    case 0x421B:	return (char *)"JOY2H";
    case 0x421C:	return (char *)"JOY3L";
    case 0x421D:	return (char *)"JOY3H";
    case 0x421E:	return (char *)"JOY4L";
    case 0x421F:	return (char *)"JOY4H";

    default:
        switch (uAddr & 0x430F)
        {
        case 0x4300:  return (char *)"dmapx";
        case 0x4301:  return (char *)"bbadx";
        case 0x4302:  return (char *)"a1txl";
        case 0x4303:  return (char *)"a1txh";
        case 0x4304:  return (char *)"a1bx";
        case 0x4305:  return (char *)"dasxl";
        case 0x4306:  return (char *)"dasxh";
        case 0x4307:  return (char *)"dasbx";
        case 0x4308:  return (char *)"a2axl";
        case 0x4309:  return (char *)"a2axh";
        case 0x430A:  return (char *)"ntlrx";
        }
        return SnesPPU::GetRegName(uAddr);
    }
}

#endif
