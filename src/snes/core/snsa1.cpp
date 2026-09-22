/*
 * Experimental SA-1 support for SNESticle Revive.
 *
 * Phase 2 gives the chip an independent 65C816 execution context and address
 * map.  The execution backend remains selected globally by SNCPUSetExecuteFunc:
 * PS2 therefore runs this context through sn65816.S, while host tests use the
 * portable C core.  DMA/arithmetic/bitmap engines remain later phases.
 */

#include <string.h>
#include "snsa1.h"

static const Uint8 _SA1LoBankBase[4] = { 0x00, 0x20, 0x80, 0xA0 };

SNSA1::SNSA1()
{
	m_pRom = NULL;
	m_uRomBytes = 0;
	m_pBWRAM = NULL;
	m_uBWRAMBytes = 0;
	SNCPUNew(&m_Cpu);
	m_Cpu.pUserData = this;
	Reset(TRUE);
}

SNSA1::~SNSA1()
{
	SNCPUDelete(&m_Cpu);
}

void SNSA1::SetMemory(const Uint8 *pRom, Uint32 uRomBytes,
                      Uint8 *pBWRAM, Uint32 uBWRAMBytes)
{
	m_pRom = pRom;
	m_uRomBytes = uRomBytes;
	m_pBWRAM = pBWRAM;
	m_uBWRAMBytes = uBWRAMBytes;
	MapCpuMemory();
}

void SNSA1::ResetCPUContext()
{
	SNCPUResetCounters(&m_Cpu);
	SNCPUResetRegs(&m_Cpu);
	m_Cpu.pUserData = this;
	m_Cpu.uSignal = 0;
	m_Cpu.uNmiDmaDelay = 0;
	m_Cpu.uIrqPending = 0;
	m_Cpu.Regs.rP = SNCPU_FLAG_M | SNCPU_FLAG_X | SNCPU_FLAG_I;
	m_Cpu.Regs.rE = 1;
	m_Cpu.Regs.rS.w = 0x01FF;
	m_Cpu.Regs.rPC = 0;
	m_Cpu.Regs.rDB = 0;
	m_Cpu.Regs.rDP = 0;
}

void SNSA1::Reset(Bool bHardReset)
{
	memset(&m_State, 0, sizeof(m_State));
	if (bHardReset)
		memset(m_IRAM, 0, sizeof(m_IRAM));

	m_State.Registers[0x000] = 0x20; // $2200: SA-1 held in reset
	m_State.Registers[0x020] = 0x00; // $2220: C bank
	m_State.Registers[0x021] = 0x01; // $2221: D bank
	m_State.Registers[0x022] = 0x02; // $2222: E bank
	m_State.Registers[0x023] = 0x03; // $2223: F bank
	m_State.Registers[0x028] = 0x0F; // $2228: BW-RAM protected area
	m_State.Running = FALSE;

	ResetCPUContext();
	MapCpuMemory();
}

Uint16 SNSA1::GetResetVector() const
{
	return (Uint16)(m_State.Registers[0x003] |
	                ((Uint16)m_State.Registers[0x004] << 8));
}

void SNSA1::ReleaseCPUReset()
{
	ResetCPUContext();
	m_State.LastResetVector = GetResetVector();
	m_State.ResetEpoch++;
	m_State.MasterRemainder = 0;
	m_Cpu.Regs.rPC = m_State.LastResetVector;
}

Uint8 SNSA1::ReadRegister(Uint16 uAddr) const
{
	if (uAddr < SNSA1_REGISTER_BASE || uAddr > SNSA1_REGISTER_LAST)
		return 0xFF;

	if (uAddr == 0x2300)
		return (Uint8)((m_State.Registers[0x009] & 0x5F) |
		               (m_State.Registers[0x100] & 0xA0));
	if (uAddr == 0x2301)
		return (Uint8)((m_State.Registers[0x000] & 0x0F) |
		               (m_State.Registers[0x101] & 0xF0));
	if (uAddr == 0x230E)
		return SNSA1_VERSION_CODE;

	return m_State.Registers[uAddr - SNSA1_REGISTER_BASE];
}

void SNSA1::UpdateIRQLine()
{
	Bool bPending = ((m_State.Registers[0x101] & 0x80) &&
	                 (m_State.Registers[0x00A] & 0x80)) ? TRUE : FALSE;
	SNCPUSignalIRQ(&m_Cpu, bPending ? 1 : 0);
}

