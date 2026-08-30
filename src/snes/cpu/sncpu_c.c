/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements sncpu c behavior for SNES CPU emulation.
 */

/*
	Problems:

		PC can does not wrap to 16-bits.
		The Program Bank register is not affected by the Relative, Relative Long,
		Absolute, Absolute Indirect, and Absolute Indexed Indirect addressing modes
		or by incrementing the Program Counter from FFFF. The only instructions that
		affect the Program Bank register are: RTI, RTL, JML, JSL, and JMP Absolute
		Long. Program code may exceed 64K bytes altough code segments may not span
		bank boundaries.

		Extra cycle for (DP & FF)!=0

 */

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "sncpu.h"
#include "sncpu_c.h"

#define SNCPU_PROFILE (CODE_DEBUG && FALSE)
#define SNCPU_TRACE (CODE_DEBUG && FALSE)
#define SNCPU_TRACE_NUM 256

// opcode begin/end

#define SNCPU_OPTABLE_BEGIN()
#define SNCPU_OPTABLE_OP(x)
#define SNCPU_OPTABLE_END()

#define SNCPU_OPCODE_X	0x100
#define SNCPU_OPCODE_M	0x200
#define SNCPU_OPCODE_E	0x400

#define SNCPU_ENDOP(_Cycles) \
	SNCPU_SUBCYCLES(_Cycles); \
	break;

#define SNCPU_OP(_Opcode) \
	case _Opcode:

#define SNCPU_OP_ALL(_Opcode) \
	case 0x000|(_Opcode):	\
	case 0x100|(_Opcode):	\
	case 0x200|(_Opcode):	\
	case 0x300|(_Opcode):	\
	case 0x400|(_Opcode):

#define SNCPU_OP_E0(_Opcode) \
	case 0x000|(_Opcode):	\
	case 0x100|(_Opcode):	\
	case 0x200|(_Opcode):	\
	case 0x300|(_Opcode):

#define SNCPU_OP_E1(_Opcode) \
	case 0x400|(_Opcode):

#define SNCPU_OP_X0(_Opcode) \
	case 0x000|(_Opcode):	\
	case 0x200|(_Opcode):	\

#define SNCPU_OP_X1E1(_Opcode) \
	case 0x100|(_Opcode):	\
	case 0x300|(_Opcode):	\
	case 0x400|(_Opcode):

// i/o macros

#if defined(SNCPU_TEST) && SNCPU_TEST
#define SNCPU_TESTCYCLES(_nCycles) pCpu->uTestCycles += (_nCycles);
#else
#define SNCPU_TESTCYCLES(_nCycles)
#endif

#define SNCPU_SUBCYCLES(_nCycles) \
	do { \
		pCpu->Cycles-=(_nCycles)*SNCPU_CYCLE_FAST; \
		SNCPU_TESTCYCLES(_nCycles) \
	} while (0);
#define SNCPU_SUBCYCLESSLOW(_nCycles) \
	do { \
		pCpu->Cycles-=(_nCycles)*SNCPU_CYCLE_SLOW; \
		SNCPU_TESTCYCLES(_nCycles) \
	} while (0);
#define SNCPU_SUBMEMCYCLES(_Addr, _nBytes) \
	do { \
		pCpu->Cycles -= pCpu->Bank[(_Addr) >> SNCPU_BANK_SHIFT].uBankCycle * (_nBytes); \
		SNCPU_TESTCYCLES(_nBytes) \
	} while (0);

#define SNCPU_INC_PC(_Bytes) \
	rPC = (rPC & 0xFF0000) | ((rPC + (_Bytes)) & 0xFFFF);
#define SNCPU_FETCH8(_Reg)  _Reg=_SNCPUFetch8(pCpu, rPC);  SNCPU_INC_PC(1)
#define SNCPU_FETCH16(_Reg) _Reg=_SNCPUFetch16(pCpu, rPC); SNCPU_INC_PC(2)
#define SNCPU_FETCH24(_Reg) _Reg=_SNCPUFetch24(pCpu, rPC); SNCPU_INC_PC(3)

#define SNCPU_WRITE8(_Addr, _Data)  _SNCPUWrite8(pCpu, _Addr, _Data);
#define SNCPU_WRITE16(_Addr, _Data) _SNCPUWrite16(pCpu, _Addr, _Data);
#define SNCPU_WRITE16_WRAP16(_Addr, _Data) \
	_SNCPUWrite16Wrap16(pCpu, _Addr, _Data);

#define SNCPU_READ8(_Addr, _x)  _x = _SNCPURead8(pCpu, _Addr);
#define SNCPU_READ16(_Addr, _x) _x = _SNCPURead16(pCpu, _Addr);
#define SNCPU_READ16_WRAP16(_Addr, _x) _x = _SNCPURead16Wrap16(pCpu, _Addr);
#define SNCPU_READ24_WRAP16(_Addr, _x) _x = _SNCPURead24Wrap16(pCpu, _Addr);
#define SNCPU_READ16_DP(_Addr, _x) _x = _SNCPURead16DP(pCpu, _Addr, R_DPMASK);
#define SNCPU_READ16_DPX(_Addr, _x) \
	_x = _SNCPURead16Wrap16(pCpu, _Addr);

