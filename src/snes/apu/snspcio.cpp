/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snspcio behavior for SNES audio processing.
 */

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "console.h"
#include "snspcio.h"
#include "sntiming.h"
extern "C" {
#include "snspc.h"
};
#include "snspcdsp.h"

#define SNES_DEBUGSPCIO (CODE_DEBUG && FALSE)

#if SNES_DEBUGSPCIO
Uint32  _SpcOp;
Uint32 _SpcAddr;
Uint32 _SpcData;

static void _SpcDebugRead(SNSpcT *pSpc, Uint32 uAddr, Uint32 uData)
{
	if (_SpcOp || _SpcAddr!=uAddr || _SpcData!=uData)
	{
		_SpcOp = 0;
		_SpcAddr = uAddr;
		_SpcData = uData;
	}
}

static void _SpcDebugWrite(SNSpcT *pSpc, Uint32 uAddr, Uint32 uData)
{
	if (!_SpcOp || _SpcAddr!=uAddr || _SpcData!=uData)
	{
		ConDebug("%08d: spc write %04X %02X %02X %02X %02X %02X %08X\n",
			SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL, 0),
			uAddr, uData,
			pSpc->Regs.rA,
			pSpc->Regs.rX,
			pSpc->Regs.rY,
			pSpc->Regs.rSP,
			SNSPCMemChecksum(pSpc)
			);
		_SpcOp = 1;
		_SpcAddr = uAddr;
		_SpcData = uData;
	}
}
#endif

Bool SNSpcIO::EnqueueWrite(Uint32 uCycle, Uint32 uAddr, Uint8 uData)
{
	return m_Queue.Enqueue(uCycle, uAddr, uData);
}

void SNSpcIO::ResetCpuPortPending()
{
	m_uCpuPendingMask = 0;
	memset(m_CpuPendingData, 0, sizeof(m_CpuPendingData));
	memset(m_CpuPendingCycle, 0, sizeof(m_CpuPendingCycle));
}

void SNSpcIO::SyncCpuPorts(Uint32 uSpcMasterCycle)
{
	Uint32 i;
	Uint8 uMask = m_uCpuPendingMask;

	for (i = 0; i < 4 && uMask; ++i)
	{
		Uint8 uBit = (Uint8)(1u << i);
		if (!(uMask & uBit))
			continue;

		/* Signed subtraction is wrap-safe as long as events are less than
		   2^31 master clocks apart (we only ever defer by one SPC cycle). */
		if ((Int32)(uSpcMasterCycle - m_CpuPendingCycle[i]) >= 0)
		{
			m_Regs.apu_w[i] = m_CpuPendingData[i];
			m_uCpuPendingMask &= (Uint8)~uBit;
			uMask &= (Uint8)~uBit;
		}
	}
}

void SNSpcIO::WriteCpuPort(Uint32 uCpuMasterCycle, Uint32 uSpcMasterCycle,
	Uint32 uPort, Uint8 uData)
{
	Uint8 uBit;

	(void)uCpuMasterCycle;
	uPort &= 3u;
	uBit = (Uint8)(1u << uPort);
	m_CpuPendingData[uPort] = uData;
	m_CpuPendingCycle[uPort] = uSpcMasterCycle;
	m_Regs.apu_w[uPort] = uData;
	m_uCpuPendingMask &= (Uint8)~uBit;
}

void SNSpcIO::ClearCpuPorts(Uint8 uPortMask)
{
	Uint32 i;
	for (i = 0; i < 4; ++i)
	{
		Uint8 uBit = (Uint8)(1u << i);
		if (uPortMask & uBit)
		{
			m_Regs.apu_w[i] = 0;
			m_CpuPendingData[i] = 0;
			m_uCpuPendingMask &= (Uint8)~uBit;
		}
	}
}

void SNSpcIO::SyncQueueAll()
{
	SNQueueElementT *pElement;

	// dequeue all pending writes
	while ( (pElement=m_Queue.Dequeue()) != NULL)
	{
		// perform write
		m_Regs.apu_w[pElement->uAddr] = pElement->uData;
	}

	// empty queue
	m_Queue.Reset();
}

void SNSpcIO::SyncQueue(Uint32 uCycle)
{
	SNQueueElementT *pElement;

	/* CPU->SPC writes become visible when the SPC reaches the same master
	   timestamp.  The generic PPU queue intentionally uses a strict compare,
	   but APUIO needs the inclusive edge. */
	while ( (pElement=m_Queue.DequeueAtOrBefore(uCycle)) != NULL)
		m_Regs.apu_w[pElement->uAddr] = pElement->uData;
}

void SNSpcIO::Reset()
{
	memset(&m_Regs, 0, sizeof(m_Regs));
	ResetCpuPortPending();

	m_Queue.Reset();

	SNSpcTimerReset(&m_Regs.spc_timer[0], 128 * SNSPC_CYCLE); //SNES_MASTERCLOCKRATE / 8000);
	SNSpcTimerReset(&m_Regs.spc_timer[1], 128 * SNSPC_CYCLE); //SNES_MASTERCLOCKRATE / 8000);
	SNSpcTimerReset(&m_Regs.spc_timer[2], 16  * SNSPC_CYCLE); //SNES_MASTERCLOCKRATE / 64000);
}

