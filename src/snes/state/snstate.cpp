/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snstate behavior for SNES save-state serialization.
 */

#include <string.h>
#include <stdio.h>
#include "types.h"
#include "console.h"
#include "snes.h"
#include "snstate.h"

void SnesSystem::SaveState(void *pState, Int32 nStateBytes)
{
    if (nStateBytes == sizeof(SnesStateT))
    {
        SaveState((SnesStateT *)pState);
    }
}

void SnesSystem::RestoreState(void *pState, Int32 nStateBytes)
{
	if (nStateBytes == (Int32)sizeof(SnesStateT))
	{
		RestoreState((SnesStateT *)pState);
	}
	else if (nStateBytes == (Int32)SNSTATE_LEGACY_BYTES)
	{
		SnesStateT State;
		memset(&State, 0, sizeof(State));
		memcpy(&State, pState, SNSTATE_LEGACY_BYTES);
		RestoreState(&State);
	}
}

Int32 SnesSystem::GetStateSize()
{
    return sizeof(SnesStateT);
}

void SnesSystem::SaveState(SnesStateT *pState)
{
	/* The state is persisted as an opaque payload by the PS2 front-end.
	   Clear padding and currently-unused fields first so two equivalent
	   states have deterministic bytes (and therefore a deterministic
	   CRC), instead of leaking whatever happened to be in the buffer. */
	memset(pState, 0, sizeof(*pState));

	// set tag
	pState->Tag[0] = 'S';
	pState->Tag[1] = 'N';
	pState->Tag[2] = 'S';
	pState->Tag[3] = '\0';

	pState->uFrame = m_uFrame;
	pState->uLine  = m_uLine;

	// copy cpu state
	pState->CPU.Regs = m_Cpu.Regs;
	pState->CPU.Cycles = m_Cpu.Cycles;
	pState->CPU.Counter[0] = m_Cpu.Counter[0];
	pState->CPU.Counter[1] = m_Cpu.Counter[1];
	pState->CPU.Counter[2] = m_Cpu.Counter[2];
	pState->CPU.Counter[3] = m_Cpu.Counter[3];
	pState->CPU.uSignal    = m_Cpu.uSignal;

	pState->SPC.Regs = m_Spc.Regs;
	pState->SPC.Cycles = m_Spc.Cycles;
	pState->SPC.Counter[0] = m_Spc.Counter[0];
	pState->SPC.Counter[1] = m_Spc.Counter[1];
	/* Reuse the legacy byte without changing SnesStateT size: it now carries
	   the S-SMP $F0 TEST latch. */
	pState->SPC.uCycleShift = m_Spc.uTestReg;

	m_PPU.SaveState(&pState->PPU);
	m_DMAC.SaveState(&pState->DMAC);
	m_IO.SaveState(&pState->IO);
	m_SpcDsp.SaveState(&pState->SPCDSP);
	m_SpcDspMixer.SaveState(&pState->SPCDSP);
	m_SpcIO.SaveState(&pState->SPCIO);
	m_SA1.SaveState(&pState->SA1);

	/* v2 appended DSP runtime state. The prefix above remains compatible with
	   old saves; this block makes an in-flight audio state deterministic. */
	pState->DspRuntime.uMagic = SNSTATE_DSP_EXT_MAGIC;
	pState->DspRuntime.uVersion = SNSTATE_DSP_EXT_VERSION;
	pState->DspRuntime.uFullDspCounter = m_SpcDspMixer.GetDspCounter();
	pState->DspRuntime.uSilentDspCounter = m_SpcDspSilentMixer.GetDspCounter();
	m_SpcDspMixer.CopyTransientState(
		&pState->DspRuntime.Echo,
		&pState->DspRuntime.iNoisePhase,
		&pState->DspRuntime.uNoiseGen);
	m_SpcDspSilentMixer.CopyChannels(pState->DspRuntime.SilentChannels);
	pState->DspRuntime.nDspQueue = m_SpcDsp.CopyWriteQueue(
		pState->DspRuntime.DspQueue, SNQUEUE_SIZE);
	pState->DspRuntime.nSpcIoQueue = m_SpcIO.CopyWriteQueue(
		pState->DspRuntime.SpcIoQueue, SNQUEUE_SIZE);
	m_SpcIO.CopyPendingCpuPorts(
		&pState->DspRuntime.uCpuPendingMask,
		pState->DspRuntime.CpuPendingData,
		pState->DspRuntime.CpuPendingCycle);

	// save memory state
	memcpy(pState->Ram, m_Ram, sizeof(pState->Ram));
	memcpy(pState->SRam, m_SRam, sizeof(pState->SRam));

    // copy spc ram
    pState->SPC.bRomEnable = m_Spc.bRomEnable;

    SNSPCSetRomEnable(&m_Spc, FALSE);
	memcpy(pState->SpcRam, m_Spc.Mem, SNSPC_MEM_SIZE);
    SNSPCSetRomEnable(&m_Spc,  pState->SPC.bRomEnable);
}