#define SNCPU_PUSH8(_Data)	_SNCPUPush8(pCpu, _Data);
#define SNCPU_PUSH16(_Data)	_SNCPUPush16(pCpu, _Data);
#define SNCPU_PUSH24(_Data)	_SNCPUPush24(pCpu, _Data);
#define SNCPU_PUSH16_RAW(_Data) _SNCPUPush16Raw(pCpu, _Data);
#define SNCPU_PUSH24_RAW(_Data) _SNCPUPush24Raw(pCpu, _Data);
#define SNCPU_POP8(_x)	_x = _SNCPUPop8(pCpu);
#define SNCPU_POP8_RAW(_x) _x = _SNCPUPop8Raw(pCpu);
#define SNCPU_POP16(_x)	_x = _SNCPUPop16(pCpu);
#define SNCPU_POP24(_x)	_x = _SNCPUPop24(pCpu);
#define SNCPU_POP16_RAW(_x) _x = _SNCPUPop16Raw(pCpu);
#define SNCPU_POP24_RAW(_x) _x = _SNCPUPop24Raw(pCpu);

// register defines

#define fE    pCpu->Regs.rE
#define R_DP   pCpu->Regs.rDP
#define R_DB pCpu->Regs.rDB
#define R_A8  pCpu->Regs.rA.b.l
#define R_A16 pCpu->Regs.rA.w
#define R_X   pCpu->Regs.rX.w
#define R_X8  pCpu->Regs.rX.b.l
#define R_X16 pCpu->Regs.rX.w
#define R_Y   pCpu->Regs.rY.w
#define R_Y8  pCpu->Regs.rY.b.l
#define R_Y16 pCpu->Regs.rY.w
#define R_S   pCpu->Regs.rS.w
#define R_S8  pCpu->Regs.rS.b.l
#define R_S16 pCpu->Regs.rS.w
#define R_P  pCpu->Regs.rP
#define R_PC  rPC
#define R_t0  t0
#define R_t1  t1
#define R_t2  t2

// get/set register

#define SNCPU_SET_A8(_x) R_A8 = _x;
#define SNCPU_SET_X8(_x) R_X8 = _x;
#define SNCPU_SET_Y8(_x) R_Y8 = _x;
#define SNCPU_SET_S8(_x) R_S8 = _x;

#define SNCPU_GET_A8(_x) _x = R_A8;
#define SNCPU_GET_X8(_x) _x = R_X8;
#define SNCPU_GET_Y8(_x) _x = R_Y8;
#define SNCPU_GET_S8(_x) _x = R_S8;

#define SNCPU_SET_A16(_x) R_A16 = _x;
#define SNCPU_SET_X16(_x) R_X16 = _x;
#define SNCPU_SET_Y16(_x) R_Y16 = _x;
#define SNCPU_SET_S16(_x) R_S16 = _x;

#define SNCPU_GET_A16(_x) _x = R_A16;
#define SNCPU_GET_X16(_x) _x = R_X16;
#define SNCPU_GET_Y16(_x) _x = R_Y16;
#define SNCPU_GET_S16(_x) _x = R_S16;

#define SNCPU_GETI(_x, _imm) _x = _imm;

#define SNCPU_GET_DP(_x) _x = R_DP;
#define SNCPU_SET_DP(_x) R_DP = _x; SNCPU_UPDATE_DPMASK();

#define SNCPU_GET_DB(_x) _x = R_DB;
#define SNCPU_SET_DB(_x) R_DB = _x;

#define SNCPU_GET_PC(_x) _x = R_PC;

#define SNCPU_SET_PC16(_Addr)	\
		R_PC&= ~0xFFFF;			\
		R_PC|= (_Addr) & 0xFFFF;

#define SNCPU_SET_PC24(_Addr)	\
		R_PC= _Addr & 0xFFFFFF;

#define SNCPU_SET_P(_x) R_P = _x; SNCPU_UNPACKFLAGS();
#define SNCPU_GET_P(_x) SNCPU_PACKFLAGS(); _x = R_P;

// get/set flag

#define SNCPU_SETFLAG_Z8(_x)  fZ = (_x) << 8;
#define SNCPU_SETFLAG_Z16(_x) fZ = (_x) << 0;
#define SNCPU_SETFLAG_N8(_x)  fN = (_x) << 8;
#define SNCPU_SETFLAG_N16(_x) fN = (_x) << 0;
#define SNCPU_SETFLAG_C(_x)  fC = (_x) & 1;
#define SNCPU_SETFLAGI_C(_x)  fC = (_x) & 1;
#define SNCPU_GETFLAG_C(_x)  _x = fC & 1;
#define SNCPU_GETFLAG_E(_x)  _x = pCpu->Regs.rE;

#define SNCPU_SETFLAG_I() R_P |= SNCPU_FLAG_I;
#define SNCPU_CLRFLAG_I() R_P &= ~SNCPU_FLAG_I; SNCPU_CHECKIRQ(1);
#define SNCPU_SETFLAG_D() R_P |= SNCPU_FLAG_D;
#define SNCPU_CLRFLAG_D() R_P &= ~SNCPU_FLAG_D;
#define SNCPU_SETFLAGI_V(__V) R_P &= ~SNCPU_FLAG_V; R_P |= ((__V) & 1) << 6;
#define SNCPU_SETFLAG_V(__V) R_P &= ~SNCPU_FLAG_V; R_P |= ((__V) & 1) << 6;

// flag pack/unpack

/* In emulation mode, indexed direct-page addressing wraps in the selected
   page only when DL is zero.  Pack the page base in the upper half and the
   arithmetic mask in the lower half so the hot opcode path needs no branch. */
