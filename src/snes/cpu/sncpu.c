/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements sncpu behavior for SNES CPU emulation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "sndisasm.h"
#include "sncpu.h"

volatile Uint32 g_SNCPU_SA1BusTrackEnabled = 0;
volatile Uint32 g_SNCPU_SA1BusEventCount = 0;
Uint32 g_SNCPU_SA1BusEventAddr[SNCPU_SA1_BUS_EVENT_MAX];
Uint32 g_SNCPU_SA1BusEventInfo[SNCPU_SA1_BUS_EVENT_MAX];
SNCpuT *g_SNCPU_SA1BusHostCpu = NULL;
SNCpuT *g_SNCPU_SA1ExecCpu = NULL;
Uint8 *g_SNCPU_SA1IRAM = NULL;
Uint8 g_SNCPU_SA1CIWP = 0;
Uint8 *g_SNCPU_SA1BWRAM = NULL;
Uint32 g_SNCPU_SA1BWRAMMask = 0xFFFFFFFFu;
Uint32 g_SNCPU_SA1BWRAMWriteEnabled = 0;
Uint32 g_SNCPU_SA1BWRAMProtectedBytes = 0xFFFFFFFFu;
Uint32 g_SNCPU_SA1BWRAMMap = 0;
Uint8 g_SNCPU_SA1BusPenaltyUnits[4][SNCPU_SA1_BUS_UNIT_MAX];
static Uint32 g_SNCPU_SA1BusFinalized = 0;
static Uint32 g_SNCPU_SA1BusBits[4][SNCPU_SA1_BUS_TICK_MAX / 32];
static Uint32 g_SNCPU_SA1BusFastIRAM[SNCPU_SA1_BUS_TICK_MAX / 32];
Uint32 g_SNCPU_SA1BusConflictTicks = 0;
static Uint32 g_SNCPU_SA1BusDroppedEvents = 0;

static Uint8 SNCPUSA1BusType(Uint32 uAddr, Bool bSA1Side)
{
	Uint8 uBank=(Uint8)((uAddr>>16)&0xFF); Uint16 uLow=(Uint16)uAddr;
	Bool bSystem=(uBank<=0x3F || (uBank>=0x80 && uBank<=0xBF));
	if (bSystem) {
		if (bSA1Side && (uLow<0x0800 || (uLow>=0x3000 && uLow<0x3800))) return SNCPU_SA1_BUS_IRAM;
		if (!bSA1Side && uLow>=0x3000 && uLow<0x3800) return SNCPU_SA1_BUS_IRAM;
		if (uLow>=0x6000 && uLow<=0x7FFF) return SNCPU_SA1_BUS_BWRAM;
		if (uLow>=0x8000) return SNCPU_SA1_BUS_ROM;
	}
	if (bSA1Side) { if (uBank>=0x40 && uBank<=0x6F) return SNCPU_SA1_BUS_BWRAM; }
	else if (uBank>=0x40 && uBank<=0x4F) return SNCPU_SA1_BUS_BWRAM;
	if (uBank>=0xC0) return SNCPU_SA1_BUS_ROM;
	return SNCPU_SA1_BUS_NONE;
}

static void SNCPUSA1BusMark(Uint8 uType, Uint32 uFirst, Uint32 uLast, Bool bFast)
{
	Uint32 uTick;
	if (!uType || uFirst>=SNCPU_SA1_BUS_TICK_MAX) return;
	if (uLast>=SNCPU_SA1_BUS_TICK_MAX) uLast=SNCPU_SA1_BUS_TICK_MAX-1;
	for (uTick=uFirst; uTick<=uLast; uTick++) {
		Uint32 uMask=1u<<(uTick&31);
		Uint32 uUnit=uTick*SNCPU_CYCLE_FAST;
		Uint8 uPenalty=(uType==SNCPU_SA1_BUS_BWRAM ||
		                  (uType==SNCPU_SA1_BUS_IRAM && bFast))
			? (Uint8)(SNCPU_CYCLE_FAST*2) : (Uint8)SNCPU_CYCLE_FAST;
		g_SNCPU_SA1BusBits[uType][uTick>>5]|=uMask;
		if (uType==SNCPU_SA1_BUS_IRAM && bFast)
			g_SNCPU_SA1BusFastIRAM[uTick>>5]|=uMask;
		if (uPenalty>g_SNCPU_SA1BusPenaltyUnits[uType][uUnit])
			g_SNCPU_SA1BusPenaltyUnits[uType][uUnit]=uPenalty;
		if (uType==SNCPU_SA1_BUS_BWRAM && uTick>0) {
			Uint32 uPrev=(uTick-1)*SNCPU_CYCLE_FAST;
			if (uPenalty>g_SNCPU_SA1BusPenaltyUnits[uType][uPrev])
				g_SNCPU_SA1BusPenaltyUnits[uType][uPrev]=uPenalty;
		}
	}
}

void SNCPUSA1BusSetHost(SNCpuT *pCpu)
{
	g_SNCPU_SA1BusHostCpu=pCpu;
	g_SNCPU_SA1BusTrackEnabled=pCpu?1u:0u;
	g_SNCPU_SA1BusConflictTicks=0;
	g_SNCPU_SA1BusDroppedEvents=0;
	SNCPUSA1BusBeginLine();
}
void SNCPUSA1BusSetExecCpu(SNCpuT *pCpu)
{
	g_SNCPU_SA1ExecCpu=pCpu;
}

void SNCPUSA1BusSetIRAM(Uint8 *pIRAM)
{
	g_SNCPU_SA1IRAM=pIRAM;
}