#if SNES_STATEDEBUG
extern "C" Bool g_bStateDebug;
#endif

Uint8 SNSpcIO::Read8Trap(SNSpcT *pSpc, Uint32 uAddr)
{
	SNSpcIO *pIO = (SNSpcIO *)pSpc->pUserData;

#if SNES_STATEDEBUG
	if (g_bStateDebug)
		ConDebug("read_spc[%04X] %d %d %04X\n", uAddr, pSpc->Cycles, pIO->m_Regs.spc_timer[0].nElapsedCycles, pSpc->Regs.rPC);
#endif
	switch (uAddr)
	{
	case 0xF2:
		return pSpc->Mem[uAddr];

	case 0xF3:
		pIO->m_pSpcDsp->Sync();
		return pIO->m_pSpcDsp->Read8(pSpc->Mem[0xF2]);
	case 0xF4: // port 0-4
	case 0xF5:
	case 0xF6:
	case 0xF7:
		#if SNES_DEBUGSPCIO
		#endif

		pIO->SyncCpuPorts((Uint32)SNSPCGetCounter(
			pSpc, SNSPC_COUNTER_TOTAL));
		return pIO->m_Regs.apu_w[uAddr & 3];

	case 0xFD:	// counter0
		return SNSpcTimerGetCounter(&pIO->m_Regs.spc_timer[0], SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL));
	case 0xFE:	// counter1
		return SNSpcTimerGetCounter(&pIO->m_Regs.spc_timer[1], SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL));
	case 0xFF:	// counter2
		return SNSpcTimerGetCounter(&pIO->m_Regs.spc_timer[2], SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL));
	default:
#if SNES_DEBUGPRINT
		ConDebug("read_spc[%04X]\n", uAddr);
#endif
		return 	pSpc->Mem[uAddr];
	}
}

void SNSpcIO::Write8Trap(SNSpcT *pSpc, Uint32 uAddr, Uint8 uData)
{
	SNSpcIO *pIO = (SNSpcIO *)pSpc->pUserData;
	Int32 iCycle;

	iCycle = SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL);

#if SNES_STATEDEBUG
	if (g_bStateDebug)
		ConDebug("write_spc[%04X]=%02X %d %d %04X\n", uAddr, uData, pSpc->Cycles, pIO->m_Regs.spc_timer[0].nElapsedCycles, pSpc->Regs.rPC);
#endif
	switch (uAddr)
	{
	case 0xF1:	// control
		{
			/* Apply already-visible CPU writes, then clear both the live latch
			   and any deferred value for the selected port pair. */
			pIO->SyncCpuPorts((Uint32)SNSPCGetCounter(
				pSpc, SNSPC_COUNTER_TOTAL));
			if (uData&0x10)
			{
				pIO->ClearCpuPorts(0x03);
				pSpc->Mem[0xf4] = 0x00;
				pSpc->Mem[0xf5] = 0x00;
			}
			if (uData&0x20)
			{
				pIO->ClearCpuPorts(0x0C);
				pSpc->Mem[0xf6] = 0x00;
				pSpc->Mem[0xf7] = 0x00;
			}
			SNSpcTimerSetEnable(&pIO->m_Regs.spc_timer[0], iCycle, (uData & 1));
			SNSpcTimerSetEnable(&pIO->m_Regs.spc_timer[1], iCycle, (uData & 2));
			SNSpcTimerSetEnable(&pIO->m_Regs.spc_timer[2], iCycle, (uData & 4));

			// set rom enable
			SNSPCSetRomEnable(pSpc, uData & 0x80);
		}
		break;
	case 0xF2:	// dsp addr
		break;
	case 0xF3:  // dsp data
		while (!pIO->m_pSpcDsp->EnqueueWrite(SNSPCGetCounter(pSpc, SNSPC_COUNTER_FRAME), pSpc->Mem[0xF2] & 0x7F, uData))
		{
			pIO->m_pSpcDsp->Sync();
		}
		//pIO->m_pSpcDsp->Write8(pSpc->Mem[0xF2] & 0x7F, uData);
		break;

	case 0xF4:
	case 0xF5:
	case 0xF6:
	case 0xF7:
		pIO->m_Regs.apu_r[uAddr & 3] = uData;
		#if SNES_DEBUGSPCIO
		_SpcDebugWrite(pSpc, uAddr, uData);
		#endif
		break;

	case 0xFA:	// timer0
		SNSpcTimerSync(&pIO->m_Regs.spc_timer[0], iCycle);
		SNSpcTimerSetTimer(&pIO->m_Regs.spc_timer[0], uData);
		break;
	case 0xFB:	// timer1
		SNSpcTimerSync(&pIO->m_Regs.spc_timer[1], iCycle);
		SNSpcTimerSetTimer(&pIO->m_Regs.spc_timer[1], uData);
		break;
	case 0xFC:	// timer2
		SNSpcTimerSync(&pIO->m_Regs.spc_timer[2], iCycle);
		SNSpcTimerSetTimer(&pIO->m_Regs.spc_timer[2], uData);
		break;

	default:
#if SNES_DEBUGPRINT
		ConDebug("write_spc[%04X]=%02X\n", uAddr, uData);
#endif
		break;
	}

	// store to memory
	pSpc->Mem[uAddr] = uData;
}