#define SNCPU_UPDATE_DPMASK()							\
	do {											\
		if (pCpu->Regs.rE && !(R_DP & 0x00FF))			\
			R_DPMASK = ((Uint32)(R_DP & 0xFF00) << 16) | 0x00FF; \
		else										\
			R_DPMASK = (R_DP & 0x00FF ?				\
				((Uint32)SNCPU_CYCLE_FAST << 16) : 0) | 0xFFFF; \
	} while (0);

#define SNCPU_WRAP_DP_INDEX(_Addr)						\
	do {											\
		(_Addr) = ((_Addr) & (R_DPMASK & 0xFFFF)) |	\
			((R_DPMASK >> 16) & 0xFF00);			\
	} while (0);

#define SNCPU_DP_PENALTY()							\
	do {											\
		Uint32 _dpCycles = (R_DPMASK >> 16) & 0xFF;	\
		pCpu->Cycles -= _dpCycles;					\
		if (_dpCycles) {							\
			SNCPU_TESTCYCLES(1)					\
		}									\
	} while (0);

#define SNCPU_INDEX_READ_PENALTY(_Base, _Index)			\
	do {											\
		if (!(R_P & SNCPU_FLAG_X) ||					\
			((((_Base) + (_Index)) ^ (_Base)) & 0xFF00)) \
			SNCPU_SUBCYCLES(1);					\
	} while (0);

#define SNCPU_SETFLAG_E(_E)								\
	pCpu->Regs.rE = (_E) ? 1 : 0;						\
	if (pCpu->Regs.rE)									\
	{													\
		uOpcodeMod = SNCPU_OPCODE_E;					\
		R_P |= (SNCPU_FLAG_M | SNCPU_FLAG_X);			\
		pCpu->Regs.rX.b.h = 0;							\
		pCpu->Regs.rY.b.h = 0;							\
		pCpu->Regs.rS.b.h = 1;							\
	} else												\
	{													\
		uOpcodeMod = (R_P << 4) & 0x300;				\
	}												\
	SNCPU_UPDATE_DPMASK();

#define SNCPU_CHECKIRQ(_TailCycles)                         \
	do {                                                     \
		if ((pCpu->uSignal & SNCPU_SIGNAL_IRQ) &&             \
			!(R_P & SNCPU_FLAG_I))                             \
		{                                                     \
			Int32 nRemain = pCpu->Cycles -                     \
				(_TailCycles) * SNCPU_CYCLE_FAST;               \
			if (nRemain > 0)                                   \
			{                                                   \
				pCpu->nAbortCycles = nRemain;                    \
				pCpu->Cycles = 0;                                \
			}                                                   \
		}                                                     \
	} while (0);

#define SNCPU_UNPACKFLAGS()					\
	fC = (R_P & SNCPU_FLAG_C);					\
	fZ = (R_P & SNCPU_FLAG_Z) ^ SNCPU_FLAG_Z;	\
	fN = (R_P << 8);							\
	if (R_P&SNCPU_FLAG_X) {pCpu->Regs.rX.b.h = 0; pCpu->Regs.rY.b.h = 0;} \
	SNCPU_SETFLAG_E(pCpu->Regs.rE);

#define SNCPU_PACKFLAGS()								\
	R_P &= ~(SNCPU_FLAG_C | SNCPU_FLAG_Z |SNCPU_FLAG_N);	\
	R_P |= fC;												\
	R_P |= (fN >> 8) & 0x80;								\
	if (!(fZ&0xFFFF)) R_P|=SNCPU_FLAG_Z;

// operations

#define SNCPU_NOT8(_Dest) _Dest^=0xFF;
#define SNCPU_NOT16(_Dest) _Dest^=0xFFFF;

#define SNCPU_MUL(_Dest, _Src) _Dest*=_Src;
#define SNCPU_ADD(_Dest, _Src) _Dest+=_Src;
#define SNCPU_SUB(_Dest, _Src) _Dest-=_Src;

#define SNCPU_OR(_Dest, _Src) _Dest|=_Src;
#define SNCPU_XOR(_Dest, _Src) _Dest^=_Src;
#define SNCPU_AND(_Dest, _Src) _Dest&=_Src;
#define SNCPU_SHL(_Dest, _Src) _Dest<<=_Src;
#define SNCPU_SHR(_Dest, _Src) _Dest>>=_Src;

#define SNCPU_ADDI(_Dest, _Src) _Dest+=_Src;
#define SNCPU_SUBI(_Dest, _Src) _Dest-=_Src;
#define SNCPU_ORI(_Dest, _Src) _Dest|=_Src;
#define SNCPU_XORI(_Dest, _Src) _Dest^=_Src;
#define SNCPU_ANDI(_Dest, _Src) _Dest&=_Src;
#define SNCPU_SHLI(_Dest, _Src) _Dest<<=_Src;
#define SNCPU_SHRI(_Dest, _Src) _Dest>>=_Src;

#define SNCPU_ADC8(_Dest,_Src)					\
		{										\
			Uint32 _Target = _Dest;				\
			_Dest = _Target + _Src  + (fC&1);				\
			if ( ~(_Target ^ _Src) & (_Target ^ _Dest) & 0x80 )	\
					R_P |= SNCPU_FLAG_V;		\
			else	R_P &= ~SNCPU_FLAG_V;		\
			if (R_P & SNCPU_FLAG_D) {						\
				_Dest = _SNCpuDecimalADC8((fC&1), _Target, _Src); \
				if (_Dest & 0x200) R_P |= SNCPU_FLAG_V;	\
				else R_P &= ~SNCPU_FLAG_V;				\
			}										\
		}