void SNCPUSA1FastMemConfig(Uint8 *pIRAM, Uint8 uCIWP,
                           Uint8 *pBWRAM, Uint32 uBWRAMBytes,
                           Bool bBWRAMWriteEnabled, Uint32 uProtectedBytes,
                           Uint8 uBWRAMMap)
{
	g_SNCPU_SA1IRAM = pIRAM;
	g_SNCPU_SA1CIWP = uCIWP;
	g_SNCPU_SA1BWRAM = pBWRAM;
	g_SNCPU_SA1BWRAMWriteEnabled = bBWRAMWriteEnabled ? 1u : 0u;
	g_SNCPU_SA1BWRAMProtectedBytes = uProtectedBytes;
	g_SNCPU_SA1BWRAMMap = uBWRAMMap;

	/* Common SA-1 BW-RAM sizes are powers of two. Keep the R5900 hot path
	   branchless after address masking; unusual sizes fall back to C. */
	if (pBWRAM && uBWRAMBytes && !(uBWRAMBytes & (uBWRAMBytes - 1u)))
		g_SNCPU_SA1BWRAMMask = uBWRAMBytes - 1u;
	else
		g_SNCPU_SA1BWRAMMask = 0xFFFFFFFFu;
}

void SNCPUSA1BusTagCpu(SNCpuT *pCpu)
{
	Uint32 i;
	if (!pCpu) return;
	for (i=0; i<SNCPU_BANK_NUM; i++) {
		Uint32 uAddr=i<<SNCPU_BANK_SHIFT;
		Uint8 uBank=(Uint8)(uAddr>>16);
		Uint16 uLow=(Uint16)uAddr;
		Bool bSystem=(uBank<=0x3F || (uBank>=0x80 && uBank<=0xBF));
		Uint8 uType=SNCPU_SA1_BUS_NONE;
		if (bSystem) {
			if (uLow<0x2000) uType=SNCPU_SA1_BUS_MIX_LOW;
			else if (uLow<0x4000) uType=SNCPU_SA1_BUS_MIX_HIGH;
			else if (uLow>=0x6000 && uLow<0x8000) uType=SNCPU_SA1_BUS_BWRAM;
			else if (uLow>=0x8000) uType=SNCPU_SA1_BUS_ROM;
		} else if (uBank>=0x40 && uBank<=0x6F) {
			uType=SNCPU_SA1_BUS_BWRAM;
		} else if (uBank>=0xC0) {
			uType=SNCPU_SA1_BUS_ROM;
		}
		pCpu->Bank[i].uPad[0]=uType;
	}
}

void SNCPUSA1BusBeginLine(void)
{
	g_SNCPU_SA1BusEventCount=0; g_SNCPU_SA1BusFinalized=0;
	memset(g_SNCPU_SA1BusBits,0,sizeof(g_SNCPU_SA1BusBits));
	memset(g_SNCPU_SA1BusFastIRAM,0,sizeof(g_SNCPU_SA1BusFastIRAM));
	memset(g_SNCPU_SA1BusPenaltyUnits,0,sizeof(g_SNCPU_SA1BusPenaltyUnits));
}
void SNCPUSA1BusRecord(SNCpuT *pCpu, Uint32 uAddr, Uint32 uCyclesPerByte, Uint32 nBytes)
{
	Uint32 uIndex,uEnd,uInfo;
	if (!g_SNCPU_SA1BusTrackEnabled || pCpu!=g_SNCPU_SA1BusHostCpu || !nBytes) return;
	uIndex=g_SNCPU_SA1BusEventCount;
	if (uIndex>=SNCPU_SA1_BUS_EVENT_MAX) { g_SNCPU_SA1BusDroppedEvents++; return; }
	uEnd=(Uint32)SNCPUGetCounter(pCpu,SNCPU_COUNTER_LINE)&0xFFFFu;
	uInfo=uEnd | ((uCyclesPerByte<SNCPU_CYCLE_SLOW)?(1u<<16):0) | ((nBytes&3u)<<17);
	g_SNCPU_SA1BusEventAddr[uIndex]=uAddr&0xFFFFFFu;
	g_SNCPU_SA1BusEventInfo[uIndex]=uInfo;
	g_SNCPU_SA1BusEventCount=uIndex+1;
}
void SNCPUSA1BusFinalize(Uint32 uMasterClock)
{
	Uint32 uCount=g_SNCPU_SA1BusEventCount;
	while (g_SNCPU_SA1BusFinalized<uCount) {
		Uint32 i=g_SNCPU_SA1BusFinalized, uAddr=g_SNCPU_SA1BusEventAddr[i], uInfo=g_SNCPU_SA1BusEventInfo[i];
		Uint32 uEnd=uInfo&0xFFFFu, nBytes=(uInfo>>17)&3u;
		Bool bFast=(uInfo&(1u<<16))?TRUE:FALSE;
		Uint32 uSpeed=bFast?SNCPU_CYCLE_FAST:SNCPU_CYCLE_SLOW, uDuration, uStart;
		Uint8 uType;
		if (!nBytes) nBytes=1;
		if (uEnd>uMasterClock) break;
		uDuration=uSpeed*nBytes; uStart=(uEnd>=uDuration)?uEnd-uDuration:0;
		uType=SNCPUSA1BusType(uAddr,FALSE);
		if (uType && uEnd>uStart) SNCPUSA1BusMark(uType,uStart>>1,(uEnd-1)>>1,bFast);
		g_SNCPU_SA1BusFinalized++;
	}
}
Uint32 SNCPUSA1BusPenalty(SNCpuT *pSA1Cpu, Uint32 uAddr, Uint32 uCyclesPerByte)
{
	Uint8 uType=SNCPUSA1BusType(uAddr,TRUE); Uint32 uLine,uFirst,nTicks,i,uPenaltyTicks; Bool bConflict=FALSE,bFastIRAM=FALSE;
	if (!g_SNCPU_SA1BusTrackEnabled || !uType || !pSA1Cpu) return 0;
	uLine=(Uint32)SNCPUGetCounter(pSA1Cpu,SNCPU_COUNTER_LINE); uFirst=uLine/SNCPU_CYCLE_FAST;
	nTicks=uCyclesPerByte/SNCPU_CYCLE_FAST; if (!nTicks) nTicks=1;
	for (i=0;i<nTicks;i++) { Uint32 uTick=uFirst+i,uMask; if (uTick>=SNCPU_SA1_BUS_TICK_MAX) break; uMask=1u<<(uTick&31);
		if (g_SNCPU_SA1BusBits[uType][uTick>>5]&uMask) { bConflict=TRUE; if (uType==SNCPU_SA1_BUS_IRAM && (g_SNCPU_SA1BusFastIRAM[uTick>>5]&uMask)) bFastIRAM=TRUE; } }
	if (!bConflict) return 0;
	uPenaltyTicks=(uType==SNCPU_SA1_BUS_BWRAM)?2u:1u; if (uType==SNCPU_SA1_BUS_IRAM && bFastIRAM) uPenaltyTicks++;
	g_SNCPU_SA1BusConflictTicks+=uPenaltyTicks; return uPenaltyTicks*SNCPU_CYCLE_FAST;
}
Uint32 SNCPUSA1BusDMAPenaltyTicks(SNCpuT *pSA1Cpu, Uint8 uSourceDevice, Bool bDestBWRAM)
{
	Uint32 uLineUnits, uTick, uMask, uPenalty = 0;
	Bool bROM, bBW, bIRAM;
	if (!g_SNCPU_SA1BusTrackEnabled || !pSA1Cpu) return 0;
	uLineUnits=(Uint32)SNCPUGetCounter(pSA1Cpu,SNCPU_COUNTER_LINE);
	uTick=uLineUnits/SNCPU_CYCLE_FAST;
	if (uTick>=SNCPU_SA1_BUS_TICK_MAX) return 0;
	uMask=1u<<(uTick&31);
	bROM=(g_SNCPU_SA1BusBits[SNCPU_SA1_BUS_ROM][uTick>>5]&uMask)?TRUE:FALSE;
	bBW=(g_SNCPU_SA1BusBits[SNCPU_SA1_BUS_BWRAM][uTick>>5]&uMask)?TRUE:FALSE;
	bIRAM=(g_SNCPU_SA1BusBits[SNCPU_SA1_BUS_IRAM][uTick>>5]&uMask)?TRUE:FALSE;

	if (uSourceDevice==0 && !bDestBWRAM) {
		if (bROM || bIRAM) uPenalty++;
		if (bIRAM) uPenalty++;
	} else if (uSourceDevice==0 && bDestBWRAM) {
		if (bBW) uPenalty+=2;
	} else if ((uSourceDevice==1 && !bDestBWRAM) ||
	           (uSourceDevice==2 && bDestBWRAM)) {
		if (bBW || bIRAM) uPenalty++;
		if (bBW) uPenalty++;
	}
	g_SNCPU_SA1BusConflictTicks+=uPenalty;
	return uPenalty;
}

