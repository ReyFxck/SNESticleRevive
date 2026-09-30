/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements sndma behavior for the SNES emulation core.
 */

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "types.h"
#include "console.h"
#include "prof.h"
extern "C" {
#include "sncpu.h"
};
#include "sndma.h"
#include "snppu.h"
#include "snsdd1.h"
#include "snsa1.h"
#include "sndbglog.h"

#define SNESDMA_DEBUG 0

static Uint8	_SNDma_MDMATransfer[8][4]=
{
	// 000 1-address
	{0,0,0,0},
	// 001 2-address (l,h)
	{0,1,0,1},
	// 010 1-address
	{0,0,0,0},
	// 011 2-address (l,l,h,h)
	{0,0,1,1},
	// 100 4-address (l,h,l,h)
	{0,1,2,3},
	// 101 4-address (l,h,l,h)
	{0,1,0,1},
	// 110 4-address (l,h,l,h)
	{0,0,0,0},
	// 111 4-address (l,h,l,h)
	{0,0,1,1},
};

static Uint8 _SNDma_HDMABytes[8] =
{
	1, 2, 2, 4, 4, 4, 2, 4
};

static Int8 _SNDma_MDMAInc[4] =
{
	1, 0, -1, 0
};

static _INLINE Uint8 SnesHDMARead8(SNCpuT *pCPU, Uint32 uAddr);

SnesDMAC::SnesDMAC()
{
	memset(m_Channels, 0, sizeof(m_Channels));
	m_MDMAEnable = 0;
	m_HDMAEnable = 0;
	m_HDMAEnded = 0;
	m_HDMADoTransfer = 0;
	m_MDMAStartedMask = 0;
	m_bDMATimingActive = FALSE;
	m_uDMAClockCounter = 0;
	m_uVideoLine = 0;
	m_pCPU = NULL;
	m_pPPU = NULL;
	m_pSDD1 = NULL;
	m_pSA1 = NULL;
}

void SnesDMAC::ConsumeMasterClocks(Int32 nClocks)
{
	if (nClocks <= 0)
		return;
	m_pCPU->Cycles -= nClocks;
	if (m_bDMATimingActive)
		m_uDMAClockCounter += (Uint32)nClocks;
}

Uint32 SnesDMAC::GetPausedCpuSpeed() const
{
	if (!m_pCPU)
		return SNCPU_CYCLE_SLOW;
	Uint32 uPC = m_pCPU->Regs.rPC & 0xFFFFFFu;
	Uint32 uSpeed = m_pCPU->Bank[uPC >> SNCPU_BANK_SHIFT].uBankCycle;
	if (uSpeed != 6u && uSpeed != 8u && uSpeed != 12u)
		uSpeed = SNCPU_CYCLE_SLOW;
	return uSpeed;
}

void SnesDMAC::BeginDMATiming()
{
	if (m_bDMATimingActive || !m_pCPU)
		return;
	m_bDMATimingActive = TRUE;
	m_uDMAClockCounter = 0;
	/* After the CPU pause, DMA waits until the next 8-master-clock boundary.
	   Since S-CPU clocks are even this is the documented 2/4/6/8 clocks. */
	ConsumeMasterClocks((Int32)CalcStartSync(
		(Uint32)SNCPUGetCounter(m_pCPU, SNCPU_COUNTER_TOTAL)));
}

void SnesDMAC::EndDMATiming()
{
	if (!m_bDMATimingActive)
		return;
	/* Resume on the next boundary of the CPU clock that was paused. */
	ConsumeMasterClocks((Int32)CalcEndSync(
		m_uDMAClockCounter, GetPausedCpuSpeed()));
	m_bDMATimingActive = FALSE;
	m_uDMAClockCounter = 0;
}

Bool SnesDMAC::IsABusForbidden(Uint32 uAddr) const
{
	Uint32 uBank = (uAddr >> 16) & 0xFF;
	Uint32 uLow = uAddr & 0xFFFF;
	Bool bSystemBank =
		(uBank <= 0x3F || (uBank >= 0x80 && uBank <= 0xBF)) ? TRUE : FALSE;
	if (!bSystemBank)
		return FALSE;
	if (uLow >= 0x2100 && uLow <= 0x21FF)
		return TRUE;
	if (uLow == 0x420B || uLow == 0x420C ||
	    (uLow >= 0x4300 && uLow <= 0x437F))
		return TRUE;
	return FALSE;
}

Bool SnesDMAC::IsWorkRAMAddress(Uint32 uAddr) const
{
	Uint32 uBank = (uAddr >> 16) & 0xFF;
	Uint32 uLow = uAddr & 0xFFFF;
	if (uBank == 0x7E || uBank == 0x7F)
		return TRUE;
	return ((uBank <= 0x3F || (uBank >= 0x80 && uBank <= 0xBF)) &&
	        uLow < 0x2000) ? TRUE : FALSE;
}

Uint8 SnesDMAC::ReadABus(Uint32 uAddr)
{
	if (IsABusForbidden(uAddr))
		return m_pCPU->uOpenBus;
	return SnesHDMARead8(m_pCPU, uAddr);
}

void SnesDMAC::WriteABus(Uint32 uAddr, Uint8 uData)
{
	/* DMA writes always drive the data bus, even when a CPU/DMA-controller
	   register rejects the A-bus access. */
	if (IsABusForbidden(uAddr))
	{
		m_pCPU->uOpenBus = uData;
		return;
	}
	SNCPUWrite8(m_pCPU, uAddr, uData);
}

