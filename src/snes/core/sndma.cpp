
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
	if (uPortB < 0x40)
		/* O fallback generico contabiliza a unica falha real da fila. */
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
	return pMem ? pMem[uAddr] : pBank->pReadTrapFunc(pCPU, uAddr);
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



Uint8 SnesDMAC::Read8(Uint32 uChan, Uint32 uAddr)
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
		//ConDebug("read_dma8[%06X]\n", uAddr);
		return 0x00;
	}
}






void SnesDMAC::Write8(Uint32 uChan, Uint32 uAddr, Uint8 uData)
{
	SnesDMAChT *pChan;

	pChan = &m_Channels[uChan];
//	if (uChan==7)
//	ConDebug("dma%d[%02X]=%02X\n", uChan, uAddr & 0xF, uData);

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
//		ConDebug("write_dma8[%06X]=%02X\n", uAddr, uData);
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
		if (g_DbgCaptureActive)
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
			g_DbgDMAWraps++;

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
        SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 1);
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

#if 0
Uint32 SnesDMAC::ProcessMDMACh(Uint32 uChan)
{
	SnesDMAChT *pChan;
	Int32 uSrcDelta;
	Uint8 *pTransfer;
	Int32 iTransfer=0;

	assert(uChan < SNESDMAC_CHANNEL_NUM);

	pChan = &m_Channels[uChan];
	
	#if SNESDMA_DEBUG
	ConDebug("dma%d: %02X %02X%04X -> %02X %04X (%04X)\n", uChan, 
		pChan->dmapx,
		pChan->a1bx, 
		pChan->a1tx, 
		pChan->bbadx, 
		pChan->dasx,
		m_pPPU->m_Regs.vmaddr.w
		);
	#endif


	if (pChan->dmapx & 0x80)
	{
		// ppu -> mem
		return ProcessMDMAChRead(uChan);
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

		// read byte
		uData = SNCPURead8(m_pCPU, pChan->a1tx | (pChan->a1bx << 16));

		// increment src address (does overflow go into next bank?)
		pChan->a1tx += uSrcDelta;

		// get address to write to (b-bus)
		uAddr = 0x2100 + pChan->bbadx + pTransfer[iTransfer & 3];
		iTransfer++;

		// write byte
		SNCPUWrite8(m_pCPU, uAddr, uData);

		// decrement cpu clock cycles
		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 1);

		// decrement byte count
		pChan->dasx--;
	}
	while (pChan->dasx!=0);

	return 1;
}
#endif

void SnesDMAC::TransferData(SnesDMAChT *pChan, Uint8 *pData, Int32 nBytes)
{
    SNCPUConsumeCycles(m_pCPU,  SNCPU_CYCLE_SLOW * nBytes);

    // special case simple transfer mode llll
	if ((pChan->dmapx & 7)==0)
	{
		switch (pChan->bbadx)
		{
		case 0x04: // oamdata (oam data)
			m_pPPU->WriteOAMBlock(pData, nBytes);
			break;
		case 0x18: // vmaddl (video port address low)
			while (nBytes > 0)
			{
				m_pPPU->WriteVMDATAL(*pData++);
				nBytes--;
			}
			break;
		case 0x19: // vmdatah (video port data hi)
			while (nBytes > 0)
			{
				m_pPPU->WriteVMDATAH(*pData++);
				nBytes--;
			}
			break;
		case 0x22: // cgdata (color data)
			while (nBytes > 0)
			{
				m_pPPU->WriteCGDATA(*pData++);
				nBytes--;
			}
			break;

		default:
			// generic write byte
			while (nBytes > 0)
			{
				SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, *pData++);
				nBytes--;
			}
		}

	} else
	if ((pChan->dmapx & 7)==1 && pChan->bbadx==0x18)
	{
		m_pPPU->WriteVMDATABlock(pData, nBytes);
	} else
	{
		Uint8 *pTransfer;
		Int32 iTransfer=0;

		// get transfer order
		pTransfer = _SNDma_MDMATransfer[pChan->dmapx & 7];

		while (nBytes > 0)
		{
			Uint8 uData;
			Uint32 uAddr;

			// fetch data byte
			uData = pData[iTransfer];

			// get address to write to (8-bit b-bus, wrapping at $21FF)
			uAddr = (pChan->bbadx + pTransfer[iTransfer & 3]) & 0xFF;
			iTransfer++;

			switch (uAddr)
			{
			case 0x04: // oamdata (oam data)
				m_pPPU->WriteOAMDATA(uData);
				break;
			case 0x18: // vmaddl (video port address low)
				m_pPPU->WriteVMDATAL(uData);
				break;
			case 0x19: // vmdatah (video port data hi)
				m_pPPU->WriteVMDATAH(uData);
				break;
			case 0x22: // cgdata (color data)
				m_pPPU->WriteCGDATA(uData);
				break;

			default:
				/* PPU MDMA bytes must bypass the normal per-scanline write
				   queue.  First Samurai uses mode 4 at BBAD=$16, producing
				   $2116,$2117,$2118,$2119 groups; queuing only the first
				   two made every tile word land at a stale VRAM address. */
				if (uAddr < 0x40)
					SnesDMAWritePPUPort(m_pPPU, uAddr, uData);
				else
					SNCPUWrite8(m_pCPU, 0x2100 + uAddr, uData);
			}
			nBytes--;
		}
	}
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
			m_pSDD1->Decompress(s_DecodeBuf, pIn, (Int32)pChan->dasx);
			TransferData(pChan, s_DecodeBuf, count);