Uint32 SNCPUSA1BusGetConflictTicks(void){return g_SNCPU_SA1BusConflictTicks;}
Uint32 SNCPUSA1BusGetDroppedEvents(void){return g_SNCPU_SA1BusDroppedEvents;}

#include "console.h"
#include "sndebug.h"
#include <stddef.h>
#include <string.h>

#if defined(__mips__)
typedef char SNCpuIrqPendingOffsetMustStay51[
	(offsetof(SNCpuT, uIrqPending) == 51) ? 1 : -1];
typedef char SNCpuBankOffsetMustStay52[
	(offsetof(SNCpuT, Bank) == 52) ? 1 : -1];
#endif

#define SNCPU_FASTREADMEM TRUE

static Int32 _SNCPUDefaultExecuteFunc(SNCpuT *pCpu);
static Uint8 SNCPU_TRAPFUNC _SNCPUDefaultRead(SNCpuT *pCpu, Uint32 Addr);
static void SNCPU_TRAPFUNC _SNCPUDefaultWrite(SNCpuT *pCpu, Uint32 Addr, Uint8 Data);
static Uint8 SNCPU_TRAPFUNC _SNCPUWrap24Read(SNCpuT *pCpu, Uint32 Addr);
static void SNCPU_TRAPFUNC _SNCPUWrap24Write(SNCpuT *pCpu, Uint32 Addr, Uint8 Data);

static SNCpuExecuteFuncT _SNCPU_pExecuteFunc = _SNCPUDefaultExecuteFunc;
static SNCpuExecuteFuncT _SNCPU_pDebugExecuteFunc = _SNCPUDefaultExecuteFunc;
static Bool _SNCPU_bDebug = FALSE;
static Int32 _SNCPU_nDebugCycles = 1;

static Uint8 SNCPU_TRAPFUNC _SNCPUDefaultRead(SNCpuT *pCpu, Uint32 Addr)
{
	return 0xFF;
}

static void SNCPU_TRAPFUNC _SNCPUDefaultWrite(SNCpuT *pCpu, Uint32 Addr, Uint8 Data)
{
}

static Int32 _SNCPUDefaultExecuteFunc(SNCpuT *pCpu)
{
	pCpu->Cycles = 0;
	return 0;
}