void SnesDMAC::CopyDMABusByte(Uint32 uAddrA, Uint32 uAddrB,
	Bool bBToA, Uint32 uHClock)
{
	/* $2180 and WRAM share the same physical WRAM bus. Hardware cannot use
	   both ends of that bus in one DMA byte. */
	if ((uAddrB & 0xFFFF) == 0x2180 && IsWorkRAMAddress(uAddrA))
	{
		if (bBToA)
			WriteABus(uAddrA, 0xFF);
		/* A->B performs no read and no write; open bus remains unchanged. */
		ConsumeMasterClocks(8);
		return;
	}

	if (bBToA)
	{
		Uint8 uData;
		if ((uAddrB & 0xFFC0u) == 0x2100u)
			uData = m_pPPU->ReadTimed(uAddrB, m_pCPU->uOpenBus, TRUE,
			                         m_uVideoLine, uHClock + 4u);
		else
			uData = SNCPURead8(m_pCPU, uAddrB);
		WriteABus(uAddrA, uData);
	}
	else
	{
		Uint8 uData = ReadABus(uAddrA);
		if ((uAddrB & 0xFFC0u) == 0x2100u)
			m_pPPU->WriteTimed(uAddrB, uData, m_uVideoLine, uHClock + 8u);
		else
			SNCPUWrite8(m_pCPU, uAddrB, uData);
	}
	ConsumeMasterClocks(8);
}

void SnesDMAWritePPUPort(SnesPPU *pPPU, Uint32 uPort, Uint8 uData)
{
	assert(pPPU != NULL);
	assert((uPort & 0xFF) < 0x40);

	/* The caller has already synchronized the scanline write queue at MDMA
	   start.  Do not re-enter SNCPUWrite8 here: it would enqueue address and
	   control registers while the optimized data ports below take effect
	   immediately, reversing the byte order observed by the PPU. */
	pPPU->Write8(0x2100 + (uPort & 0xFF), uData);
}

/* HDMA A->B para $2100-$213F termina exatamente na mesma fila usada por
   SnesSystem::Write2000(). Enfileirar a mesma tupla aqui evita o despacho
   generico de banco/trap para cada byte, sem alterar ordem, scanline ou os
   ciclos emulados. Fila cheia retorna FALSE e preserva o caminho original,
   que sincroniza o PPU e tenta novamente. */
static _INLINE Bool SnesHDMATryQueuePPUWrite(
	SnesPPU *pPPU, Uint32 uLine, Uint8 uPortB, Uint8 uData)
{
	/* Memory ports require the original H/V position. Let the fallback route
	   those through SnesSystem::Write2000 instead of losing timing in the
	   scanline-only queue. */
	if (uPortB == 0x04 || uPortB == 0x16 || uPortB == 0x17 ||
	    uPortB == 0x18 || uPortB == 0x19 || uPortB == 0x22)
		return FALSE;
	if (uPortB < 0x40)
		return pPPU->EnqueueWrite(
			uLine, 0x2100u | uPortB, uData, FALSE);
	return FALSE;
}

/* SNCPURead8 lives in a separate C translation unit, so GCC cannot inline its
   very small direct-memory fast path into the per-scanline HDMA loops.  Keep
   trap semantics identical while avoiding thousands of calls per second for
   ordinary ROM/WRAM table reads. */
static _INLINE Uint8 SnesHDMARead8(SNCpuT *pCPU, Uint32 uAddr)
{
	SNCpuBankT *pBank = &pCPU->Bank[uAddr >> SNCPU_BANK_SHIFT];
	Uint8 *pMem = pBank->pMem;
	Uint8 uData = pMem ? pMem[uAddr] : pBank->pReadTrapFunc(pCPU, uAddr);
	/* HDMA bypasses SNCPURead8 for speed, but it still drives the same A-bus
	   byte seen by subsequent open-bus reads. */
	pCPU->uOpenBus = uData;
	return uData;
}

#if SNDBG_DEEP
struct SNDmaOAMCaptureT
{
	Bool Active;
	Uint32 Frame;
	Uint32 SourceHash;
	Uint32 Bytes;
	Uint16 StartAddress;
	Uint8 Channel;
};

static SNDmaOAMCaptureT _SNDmaOAMCapture;
static Uint32 _SNDmaTraceFrame = (Uint32)-1;
static Uint32 _SNDmaTraceCount = 0;
static Uint32 _SNDmaSDD1TraceFrame = (Uint32)-1;
static Uint32 _SNDmaSDD1TraceCount = 0;

static Bool _SNDmaTraceGeneral(void)
{
	if (!g_DbgCaptureActive)
		return FALSE;
	if (_SNDmaTraceFrame != g_DbgCaptureFrameNo)
	{
		_SNDmaTraceFrame = g_DbgCaptureFrameNo;
		_SNDmaTraceCount = 0;
	}
	return (_SNDmaTraceCount++ < 16u) ? TRUE : FALSE;
}

static Bool _SNDmaTraceSDD1(void)
{
	if (!g_DbgCaptureActive)
		return FALSE;
	if (_SNDmaSDD1TraceFrame != g_DbgCaptureFrameNo)
	{
		_SNDmaSDD1TraceFrame = g_DbgCaptureFrameNo;
		_SNDmaSDD1TraceCount = 0;
	}
	return (_SNDmaSDD1TraceCount++ < 8u) ? TRUE : FALSE;
}