#define SNCPU_ADC16(_Dest,_Src)					\
		{										\
			Uint32 _Target = _Dest;				\
			_Dest = _Target + _Src + (fC&1);				\
			if ( ~(_Target ^ _Src) & (_Target ^ _Dest) & 0x8000 )	\
					R_P |= SNCPU_FLAG_V;		\
			else	R_P &= ~SNCPU_FLAG_V;		\
			if (R_P & SNCPU_FLAG_D) {						\
				_Dest = _SNCpuDecimalADC16((fC&1), _Target, _Src); \
				if (_Dest & 0x20000) R_P |= SNCPU_FLAG_V;	\
				else R_P &= ~SNCPU_FLAG_V;				\
			}										\
		}

#define SNCPU_SBC8(_Dest,_Src)					\
		{										\
			Uint32 _Target = _Dest;				\
			_Dest = _Target + _Src + (fC&1);				\
			if ( ~(_Target ^ _Src) & (_Target ^ _Dest) & 0x80 )	\
					R_P |= SNCPU_FLAG_V;		\
			else	R_P &= ~SNCPU_FLAG_V;		\
			if (R_P & SNCPU_FLAG_D)	_Dest = _SNCpuDecimalSBC8((fC&1),_Target, _Src);				\
		}

#define SNCPU_SBC16(_Dest,_Src)					\
		{										\
			Uint32 _Target = _Dest;				\
			_Dest = _Target + _Src + (fC&1);				\
			if ( ~(_Target ^ _Src) & (_Target ^ _Dest) & 0x8000 )	\
					R_P |= SNCPU_FLAG_V;		\
			else	R_P &= ~SNCPU_FLAG_V;		\
			if (R_P & SNCPU_FLAG_D)	_Dest = _SNCpuDecimalSBC16((fC&1),_Target, _Src);				\
		}

#define SNCPU_BRREL(_bTest)				\
		{								\
		Int32	iRel;				\
		SNCPU_FETCH8(iRel);			\
		if (_bTest)					\
			{							\
			Uint32 uOldPC = rPC;		\
			iRel <<=24;				\
			iRel >>=24;				\
			rPC = (rPC & 0xFF0000) |	\
				((rPC + iRel) & 0xFFFF); \
			SNCPU_SUBCYCLES(1);		\
			if (fE && ((uOldPC ^ rPC) & 0xFF00)) \
				SNCPU_SUBCYCLES(1);	\
			}							\
		}

#include "sndebug.h"

Uint32 _SNCpuDecimalADC8(Uint32 uDest, Uint32 uTarget, Uint32 uSrc)
{
	Uint32 uResult;
	Uint32 uOverflow;

	uResult = (uTarget & 0x0F) + (uSrc & 0x0F) + (uDest & 1);
	if (uResult > 0x09)
		uResult += 0x06;
	uResult = (uTarget & 0xF0) + (uSrc & 0xF0) +
		(uResult > 0x0F ? 0x10 : 0) + (uResult & 0x0F);
	uOverflow = (~(uTarget ^ uSrc) & (uTarget ^ uResult) & 0x80) ?
		0x200 : 0;
	if (uResult > 0x9F)
		uResult += 0x60;
	return (Uint8)uResult | (uResult > 0xFF ? 0x100 : 0) | uOverflow;
}

Uint32 _SNCpuDecimalADC16(Uint32 uDest, Uint32 uTarget, Uint32 uSrc)
{
	Uint32 uResult;
	Uint32 uOverflow;

	uResult = (uTarget & 0x000F) + (uSrc & 0x000F) + (uDest & 1);
	if (uResult > 0x0009)
		uResult += 0x0006;
	uResult = (uTarget & 0x00F0) + (uSrc & 0x00F0) +
		(uResult > 0x000F ? 0x0010 : 0) + (uResult & 0x000F);
	if (uResult > 0x009F)
		uResult += 0x0060;
	uResult = (uTarget & 0x0F00) + (uSrc & 0x0F00) +
		(uResult > 0x00FF ? 0x0100 : 0) + (uResult & 0x00FF);
	if (uResult > 0x09FF)
		uResult += 0x0600;
	uResult = (uTarget & 0xF000) + (uSrc & 0xF000) +
		(uResult > 0x0FFF ? 0x1000 : 0) + (uResult & 0x0FFF);
	uOverflow = (~(uTarget ^ uSrc) & (uTarget ^ uResult) & 0x8000) ?
		0x20000 : 0;
	if (uResult > 0x9FFF)
		uResult += 0x6000;
	return (Uint16)uResult | (uResult > 0xFFFF ? 0x10000 : 0) | uOverflow;
}

Uint32 _SNCpuDecimalSBC8(Uint32 uDest, Uint32 uTarget, Uint32 uSrc)
{
	Int32 iResult;
	Uint32 uEncoded;

	/* uSrc is already one's-complemented by the opcode, matching the
	   65C816's binary A + ~M + C subtraction path. */
	iResult = (Int32)(uTarget & 0x0F) + (Int32)(uSrc & 0x0F) +
		(Int32)(uDest & 1);
	if (iResult <= 0x0F)
		iResult -= 0x06;
	iResult = (Int32)(uTarget & 0xF0) + (Int32)(uSrc & 0xF0) +
		(iResult > 0x0F ? 0x10 : 0) + (iResult & 0x0F);
	if (iResult <= 0xFF)
		iResult -= 0x60;

	uEncoded = (Uint8)iResult;
	if (iResult > 0xFF)
		uEncoded |= 0x100;
	return uEncoded;
}