void SNCPUNew(SNCpuT *pCpu)
{
	memset(pCpu, 0, sizeof(*pCpu));

	pCpu->pUserData = NULL;

	SNCPUSetBank(pCpu, 0, SNCPU_MEM_SIZE, NULL, FALSE);
	SNCPUSetTrap(pCpu, 0, SNCPU_MEM_SIZE, NULL, NULL);
	SNCPUSetMemSpeed(pCpu, 0, SNCPU_MEM_SIZE, SNCPU_CYCLE_SLOW);

	SNCPUResetRegs(pCpu);
	SNCPUResetCounters(pCpu);
}

void SNCPUResetCounters(SNCpuT *pCpu)
{
	Int32 iCounter;
	// reset all counters
	pCpu->Cycles = 0;
	pCpu->nAbortCycles = 0;
	for (iCounter=0; iCounter < SNCPU_COUNTER_NUM; iCounter++)
	{
		pCpu->Counter[iCounter] = 0;
	}
}

void SNCPUReset(SNCpuT *pCpu, Bool bHardReset)
{
	if (bHardReset)
	{
		SNCPUResetCounters(pCpu);
		SNCPUResetRegs(pCpu);
	}

	// no IRQ
	pCpu->uSignal = 0;
	pCpu->uNmiDmaDelay = 0;
	pCpu->uIrqPending = 0;

	// set cpu flags to default state
	pCpu->Regs.rP  = SNCPU_FLAG_M | SNCPU_FLAG_X |  SNCPU_FLAG_I;
	// set emulation bit
	pCpu->Regs.rE  = 1;
	// reset pc
	pCpu->Regs.rPC = SNCPURead16(pCpu, SNCPU_VECTOR_RESET);
	// A hardware reset starts in emulation mode with S=$01FF.  Keeping the
	// low byte left by SNCPUResetRegs ($00) made the first pushes land one
	// page position too early and breaks games which inspect the reset stack.
	// A soft reset keeps the low byte, as on the 65C816, and only restores
	// the emulation-mode page.
	if (bHardReset)
		pCpu->Regs.rS.w = 0x01FF;
	else
		pCpu->Regs.rS.b.h = 0x01;
}

void SNCPUDelete(SNCpuT *pCpu)
{
}

void SNCPUResetRegs(SNCpuT *pCpu)
{
	// reset registers
	pCpu->Regs.rA.w = 0;
	pCpu->Regs.rX.w = 0;
	pCpu->Regs.rY.w = 0;
	pCpu->Regs.rS.w = 0;
	pCpu->Regs.rP   = SNCPU_FLAG_I;

	pCpu->Regs.rPC = 0;
	pCpu->Regs.rDP = 0;
	pCpu->Regs.rDB = 0;
}

void SNCPUSetBank(SNCpuT *pCpu, Uint32 Addr, Uint32 Size, Uint8 *pMem, Bool bRAM)
{
	Int32 iBank, nBanks;

	assert(!(Size & SNCPU_BANK_MASK));
	assert(!(Addr & SNCPU_BANK_MASK));

	iBank  = Addr >> SNCPU_BANK_SHIFT;
	nBanks = Size >> SNCPU_BANK_SHIFT;

    if (pMem==NULL)
    {
        bRAM = FALSE;
    }

	while ((nBanks > 0) && (iBank < SNCPU_BANK_NUM))
	{
		// set bank pointer
		pCpu->Bank[iBank].pMem = pMem  ? (pMem  - Addr) : NULL;
		pCpu->Bank[iBank].bRAM = bRAM ? 0xFF : 0;

		// next bank
		iBank++;
		nBanks--;
	}
}

void SNCPUSetTrap(SNCpuT *pCpu, Uint32 Addr, Uint32 Size, SNCpuReadTrapFuncT pReadTrap, SNCpuWriteTrapFuncT pWriteTrap)
{
	Int32 iBank, nBanks;

	assert(!(Size & SNCPU_BANK_MASK));
	assert(!(Addr & SNCPU_BANK_MASK));

	iBank  = Addr >> SNCPU_BANK_SHIFT;
	nBanks = Size >> SNCPU_BANK_SHIFT;

	if (pReadTrap==NULL) pReadTrap = _SNCPUDefaultRead;
	if (pWriteTrap==NULL) pWriteTrap = _SNCPUDefaultWrite;

	while ((nBanks > 0) && (iBank < SNCPU_BANK_NUM))
	{
		// set bank pointer
		pCpu->Bank[iBank].pReadTrapFunc  = pReadTrap;
		pCpu->Bank[iBank].pWriteTrapFunc = pWriteTrap;
		pCpu->Bank[iBank].pMem = NULL;
		pCpu->Bank[iBank].bRAM =  0;

		// next bank
		iBank++;
		nBanks--;
	}
}

void SNCPUSetMemSpeed(SNCpuT *pCpu, Uint32 Addr, Uint32 Size, Uint32 uCycles)
{
	Int32 iBank, nBanks;

	assert(!(Size & SNCPU_BANK_MASK));
	assert(!(Addr & SNCPU_BANK_MASK));

	iBank  = Addr >> SNCPU_BANK_SHIFT;
	nBanks = Size >> SNCPU_BANK_SHIFT;

	while ((nBanks > 0) && (iBank < SNCPU_BANK_NUM))
	{
		// set cycle count
		pCpu->Bank[iBank].uBankCycle = uCycles;

		// next bank
		iBank++;
		nBanks--;
	}
}

void SNCPUSetRomSpeed(SNCpuT *pCpu, Uint32 Addr, Uint32 Size, Uint32 uCycles)
{
	Int32 iBank, nBanks;

	assert(!(Size & SNCPU_BANK_MASK));
	assert(!(Addr & SNCPU_BANK_MASK));

	iBank  = Addr >> SNCPU_BANK_SHIFT;
	nBanks = Size >> SNCPU_BANK_SHIFT;

	while ((nBanks > 0) && (iBank < SNCPU_BANK_NUM))
	{
		SNCpuBankT *pBank = &pCpu->Bank[iBank];

		// is this a rom bank?
		if (pBank->pMem!=NULL && !pBank->bRAM)
		{
			// set cycle count
			pBank->uBankCycle = uCycles;
		}

		// next bank
		iBank++;
		nBanks--;
	}
}