static Uint32 _SNDmaHashBytes(const Uint8 *pData, Uint32 nBytes)
{
	Uint32 h = 2166136261u;
	while (nBytes-- > 0)
	{
		h ^= *pData++;
		h *= 16777619u;
	}
	return h;
}
#endif

Uint8 SnesDMAC::Read8(Uint32 uChan, Uint32 uAddr, Uint8 uOpenBus)
{
	SnesDMAChT *pChan;
	pChan = &m_Channels[uChan];

	switch(uAddr & 0xF)
	{
	case 0x0:
		return pChan->dmapx;

	case 0x1:
		return pChan->bbadx;

	case 0x2:
		return pChan->a1tx & 0xFF;

	case 0x3:
		return pChan->a1tx >> 8;

	case 0x4:
		return pChan->a1bx;

	case 0x5:
		return	pChan->dasx & 0xFF;

	case 0x6:
		return	pChan->dasx >> 8;

	case 0x7:
		return pChan->dasbx;

	case 0x8:
		return pChan->a2ax & 0xFF;

	case 0x9:
		return pChan->a2ax >> 8;

	case 0xA:
		return pChan->ntlrx;

	case 0xB:
	case 0xF:
		return pChan->unknown;

	default:
		/* $43xC-$43xE are not backed by DMA state.  They leave the CPU data
		   bus visible instead of manufacturing zero. */
		return uOpenBus;
	}
}

void SnesDMAC::Write8(Uint32 uChan, Uint32 uAddr, Uint8 uData)
{
	SnesDMAChT *pChan;

	pChan = &m_Channels[uChan];

	switch(uAddr & 0xF)
	{
	case 0x0:
		pChan->dmapx = uData;
		break;

	case 0x1:
		pChan->bbadx = uData;
		break;

	case 0x2:
		pChan->a1tx &= 0xFF00;
		pChan->a1tx |= uData << 0;
		break;

	case 0x3:
		pChan->a1tx &= 0x00FF;
		pChan->a1tx |= uData << 8;
		break;

	case 0x4:
		pChan->a1bx = uData;
		break;

	case 0x5:
		pChan->dasx &= 0xFF00;
		pChan->dasx |= uData << 0;
		break;

	case 0x6:
		pChan->dasx &= 0x00FF;
		pChan->dasx |= uData << 8;
		break;

	case 0x7:
		pChan->dasbx = uData;
		break;

	case 0x8:
		pChan->a2ax &= 0xFF00;
		pChan->a2ax |= uData << 0;
		break;

	case 0x9:
		pChan->a2ax &= 0x00FF;
		pChan->a2ax |= uData << 8;
		break;

	case 0xA:
		pChan->ntlrx = uData;
		break;

	case 0xB:
	case 0xF:
		pChan->unknown = uData;
		break;

	default:
		break;
	}
}

void SnesDMAC::SetMDMAEnable(Uint8 uData)
{
#if SNDBG_LOG
	Uint32 uChan;

	for (uChan = 0; uChan < SNESDMAC_CHANNEL_NUM; uChan++)
	{
		SnesDMAChT *pChan;
		Uint32 uBytes;
		Uint32 uMode;
		Int32 iDelta;

		if (!(uData & (1 << uChan)))
			continue;

		pChan = &m_Channels[uChan];
		uBytes = pChan->dasx ? (Uint32)pChan->dasx : 0x10000u;
		uMode = pChan->dmapx & 7;
		iDelta = _SNDma_MDMAInc[(pChan->dmapx >> 3) & 3];

		g_DbgDMAStarts++;
		g_DbgDMAModes[uMode]++;
		if (uBytes > g_DbgDMAMaxBytes)
			g_DbgDMAMaxBytes = uBytes;

		#if SNDBG_DEEP
		if (_SNDmaTraceGeneral())
		{
			const SnesPPURegsT *pRegs = m_pPPU->GetRegs();
			DLog("[snes-dma-start] f=%u ch=%u dmap=%02X mode=%u dir=%s src=%02X:%04X len=%u bbad=%02X oam/vm/cg=%04X/%04X/%04X vmain=%02X",
				(unsigned)g_DbgCaptureFrameNo, (unsigned)uChan,
				(unsigned)pChan->dmapx, (unsigned)uMode,
				(pChan->dmapx & 0x80) ? "B>A" : "A>B",
				(unsigned)pChan->a1bx, (unsigned)pChan->a1tx,
				(unsigned)uBytes, (unsigned)pChan->bbadx,
				(unsigned)pRegs->oamaddr.w, (unsigned)pRegs->vmaddr.w,
				(unsigned)pRegs->cgadd.w, (unsigned)(Uint8)pRegs->vmain);

			/* Snapshot the complete WRAM OAM mirror before its DMA. At channel
			   completion we hash physical OAM too, proving whether corruption
			   happened before or inside the $2104 transfer path. */
			if (!(pChan->dmapx & 0x80) && uMode == 0 &&
			    pChan->bbadx == 0x04 && iDelta == 1 &&
			    uBytes == sizeof(SnesOAMT))
			{
				Uint32 uAddr = ((Uint32)pChan->a1bx << 16) | pChan->a1tx;
				Uint32 uHash = 2166136261u;
				Uint32 i;
				for (i = 0; i < uBytes; i++)
				{
					uHash ^= SNCPUPeek8(m_pCPU,
						((Uint32)pChan->a1bx << 16) |
						((pChan->a1tx + i) & 0xFFFF));
					uHash *= 16777619u;
				}
				_SNDmaOAMCapture.Active = TRUE;
				_SNDmaOAMCapture.Frame = g_DbgCaptureFrameNo;
				_SNDmaOAMCapture.SourceHash = uHash;
				_SNDmaOAMCapture.Bytes = uBytes;
				_SNDmaOAMCapture.StartAddress = pRegs->oamaddr.w;
				_SNDmaOAMCapture.Channel = (Uint8)uChan;
				DLog("[snes-oam-dma] f=%u ch=%u src=%06X bytes=%u start=%04X source-hash=%08X",
					(unsigned)g_DbgCaptureFrameNo, (unsigned)uChan,
					(unsigned)uAddr, (unsigned)uBytes,
					(unsigned)pRegs->oamaddr.w, (unsigned)uHash);
			}
		}
		#endif

		if ((iDelta > 0 && uBytes > 0x10000u - pChan->a1tx) ||
		    (iDelta < 0 && uBytes > (Uint32)pChan->a1tx + 1u))
		{
			g_DbgDMAWraps++;
			SnesDbgRequestCapture(SNDBG_CAPTURE_DMA_WRAP);
		}

		if (pChan->dmapx & 0x80)
		{
			g_DbgDMAReadBytes += uBytes;
		}
		else
		{
			/* Count the exact B-bus ports selected by the four-byte mode
			   pattern, rather than assuming that BBAD alone names the port. */
			Uint32 uPhase;
			for (uPhase = 0; uPhase < 4 && uPhase < uBytes; uPhase++)
			{
				Uint32 uCount = 1u + (uBytes - 1u - uPhase) / 4u;
				Uint32 uPort = (pChan->bbadx +
					_SNDma_MDMATransfer[uMode][uPhase]) & 0xFF;
				if (uPort == 0x04)
					g_DbgDMAOAMBytes += uCount;
				else if (uPort == 0x18 || uPort == 0x19)
					g_DbgDMAVRAMBytes += uCount;
				else if (uPort == 0x22)
					g_DbgDMACGRAMBytes += uCount;
				else
					g_DbgDMAOtherBytes += uCount;
			}
		}
	}
#endif
	/* A new $420B command begins a fresh DMA session. CPU execution cannot
	   program another command while the previous DMA owns the bus. */
	if (uData)
		m_MDMAStartedMask = 0;
	m_MDMAEnable = uData;
}