Uint32 _SNCpuDecimalSBC16(Uint32 uDest, Uint32 uTarget, Uint32 uSrc)
{
	Int32 iResult;
	Uint32 uEncoded;

	iResult = (Int32)(uTarget & 0x000F) + (Int32)(uSrc & 0x000F) +
		(Int32)(uDest & 1);
	if (iResult <= 0x000F)
		iResult -= 0x0006;
	iResult = (Int32)(uTarget & 0x00F0) + (Int32)(uSrc & 0x00F0) +
		(iResult > 0x000F ? 0x0010 : 0) + (iResult & 0x000F);
	if (iResult <= 0x00FF)
		iResult -= 0x0060;
	iResult = (Int32)(uTarget & 0x0F00) + (Int32)(uSrc & 0x0F00) +
		(iResult > 0x00FF ? 0x0100 : 0) + (iResult & 0x00FF);
	if (iResult <= 0x0FFF)
		iResult -= 0x0600;
	iResult = (Int32)(uTarget & 0xF000) + (Int32)(uSrc & 0xF000) +
		(iResult > 0x0FFF ? 0x1000 : 0) + (iResult & 0x0FFF);
	if (iResult <= 0xFFFF)
		iResult -= 0x6000;

	uEncoded = (Uint16)iResult;
	if (iResult > 0xFFFF)
		uEncoded |= 0x10000;
	return uEncoded;
}

// memory i/o

static __inline Uint8 __SNCPURead8(SNCpuT *pCpu, Uint32 Addr)
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
		return pCpu->Bank[iBank].pReadTrapFunc(pCpu, Addr);
	}
}

static Uint8 _SNCPURead8(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uData;
	SNCPU_SUBMEMCYCLES(Addr,1);
	uData =  __SNCPURead8(pCpu, Addr);
	return  uData;
}

static Uint16 _SNCPURead16(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uData;
	SNCPU_SUBMEMCYCLES(Addr,2);
	uData =  __SNCPURead8(pCpu, Addr);
	uData|= (__SNCPURead8(pCpu, Addr+1)<<8);
	return  uData;
}

static Uint16 _SNCPURead16Wrap16(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uAddr1 = (Addr & 0xFF0000) | ((Addr + 1) & 0xFFFF);
	Uint32 uData;
	SNCPU_SUBMEMCYCLES(Addr, 1);
	SNCPU_SUBMEMCYCLES(uAddr1, 1);
	uData = __SNCPURead8(pCpu, Addr);
	uData |= __SNCPURead8(pCpu, uAddr1) << 8;
	return (Uint16)uData;
}

static Uint32 _SNCPURead24Wrap16(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uAddr1 = (Addr & 0xFF0000) | ((Addr + 1) & 0xFFFF);
	Uint32 uAddr2 = (Addr & 0xFF0000) | ((Addr + 2) & 0xFFFF);
	Uint32 uData;
	SNCPU_SUBMEMCYCLES(Addr, 1);
	SNCPU_SUBMEMCYCLES(uAddr1, 1);
	SNCPU_SUBMEMCYCLES(uAddr2, 1);
	uData = __SNCPURead8(pCpu, Addr);
	uData |= __SNCPURead8(pCpu, uAddr1) << 8;
	uData |= __SNCPURead8(pCpu, uAddr2) << 16;
	return uData;
}

static Uint16 _SNCPURead16DP(SNCpuT *pCpu, Uint32 Addr, Uint32 uWrap)
{
	Uint32 uMask = uWrap & 0xFFFF;
	Uint32 uBase = (uWrap >> 16) & 0xFF00;
	Uint32 uAddr1 = ((Addr + 1) & uMask) | uBase;
	Uint32 uData;
	SNCPU_SUBMEMCYCLES(Addr, 1);
	SNCPU_SUBMEMCYCLES(uAddr1, 1);
	uData = __SNCPURead8(pCpu, Addr);
	uData |= __SNCPURead8(pCpu, uAddr1) << 8;
	return (Uint16)uData;
}

static __inline Uint8 __SNCPUFetch8(SNCpuT *pCpu, Uint32 Addr)
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
		return pCpu->Bank[iBank].pReadTrapFunc(pCpu, Addr);

	}
}

static Uint8 _SNCPUFetch8(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uData;
	SNCPU_SUBMEMCYCLES(Addr,1);
	uData =  __SNCPUFetch8(pCpu, Addr);
	return  uData;
}

static Uint16 _SNCPUFetch16(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uData, uAddr1;
	uAddr1 = (Addr & 0xFF0000) | ((Addr + 1) & 0xFFFF);
	SNCPU_SUBMEMCYCLES(Addr,1);
	SNCPU_SUBMEMCYCLES(uAddr1,1);
	uData =  __SNCPUFetch8(pCpu, Addr);
	uData|= (__SNCPUFetch8(pCpu, uAddr1)<<8);
	return  uData;
}