/* Effective addresses such as $FF:FFFF,X may carry into $100:xxxx before
   reaching the memory helpers.  The 65816 has only a 24-bit address bus, so
   that carry wraps to bank $00.  SNCPU_MEM_SIZE deliberately reserves one
   extra 64 KiB for this case; mirror the eight bank descriptors for $00 here
   so the hot ASM memory path gets the wrap without adding a mask to every
   read/write. */
void SNCPUMirror24BitBus(SNCpuT *pCpu)
{
	Uint32 i;
	const Uint32 uBusBytes = 0x1000000;
	const Uint32 iMirror = uBusBytes >> SNCPU_BANK_SHIFT;
	const Uint32 nBanks = 0x10000 >> SNCPU_BANK_SHIFT;

	for (i = 0; i < nBanks; i++)
	{
		pCpu->Bank[iMirror + i] = pCpu->Bank[i];
		if (pCpu->Bank[iMirror + i].pMem)
			pCpu->Bank[iMirror + i].pMem -= uBusBytes;
		else
			pCpu->Bank[iMirror + i].pReadTrapFunc = _SNCPUWrap24Read;

		/* ROM and I/O descriptors can have a direct read pointer but still use
		   their write trap.  Route every trapped overflow write through the
		   wrapped address as well. */
		pCpu->Bank[iMirror + i].pWriteTrapFunc = _SNCPUWrap24Write;
	}
}

static Uint8 SNCPU_TRAPFUNC _SNCPUWrap24Read(SNCpuT *pCpu, Uint32 Addr)
{
	return SNCPURead8(pCpu, Addr & 0xFFFFFF);
}

static void SNCPU_TRAPFUNC _SNCPUWrap24Write(SNCpuT *pCpu, Uint32 Addr, Uint8 Data)
{
	SNCPUWrite8(pCpu, Addr & 0xFFFFFF, Data);
}

Uint8 SNCPUPeek8(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 iBank;
	Uint8 *pBankMem;

	iBank = Addr >> SNCPU_BANK_SHIFT;
	pBankMem = pCpu->Bank[iBank].pMem;

	if (pBankMem)
	{
		return pBankMem[Addr];
	}
	else
	{
		return 0xFF;
	}
}

void SNCPUPeekMem(SNCpuT *pCpu, Uint32 Addr, Uint8 *pBuffer, Uint32 nBytes)
{
	while (nBytes > 0)
	{
		// read byte into buffer
		*pBuffer = SNCPUPeek8(pCpu, Addr);

		Addr++;
		pBuffer++;
		nBytes--;
	}
}

//Uint32 uBankRead[256 * 8];
//Uint32 uBankWrite[256 * 8];
//Uint32 uLastAddr[128];

Uint8 SNCPURead8(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 iBank;
	Uint8 *pBankMem;
	Uint8 uData;

	iBank = Addr >> SNCPU_BANK_SHIFT;
	pBankMem = pCpu->Bank[iBank].pMem;

//	uBankRead[iBank]++;
/*
	for (i=127; i >=1; i--)
		uLastAddr[i] = uLastAddr[i-1];
	uLastAddr[0] = Addr;*/
	if (pBankMem)
	{
//		char str[64];
		uData = pBankMem[Addr];
	}
	else
	{
		//call trap function
		uData = pCpu->Bank[iBank].pReadTrapFunc(pCpu, Addr);
	}
	SNCPUSetOpenBus(pCpu, uData);
	return uData;
}

Uint16 SNCPURead16(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uData;
	uData =  SNCPURead8(pCpu, Addr);
	uData|= (SNCPURead8(pCpu, Addr+1)<<8);
	return  uData;
}

Uint32 SNCPURead24(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uData;
	uData = (SNCPURead8(pCpu, Addr+0) << 0);
	uData|= (SNCPURead8(pCpu, Addr+1) << 8);
	uData|= (SNCPURead8(pCpu, Addr+2) << 16);
	return  uData;
}

#if !SNCPU_FASTREADMEM

void SNCPUReadMem(SNCpuT *pCpu, Uint32 Addr, Uint8 *pBuffer, Uint32 nBytes)
{
	while (nBytes > 0)
	{
		// read byte into buffer
		*pBuffer = SNCPURead8(pCpu, Addr);

		Addr++;
		pBuffer++;
		nBytes--;
	}
}

#else

void SNCPUReadMem(SNCpuT *pCpu, Uint32 uAddr, Uint8 *pBuffer, Uint32 nTotalBytes)
{
	while (nTotalBytes > 0)
	{
		Uint32 nBankBytes, nBytes;
		Uint32 iBank;
		Uint8 *pBankMem;

		nBytes = nTotalBytes;

		// calculate number of bytes to end of bank
		nBankBytes = SNCPU_BANK_SIZE - (uAddr & (SNCPU_BANK_SIZE-1));

		// clamp size to bank size
		if (nBytes > nBankBytes) nBytes = nBankBytes;

		// resolve bank
		iBank = uAddr >> SNCPU_BANK_SHIFT;
		pBankMem = pCpu->Bank[iBank].pMem;

		if (pBankMem)
		{
			// copy data directly from bank memory
			memcpy(pBuffer, pBankMem + uAddr, nBytes);
			if (nBytes > 0)
				SNCPUSetOpenBus(pCpu, pBuffer[nBytes - 1]);

			pBuffer += nBytes;
			nTotalBytes -= nBytes;
			uAddr += nBytes;
		} else
		{
			// trapped memory space
			while (nBytes > 0)
			{
				// call trap function
				*pBuffer = pCpu->Bank[iBank].pReadTrapFunc(pCpu, uAddr);
				SNCPUSetOpenBus(pCpu, *pBuffer);

				pBuffer++;
				nBytes--;
				nTotalBytes--;
				uAddr++;
			}
		}
	}
}
#endif