void SnesDMAC::SetHDMAEnable(Uint8 uData)
{
	// confirm:
	// ghouls and ghosts enabled hdma mid-frame
	/* $420C keeps the programmed enable bits.  A channel that already read
	   its zero terminator stays stopped until the next frame, even if $420C
	   is written again (Mesen/Snes9x keep a separate ended-channel mask). */
	m_HDMAEnable = uData;
}

void SnesDMAC::ProcessMDMAChRead(Uint32 uChan)
{
    SnesDMAChT *pChan;

    assert(uChan < SNESDMAC_CHANNEL_NUM);

    pChan = &m_Channels[uChan];

    Int32 uSrcDelta;
	Uint8 *pTransfer;
	Int32 iTransfer=0;

    // any cycles available?
    if (m_pCPU->Cycles <= 0) {
        return;
    }
	// determine a-bus increment
	uSrcDelta = _SNDma_MDMAInc[(pChan->dmapx>>3) & 3];

	// get transfer order
	pTransfer = _SNDma_MDMATransfer[pChan->dmapx & 7];
	iTransfer = 0;

	do
	{
		Uint8 uData;
		Uint32 uAddr;

		// get address to read from
		uAddr = 0x2100 + pChan->bbadx + pTransfer[iTransfer & 3];
		iTransfer++;

		// read byte
		uData = SNCPURead8(m_pCPU, uAddr);

		// write byte
		SNCPUWrite8(m_pCPU, pChan->a1tx | (pChan->a1bx << 16), uData);

		// increment src address (does overflow go into next bank?)
		pChan->a1tx += uSrcDelta;

		// decrement byte count
		pChan->dasx--;

        // decrement cpu clock cycles
        ConsumeMasterClocks(SNCPU_CYCLE_SLOW * 1);
	}
	/* Finish the four-byte B-bus pattern once a slice starts.  Otherwise a
	   scheduler boundary after byte 1/2/3 restarts the next slice at phase 0
	   and corrupts reverse DMA modes 1, 3, 4, 5 and 7. */
	while (pChan->dasx != 0 &&
		(m_pCPU->Cycles > 0 || (iTransfer & 3) != 0));

    // are we done?
    if (pChan->dasx == 0)
    {
        // clear channel enable bit
        m_MDMAEnable &= ~(1 << uChan);
    }
}

