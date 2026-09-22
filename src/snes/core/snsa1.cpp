/*
 * Experimental SA-1 support for SNESticle Revive.
 *
 * Phase 1: register file, reset state, I-RAM/BW-RAM access and deterministic
 * scheduler credits. DMA, arithmetic, bitmap conversion and 65C816 execution
 * are deliberately left for later isolated phases.
 */

#include <string.h>
#include "snsa1.h"

SNSA1::SNSA1()
{
	m_pRom = NULL;
	m_uRomBytes = 0;
	m_pBWRAM = NULL;
	m_uBWRAMBytes = 0;
	Reset(TRUE);
}

void SNSA1::SetMemory(const Uint8 *pRom, Uint32 uRomBytes,
                      Uint8 *pBWRAM, Uint32 uBWRAMBytes)
{
	m_pRom = pRom;
	m_uRomBytes = uRomBytes;
	m_pBWRAM = pBWRAM;
	m_uBWRAMBytes = uBWRAMBytes;
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
}

Uint16 SNSA1::GetResetVector() const
{
	return (Uint16)(m_State.Registers[0x003] |
	                ((Uint16)m_State.Registers[0x004] << 8));
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
		{
			m_State.LastResetVector = GetResetVector();
			m_State.ResetEpoch++;
			m_State.MasterRemainder = 0;
		}
		if (uData & 0x80)
			m_State.Registers[0x101] |= 0x80;
		m_State.Running = ((uData & 0x60) == 0) ? TRUE : FALSE;
		if (!m_State.Running)
			m_State.MasterRemainder = 0;
		break;

	case 0x2202:
		if (uData & 0x80) m_State.Registers[0x100] &= (Uint8)~0x80;
		if (uData & 0x20) m_State.Registers[0x100] &= (Uint8)~0x20;
		break;

	case 0x2209:
		if (uData & 0x80)
			m_State.Registers[0x100] |= 0x80;
		break;

	case 0x220B:
		if (uData & 0x80) m_State.Registers[0x101] &= (Uint8)~0x80;
		if (uData & 0x40) m_State.Registers[0x101] &= (Uint8)~0x40;
		if (uData & 0x20) m_State.Registers[0x101] &= (Uint8)~0x20;
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
	// $2229/$222A write-protection is added with the full bus-arbitration pass.
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
}