void  SNCPUWrite8(SNCpuT *pCpu, Uint32 Addr, Uint8 Data)
{
	Uint32 iBank;
	Uint8 *pBankMem;

	iBank = Addr >> SNCPU_BANK_SHIFT;
	/* Writes also drive the CPU data bus, including write-only MMIO. */
	SNCPUSetOpenBus(pCpu, Data);

//	uBankWrite[iBank]++;

	if (pCpu->Bank[iBank].bRAM)
	{
		pBankMem = pCpu->Bank[iBank].pMem;
		// write directly to memory
		pBankMem[Addr] = Data;
	}
	else
	{
		//call trap function
		pCpu->Bank[iBank].pWriteTrapFunc(pCpu, Addr, Data);
	}
}

void  SNCPUWrite16(SNCpuT *pCpu, Uint32 Addr, Uint16 Data)
{
	SNCPUWrite8(pCpu, Addr + 0, Data >> 0);
	SNCPUWrite8(pCpu, Addr + 1, Data >> 8);
}

void SNCPUPush8(SNCpuT *pCpu, Uint8 Data)
{
	SNCPUWrite8(pCpu, pCpu->Regs.rS.w, Data);
	if (pCpu->Regs.rE)
	{
		// decrement 8-bit S
		pCpu->Regs.rS.b.l--;
	} else
	{
		// decrement 16-bit S
		pCpu->Regs.rS.w--;
	}
}

void SNCPUPush16(SNCpuT *pCpu, Uint16 Data)
{
	SNCPUPush8(pCpu, Data >> 8);
	SNCPUPush8(pCpu, Data & 0xFF);
}

void SNCPUPush24(SNCpuT *pCpu, Uint32 Data)
{
	SNCPUPush8(pCpu, Data >> 16);
	SNCPUPush8(pCpu, Data >> 8);
	SNCPUPush8(pCpu, Data & 0xFF);
}

Uint8 SNCPUPop8(SNCpuT *pCpu)
{
	if (pCpu->Regs.rE)
	{
		// inc 8-bit S
		pCpu->Regs.rS.b.l++;
	} else
	{
		// inc 16-bit S
		pCpu->Regs.rS.w++;
	}
	return SNCPURead8(pCpu, pCpu->Regs.rS.w);
}

Uint16 SNCPUPop16(SNCpuT *pCpu)
{
	Uint32 uData;
	uData =  SNCPUPop8(pCpu);
	uData|= (SNCPUPop8(pCpu)<<8);
	return uData;
}

Uint32 SNCPUPop24(SNCpuT *pCpu)
{
	Uint32 uData;
	uData =  SNCPUPop8(pCpu);
	uData|= (SNCPUPop8(pCpu)<<8);
	uData|= (SNCPUPop8(pCpu)<<16);
	return uData;
}

void SNCPUNMI(SNCpuT *pCpu)
{

#if SNES_DEBUG
    if (Snes_bDebugIO)
        SnesDebug("-NMI\n");
#endif

	// are we stopped at a WAI instruction?
	if (pCpu->uSignal & SNCPU_SIGNAL_WAI)
	{
		/* WAI has already advanced PC to the following instruction. */
		pCpu->uSignal &= ~SNCPU_SIGNAL_WAI;
	}

	if (pCpu->Regs.rE)
	{
		// emulation
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 0));
		SNCPUPush8(pCpu, pCpu->Regs.rP & (~SNCPU_FLAG_B));

		pCpu->Regs.rPC = SNCPURead16(pCpu, SNCPU_VECTORE_NMI);
	} else
	{
		// native
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 16));
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 0));
		SNCPUPush8(pCpu, pCpu->Regs.rP);

		pCpu->Regs.rPC = SNCPURead16(pCpu, SNCPU_VECTOR_NMI);
	}

	pCpu->Regs.rP &= ~(SNCPU_FLAG_D);
	pCpu->Regs.rP |= SNCPU_FLAG_I;

	SNCPUConsumeCycles(pCpu,
		SNCPU_CYCLE_SLOW * (pCpu->Regs.rE ? 5 : 6) +
		SNCPU_CYCLE_FAST * 2);
}

void SNCPUIRQ(SNCpuT *pCpu)
{
	/* Any asserted IRQ releases WAI, even when I masks entry into the IRQ
	   handler.  The old placement inside the !I block could leave the CPU
	   asleep forever on a masked interrupt. */
	if (pCpu->uSignal & SNCPU_SIGNAL_WAI)
	{
		pCpu->uSignal &= ~SNCPU_SIGNAL_WAI;
	}

	if (!(pCpu->Regs.rP & SNCPU_FLAG_I))
	{
#if SNES_DEBUG
        if (Snes_bDebugIO)
            SnesDebug("-IRQ\n");
#endif

		if (pCpu->Regs.rE)
		{
			// emulation
			SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
			SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 0));
			SNCPUPush8(pCpu, pCpu->Regs.rP & (~SNCPU_FLAG_B));

			pCpu->Regs.rPC = SNCPURead16(pCpu, SNCPU_VECTORE_IRQ);
		} else
		{
			// native
			SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 16));
			SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
			SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 0));
			SNCPUPush8(pCpu, pCpu->Regs.rP);

			pCpu->Regs.rPC = SNCPURead16(pCpu, SNCPU_VECTOR_IRQ);
		}

		pCpu->Regs.rP &= ~(SNCPU_FLAG_D);
		pCpu->Regs.rP |= SNCPU_FLAG_I;

		SNCPUConsumeCycles(pCpu,
			SNCPU_CYCLE_SLOW * (pCpu->Regs.rE ? 5 : 6) +
			SNCPU_CYCLE_FAST * 2);
	}
}


