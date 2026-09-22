/*
 * Experimental SA-1 support for SNESticle Revive.
 *
 * The SA-1 owns a completely separate 65C816 context.  Correctness builds
 * intentionally execute that context with the portable C interpreter even
 * when the S-CPU selects the hand-written R5900 backend on PlayStation 2.
 */

#ifndef _SNSA1_H
#define _SNSA1_H

#include "types.h"

extern "C" {
#include "sncpu.h"
}

#define SNSA1_IRAM_SIZE          0x0800
#define SNSA1_REGISTER_BASE      0x2200
#define SNSA1_REGISTER_LAST      0x23FF
#define SNSA1_REGISTER_COUNT     0x0200
#define SNSA1_VERSION_CODE       0x23
#define SNSA1_MASTER_PER_TICK    2

struct SA1State
{
	Uint8  Registers[SNSA1_REGISTER_COUNT];
	Uint64 MasterCycles;
	Uint64 ScheduledCycles;
	Uint64 ExecutedCycles;
	Uint64 DMAStallTicks;
	Uint32 LastSliceCycles;
	Uint32 ExecutionSlices;
	Uint32 DMATransferredBytes;
	Uint32 ResetEpoch;
	Uint16 LastResetVector;
	Uint32 HCounter;
	Uint16 VCounter;
	Uint16 HCounterLatch;
	Uint16 VCounterLatch;
	Uint16 TimerScanlines;
	Uint16 DMARemaining;
	Uint16 ArithmeticOp1;
	Uint16 ArithmeticOp2;
	Uint64 ArithmeticResult;
	Uint32 ArithmeticStartClock;
	Uint16 VariableData;
	Uint32 DMASource;
	Uint32 DMADest;
	Uint8  VariableBitPos;
	Uint8  MasterRemainder;
	Uint8  TimerRemainder;
	Uint8  DMAWaitTicks;
	Uint8  CharConvLine;
	Bool   ArithmeticOverflow;
	Bool   ArithmeticPending;
	Bool   TimerMatch;
	Bool   NMIPending;
	Bool   CC1Active;
	Bool   DMARunning;
	Bool   Running;
};

struct SA1SaveState
{
	SA1State State;
	SNCpuRegsT CpuRegs;
	Int32 CpuCycles;
	Int32 CpuCounter[SNCPU_COUNTER_NUM];
	Int32 CpuAbortCycles;
	Uint8 CpuSignal;
	Uint8 CpuNmiDmaDelay;
	Uint8 CpuIrqPending;
	Uint8 Reserved;
	Uint8 IRAM[SNSA1_IRAM_SIZE];
};

class SNSA1
{
public:
	SNSA1();
	~SNSA1();

	void SetMemory(const Uint8 *pRom, Uint32 uRomBytes,
	               Uint8 *pBWRAM, Uint32 uBWRAMBytes);
	void SetVideoRegion(Bool bPAL);
	void Reset(Bool bHardReset);
	void SaveState(SA1SaveState *pState) const;
	void RestoreState(const SA1SaveState *pState);

	// Raw register helpers are kept for focused unit tests and state setup.
	Uint8 ReadRegister(Uint16 uAddr);
	void  WriteRegister(Uint16 uAddr, Uint8 uData);

	// Hardware-visible register ports differ between the S-CPU and SA-1 CPU.
	Uint8 ReadSCPURegister(Uint16 uAddr);
	void  WriteSCPURegister(Uint16 uAddr, Uint8 uData);
	Uint8 ReadSA1Register(Uint16 uAddr);
	void  WriteSA1Register(Uint16 uAddr, Uint8 uData);

	Uint8 ReadIRAM(Uint16 uAddr) const;
	// Public write path is the S-CPU side ($3000-$37FF).
	void  WriteIRAM(Uint16 uAddr, Uint8 uData);

	// Raw BW-RAM accessors.  S-CPU reads use the wrappers below so active
	// character-conversion type 1 can replace the packed BW-RAM data.
	Uint8 ReadBWRAMWindow(Uint16 uAddr) const;
	void  WriteBWRAMWindow(Uint16 uAddr, Uint8 uData);
	Uint8 ReadBWRAMDirect(Uint32 uAddr) const;
	void  WriteBWRAMDirect(Uint32 uAddr, Uint8 uData);
	Uint8 ReadSCPUBWRAMWindow(Uint16 uAddr);
	Uint8 ReadSCPUBWRAMDirect(Uint32 uAddr);

