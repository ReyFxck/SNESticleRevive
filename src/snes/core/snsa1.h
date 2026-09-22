/*
 * Experimental SA-1 support for SNESticle Revive.
 *
 * Clean-room implementation based on public hardware behavior. Phase 1
 * intentionally keeps the coprocessor state independent from the S-CPU and
 * does not execute SA-1 65C816 opcodes yet.
 */

#ifndef _SNSA1_H
#define _SNSA1_H

#include "types.h"

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
	Uint32 LastSliceCycles;
	Uint32 ResetEpoch;
	Uint16 LastResetVector;
	Uint8  MasterRemainder;
	Bool   Running;
};

class SNSA1
{
public:
	SNSA1();

	void SetMemory(const Uint8 *pRom, Uint32 uRomBytes,
	               Uint8 *pBWRAM, Uint32 uBWRAMBytes);
	void Reset(Bool bHardReset);

	Uint8 ReadRegister(Uint16 uAddr) const;
	void  WriteRegister(Uint16 uAddr, Uint8 uData);

	Uint8 ReadIRAM(Uint16 uAddr) const;
	void  WriteIRAM(Uint16 uAddr, Uint8 uData);

	Uint8 ReadBWRAMWindow(Uint16 uAddr) const;
	void  WriteBWRAMWindow(Uint16 uAddr, Uint8 uData);
	Uint8 ReadBWRAMDirect(Uint32 uAddr) const;
	void  WriteBWRAMDirect(Uint32 uAddr, Uint8 uData);

	// Deterministic scheduler credit. One SA-1 tick is two SNES master clocks.
	void StepMasterCycles(Int32 nMasterCycles);

	Bool   IsRunning() const { return m_State.Running; }
	Uint16 GetResetVector() const;
	Uint32 GetLastSliceCycles() const { return m_State.LastSliceCycles; }
	const SA1State *GetState() const { return &m_State; }

private:
	Uint32 MirrorBWRAM(Uint32 uOffset) const;

	SA1State m_State;
	Uint8    m_IRAM[SNSA1_IRAM_SIZE];

	const Uint8 *m_pRom;
	Uint32       m_uRomBytes;
	Uint8       *m_pBWRAM;
	Uint32       m_uBWRAMBytes;
};

#endif