void SNCPUNMIToVector(SNCpuT *pCpu, Uint16 uVector)
{
	if (pCpu->uSignal & SNCPU_SIGNAL_WAI)
		pCpu->uSignal &= ~SNCPU_SIGNAL_WAI;

	if (pCpu->Regs.rE)
	{
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
		SNCPUPush8(pCpu, (Uint8)pCpu->Regs.rPC);
		SNCPUPush8(pCpu, pCpu->Regs.rP & (~SNCPU_FLAG_B));
	}
	else
	{
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 16));
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
		SNCPUPush8(pCpu, (Uint8)pCpu->Regs.rPC);
		SNCPUPush8(pCpu, pCpu->Regs.rP);
	}

	pCpu->Regs.rPC = uVector;
	pCpu->Regs.rP &= ~(SNCPU_FLAG_D);
	pCpu->Regs.rP |= SNCPU_FLAG_I;
	SNCPUConsumeCycles(pCpu,
		SNCPU_CYCLE_SLOW * (pCpu->Regs.rE ? 5 : 6) +
		SNCPU_CYCLE_FAST * 2);
}

void SNCPUIRQToVector(SNCpuT *pCpu, Uint16 uVector)
{
	if (pCpu->uSignal & SNCPU_SIGNAL_WAI)
		pCpu->uSignal &= ~SNCPU_SIGNAL_WAI;

	if (pCpu->Regs.rP & SNCPU_FLAG_I)
		return;

	if (pCpu->Regs.rE)
	{
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
		SNCPUPush8(pCpu, (Uint8)pCpu->Regs.rPC);
		SNCPUPush8(pCpu, pCpu->Regs.rP & (~SNCPU_FLAG_B));
	}
	else
	{
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 16));
		SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
		SNCPUPush8(pCpu, (Uint8)pCpu->Regs.rPC);
		SNCPUPush8(pCpu, pCpu->Regs.rP);
	}

	pCpu->Regs.rPC = uVector;
	pCpu->Regs.rP &= ~(SNCPU_FLAG_D);
	pCpu->Regs.rP |= SNCPU_FLAG_I;
	SNCPUConsumeCycles(pCpu,
		SNCPU_CYCLE_SLOW * (pCpu->Regs.rE ? 5 : 6) +
		SNCPU_CYCLE_FAST * 2);
}

Int32 SNCPUDisassemble(SNCpuT *pCpu, Uint32 Addr, char *pStr, Uint8 *pFlags)
{
	Uint8 Opcode[4];
    Uint8 uFlags;

    if (!pFlags)
    {
        uFlags = pCpu->Regs.rP;
        pFlags = &uFlags;
    }

	// read memory
	SNCPUReadMem(pCpu, Addr, Opcode, sizeof(Opcode));

	// disassemble
	return SNDisasm(pStr, Opcode, Addr, pFlags);
}

void SNCPUDumpRegs(SNCpuT *pCpu, char *pStr)
{
	Uint8 rF = pCpu->Regs.rP;

	sprintf(pStr, "A:%04X X:%04X Y:%04X S:%04X PC:%06X DB:%02X DP:%04X %c%c%c%c%c%c%c%c %c",
		pCpu->Regs.rA.w,
		pCpu->Regs.rX.w,
		pCpu->Regs.rY.w,
		pCpu->Regs.rS.w,
		pCpu->Regs.rPC,
		pCpu->Regs.rDB >> 16,
		pCpu->Regs.rDP,
		(rF & SNCPU_FLAG_N) ? 'N' : 'n',
		(rF & SNCPU_FLAG_V) ? 'V' : 'v',
		(rF & SNCPU_FLAG_M) ? 'M' : 'm',
		(rF & SNCPU_FLAG_X) ? 'X' : 'x',
		(rF & SNCPU_FLAG_D) ? 'D' : 'd',
		(rF & SNCPU_FLAG_I) ? 'I' : 'i',
		(rF & SNCPU_FLAG_Z) ? 'Z' : 'z',
		(rF & SNCPU_FLAG_C) ? 'C' : 'c',
		(pCpu->Regs.rE) ? 'E' : 'e'
		);

}

void SNCPUResetCounter(SNCpuT *pCpu, Int32 iCounter)
{
	pCpu->Counter[iCounter] = 0;
}

void SNCPUSetExecuteFunc(SNCpuExecuteFuncT pFunc)
{
	if (_SNCPU_bDebug)
	{
		_SNCPU_pDebugExecuteFunc = pFunc;
	} else
	{
		_SNCPU_pExecuteFunc = pFunc;
	}
}

Bool SNCPUExecute(SNCpuT *pCpu)
{
    pCpu->nAbortCycles = 0;

    // execute cpu cycles
    pCpu->bRunning = TRUE;
    _SNCPU_pExecuteFunc(pCpu);
    pCpu->bRunning = FALSE;

    // restore cycle count if we were just aborted
    if (pCpu->nAbortCycles != 0)
    {
        pCpu->Cycles = pCpu->nAbortCycles;
        pCpu->nAbortCycles = 0;
        return FALSE;
    } else
    {
        return TRUE;
    }
}