#if SNDBG_LOG
			{
				static int n = 0;
				if (n < 120) {
					DLog("[sdd1] dma ch=%d src=%06X cnt=%d bbad=%02X mode=%d hdr=%02X out=%02X%02X%02X%02X",
						(int)uChan, (unsigned)srcAddr, (int)count,
						(int)pChan->bbadx, (int)(pChan->dmapx & 7),
						(int)pIn[0], (int)s_DecodeBuf[0], (int)s_DecodeBuf[1],
						(int)s_DecodeBuf[2], (int)s_DecodeBuf[3]);
					n++;
				}
			}
#endif
		}
#if SNDBG_LOG
		else
		{
			static int nn = 0;
			if (nn < 40) {
				DLog("[sdd1] dma ch=%d src=%06X SEM PONTEIRO (banco nao mapeado!)",
					(int)uChan, (unsigned)srcAddr);
				nn++;
			}
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
					Int32 nFirst = nBytes;
					Int32 nToBankEnd = 0x10000 - (Int32)pChan->a1tx;

					if (nFirst > nToBankEnd)
						nFirst = nToBankEnd;

			        // A1T wraps inside A1B. Keep both pieces in one buffer so
			        // TransferData also keeps its B-bus mode phase continuous.
			        SNCPUReadMem(m_pCPU,
			                     pChan->a1tx | (pChan->a1bx << 16),
			                     DmaBuffer, nFirst);
					if (nFirst < nBytes)
					{
						SNCPUReadMem(m_pCPU, pChan->a1bx << 16,
						             DmaBuffer + nFirst, nBytes - nFirst);
					}

			        // increment src address 
			        pChan->a1tx = (Uint16)(pChan->a1tx + nBytes);
                }
			    break;
		    case 2: //-1 decrement
			    {
                    // read data into dma buffer (decrement)
				    Int32 iByte;
				    for (iByte=0; iByte < nBytes; iByte++)
				    {
					    DmaBuffer[iByte] = SNCPURead8(m_pCPU, pChan->a1tx | (pChan->a1bx << 16));
					    pChan->a1tx--;
				    }
			    }
			    break;
		    case 1:
		    case 3: // 0
			    // read data into dma buffer (no increment)
			    memset(DmaBuffer, SNCPURead8(m_pCPU, pChan->a1tx | (pChan->a1bx << 16)), nBytes);
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

	/* Mesen: HDMA initialization has one 8-clock global overhead, then
	   initializes every enabled channel before the first scanline transfer. */
	SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);

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
		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);
		pChan->a2ax++;

		bStopped = pChan->ntlrx == 0;
		if (bStopped)
			m_HDMAEnded |= uMask;

		if (pChan->dmapx & 0x40)
		{
			uLow = SnesHDMARead8(m_pCPU,
				(Uint16)pChan->a2ax | (pChan->a1bx << 16));
			SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);
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
				SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);
				pChan->a2ax++;
			}
		}
	}
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
		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);
	}
}






void SnesDMAC::ProcessMDMA()
{
    Uint32 uChan = 0;

    while (m_MDMAEnable && (m_pCPU->Cycles > 0))
    {
        if (m_MDMAEnable & (1 << uChan))
        {
            // process channel
            ProcessMDMAChFast(uChan);
        } 
        else
        {
            // next channel
            uChan++;
        }
    }
}

void SnesDMAC::ProcessHDMA(Uint32 uLine)
{
	Uint8 uActive = m_HDMAEnable & ~m_HDMAEnded;

	if (!uActive)
		return;

#if SNDBG_LOG
	Uint32 _tHDMAData = ProfCtrGetCycle();
	g_DbgHDMALines++;
#endif

	/* Mesen performs every channel's data phase first, followed by every
	   channel's counter/table phase.  Interleaving those phases changes both
	   B-bus side effects and the point at which IRQ/NMI can be observed. */
	SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);
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
		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);

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
				SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);
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
					SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW);
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
}

void SnesDMAC::Reset()
{
	memset(m_Channels, 0xFF, sizeof(m_Channels));
	m_MDMAEnable = 0;
	m_HDMAEnable = 0;
	m_HDMAEnded = 0;
	m_HDMADoTransfer = 0;
}