void SnesDMAC::TransferData(SnesDMAChT *pChan, Uint8 *pData, Int32 nBytes)
{
	Int32 nOriginalBytes = nBytes;
	Uint8 uLastBus = nBytes > 0 ? pData[nBytes - 1] : m_pCPU->uOpenBus;
	Uint32 uStartH = (Uint32)SNCPUGetCounter(m_pCPU, SNCPU_COUNTER_LINE);
	Int32 iBusByte = 0;
	Bool bVRAMAllowed = m_pPPU->CanAccessVRAM(m_uVideoLine);
	Bool bActive = m_pPPU->IsActiveDisplay(m_uVideoLine);

	ConsumeMasterClocks(SNCPU_CYCLE_SLOW * nBytes);

	/* Keep the bulk paths for the overwhelmingly common VBlank/forced-blank
	   uploads. Active-display transfers must visit each bus byte because OAM
	   is redirected and CGRAM has H-clock access windows. */
	if ((pChan->dmapx & 7) == 0)
	{
		switch (pChan->bbadx)
		{
		case 0x04:
			if (!bActive)
				m_pPPU->WriteOAMBlock(pData, nBytes);
			else
				while (nBytes-- > 0)
				{
					m_pPPU->WriteTimed(0x2104, *pData++, m_uVideoLine,
						uStartH + (Uint32)(iBusByte++ * SNCPU_CYCLE_SLOW));
				}
			break;

		case 0x18:
			if (bVRAMAllowed)
				while (nBytes-- > 0) m_pPPU->WriteVMDATAL(*pData++);
			else
				while (nBytes-- > 0)
				{
					m_pPPU->WriteTimed(0x2118, *pData++, m_uVideoLine,
						uStartH + (Uint32)(iBusByte++ * SNCPU_CYCLE_SLOW));
				}
			break;

		case 0x19:
			if (bVRAMAllowed)
				while (nBytes-- > 0) m_pPPU->WriteVMDATAH(*pData++);
			else
				while (nBytes-- > 0)
				{
					m_pPPU->WriteTimed(0x2119, *pData++, m_uVideoLine,
						uStartH + (Uint32)(iBusByte++ * SNCPU_CYCLE_SLOW));
				}
			break;

		case 0x22:
			if (!bActive)
				while (nBytes-- > 0) m_pPPU->WriteCGDATA(*pData++);
			else
				while (nBytes-- > 0)
				{
					m_pPPU->WriteTimed(0x2122, *pData++, m_uVideoLine,
						uStartH + (Uint32)(iBusByte++ * SNCPU_CYCLE_SLOW));
				}
			break;

		default:
			while (nBytes-- > 0)
			{
				Uint32 uPort = pChan->bbadx & 0xFF;
				if (uPort < 0x40)
					m_pPPU->WriteTimed(0x2100 + uPort, *pData++,
						m_uVideoLine,
						uStartH + (Uint32)(iBusByte++ * SNCPU_CYCLE_SLOW));
				else
					SNCPUWrite8(m_pCPU, 0x2100 + uPort, *pData++);
			}
			break;
		}
	}
	else if ((pChan->dmapx & 7) == 1 && pChan->bbadx == 0x18 &&
	         bVRAMAllowed)
	{
		m_pPPU->WriteVMDATABlock(pData, nBytes);
	}
	else
	{
		Uint8 *pTransfer = _SNDma_MDMATransfer[pChan->dmapx & 7];
		Int32 iTransfer = 0;
		while (nBytes-- > 0)
		{
			Uint8 uData = pData[iTransfer];
			Uint32 uAddr =
				(pChan->bbadx + pTransfer[iTransfer & 3]) & 0xFF;
			Uint32 uHClock =
				uStartH + (Uint32)(iBusByte++ * SNCPU_CYCLE_SLOW);
			iTransfer++;

			if (uAddr < 0x40)
				m_pPPU->WriteTimed(0x2100 + uAddr, uData,
				                   m_uVideoLine, uHClock);
			else
				SNCPUWrite8(m_pCPU, 0x2100 + uAddr, uData);
		}
	}

	if (nOriginalBytes > 0)
		m_pCPU->uOpenBus = uLastBus;
}