Bool SNCPUExecuteOne(SNCpuT *pCpu)
{
    if (pCpu->Cycles > 0)
    {
        // execute one instruction
        int delta = pCpu->Cycles - 1;

        pCpu->Cycles    -= delta;
        pCpu->Counter[0]-= delta;
        pCpu->Counter[1]-= delta;
        pCpu->Counter[2]-= delta;
        pCpu->Counter[3]-= delta;

        SNCPUExecute(pCpu);
        pCpu->Cycles    += delta;
        pCpu->Counter[0]+= delta;
        pCpu->Counter[1]+= delta;
        pCpu->Counter[2]+= delta;
        pCpu->Counter[3]+= delta;

        return TRUE;
    } else
    {
        return FALSE;
    }
}

Int32 SNCPUExecuteDebug(SNCpuT *pCpu)
{
    SNCPUSetDebug(0, 1);

    while (pCpu->Cycles > 0)
    {
        char str[64];

        // disassemble instruction
        SNCPUDisassemble(pCpu, pCpu->Regs.rPC, str, NULL);
        ConDebug("%06d: cpu %06X: %s\n", SNCPUGetCounter(pCpu, SNCPU_COUNTER_FRAME), pCpu->Regs.rPC, str);

        // execute just one instruction
        SNCPUExecuteOne(pCpu);

        // print registers
        SNCPUDumpRegs(pCpu, str);
        ConDebug("%06d: cpu %s\n", SNCPUGetCounter(pCpu, SNCPU_COUNTER_FRAME), str);

    }
    SNCPUSetDebug(1, 1);
	return 0;
}

void SNCPUSetDebug(Bool bDebug, Int32 nDebugCycles)
{
	if (_SNCPU_bDebug!=bDebug)
	{
		if (bDebug)
		{
			_SNCPU_pDebugExecuteFunc = _SNCPU_pExecuteFunc;
			_SNCPU_pExecuteFunc = SNCPUExecuteDebug;
		} else
		{
			_SNCPU_pExecuteFunc = _SNCPU_pDebugExecuteFunc;
		}

		_SNCPU_bDebug=bDebug;
	}

	_SNCPU_nDebugCycles = nDebugCycles;
}

void SNCPUAbort(SNCpuT *pCpu)
{
	if (pCpu->bRunning)
	{
		// cpu is executing, so force it to terminate by setting
		// the cycle count to zero. Termination will
		// cause the abort handler to be called, then execution will resume
		pCpu->nAbortCycles = pCpu->Cycles;
		pCpu->Cycles       = 0;
	}
}

void SNCPUSignalIRQ(SNCpuT *pCpu, Uint32 bEnable)
{
    // IRQs will continuously occur while the IRQ signal is set.
    // IRQs can be enabled/disabled by CPU flags
    // IRQ can be cleared by reading timeup register or setting nmitimen v-en, h-en to 0
	if (bEnable)
	{
		pCpu->uSignal |= SNCPU_SIGNAL_IRQ;
        // if we're currently running, abort so IRQ can happen now
		SNCPUAbort(pCpu);
	} else
	{
		pCpu->uSignal &= ~SNCPU_SIGNAL_IRQ;
		pCpu->uIrqPending = 0;
	}
}

void SNCPUSetIRQDelay(SNCpuT *pCpu, Uint8 nOpcodes)
{
	pCpu->uIrqPending = nOpcodes;
}

Bool SNCPUExecuteIRQDelay(SNCpuT *pCpu)
{
	if (pCpu->uIrqPending == 0)
		return FALSE;

	/* An already sleeping WAI wakes as soon as /IRQ is asserted.  Delaying
	   that wake would turn the compatibility aid into a new deadlock. */
	if (pCpu->uSignal & SNCPU_SIGNAL_WAI)
	{
		pCpu->uIrqPending = 0;
		return FALSE;
	}

	if (!SNCPUExecuteOne(pCpu))
		return FALSE;

	/* The delayed opcode itself may read $4211 and lower /IRQ.  That path
	   clears the counter, so do not wrap zero back to 255 here. */
	if (pCpu->uIrqPending != 0)
		pCpu->uIrqPending--;
	return TRUE;
}

void SNCPUSignalNMI(SNCpuT *pCpu, Uint32 bEnable)
{
    // NMIs are edge triggered.
    // If the signal transitions from 0 -> 1 then the NMIEDGE flag becomes set.
    // When NMIEDGE is set, a cpu nmi will trigger with a one instruction delay.
    // Lowering the input does not cancel an edge already latched by the CPU.
	if (bEnable)
	{
		if (!(pCpu->uSignal & SNCPU_SIGNAL_NMI))
		{
			// trigger NMI on lo->hi transition
			pCpu->uSignal |= SNCPU_SIGNAL_NMIEDGE;
			/* On the S-CPU, an NMI edge captured while MDMA owns the bus is
			   held until DMA ends, followed by a 24-30 master-clock recovery
			   delay.  Wild Guns depends on this ordering. */
			if (pCpu->uSignal & SNCPU_SIGNAL_DMA)
				pCpu->uNmiDmaDelay = 24;
            // if we're currently running abort so NMI can happen now
			SNCPUAbort(pCpu);
		}
		pCpu->uSignal |= SNCPU_SIGNAL_NMI;
	} else
	{
		pCpu->uSignal &= ~SNCPU_SIGNAL_NMI;
	}
}

void SNCPUSignalDMA(SNCpuT *pCpu, Uint32 bEnable)
{
    if (bEnable)
    {
        pCpu->uSignal |= SNCPU_SIGNAL_DMA;
        // if we're currently running, abort so we can MDMA
        SNCPUAbort(pCpu);
    } else
    {
        pCpu->uSignal &= ~SNCPU_SIGNAL_DMA;
    }
}