Bool SnesSystem::RestoreState(SnesStateT *pState)
{
	if (memcmp(pState->Tag, "SNS", 4))
	{
		return FALSE;
	}

	m_uFrame = pState->uFrame;
	m_uLine  = pState->uLine;

	// restore state
	m_Cpu.Regs = pState->CPU.Regs;
	m_Cpu.Cycles = pState->CPU.Cycles;
	m_Cpu.Counter[0] = pState->CPU.Counter[0];
	m_Cpu.Counter[1] = pState->CPU.Counter[1];
	m_Cpu.Counter[2] = pState->CPU.Counter[2];
	m_Cpu.Counter[3] = pState->CPU.Counter[3];
	m_Cpu.nAbortCycles = 0;
	m_Cpu.uSignal    = pState->CPU.uSignal;
	m_Cpu.uNmiDmaDelay = 0;
	m_Cpu.uIrqPending = 0;
	/* Legacy states have no CPU data-bus or 5A22 machine-cycle timestamp. */
	m_Cpu.uOpenBus = 0;
	m_Cpu.uCpuCycleCount = 0;

	m_Spc.Regs = pState->SPC.Regs;
	m_Spc.Cycles = pState->SPC.Cycles;
	m_Spc.Counter[0] = pState->SPC.Counter[0];
	m_Spc.Counter[1] = pState->SPC.Counter[1];
	/* Old states wrote zero here. Treat that legacy zero as the hardware
	   power-on TEST value so loading an old state does not disable APURAM. */
	m_Spc.uTestReg = pState->SPC.uCycleShift ?
		pState->SPC.uCycleShift : 0x0A;

	m_PPU.RestoreState(&pState->PPU);
	/* Region is hardware identity, not game state. Older PAL save states were
	   written while STAT78 bit 4 was incorrectly clear, so re-derive it from
	   the currently loaded ROM after restoring the serialized PPU registers. */
	m_PPU.SetVideoRegion(
		m_pRom && m_pRom->m_eVideoType == SNROM_VIDEO_PAL ? TRUE : FALSE);
	m_DMAC.RestoreState(&pState->DMAC);
	m_IO.RestoreState(&pState->IO);
	/* The legacy state payload does not contain the DSP write queue,
	   echo history or noise generator.  Reset those transient pieces
	   before applying the serialized registers/channels so a load does
	   not inherit audio work from the future state it is replacing. */
	m_SpcDsp.Reset();
	m_SpcDspMixer.Reset();
	m_SpcDspSilentMixer.Reset();
	m_SpcDsp.RestoreState(&pState->SPCDSP);
	m_SpcDspMixer.RestoreState(&pState->SPCDSP);
	m_SpcIO.RestoreState(&pState->SPCIO);

	if (pState->DspRuntime.uMagic == SNSTATE_DSP_EXT_MAGIC &&
	    pState->DspRuntime.uVersion == SNSTATE_DSP_EXT_VERSION)
	{
		m_SpcDspMixer.SetDspCounter(pState->DspRuntime.uFullDspCounter);
		m_SpcDspMixer.RestoreTransientState(
			&pState->DspRuntime.Echo,
			pState->DspRuntime.iNoisePhase,
			pState->DspRuntime.uNoiseGen);
		m_SpcDspSilentMixer.RestoreChannels(
			pState->DspRuntime.SilentChannels);
		m_SpcDspSilentMixer.SetDspCounter(
			pState->DspRuntime.uSilentDspCounter);
		m_SpcDsp.RestoreWriteQueue(
			pState->DspRuntime.DspQueue,
			pState->DspRuntime.nDspQueue);
		m_SpcIO.RestoreWriteQueue(
			pState->DspRuntime.SpcIoQueue,
			pState->DspRuntime.nSpcIoQueue);
		m_SpcIO.RestorePendingCpuPorts(
			pState->DspRuntime.uCpuPendingMask,
			pState->DspRuntime.CpuPendingData,
			pState->DspRuntime.CpuPendingCycle);
	}
	else
	{
		/* Legacy states did not contain transient audio state. Keep both mixers
		   coherent and resume from a clean DSP/noise/echo/queue boundary. */
		m_SpcDspSilentMixer.RestoreState(&pState->SPCDSP);
	}

	// Restore shared RAM before rebuilding the SA-1 maps, because BW-RAM is
	// backed by m_SRam and the coprocessor keeps only that live pointer.
	memcpy(m_Ram, pState->Ram, sizeof(m_Ram));
	memcpy(m_SRam, pState->SRam, sizeof(m_SRam));
	if (m_bSA1)
	{
		m_SA1.SetMemory(m_pRom ? m_pRom->GetData() : NULL,
		                 m_pRom ? m_pRom->GetBytes() : 0,
		                 m_SRam, m_uSramSize);
		m_SA1.RestoreState(&pState->SA1);
		for (Uint32 i = 0; i < 4; i++)
			RemapSA1ROM(i, pState->SA1.State.Registers[0x020 + i]);
		RefreshSCPUIRQ();
	}

	// Base RAM/SRAM were restored above so coprocessor maps already point at
	// the restored backing bytes.

    // copy spc ram
    SNSPCSetRomEnable(&m_Spc, FALSE);
	memcpy(m_Spc.Mem, pState->SpcRam, SNSPC_MEM_SIZE);
    SNSPCSetRomEnable(&m_Spc, pState->SPC.bRomEnable);

	// set fast or slow rom
	if (m_IO.m_Regs.memsel & 1)
	{
		SetFastRom();
	} else
	{
		SetSlowRom();
	}

	/* Per-scanline SA-1 synchronization is transient scheduler state.  A
	   restored frame begins a fresh line slice. */
	m_nSA1LineClock = 0;
	m_uNmiFlagSetClock = GetSCPUMasterClock();
	m_uIrqFlagSetClock = GetSCPUMasterClock();

	return TRUE;
}