void SNSA1::WriteRegister(Uint16 uAddr, Uint8 uData)
{
	Uint8 uOld;

	if (uAddr < 0x2200 || uAddr > 0x22FF)
		return;

	uOld = m_State.Registers[uAddr - SNSA1_REGISTER_BASE];
	m_State.Registers[uAddr - SNSA1_REGISTER_BASE] = uData;

	switch (uAddr)
	{
	case 0x2200:
		if ((uOld & 0x20) && !(uData & 0x20))
			ReleaseCPUReset();
		else if (!(uOld & 0x20) && (uData & 0x20))
			ResetCPUContext();

		if (uData & 0x80)
			m_State.Registers[0x101] |= 0x80;

		m_State.Running = ((uData & 0x60) == 0) ? TRUE : FALSE;
		if (!m_State.Running)
			m_State.MasterRemainder = 0;
		UpdateIRQLine();
		break;

	case 0x2202:
		if (uData & 0x80) m_State.Registers[0x100] &= (Uint8)~0x80;
		if (uData & 0x20) m_State.Registers[0x100] &= (Uint8)~0x20;
		break;

	case 0x2209:
		if (uData & 0x80)
			m_State.Registers[0x100] |= 0x80;
		break;

	case 0x220A:
		UpdateIRQLine();
		break;

	case 0x220B:
		if (uData & 0x80) m_State.Registers[0x101] &= (Uint8)~0x80;
		if (uData & 0x40) m_State.Registers[0x101] &= (Uint8)~0x40;
		if (uData & 0x20) m_State.Registers[0x101] &= (Uint8)~0x20;
		if (uData & 0x10) m_State.Registers[0x101] &= (Uint8)~0x10;
		UpdateIRQLine();
		break;

	case 0x2220:
	case 0x2221:
	case 0x2222:
	case 0x2223:
		MapRomGroup((Uint32)(uAddr - 0x2220), uData);
		break;

	default:
		break;
	}
}

Uint8 SNSA1::ReadIRAM(Uint16 uAddr) const
{
	return m_IRAM[uAddr & (SNSA1_IRAM_SIZE - 1)];
}

void SNSA1::WriteIRAM(Uint16 uAddr, Uint8 uData)
{
	// $2229/$222A write-protection is added with the bus-arbitration pass.
	m_IRAM[uAddr & (SNSA1_IRAM_SIZE - 1)] = uData;
}

Uint32 SNSA1::MirrorBWRAM(Uint32 uOffset) const
{
	if (!m_uBWRAMBytes)
		return 0;
	return uOffset % m_uBWRAMBytes;
}

Uint8 SNSA1::ReadBWRAMWindow(Uint16 uAddr) const
{
	Uint32 uPage, uOffset;
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return 0xFF;

	uPage = m_State.Registers[0x024] & 0x1F;
	uOffset = (uPage << 13) | (uAddr & 0x1FFF);
	return m_pBWRAM[MirrorBWRAM(uOffset)];
}

void SNSA1::WriteBWRAMWindow(Uint16 uAddr, Uint8 uData)
{
	Uint32 uPage, uOffset;
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return;

	uPage = m_State.Registers[0x024] & 0x1F;
	uOffset = (uPage << 13) | (uAddr & 0x1FFF);
	m_pBWRAM[MirrorBWRAM(uOffset)] = uData;
}

Uint8 SNSA1::ReadSA1BWRAMWindow(Uint16 uAddr) const
{
	Uint8 uMap = m_State.Registers[0x025];
	Uint32 uOffset;
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return 0xFF;

	// Bitmap windows ($2225 bit 7) are intentionally deferred to phase 3.
	if (uMap & 0x80)
		return 0xFF;

	uOffset = ((Uint32)(uMap & 0x1F) << 13) | (uAddr & 0x1FFF);
	return m_pBWRAM[MirrorBWRAM(uOffset)];
}

void SNSA1::WriteSA1BWRAMWindow(Uint16 uAddr, Uint8 uData)
{
	Uint8 uMap = m_State.Registers[0x025];
	Uint32 uOffset;
	if (!m_pBWRAM || !m_uBWRAMBytes || (uMap & 0x80))
		return;

	uOffset = ((Uint32)(uMap & 0x1F) << 13) | (uAddr & 0x1FFF);
	m_pBWRAM[MirrorBWRAM(uOffset)] = uData;
}

Uint8 SNSA1::ReadBWRAMDirect(Uint32 uAddr) const
{
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return 0xFF;
	return m_pBWRAM[MirrorBWRAM(uAddr & 0x3FFFF)];
}

void SNSA1::WriteBWRAMDirect(Uint32 uAddr, Uint8 uData)
{
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return;
	m_pBWRAM[MirrorBWRAM(uAddr & 0x3FFFF)] = uData;
}