void SnesDMAC::ProcessMDMAChFast(Uint32 uChan)
{
	SnesDMAChT *pChan;

	assert(uChan < SNESDMAC_CHANNEL_NUM);

    pChan = &m_Channels[uChan];

#if SNESDMA_DEBUG
	ConDebug("dma%d: %02X %02X%04X -> %02X %04X vram=%04X nCycles=%d\n", uChan,
		pChan->dmapx,
		pChan->a1bx,
		pChan->a1tx,
		pChan->bbadx,
		pChan->dasx,
		m_pPPU->m_Regs.vmaddr.w,
        m_pCPU->Cycles
		);
#endif

    // any cycles available?
    if (m_pCPU->Cycles <= 0) {
        return;
    }
	if (pChan->dmapx & 0x80)
	{
		// ppu -> mem
		return ProcessMDMAChRead(uChan);
	}

	Bool bSA1CC1 = (m_pSA1 && m_pSA1->IsCC1Active() &&
	               (pChan->a1bx & 0xF0) == 0x40) ? TRUE : FALSE;

	// S-DD1: descomprime quando o DMA tem endereco-A fixo (dmapx bit 0x08) e
	// $4801 != 0 (mesma condicao do snes9x). Antes eu so' checava o bit do
	// canal em $4801, o que podia disparar num DMA normal por engano.
	if (m_pSDD1 && (pChan->dmapx & 0x08) && m_pSDD1->DmaActive())
	{
		static Uint8 s_DecodeBuf[0x10000];
		Int32  count   = pChan->dasx ? pChan->dasx : 0x10000;
		Uint32 srcAddr = ((Uint32)pChan->a1bx << 16) | pChan->a1tx;
		Uint8 *pIn     = m_pCPU->Bank[srcAddr >> SNCPU_BANK_SHIFT].pMem;

		if (pIn)
		{
			// pMem ja' inclui (-base do banco), entao soma-se o endereco
			// completo (mesma convencao de SNCPUPeek8: pMem[Addr]).
			pIn += srcAddr;
			m_pSDD1->Decompress(s_DecodeBuf, pIn, count);
			TransferData(pChan, s_DecodeBuf, count);
#if SNDBG_LOG
			g_DbgSDD1DmaTransfers++;
			g_DbgSDD1DecompressedBytes += (Uint32)count;
#endif
#if SNDBG_DEEP
			if (_SNDmaTraceSDD1())
			{
				DLog("[snes-sdd1-trace] f=%u ch=%u src=%06X bytes=%u bbad=%02X mode=%u hdr=%02X out=%02X%02X%02X%02X",
					(unsigned)g_DbgCaptureFrameNo, (unsigned)uChan,
					(unsigned)srcAddr, (unsigned)count,
					(unsigned)pChan->bbadx, (unsigned)(pChan->dmapx & 7),
					(unsigned)pIn[0], (unsigned)s_DecodeBuf[0],
					(unsigned)s_DecodeBuf[1], (unsigned)s_DecodeBuf[2],
					(unsigned)s_DecodeBuf[3]);
			}
#endif
		}
#if SNDBG_LOG
		else
		{
			g_DbgSDD1SourceFailures++;
			SnesDbgRequestCapture(SNDBG_CAPTURE_CHIP);
			#if SNDBG_DEEP
			if (_SNDmaTraceSDD1())
				DLog("[snes-sdd1-error] f=%u ch=%u src=%06X reason=unmapped-source",
					(unsigned)g_DbgCaptureFrameNo, (unsigned)uChan,
					(unsigned)srcAddr);
			#endif
		}
#endif

		m_pSDD1->ClearDmaEnable();
		pChan->dasx = 0;
		m_MDMAEnable &= ~(1 << uChan);
		return;
	}

    do
	{
		Uint8 DmaBuffer[256];
		Int32 nBytes;

		// calculate number of bytes remaining to transfer
		nBytes = pChan->dasx ? pChan->dasx : 0x10000;

        // clamp number of bytes to the size of our temporary buffer
		if (nBytes > (Int32)sizeof(DmaBuffer))
            nBytes = sizeof(DmaBuffer);

        // clamp number of bytes to cycle time remaining
        Int32 nMaxBytes = ((m_pCPU->Cycles+7) >> 3);
        // we must transfer a multiple of 4-bytes at a time though....
        nMaxBytes = (nMaxBytes + 3) & ~3;
        if (nBytes > nMaxBytes)
            nBytes = nMaxBytes;

        // any bytes to transfer?
        if (nBytes > 0)
        {
		    PROF_ENTER("DMAREADMEM");
		    switch ((pChan->dmapx>>3) & 3)
		    {
		    case 0: //+1 increment
                {
					if (bSA1CC1)
					{
						Int32 iByte;
						for (iByte = 0; iByte < nBytes; iByte++)
						{
							Uint32 uSrc = (Uint32)pChan->a1tx |
							              ((Uint32)pChan->a1bx << 16);
							DmaBuffer[iByte] = m_pSA1->ReadSCPUBWRAMDirect(uSrc);
							pChan->a1tx++;
						}
					}
					else
					{
						Int32 nFirst = nBytes;
						Int32 nToBankEnd = 0x10000 - (Int32)pChan->a1tx;
						if (nFirst > nToBankEnd)
							nFirst = nToBankEnd;
						SNCPUReadMem(m_pCPU,
						             pChan->a1tx | (pChan->a1bx << 16),
						             DmaBuffer, nFirst);
						if (nFirst < nBytes)
							SNCPUReadMem(m_pCPU, pChan->a1bx << 16,
							             DmaBuffer + nFirst, nBytes - nFirst);
						pChan->a1tx = (Uint16)(pChan->a1tx + nBytes);
					}
                }
			    break;
		    case 2: //-1 decrement
			    {
                    // read data into dma buffer (decrement)
				    Int32 iByte;
				    for (iByte=0; iByte < nBytes; iByte++)
				    {
					    Uint32 uSrc = (Uint32)pChan->a1tx | ((Uint32)pChan->a1bx << 16);
					    DmaBuffer[iByte] = bSA1CC1 ?
					        m_pSA1->ReadSCPUBWRAMDirect(uSrc) : SNCPURead8(m_pCPU, uSrc);
					    pChan->a1tx--;
				    }
			    }
			    break;
		    case 1:
		    case 3: // 0
			    // read data into dma buffer (no increment)
			    {
					Uint32 uSrc = (Uint32)pChan->a1tx | ((Uint32)pChan->a1bx << 16);
					Uint8 uData = bSA1CC1 ?
					    m_pSA1->ReadSCPUBWRAMDirect(uSrc) : SNCPURead8(m_pCPU, uSrc);
					memset(DmaBuffer, uData, nBytes);
			    }
			    break;
		    }
		    PROF_LEAVE("DMAREADMEM");

            // transfer cached data to B-bus
		    TransferData(pChan, DmaBuffer, nBytes);

            // decrement byte count
            pChan->dasx -= nBytes;
        }

	}	while ( (pChan->dasx!=0) && (m_pCPU->Cycles > 0) );

    // are we done?
    if (pChan->dasx == 0)
    {
#if SNDBG_DEEP
		if (_SNDmaOAMCapture.Active &&
		    _SNDmaOAMCapture.Frame == g_DbgCaptureFrameNo &&
		    _SNDmaOAMCapture.Channel == uChan)
		{
			Uint32 uDestHash = _SNDmaHashBytes(
				(const Uint8 *)m_pPPU->GetOAM(), sizeof(SnesOAMT));
			Bool bComparable =
				(_SNDmaOAMCapture.StartAddress & 0x3FF) == 0 &&
				_SNDmaOAMCapture.Bytes == sizeof(SnesOAMT);
			DLog("[snes-oam-dma] f=%u ch=%u dest-hash=%08X comparable=%u match=%u end=%04X",
				(unsigned)g_DbgCaptureFrameNo, (unsigned)uChan,
				(unsigned)uDestHash, (unsigned)bComparable,
				(unsigned)(bComparable &&
					uDestHash == _SNDmaOAMCapture.SourceHash),
				(unsigned)m_pPPU->GetRegs()->oamaddr.w);
			_SNDmaOAMCapture.Active = FALSE;
		}
#endif
        // clear channel enable bit
        m_MDMAEnable &= ~(1 << uChan);
    }
}