void SnesIO::SaveState(struct SNStateIOT *pState)
{
	pState->Input = m_Input;
	pState->Regs = m_Regs;
}
void SnesIO::RestoreState(struct SNStateIOT *pState)
{
	m_Input = pState->Input;
	m_Regs = pState->Regs;
	/* The existing state format predates in-flight ALU timing. Preserve its
	   binary size: restored result/input latches remain, pending work stops. */
	ResetALUTiming(0);
	m_uAutoReadClockStart = 0;
	m_uAutoReadNextClock = 0;
	m_uAutoReadPort1Value = 0;
	m_uAutoReadPort2Value = 0;
	m_bAutoReadActive = FALSE;
	m_bAutoReadDisabled = TRUE;
	/* Keep the historical state-file size unchanged. Peripheral reports are
	   re-latched on the next OUT0 strobe after restore. */
	m_uMouseSerial = 0xFFFFFFFFu;
	m_uScopeSerial = 0xFFFFu;
	m_uJustifierSerial = 0xFFFFFFFFu;
	m_uMouseSensitivity = 0;
	m_uJustifierActive = 0;
	m_uPeripheralPad = 0;
}

void SNSpcIO::SaveState(struct SNStateSPCIOT *pState)
{
	pState->Regs = m_Regs;
}
void SNSpcIO::RestoreState(struct SNStateSPCIOT *pState)
{
	m_Regs = pState->Regs;
	/* The legacy state format never serialized sub-cycle APUIO work. */
	ResetCpuPortPending();
	m_Queue.Reset();
}

void SnesDMAC::SaveState(struct SNStateDMACT *pState)
{
	pState->m_HDMAEnable = m_HDMAEnable;
	pState->m_HDMAEnded = m_HDMAEnded;
	pState->m_HDMADoTransfer = m_HDMADoTransfer;
	pState->m_MDMAEnable = m_MDMAEnable;
	memcpy(pState->m_Channels, m_Channels, sizeof(pState->m_Channels));
}

void SnesDMAC::RestoreState(struct SNStateDMACT *pState)
{
	m_HDMAEnable = pState->m_HDMAEnable;
	m_HDMAEnded = pState->m_HDMAEnded;
	m_HDMADoTransfer = pState->m_HDMADoTransfer;
	m_MDMAEnable = pState->m_MDMAEnable;
	memcpy(m_Channels, pState->m_Channels, sizeof(m_Channels));
	/* Legacy states do not serialize sub-cycle DMA ownership. Resume from a
	   clean boundary and let an enabled MDMA command reacquire the bus. */
	m_MDMAStartedMask = 0;
	m_bDMATimingActive = FALSE;
	m_uDMAClockCounter = 0;
}

void SnesPPU::SaveState(struct SNStatePPUT *pState)
{
	pState->Regs  = m_Regs;
	memcpy(pState->m_CGRAM,   m_CGRAM, sizeof(pState->m_CGRAM));
	memcpy(pState->m_VRAM,    m_VRAM, sizeof(pState->m_VRAM));
	pState->m_OAM = m_OAM;
}