Uint32 SNSA1::MirrorRomOffset(Uint32 uSize, Uint32 uPos)
{
	Uint32 uMask = 0x800000;

	if (!uSize)
		return 0;
	if (uPos < uSize)
		return uPos;

	while (uMask && !(uPos & uMask))
		uMask >>= 1;
	if (!uMask)
		return uPos % uSize;

	if (uSize <= (uPos & uMask))
		return MirrorRomOffset(uSize, uPos - uMask);
	return uMask + MirrorRomOffset(uSize - uMask, uPos - uMask);
}

void SNSA1::MapRomGroup(Uint32 uWhich, Uint8 uMap)
{
	Uint32 uBank, uPage;
	Uint32 uFullSegment;
	Uint32 uLoSegment;
	Uint8 uLoBank;

	if (!m_pRom || !m_uRomBytes || uWhich >= 4)
		return;

	uFullSegment = (Uint32)(uMap & 7) * 0x100000u;
	uLoSegment = (Uint32)((uMap & 0x80) ? (uMap & 7) : uWhich) * 0x100000u;
	uLoBank = _SA1LoBankBase[uWhich];

	// $C0-$FF: sixteen full banks per MMC register.
	for (uBank = 0; uBank < 16; uBank++)
	{
		Uint32 uAddrBase = (0xC0u + uWhich * 0x10u + uBank) << 16;
		for (uPage = 0; uPage < 0x10000u; uPage += SNCPU_BANK_SIZE)
		{
			Uint32 uOff = MirrorRomOffset(
				m_uRomBytes, uFullSegment + uBank * 0x10000u + uPage);
			SNCPUSetMemSpeed(&m_Cpu, uAddrBase + uPage,
			                  SNCPU_BANK_SIZE, SNCPU_CYCLE_FAST);
			SNCPUSetBank(&m_Cpu, uAddrBase + uPage, SNCPU_BANK_SIZE,
			             (Uint8 *)m_pRom + uOff, FALSE);
		}
	}

	// $00-$3F/$80-$BF:8000-FFFF, 32 LoROM banks per MMC register.
	for (uBank = 0; uBank < 32; uBank++)
	{
		Uint32 uAddrBase = ((Uint32)uLoBank + uBank) << 16;
		for (uPage = 0; uPage < 0x8000u; uPage += SNCPU_BANK_SIZE)
		{
			Uint32 uOff = MirrorRomOffset(
				m_uRomBytes, uLoSegment + uBank * 0x8000u + uPage);
			SNCPUSetMemSpeed(&m_Cpu, uAddrBase + 0x8000u + uPage,
			                  SNCPU_BANK_SIZE, SNCPU_CYCLE_FAST);
			SNCPUSetBank(&m_Cpu, uAddrBase + 0x8000u + uPage,
			             SNCPU_BANK_SIZE, (Uint8 *)m_pRom + uOff, FALSE);
		}
	}
}

void SNSA1::MapCpuMemory()
{
	Uint32 i;

	SNCPUSetTrap(&m_Cpu, 0, SNCPU_MEM_SIZE, CpuReadTrap, CpuWriteTrap);
	SNCPUSetMemSpeed(&m_Cpu, 0, SNCPU_MEM_SIZE, SNCPU_CYCLE_FAST);

	for (i = 0; i < 4; i++)
		MapRomGroup(i, m_State.Registers[0x020 + i]);

	SNCPUMirror24BitBus(&m_Cpu);
}

Uint8 SNSA1::ReadCpuBus(Uint32 uAddr) const
{
	Uint8 uBank = (Uint8)(uAddr >> 16);
	Uint16 uLow = (Uint16)uAddr;
	Bool bSystemBank = (uBank <= 0x3F || (uBank >= 0x80 && uBank <= 0xBF));

	if (bSystemBank)
	{
		if (uLow < 0x0800)
			return ReadIRAM(uLow);
		if (uLow >= 0x2200 && uLow <= 0x23FF)
			return ReadRegister(uLow);
		if (uLow >= 0x6000 && uLow <= 0x7FFF)
			return ReadSA1BWRAMWindow(uLow);
		return 0xFF;
	}

	if (uBank >= 0x40 && uBank <= 0x4F)
		return ReadBWRAMDirect(uAddr);

	return 0xFF;
}

void SNSA1::WriteCpuBus(Uint32 uAddr, Uint8 uData)
{
	Uint8 uBank = (Uint8)(uAddr >> 16);
	Uint16 uLow = (Uint16)uAddr;
	Bool bSystemBank = (uBank <= 0x3F || (uBank >= 0x80 && uBank <= 0xBF));

	if (bSystemBank)
	{
		if (uLow < 0x0800)
		{
			WriteIRAM(uLow, uData);
			return;
		}
		if (uLow >= 0x2200 && uLow <= 0x23FF)
		{
			WriteRegister(uLow, uData);
			return;
		}
		if (uLow >= 0x6000 && uLow <= 0x7FFF)
		{
			WriteSA1BWRAMWindow(uLow, uData);
			return;
		}
		return;
	}

	if (uBank >= 0x40 && uBank <= 0x4F)
		WriteBWRAMDirect(uAddr, uData);
}