static Uint32 _SNCPUFetch24(SNCpuT *pCpu, Uint32 Addr)
{
	Uint32 uData, uAddr1, uAddr2;
	uAddr1 = (Addr & 0xFF0000) | ((Addr + 1) & 0xFFFF);
	uAddr2 = (Addr & 0xFF0000) | ((Addr + 2) & 0xFFFF);
	SNCPU_SUBMEMCYCLES(Addr,1);
	SNCPU_SUBMEMCYCLES(uAddr1,1);
	SNCPU_SUBMEMCYCLES(uAddr2,1);
	uData = (__SNCPUFetch8(pCpu, Addr+0) << 0);
	uData|= (__SNCPUFetch8(pCpu, uAddr1) << 8);
	uData|= (__SNCPUFetch8(pCpu, uAddr2) << 16);
	return  uData;
}

static __inline void  __SNCPUWrite8(SNCpuT *pCpu, Uint32 Addr, Uint8 Data)
{
	Uint32 iBank;
	Uint8 *pBankMem;

	iBank = Addr >> SNCPU_BANK_SHIFT;

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

static void  _SNCPUWrite8(SNCpuT *pCpu, Uint32 Addr, Uint8 Data)
{
	SNCPU_SUBMEMCYCLES(Addr,1);
	__SNCPUWrite8(pCpu, Addr + 0, Data >> 0);
}

static void  _SNCPUWrite16(SNCpuT *pCpu, Uint32 Addr, Uint16 Data)
{
	SNCPU_SUBMEMCYCLES(Addr,2);
	__SNCPUWrite8(pCpu, Addr + 0, Data >> 0);
	__SNCPUWrite8(pCpu, Addr + 1, Data >> 8);
}

static void _SNCPUWrite16Wrap16(SNCpuT *pCpu, Uint32 Addr, Uint16 Data)
{
	Uint32 uAddr1 = (Addr & 0xFF0000) | ((Addr + 1) & 0xFFFF);
	SNCPU_SUBMEMCYCLES(Addr, 1);
	SNCPU_SUBMEMCYCLES(uAddr1, 1);
	__SNCPUWrite8(pCpu, Addr, Data);
	__SNCPUWrite8(pCpu, uAddr1, Data >> 8);
}

static void _SNCPUPush8(SNCpuT *pCpu, Uint8 Data)
{
	SNCPU_SUBCYCLESSLOW(1);
	__SNCPUWrite8(pCpu, pCpu->Regs.rS.w, Data);
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

static void _SNCPUPush16(SNCpuT *pCpu, Uint16 Data)
{
	_SNCPUPush8(pCpu, Data >> 8);
	_SNCPUPush8(pCpu, Data & 0xFF);
}

static void _SNCPUPush24(SNCpuT *pCpu, Uint32 Data)
{
	_SNCPUPush8(pCpu, Data >> 16);
	_SNCPUPush8(pCpu, Data >> 8);
	_SNCPUPush8(pCpu, Data & 0xFF);
}

static Uint8 _SNCPUPop8(SNCpuT *pCpu)
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
	SNCPU_SUBCYCLESSLOW(1);
	return __SNCPURead8(pCpu, pCpu->Regs.rS.w);
}

static Uint16 _SNCPUPop16(SNCpuT *pCpu)
{
	Uint32 uData;
	uData =  _SNCPUPop8(pCpu);
	uData|= (_SNCPUPop8(pCpu)<<8);
	return uData;
}

static Uint32 _SNCPUPop24(SNCpuT *pCpu)
{
	Uint32 uData;
	uData =  _SNCPUPop8(pCpu);
	uData|= (_SNCPUPop8(pCpu)<<8);
	uData|= (_SNCPUPop8(pCpu)<<16);
	return uData;
}

/* Some 65C816 instructions perform a multi-byte stack transfer with the
   native 16-bit stack pointer even while E=1, and only restore page $01 after
   the complete transfer.  PHD/PLD, PEA/PEI/PER, JSL/RTL and indexed-indirect
   JSR use these raw helpers. */
static void _SNCPURestrictStack(SNCpuT *pCpu)
{
	if (pCpu->Regs.rE)
		pCpu->Regs.rS.w = 0x0100 | (pCpu->Regs.rS.w & 0x00FF);
}

static Uint8 _SNCPUPop8Raw(SNCpuT *pCpu)
{
	Uint8 uData;
	pCpu->Regs.rS.w++;
	SNCPU_SUBCYCLESSLOW(1);
	uData = __SNCPURead8(pCpu, pCpu->Regs.rS.w);
	_SNCPURestrictStack(pCpu);
	return uData;
}

static void _SNCPUPush16Raw(SNCpuT *pCpu, Uint16 Data)
{
	SNCPU_SUBCYCLESSLOW(1);
	__SNCPUWrite8(pCpu, pCpu->Regs.rS.w, Data >> 8);
	pCpu->Regs.rS.w--;
	SNCPU_SUBCYCLESSLOW(1);
	__SNCPUWrite8(pCpu, pCpu->Regs.rS.w, Data & 0xFF);
	pCpu->Regs.rS.w--;
	_SNCPURestrictStack(pCpu);
}

static void _SNCPUPush24Raw(SNCpuT *pCpu, Uint32 Data)
{
	SNCPU_SUBCYCLESSLOW(1);
	__SNCPUWrite8(pCpu, pCpu->Regs.rS.w, Data >> 16);
	pCpu->Regs.rS.w--;
	SNCPU_SUBCYCLESSLOW(1);
	__SNCPUWrite8(pCpu, pCpu->Regs.rS.w, Data >> 8);
	pCpu->Regs.rS.w--;
	SNCPU_SUBCYCLESSLOW(1);
	__SNCPUWrite8(pCpu, pCpu->Regs.rS.w, Data);
	pCpu->Regs.rS.w--;
	_SNCPURestrictStack(pCpu);
}

static Uint16 _SNCPUPop16Raw(SNCpuT *pCpu)
{
	Uint32 uData;
	pCpu->Regs.rS.w++;
	SNCPU_SUBCYCLESSLOW(1);
	uData = __SNCPURead8(pCpu, pCpu->Regs.rS.w);
	pCpu->Regs.rS.w++;
	SNCPU_SUBCYCLESSLOW(1);
	uData |= __SNCPURead8(pCpu, pCpu->Regs.rS.w) << 8;
	_SNCPURestrictStack(pCpu);
	return (Uint16)uData;
}

static Uint32 _SNCPUPop24Raw(SNCpuT *pCpu)
{
	Uint32 uData;
	pCpu->Regs.rS.w++;
	SNCPU_SUBCYCLESSLOW(1);
	uData = __SNCPURead8(pCpu, pCpu->Regs.rS.w);
	pCpu->Regs.rS.w++;
	SNCPU_SUBCYCLESSLOW(1);
	uData |= __SNCPURead8(pCpu, pCpu->Regs.rS.w) << 8;
	pCpu->Regs.rS.w++;
	SNCPU_SUBCYCLESSLOW(1);
	uData |= __SNCPURead8(pCpu, pCpu->Regs.rS.w) << 16;
	_SNCPURestrictStack(pCpu);
	return uData;
}

#if SNCPU_PROFILE
#include <windows.h>
#include <stdio.h>
#include "sndisasm.h"
Uint32 uExecCount[0x800];
Bool _bDumpExecCount = FALSE;
void DumpExecCount()
{
	Uint8 opdata[8];
	char opstr[64];
	Int32 iOp;
	memset(opdata,0,sizeof(opdata));
	for (iOp=0; iOp < 0x800; iOp++)
	{
		Uint8 flags;
		flags = (iOp>>8) << 4;
		if (uExecCount[iOp] > 0)
		{
			char str[64];
			opdata[0]=iOp;
			SNDisasm(opstr, opdata, 0, &flags);
			sprintf(str, "%03X %08d %s\n", iOp, uExecCount[iOp], opstr);
			OutputDebugString(str);
		}
	}
}
#endif

#if SNCPU_TRACE
SNCpuRegsT _LastRegs[SNCPU_TRACE_NUM];
#endif

Int32 SNCPUExecute_C(SNCpuT *pCpu)
{
//	Int32	nCycles;
	Uint32	rPC;
	Uint32 uOpcodeMod;
	Uint32 R_DPMASK;
	Uint32 fN; // N??????? ????????
	Uint32 fZ; // ZZZZZZZZ ZZZZZZZZ
	Uint32 fC; // 00000000 0000000C

#if SNCPU_PROFILE
	if (_bDumpExecCount)
	{
		DumpExecCount();
		_bDumpExecCount=FALSE;
	}
#endif

	// registerize registers
	rPC			= pCpu->Regs.rPC;

	// UNPACK flags
	SNCPU_UNPACKFLAGS();

	// rePACK flags
	while (pCpu->Cycles > 0)
	{
		Uint32 uOpcode;
		Uint32 t0,t1,t2;

#if SNCPU_TRACE
		int i;
		pCpu->Regs.rPC = rPC;
		for (i=SNCPU_TRACE_NUM-1; i > 0; i--)
		{
			_LastRegs[i] = _LastRegs[i-1];
		}
		_LastRegs[0] = pCpu->Regs;
/*
		if (rPC==0xC9C)
		{
			i++;
		}*/
#endif

		SNCPU_FETCH8(uOpcode);
		uOpcode|= uOpcodeMod;
		uOpcode&= 0x7FF;

/*		if (uOpcode & SNCPU_OPCODE_X)
		{
			assert(!(R_X & 0xFF00));
			assert(!(R_Y & 0xFF00));
		}*/

		#if SNCPU_PROFILE
		uExecCount[uOpcode]++;
		#endif

		switch (uOpcode)
		{

#include "op65816.h"

		// addR_imm8_SEP_
	SNCPU_OP_ALL(0xE2);
		SNCPU_FETCH8(t1);

		R_P |= t1; // set flags

		// update live flags
		if (t1&SNCPU_FLAG_C) fC = 1;
		if (t1&SNCPU_FLAG_Z) fZ = 0;
		if (t1&SNCPU_FLAG_N) fN = 0xFFFF;
		if (t1&SNCPU_FLAG_X) {pCpu->Regs.rX.b.h = 0; pCpu->Regs.rY.b.h = 0;}
		SNCPU_SETFLAG_E(pCpu->Regs.rE);

		SNCPU_SUBCYCLES(1);
		break;

		// addR_imm8_REP_
	SNCPU_OP_ALL(0xC2);
		SNCPU_FETCH8(t1);

		R_P |= t1; // reset flags
		R_P ^= t1; //

		// update live flags
		if (t1&SNCPU_FLAG_C) fC = 0;
		if (t1&SNCPU_FLAG_Z) fZ = 1;
		if (t1&SNCPU_FLAG_N) fN = 0;
		SNCPU_SETFLAG_E(pCpu->Regs.rE);
		SNCPU_CHECKIRQ(1);

		SNCPU_SUBCYCLES(1);
		break;

	SNCPU_OP_ALL(0x10);
		// BPL
		SNCPU_BRREL(!(fN & 0x8000));
		break;

	SNCPU_OP_ALL(0x30);
		// BMI
		SNCPU_BRREL((fN & 0x8000));
		break;

	SNCPU_OP_ALL(0xF0);
		// BEQ
		SNCPU_BRREL(!(fZ&0xFFFF));
		break;

	SNCPU_OP_ALL(0xD0);
		// BNE
		SNCPU_BRREL((fZ&0xFFFF));
		break;

	SNCPU_OP_ALL(0x90);
		// BCC
		SNCPU_BRREL(fC==0);
		break;

	SNCPU_OP_ALL(0xB0);
		// BCS
		SNCPU_BRREL(fC!=0);
		break;

	SNCPU_OP_ALL(0x50);
		// BVC
		SNCPU_BRREL(!(R_P&SNCPU_FLAG_V));
		break;

	SNCPU_OP_ALL(0x70);
		// BVS
		SNCPU_BRREL((R_P&SNCPU_FLAG_V));
		break;

	SNCPU_OP_ALL(0x80);
		// BRA
		SNCPU_BRREL(1);
		break;

	SNCPU_OP_ALL(0x82);
		// BRL
		{
			Int32	iRel;
			SNCPU_FETCH16(iRel);
			iRel <<=16;
			iRel >>=16;
			// maintain bank
#if 1
			iRel+= rPC;
			rPC = (rPC & 0xFF0000) + (iRel & 0xFFFF);
#else
			rPC+=iRel;
#endif
			SNCPU_SUBCYCLES(1);
		}
		break;

	SNCPU_OP_X0(0x54);
		// MVN
		{
			Uint32 uSrcBank, uDestBank;
			Uint8 uData;

			SNCPU_FETCH8(uDestBank);
			SNCPU_FETCH8(uSrcBank);
			uDestBank <<= 16;
			uSrcBank <<= 16;

			R_DB = uDestBank;

			SNCPU_READ8( uSrcBank | R_X16, uData );
			SNCPU_WRITE8(uDestBank | R_Y16, uData);

			R_X16++;
			R_Y16++;
			if (R_A16!=0)R_PC -=3;
			R_A16--;
		}
		SNCPU_SUBCYCLES(2);
		break;

	SNCPU_OP_X1E1(0x54);
		// MVN
		{
			Uint32 uSrcBank, uDestBank;
			Uint8 uData;

			SNCPU_FETCH8(uDestBank);
			SNCPU_FETCH8(uSrcBank);
			uDestBank <<= 16;
			uSrcBank <<= 16;

			R_DB = uDestBank;

			SNCPU_READ8( uSrcBank | R_X8, uData );
			SNCPU_WRITE8(uDestBank | R_Y8, uData);

			R_X8++;
			R_Y8++;
			if (R_A16!=0)R_PC -=3;
			R_A16--;
		}
		SNCPU_SUBCYCLES(2);
		break;

	SNCPU_OP_X0(0x44);
		// MVP
		{
			Uint32 uSrcBank, uDestBank;
			Uint8 uData;

			SNCPU_FETCH8(uDestBank);
			SNCPU_FETCH8(uSrcBank);
			uDestBank <<= 16;
			uSrcBank <<= 16;

			R_DB = uDestBank;

			SNCPU_READ8( uSrcBank | R_X16, uData );
			SNCPU_WRITE8(uDestBank | R_Y16, uData);

			R_X16--;
			R_Y16--;
			if (R_A16!=0)R_PC -=3;
			R_A16--;
		}
		SNCPU_SUBCYCLES(2);
		break;

	SNCPU_OP_X1E1(0x44);
		// MVP
		{
			Uint32 uSrcBank, uDestBank;
			Uint8 uData;

			SNCPU_FETCH8(uDestBank);
			SNCPU_FETCH8(uSrcBank);
			uDestBank <<= 16;
			uSrcBank <<= 16;

			R_DB = uDestBank;

			SNCPU_READ8( uSrcBank | R_X8, uData );
			SNCPU_WRITE8(uDestBank | R_Y8, uData);

			R_X8--;
			R_Y8--;
			if (R_A16!=0)R_PC -=3;
			R_A16--;
		}
		SNCPU_SUBCYCLES(2);
		break;

		// WAI
	SNCPU_OP_ALL(0xCB)
		if (!(pCpu->uSignal & (SNCPU_SIGNAL_IRQ | SNCPU_SIGNAL_NMIEDGE)))
		{
			// set waiting signal
			pCpu->uSignal |= SNCPU_SIGNAL_WAI;
			/* Architectural PC already points at the next instruction. */
		} else
		{
			pCpu->uSignal &= ~SNCPU_SIGNAL_WAI;
		}
		SNCPU_SUBCYCLES(3);
		if (pCpu->uSignal & SNCPU_SIGNAL_WAI)
			pCpu->Cycles = 0;
		break;

		// STP: only reset can restart the processor.
	SNCPU_OP_ALL(0xDB)
		pCpu->uSignal |= SNCPU_SIGNAL_STP;
		SNCPU_SUBCYCLES(3);
		pCpu->Cycles = 0;
		break;

	SNCPU_OP_ALL(0x42)
		Uint32 uDestBank __attribute__((unused));
		SNCPU_FETCH8(uDestBank);
		break;
		default:	// unimplemented opcode
			SNCPU_SUBCYCLES(1);
		}
	}

	// restore registers
	SNCPU_PACKFLAGS();
	pCpu->Regs.rPC	= rPC;

	return 0;
}