void SnesDMAC::BeginHDMA()
{
	Uint8 uEnabled = m_HDMAEnable;
	m_HDMAEnded = 0;
	m_HDMADoTransfer = uEnabled ? 0xFF : 0;

	if (!uEnabled)
		return;

	Bool bOwnTiming = m_bDMATimingActive ? FALSE : TRUE;
	if (bOwnTiming)
		BeginDMATiming();

	/* HDMA initialization has one 8-clock global overhead, then initializes
	   every enabled channel before the first scanline transfer. */
	ConsumeMasterClocks(8);

	for (Uint32 uChan = 0; uChan < SNESDMAC_CHANNEL_NUM; uChan++)
	{
		Uint8 uMask = (Uint8)(1 << uChan);
		SnesDMAChT *pChan;
		Bool bStopped;
		Uint8 uLow;

		if (!(uEnabled & uMask))
			continue;

		pChan = &m_Channels[uChan];
		pChan->a2ax = pChan->a1tx;
		pChan->ntlrx = SnesHDMARead8(m_pCPU,
			(Uint16)pChan->a2ax | (pChan->a1bx << 16));
		ConsumeMasterClocks(SNCPU_CYCLE_SLOW);
		pChan->a2ax++;

		bStopped = pChan->ntlrx == 0;
		if (bStopped)
			m_HDMAEnded |= uMask;

		if (pChan->dmapx & 0x40)
		{
			uLow = SnesHDMARead8(m_pCPU,
				(Uint16)pChan->a2ax | (pChan->a1bx << 16));
			ConsumeMasterClocks(SNCPU_CYCLE_SLOW);
			pChan->a2ax++;

			if (bStopped)
			{
				/* Hardware's terminal-channel oddity treats the one byte as
				   the high half of the otherwise-unused indirect address. */
				pChan->dasx = (Uint16)uLow << 8;
			}
			else
			{
				pChan->dasx = uLow | (SnesHDMARead8(m_pCPU,
					(Uint16)pChan->a2ax | (pChan->a1bx << 16)) << 8);
				ConsumeMasterClocks(SNCPU_CYCLE_SLOW);
				pChan->a2ax++;
			}
		}
	}

	if (bOwnTiming)
		EndDMATiming();
}

void SnesDMAC::ProcessHDMACh(Uint32 uChan, Uint32 uLine)
{
	SnesDMAChT *pChan;
	Uint8 *pTransfer;
	Uint32 uMode;
	Uint32 nBytes;

	assert(uChan < SNESDMAC_CHANNEL_NUM);
	pChan = &m_Channels[uChan];
	uMode = pChan->dmapx & 7;
	nBytes = _SNDma_HDMABytes[uMode];
	pTransfer = _SNDma_MDMATransfer[uMode];

	for (Uint32 i = 0; i < nBytes; i++)
	{
		Uint32 uAddrA;
		Uint8 uPortB = (Uint8)(pChan->bbadx + pTransfer[i]);
		Uint32 uAddrB = 0x2100 | uPortB;
		Uint8 uData;

		if (pChan->dmapx & 0x40)
			uAddrA = (pChan->dasbx << 16) | pChan->dasx;
		else
			uAddrA = (pChan->a1bx << 16) | pChan->a2ax;

		if (pChan->dmapx & 0x80)
		{
			uData = SnesHDMARead8(m_pCPU, uAddrB);
			SNCPUWrite8(m_pCPU, uAddrA, uData);
		}
		else
		{
			uData = SnesHDMARead8(m_pCPU, uAddrA);
			if (!SnesHDMATryQueuePPUWrite(
			        m_pPPU, uLine, uPortB, uData))
			{
				SNCPUWrite8(m_pCPU, uAddrB, uData);
			}
#if SNDBG_LOG
			{
				Uint32 uPort = uPortB;
				if (uPort >= 0x0D && uPort <= 0x14)
					g_DbgHDMAScrollBytes++;
				else if (uPort == 0x22)
					g_DbgHDMACGRAMBytes++;
				else if (uPort >= 0x23 && uPort <= 0x32)
					g_DbgHDMAWindowColorBytes++;
				else
					g_DbgHDMAOtherBytes++;
			}
#endif
		}

		if (pChan->dmapx & 0x40)
			pChan->dasx++;
		else
			pChan->a2ax++;
		ConsumeMasterClocks(SNCPU_CYCLE_SLOW);
	}
}

