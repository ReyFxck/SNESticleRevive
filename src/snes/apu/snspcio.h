/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snspcio interface for SNES audio processing.
 */

#ifndef _SNSPCIO_H
#define _SNSPCIO_H

#include <string.h>

#include "snspctimer.h"
#include "snesreg.h"
#include "snqueue.h"

struct SNSpc_t;
class SNSpcDsp;

#define SNSPCIO_WRITEQUEUE (TRUE)

struct SNSpcIORegsT
{
	SnesReg8T		apu_r[4];		// apu read ports
	SnesReg8T		apu_w[4];		// apu write ports
	SNSpcTimerT		spc_timer[3];	// spc timers
};

class SNSpcIO
{
	SNSpc_t			*m_pSpc;
	SNSpcDsp		*m_pSpcDsp;

	/* Retained for legacy state/layout compatibility. CPU writes are published
	   synchronously after the SPC has caught up, so no deferred write should
	   remain during normal execution. */
	Uint8			m_uCpuPendingMask;
	Uint8			m_CpuPendingData[4];
	Uint32			m_CpuPendingCycle[4];

public:
	SNSpcIORegsT	m_Regs;

	#if SNSPCIO_WRITEQUEUE
	SNQueue			m_Queue;
	#endif

public:

	/* DSPADDR.7 selects read-only mirrors: reads alias $00-$7f, writes
	   must not reach the underlying S-DSP registers. */
	static Bool CanWriteDspPort(Uint8 uDspAddress)
	{
		return (uDspAddress & 0x80u) == 0;
	}

	static Uint8 Read8Trap(struct SNSpc_t *pSpc, Uint32 uAddr);
	static void Write8Trap(struct SNSpc_t *pSpc, Uint32 uAddr, Uint8 uData);

	void	Reset();
	void	ResetCpuPortPending();
	void	WriteCpuPort(Uint32 uCpuMasterCycle, Uint32 uSpcMasterCycle,
		Uint32 uPort, Uint8 uData);
	void	SyncCpuPorts(Uint32 uSpcMasterCycle);
	void	ClearCpuPorts(Uint8 uPortMask);
	void	SetSpc(struct SNSpc_t *pSpc) {m_pSpc = pSpc;}
	void	SetSpcDsp(SNSpcDsp *pSpcDsp) {m_pSpcDsp = pSpcDsp;}

	void	SaveState(struct SNStateSPCIOT *pState);
	void	RestoreState(struct SNStateSPCIOT *pState);
	void	CopyPendingCpuPorts(Uint8 *pMask, Uint8 pData[4],
		Uint32 pCycle[4]) const
	{
		*pMask = m_uCpuPendingMask;
		memcpy(pData, m_CpuPendingData, sizeof(m_CpuPendingData));
		memcpy(pCycle, m_CpuPendingCycle, sizeof(m_CpuPendingCycle));
	}
	void	RestorePendingCpuPorts(Uint8 uMask, const Uint8 pData[4],
		const Uint32 pCycle[4])
	{
		m_uCpuPendingMask = uMask & 0x0F;
		memcpy(m_CpuPendingData, pData, sizeof(m_CpuPendingData));
		memcpy(m_CpuPendingCycle, pCycle, sizeof(m_CpuPendingCycle));
	}
	Int32	CopyWriteQueue(SNQueueElementT *pOut, Int32 nMax) const
	{
		return m_Queue.CopyPending(pOut, nMax);
	}
	void	RestoreWriteQueue(const SNQueueElementT *pIn, Int32 nCount)
	{
		m_Queue.RestorePending(pIn, nCount);
	}

	#if SNSPCIO_WRITEQUEUE
	Bool	EnqueueWrite(Uint32 uCycle, Uint32 uAddr, Uint8 uData);
	void	SyncQueue(Uint32 uCycle);
	void	SyncQueueAll();
	#endif

};

#endif
