/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snspc interface for SNES audio processing.
 */

#ifndef _SNSPC_H
#define _SNSPC_H

#define SNSPC_TRAPFUNC
#include "snspcdefs.h"

enum SNSpcCounterE
{
	SNSPC_COUNTER_TOTAL,
	SNSPC_COUNTER_FRAME,

	SNSPC_COUNTER_NUM
};

struct SNSpc_t;

typedef Uint8 (SNSPC_TRAPFUNC *SNSpcReadTrapFuncT)(struct SNSpc_t *pSpc, Uint32 addr);
typedef void (SNSPC_TRAPFUNC *SNSpcWriteTrapFuncT)(struct SNSpc_t *pSpc, Uint32 addr, Uint8 data);
typedef Int32 (*SNSpcExecuteFuncT)(struct SNSpc_t *pCpu);

typedef struct SNSpcRegs_t
{
	Uint16		rPC;
	Uint8		rA;
	Uint8		rY;
	Uint8		rX;
	Uint8		rSP;
	Uint8		rPSW;
    Uint8       uPad;
} SNSpcRegsT;

typedef struct SNSpc_t
{
	SNSpcRegsT	Regs;

	Int32		Cycles;
	Int32		Counter[SNSPC_COUNTER_NUM];
	void		*pUserData;						// user data pointer

	SNSpcReadTrapFuncT	pReadTrapFunc;
	SNSpcWriteTrapFuncT	pWriteTrapFunc;

	Uint8		Mem[SNSPC_MEM_SIZE];
	Uint8		ShadowMem[SNSPC_ROM_SIZE];

	Bool		bRomEnable;
	/* $F0 TEST latch. Kept in the old one-byte padding slot so SNSpcT layout
	   stays stable for the existing PS2 code. Power-on value is $0A. */
	Uint8		uTestReg;
} SNSpcT;

void SNSPCNew(SNSpcT *pCpu);
void SNSPCDelete(SNSpcT *pCpu);

void SNSPCResetRegs(SNSpcT *pCpu);
void SNSPCReset(SNSpcT *pCpu, Bool bHardReset);

Uint8 SNSPCPeek8(SNSpcT *pCpu, Uint32 uAddr);
void SNSPCPeekMem(SNSpcT *pCpu, Uint32 Addr, Uint8 *pBuffer, Uint32 nBytes);

Uint8 SNSPCRead8(SNSpcT *pCpu, Uint32 uAddr);
Uint16 SNSPCRead16(SNSpcT *pCpu, Uint32 Addr);
void SNSPCReadMem(SNSpcT *pCpu, Uint32 Addr, Uint8 *pBuffer, Uint32 nBytes);
void SNSPCWrite8(SNSpcT *pCpu, Uint32 uAddr, Uint8 uData);
void SNSPCWrite16(SNSpcT *pCpu, Uint32 Addr, Uint16 Data);

void SNSPCSetExecuteFunc(SNSpcExecuteFuncT pFunc);
Int32 SNSPCExecute(SNSpcT *pCpu, Int32 nExecCycles);

//Int32 SNSPCGetCounter(SNSpcT *pCPU, Int32 iCounter);
void SNSPCResetCounter(SNSpcT *pCPU, Int32 iCounter);
void SNSPCResetCounters(SNSpcT *pCpu);

Uint8 SNSPCRead8Trap(SNSpcT *pSpc, Uint32 uAddr);
void SNSPCWrite8Trap(SNSpcT *pSpc, Uint32 uAddr, Uint8 uData);

void SNSPCSetTrapFunc(SNSpcT *pSpc, SNSpcReadTrapFuncT pReadTrap, SNSpcWriteTrapFuncT pWriteTrap);

void SNSPCSetRomEnable(SNSpcT *pSpc, Bool bEnable);

void SNSPCDumpRegs(SNSpcT *pCpu, Char *pStr);
void SNSPCSetDebug(Bool bDebug, Int32 nDebugCycles);

static _INLINE Int32 SNSPCGetCounter(SNSpcT *pCpu, Int32 iCounter)
{
	return pCpu->Counter[iCounter] - pCpu->Cycles;
}

Uint32 SNSPCMemChecksum(SNSpcT *pCpu);

#define SNSPC_HALT_SLEEP 0x01u
#define SNSPC_HALT_STOP  0x02u

#define SNSPC_TEST_TIMERS_DISABLE 0x01u
#define SNSPC_TEST_RAM_WRITABLE   0x02u
#define SNSPC_TEST_RAM_DISABLE    0x04u
#define SNSPC_TEST_TIMERS_ENABLE  0x08u

static _INLINE Bool SNSPCRamDisabled(const SNSpcT *pCpu)
{
	return (pCpu->uTestReg & SNSPC_TEST_RAM_DISABLE) ? TRUE : FALSE;
}

static _INLINE Bool SNSPCRamWritable(const SNSpcT *pCpu)
{
	return ((pCpu->uTestReg & SNSPC_TEST_RAM_WRITABLE) &&
	        !(pCpu->uTestReg & SNSPC_TEST_RAM_DISABLE)) ? TRUE : FALSE;
}

/* Raw APURAM access used by the interpreter. IPL ROM remains readable while
   RAM is disabled, matching the S-SMP memory bus. */
static _INLINE Uint8 SNSPCReadRAM(const SNSpcT *pCpu, Uint32 uAddr)
{
	uAddr &= 0xFFFFu;
	if (uAddr >= SNSPC_ROM_ADDR && pCpu->bRomEnable)
		return pCpu->Mem[uAddr];
	if (SNSPCRamDisabled(pCpu))
		return 0x5A;
	return pCpu->Mem[uAddr];
}

static _INLINE void SNSPCWriteRAM(SNSpcT *pCpu, Uint32 uAddr, Uint8 uData)
{
	uAddr &= 0xFFFFu;
	if (!SNSPCRamWritable(pCpu))
		return;
	if (uAddr >= SNSPC_ROM_ADDR && pCpu->bRomEnable)
		pCpu->ShadowMem[uAddr & (SNSPC_ROM_SIZE - 1)] = uData;
	else
		pCpu->Mem[uAddr] = uData;
}

#endif