void SnesDMAC::ProcessMDMA()
{
	if (!m_MDMAEnable)
		return;

	/* Manual DMA waits for an 8-clock boundary, pays one global 8-clock
	   startup, then 8 clocks once per active channel before its bytes. */
	if (!m_bDMATimingActive)
	{
		BeginDMATiming();
		ConsumeMasterClocks(8);
	}

	Uint32 uChan = 0;
	while (m_MDMAEnable && m_pCPU->Cycles > 0 && uChan < SNESDMAC_CHANNEL_NUM)
	{
		Uint8 uMask = (Uint8)(1 << uChan);
		if (m_MDMAEnable & uMask)
		{
			if (!(m_MDMAStartedMask & uMask))
			{
				m_MDMAStartedMask |= uMask;
				ConsumeMasterClocks(8);
				if (m_pCPU->Cycles <= 0)
					break;
			}
			ProcessMDMAChFast(uChan);
		}
		if (!(m_MDMAEnable & uMask))
			uChan++;
	}

	if (!m_MDMAEnable)
	{
		EndDMATiming();
		m_MDMAStartedMask = 0;
	}
}

void SnesDMAC::ProcessHDMA(Uint32 uLine)
{
	m_uVideoLine = uLine;
	Uint8 uActive = m_HDMAEnable & ~m_HDMAEnded;

	if (!uActive)
		return;

	/* When MDMA is already running, HDMA steals its clocks inside the same
	   synchronized DMA session. Otherwise this HBlank owns start/end sync. */
	Bool bOwnTiming = m_bDMATimingActive ? FALSE : TRUE;
	if (bOwnTiming)
		BeginDMATiming();

#if SNDBG_LOG
	Uint32 _tHDMAData = ProfCtrGetCycle();
	g_DbgHDMALines++;
#endif

	/* Mesen performs every channel's data phase first, followed by every
	   channel's counter/table phase.  Interleaving those phases changes both
	   B-bus side effects and the point at which IRQ/NMI can be observed. */
	ConsumeMasterClocks(SNCPU_CYCLE_SLOW);
	for (Uint32 uChan = 0; uChan < SNESDMAC_CHANNEL_NUM; uChan++)
	{
		Uint8 uMask = (Uint8)(1 << uChan);
		if ((uActive & uMask) && (m_HDMADoTransfer & uMask))
		{
#if SNDBG_LOG
			g_DbgHDMATransferChannels++;
#endif
			ProcessHDMACh(uChan, uLine);
		}
	}

#if SNDBG_LOG
	g_TmgCycHDMAData += ProfCtrGetCycle() - _tHDMAData;
	Uint32 _tHDMATable = ProfCtrGetCycle();
#endif

	for (Uint32 uChan = 0; uChan < SNESDMAC_CHANNEL_NUM; uChan++)
	{
		Uint8 uMask = (Uint8)(1 << uChan);
		SnesDMAChT *pChan;
		Uint8 uNewCounter;

		if (!(uActive & uMask))
			continue;

#if SNDBG_LOG
		g_DbgHDMAActiveChannels++;
#endif

		pChan = &m_Channels[uChan];
		pChan->ntlrx--;
		if (pChan->ntlrx & 0x80)
			m_HDMADoTransfer |= uMask;
		else
			m_HDMADoTransfer &= ~uMask;

		/* The S-CPU performs this table read on every active scanline.  Its
		   value is discarded until the seven-bit line counter reaches zero. */
		uNewCounter = SnesHDMARead8(m_pCPU,
			(Uint16)pChan->a2ax | (pChan->a1bx << 16));
		ConsumeMasterClocks(SNCPU_CYCLE_SLOW);

		if ((pChan->ntlrx & 0x7F) == 0)
		{
			pChan->ntlrx = uNewCounter;
			pChan->a2ax++;

			if (pChan->dmapx & 0x40)
			{
				Uint8 uHigherMask = (Uint8)~((1u << (uChan + 1)) - 1u);
				Bool bLastActive =
					!((m_HDMAEnable & ~m_HDMAEnded) & uHigherMask);
				Uint8 uLow;

				uLow = SnesHDMARead8(m_pCPU,
					(Uint16)pChan->a2ax | (pChan->a1bx << 16));
				ConsumeMasterClocks(SNCPU_CYCLE_SLOW);
				pChan->a2ax++;

				if (uNewCounter == 0 && bLastActive)
				{
					/* The last terminating indirect channel fetches only one
					   address byte and places it in the high half. */
					pChan->dasx = (Uint16)uLow << 8;
				}
				else
				{
					pChan->dasx = uLow | (SnesHDMARead8(m_pCPU,
						(Uint16)pChan->a2ax |
						(pChan->a1bx << 16)) << 8);
					ConsumeMasterClocks(SNCPU_CYCLE_SLOW);
					pChan->a2ax++;
				}
			}

			if (uNewCounter == 0)
				m_HDMAEnded |= uMask;
			m_HDMADoTransfer |= uMask;
		}
	}
#if SNDBG_LOG
	g_TmgCycHDMATable += ProfCtrGetCycle() - _tHDMATable;
#endif
	if (bOwnTiming)
		EndDMATiming();
}

void SnesDMAC::Reset()
{
	memset(m_Channels, 0xFF, sizeof(m_Channels));
	m_MDMAEnable = 0;
	m_HDMAEnable = 0;
	m_HDMAEnded = 0;
	m_HDMADoTransfer = 0;
	m_MDMAStartedMask = 0;
	m_bDMATimingActive = FALSE;
	m_uDMAClockCounter = 0;
}