Uint8 SNCPU_TRAPFUNC SNSA1::CpuReadTrap(SNCpuT *pCpu, Uint32 uAddr)
{
	SNSA1 *pSA1 = (SNSA1 *)pCpu->pUserData;
	return pSA1 ? pSA1->ReadCpuBus(uAddr) : 0xFF;
}

void SNCPU_TRAPFUNC SNSA1::CpuWriteTrap(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	SNSA1 *pSA1 = (SNSA1 *)pCpu->pUserData;
	if (pSA1)
		pSA1->WriteCpuBus(uAddr, uData);
}

Bool SNSA1::ServiceIRQ()
{
	Uint16 uVector;

	if (!(m_Cpu.uSignal & SNCPU_SIGNAL_IRQ))
		return FALSE;

	// Any asserted IRQ wakes WAI, even if I is still set.
	m_Cpu.uSignal &= (Uint8)~SNCPU_SIGNAL_WAI;
	if (m_Cpu.Regs.rP & SNCPU_FLAG_I)
		return FALSE;

	uVector = (Uint16)(m_State.Registers[0x007] |
	                   ((Uint16)m_State.Registers[0x008] << 8));

	if (m_Cpu.Regs.rE)
	{
		SNCPUPush8(&m_Cpu, (Uint8)(m_Cpu.Regs.rPC >> 8));
		SNCPUPush8(&m_Cpu, (Uint8)m_Cpu.Regs.rPC);
		SNCPUPush8(&m_Cpu, m_Cpu.Regs.rP & (Uint8)~SNCPU_FLAG_B);
	}
	else
	{
		SNCPUPush8(&m_Cpu, (Uint8)(m_Cpu.Regs.rPC >> 16));
		SNCPUPush8(&m_Cpu, (Uint8)(m_Cpu.Regs.rPC >> 8));
		SNCPUPush8(&m_Cpu, (Uint8)m_Cpu.Regs.rPC);
		SNCPUPush8(&m_Cpu, m_Cpu.Regs.rP);
	}

	m_Cpu.Regs.rPC = uVector;
	m_Cpu.Regs.rP &= (Uint8)~SNCPU_FLAG_D;
	m_Cpu.Regs.rP |= SNCPU_FLAG_I;
	SNCPUConsumeCycles(&m_Cpu,
		(m_Cpu.Regs.rE ? 7 : 8) * SNCPU_CYCLE_FAST);
	return TRUE;
}

void SNSA1::RunScheduled(Uint32 uSA1Cycles)
{
	Int32 nExecUnits;
	Int32 nGuard;

	if (!uSA1Cycles || !m_State.Running)
		return;

	if (m_Cpu.uSignal & SNCPU_SIGNAL_STP)
		return;

	nExecUnits = (Int32)(uSA1Cycles * SNCPU_CYCLE_FAST);
	SNCPUAddCycles(&m_Cpu, nExecUnits);

	for (nGuard = 0; nGuard < 4 && m_Cpu.Cycles > 0; nGuard++)
	{
		ServiceIRQ();

		if ((m_Cpu.uSignal & SNCPU_SIGNAL_STP) ||
		    ((m_Cpu.uSignal & SNCPU_SIGNAL_WAI) &&
		     !(m_Cpu.uSignal & SNCPU_SIGNAL_IRQ)))
		{
			m_Cpu.Cycles = 0;
			break;
		}

		if (SNCPUExecute(&m_Cpu))
			break;
	}

	m_State.ExecutionSlices++;
	m_State.ExecutedCycles += uSA1Cycles;
}

void SNSA1::StepMasterCycles(Int32 nMasterCycles)
{
	Uint32 uTotal, uTicks;

	m_State.LastSliceCycles = 0;
	if (nMasterCycles <= 0)
		return;

	m_State.MasterCycles += (Uint32)nMasterCycles;
	if (!m_State.Running)
		return;

	uTotal = (Uint32)nMasterCycles + m_State.MasterRemainder;
	uTicks = uTotal / SNSA1_MASTER_PER_TICK;
	m_State.MasterRemainder = (Uint8)(uTotal % SNSA1_MASTER_PER_TICK);
	m_State.LastSliceCycles = uTicks;
	m_State.ScheduledCycles += uTicks;

	RunScheduled(uTicks);
}