void SnesPPU::RestoreState(struct SNStatePPUT *pState)
{
	m_Regs = pState->Regs;
	memcpy(m_CGRAM,   pState->m_CGRAM, sizeof(m_CGRAM));
	m_pRender->InvalidateHiresPalette();
	memcpy(m_VRAM,    pState->m_VRAM,  sizeof(m_VRAM));
	m_OAM = pState->m_OAM;
	m_OAMLatch = 0;
	/* The legacy state format has no half-written $2122 latch.  States are
	   normally captured at frame boundaries; use the current entry's low byte
	   as the safest reconstruction without changing the on-disk format. */
	m_CGRAMLatch = (Uint8)(m_CGRAM[(m_Regs.cgadd.w >> 1) &
	                              (SNESPPU_CGRAM_NUM - 1)] & 0xFF);
	m_PPU1OpenBus = 0;
	m_PPU2OpenBus = 0;
	m_bCountersLatched = FALSE;
	m_pRender->UpdateVRAMRange(0, SNESPPU_VRAM_NUMWORDS);
	UpdateOAMPriority();
}

void SNSpcDsp::SaveState(struct SNStateSPCDSPT *pState)
{
	memcpy(pState->m_Regs, m_Regs, sizeof(m_Regs));
}

void SNSpcDsp::RestoreState(struct SNStateSPCDSPT *pState)
{
	memcpy(m_Regs, pState->m_Regs, sizeof(m_Regs));
}

void SNSpcDspMix::SaveState(struct SNStateSPCDSPT *pState)
{
	memcpy(pState->m_Channels, m_Channels, sizeof(m_Channels));
}

void SNSpcDspMix::RestoreState(struct SNStateSPCDSPT *pState)
{
	memcpy(m_Channels, pState->m_Channels, sizeof(m_Channels));

	/* State format v1 originally stored the Revive 23-bit fixed-point
	   envelope/count. New states use the native 11-bit S-DSP envelope in the
	   same fields. Detect the old representation without changing payload size. */
	for (Int32 i = 0; i < SNSPCDSP_CHANNEL_NUM; ++i)
	{
		SNSpcChannelT *pChannel = &m_Channels[i];
		if (pChannel->iEnvelope > 0x7FF ||
		    pChannel->iEnvelope < 0 ||
		    pChannel->nEnvCount > 0x7FF ||
		    pChannel->nEnvCount < 0)
		{
			Int32 iEnvelope = pChannel->iEnvelope;
			if (iEnvelope < 0) iEnvelope = 0;
			iEnvelope >>= 12; /* 23-bit 1.0 -> 11-bit 1.0 */
			if (iEnvelope > 0x7FF) iEnvelope = 0x7FF;
			pChannel->iEnvelope = iEnvelope;
			pChannel->nEnvCount = iEnvelope;
			pChannel->pad = 0;
		}
	}
}

void _SNStateMemDiff(const char *pTag, Uint8 *pA, Uint8 *pB, Int32 nBytes)
{
    Int32 iOffset;

    for (iOffset=0; iOffset < nBytes; iOffset++)
    {
        if (pA[iOffset]!=pB[iOffset])
        {
            ConDebug("%s: %04X %02X %02X\n", pTag, iOffset, pA[iOffset], pB[iOffset]);
        }
    }

}

void SNStateCompare(SnesStateT *pStateA, SnesStateT *pStateB)
{
    _SNStateMemDiff("Mem", pStateA->Ram, pStateB->Ram, SNES_RAMSIZE);
    _SNStateMemDiff("SpcMem", pStateA->SpcRam, pStateB->SpcRam, SNSPC_RAM_SIZE);
    _SNStateMemDiff("SRM", pStateA->SRam, pStateB->SRam, SNES_SRAMSIZE);
    _SNStateMemDiff("Cpu", (Uint8 *)&pStateA->CPU, (Uint8 *)&pStateB->CPU, sizeof(pStateA->CPU));
    _SNStateMemDiff("SPC", (Uint8 *)&pStateA->SPC, (Uint8 *)&pStateB->SPC, sizeof(pStateA->SPC));
	_SNStateMemDiff("SPCIO", (Uint8 *)&pStateA->SPCIO, (Uint8 *)&pStateB->SPCIO, sizeof(pStateA->SPCIO));
    _SNStateMemDiff("DSP", (Uint8 *)&pStateA->SPCDSP, (Uint8 *)&pStateB->SPCDSP, sizeof(pStateA->SPCDSP));
    _SNStateMemDiff("IO", (Uint8 *)&pStateA->IO, (Uint8 *)&pStateB->IO, sizeof(pStateA->IO));
    _SNStateMemDiff("PPU", (Uint8 *)&pStateA->PPU, (Uint8 *)&pStateB->PPU, sizeof(pStateA->PPU));
    _SNStateMemDiff("DMAC", (Uint8 *)&pStateA->DMAC, (Uint8 *)&pStateB->DMAC, sizeof(pStateA->DMAC));
    _SNStateMemDiff("SA1", (Uint8 *)&pStateA->SA1, (Uint8 *)&pStateB->SA1, sizeof(pStateA->SA1));

}