	// Character conversion type 1 is exposed through the S-CPU BW-RAM bus.
	Bool  IsCC1Active() const { return m_State.CC1Active; }
	Uint8 ReadCC1Byte(Uint32 uAddr);

	// S-CPU interrupt/vector bridge.  State remains owned by the SA-1.
	Bool  SCPUIRQPending() const;
	Bool  SCPUUseNMIVector() const { return (m_State.Registers[0x009] & 0x10) ? TRUE : FALSE; }
	Bool  SCPUUseIRQVector() const { return (m_State.Registers[0x009] & 0x40) ? TRUE : FALSE; }
	Uint16 GetSCPUNMIVector() const;
	Uint16 GetSCPUIRQVector() const;

	// Add master-clock time and execute the independent SA-1 65C816 slice.
	void StepMasterCycles(Int32 nMasterCycles);

	Bool   IsRunning() const { return m_State.Running; }
	Bool   IsDMARunning() const { return m_State.DMARunning; }
	Uint16 GetResetVector() const;
	Uint32 GetLastSliceCycles() const { return m_State.LastSliceCycles; }
	const SA1State *GetState() const { return &m_State; }
	SNCpuT *GetCpu() { return &m_Cpu; }
	const SNCpuT *GetCpu() const { return &m_Cpu; }

private:
	static Uint8 SNCPU_TRAPFUNC CpuReadTrap(SNCpuT *pCpu, Uint32 uAddr);
	static void  SNCPU_TRAPFUNC CpuWriteTrap(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);

	static Uint32 MirrorRomOffset(Uint32 uSize, Uint32 uPos);
	Uint32 MirrorBWRAM(Uint32 uOffset) const;

	void ResetCPUContext();
	void ReleaseCPUReset();
	void MapCpuMemory();
	void MapRomGroup(Uint32 uWhich, Uint8 uMap);
	void RunScheduled(Uint32 uSA1Cycles);
	Bool ExecuteCpuC();
	Bool ExecuteCpuFast();
	Bool ServiceNMI();
	Bool ServiceIRQ();
	void UpdateTimer(Uint32 uMasterCycles);
	void ProcessArithmetic();
	void ExecuteArithmetic();
	Uint8 ReadVariableBus(Uint32 uAddr);
	void LoadVariableData();
	void IncrementVariablePosition();
	void StartDMA();
	Uint32 RunDMA(Uint32 uSA1Ticks);
	Uint8 ReadDMASource(Uint8 uSource, Uint32 uAddr);
	void CompleteDMA();
	void StartCC1();
	void ExecuteCC2();
	void ConvertCC1Tile(Uint32 uAddr);

	Uint8 ReadCpuBus(Uint32 uAddr);
	void  WriteCpuBus(Uint32 uAddr, Uint8 uData);
	Uint8 ReadSA1BWRAMWindow(Uint16 uAddr) const;
	void  WriteSA1BWRAMWindow(Uint16 uAddr, Uint8 uData);
	Uint8 ReadBitmap(Uint32 uVirtualAddr) const;
	void  WriteBitmap(Uint32 uVirtualAddr, Uint8 uData);
	void  WriteIRAMSA1(Uint16 uAddr, Uint8 uData);
	void  WriteBWRAMDirectSA1(Uint32 uAddr, Uint8 uData);
	Bool  CanWriteIRAM(Uint16 uAddr, Bool bSA1Side) const;
	Bool  CanWriteBWRAM(Uint32 uOffset, Bool bSA1Side) const;
	void  UpdateIRQLine();

	SA1State m_State;
	SNCpuT   m_Cpu;
	Uint8    m_IRAM[SNSA1_IRAM_SIZE];

	const Uint8 *m_pRom;
	Uint32       m_uRomBytes;
	Uint8       *m_pBWRAM;
	Uint32       m_uBWRAMBytes;
};

#endif
