/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snstate interface for SNES save-state serialization.
 */

#ifndef _SNSTATE_H
#define _SNSTATE_H

#include <stddef.h>

struct SNStateCPUT
{
	SNCpuRegsT	Regs;
	Int32		Cycles;							// cycle counter for current execution
	Int32		Counter[SNCPU_COUNTER_NUM];		// counter(s)
	Uint8		uSignal;
};

struct SNStatePPUT
{
	SnesPPURegsT	Regs;
	SnesColor16T	m_CGRAM[SNESPPU_CGRAM_NUM];			// 16-bit palette
	Uint16			m_VRAM[SNESPPU_VRAM_NUMWORDS];
	SnesOAMT		m_OAM;
};

struct SNStateIOT
{
	Emu::SysInputT		Input;
	SnesIORegsT			Regs;
};

struct SNStateDMACT
{
	SnesDMAChT	m_Channels[SNESDMAC_CHANNEL_NUM];
	Uint8		m_MDMAEnable;
	Uint8		m_HDMAEnable;
	Uint8		m_HDMAEnded;
	Uint8		m_HDMADoTransfer;
};

struct SNStateSPCIOT
{
	SNSpcIORegsT		Regs;
};

struct SNStateSPCT
{
	SNSpcRegsT	Regs;
	Int32		Cycles;
	Int32		Counter[SNSPC_COUNTER_NUM];
    Bool        bRomEnable;
	Uint8		uCycleShift;
};

struct SNStateSPCDSPT
{
	Uint8			m_Regs[SNSPCDSP_REG_NUM];
	SNSpcChannelT	m_Channels[SNSPCDSP_CHANNEL_NUM];
};

#define SNSTATE_DSP_EXT_MAGIC   0x32505344u /* "DSP2" */
#define SNSTATE_DSP_EXT_VERSION 1u

/* Appended-only v2 runtime extension. The legacy SnesStateT prefix remains
   byte-for-byte unchanged so old states can still be decompressed into the
   beginning of the current object and restored with conservative defaults. */
struct SNStateDSPRuntimeT
{
	Uint32	uMagic;
	Uint32	uVersion;

	Uint16	uFullDspCounter;
	Uint16	uSilentDspCounter;
	Int32	iNoisePhase;
	Uint32	uNoiseGen;
	SNSpcEchoT Echo;

	/* The silent/deterministic mixer has its own evolving channel state. */
	SNSpcChannelT SilentChannels[SNSPCDSP_CHANNEL_NUM];

	Int32	nDspQueue;
	Int32	nSpcIoQueue;
	SNQueueElementT DspQueue[SNQUEUE_SIZE];
	SNQueueElementT SpcIoQueue[SNQUEUE_SIZE];

	Uint8	uCpuPendingMask;
	Uint8	CpuPendingData[4];
	Uint8	uPad[3];
	Uint32	CpuPendingCycle[4];
};

struct SnesStateT
{
	Uint8			Tag[4];
	Uint32			uFrame;
	Uint32			uLine;

	SNStateCPUT		CPU;
	SNStatePPUT		PPU;
	SNStateIOT		IO;
	SNStateDMACT	DMAC;
	SNStateSPCT		SPC;
	SNStateSPCDSPT	SPCDSP;
	SNStateSPCIOT	SPCIO;
	SA1SaveState	SA1;

	Uint8			Ram[SNES_RAMSIZE];
	Uint8			SpcRam[SNSPC_RAM_SIZE];
	Uint8			SRam[SNES_SRAMSIZE];

	/* Keep this last forever: SNSTATE_LEGACY_BYTES is the exact v1 prefix. */
	SNStateDSPRuntimeT DspRuntime;
};

#define SNSTATE_LEGACY_BYTES ((Uint32)offsetof(SnesStateT, DspRuntime))

void SNStateCompare(SnesStateT *pStateA, SnesStateT *pStateB);

#endif
