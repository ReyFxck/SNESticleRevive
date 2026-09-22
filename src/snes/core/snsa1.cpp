/*
 * Experimental SA-1 support for SNESticle Revive.
 *
 * Phase 3 adds the first SA-1 hardware engines around the independent 65C816:
 * NMI/timer interrupt paths, RAM write protection, arithmetic, variable-length
 * bit processing and virtual bitmap BW-RAM. Normal/character DMA comes next.
 */

#include <string.h>
#include "snsa1.h"
#include "sntiming.h"

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
	m_State.HCounter = 0;
	m_State.VCounter = 0;
	m_State.ArithmeticResult = 0;
	m_State.ArithmeticOverflow = FALSE;
	m_State.VariableData = 0;
	m_State.VariableBitPos = 0;
	m_State.TimerMatch = FALSE;
	m_State.NMIPending = FALSE;
	m_State.CharConvLine = 0;
	m_State.CC1Active = FALSE;
	m_State.Running = FALSE;

	ResetCPUContext();
	MapCpuMemory();
}

Uint16 SNSA1::GetResetVector() const
{
	return (Uint16)(m_State.Registers[0x003] |
	                ((Uint16)m_State.Registers[0x004] << 8));
}

Bool SNSA1::SCPUIRQPending() const
{
	return ((m_State.Registers[0x100] &
	         m_State.Registers[0x001] & 0xA0) != 0) ? TRUE : FALSE;
}

Uint16 SNSA1::GetSCPUNMIVector() const
{
	return (Uint16)(m_State.Registers[0x00C] |
	                ((Uint16)m_State.Registers[0x00D] << 8));
}

Uint16 SNSA1::GetSCPUIRQVector() const
{
	return (Uint16)(m_State.Registers[0x00E] |
	                ((Uint16)m_State.Registers[0x00F] << 8));
}

void SNSA1::ReleaseCPUReset()
{
	ResetCPUContext();
	m_State.LastResetVector = GetResetVector();
	m_State.ResetEpoch++;
	m_State.MasterRemainder = 0;
	m_Cpu.Regs.rPC = m_State.LastResetVector;
}

Uint8 SNSA1::ReadRegister(Uint16 uAddr)
{
	Uint8 uValue;

	if (uAddr < SNSA1_REGISTER_BASE || uAddr > SNSA1_REGISTER_LAST)
		return 0xFF;

	if (uAddr == 0x2300)
		return (Uint8)((m_State.Registers[0x009] & 0x5F) |
		               (m_State.Registers[0x100] & 0xA0));
	if (uAddr == 0x2301)
		return (Uint8)((m_State.Registers[0x000] & 0x0F) |
		               (m_State.Registers[0x101] & 0xF0));
	if (uAddr == 0x2302 || uAddr == 0x2303)
	{
		Uint16 uH = (Uint16)(m_State.HCounter >> 2);
		return (uAddr == 0x2302) ? (Uint8)uH : (Uint8)(uH >> 8);
	}
	if (uAddr == 0x2304 || uAddr == 0x2305)
		return (uAddr == 0x2304) ? (Uint8)m_State.VCounter :
		                            (Uint8)(m_State.VCounter >> 8);
	if (uAddr >= 0x2306 && uAddr <= 0x230A)
		return (Uint8)(m_State.ArithmeticResult >> ((uAddr - 0x2306) * 8));
	if (uAddr == 0x230B)
		return m_State.ArithmeticOverflow ? 0x80 : 0x00;
	if (uAddr == 0x230C)
		return (Uint8)m_State.VariableData;
	if (uAddr == 0x230D)
	{
		uValue = (Uint8)(m_State.VariableData >> 8);
		if (m_State.Registers[0x058] & 0x80)
			UpdateVariableData(TRUE, FALSE);
		return uValue;
	}
	if (uAddr == 0x230E)
		return SNSA1_VERSION_CODE;

	return m_State.Registers[uAddr - SNSA1_REGISTER_BASE];
}

void SNSA1::UpdateIRQLine()
{
	Bool bPending = ((m_State.Registers[0x101] &
	                  m_State.Registers[0x00A] & 0xE0) != 0) ? TRUE : FALSE;
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
		if (uData & 0x10)
		{
			m_State.Registers[0x101] |= 0x10;
			m_State.NMIPending = TRUE;
		}

		m_State.Running = ((uData & 0x60) == 0) ? TRUE : FALSE;
		if (!m_State.Running)
			m_State.MasterRemainder = 0;
		UpdateIRQLine();
		break;

	case 0x2201:
		// S-CPU IRQ enables are consumed by the SnesSystem bridge.
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
		if (uData & 0x10)
		{
			m_State.Registers[0x101] &= (Uint8)~0x10;
			m_State.NMIPending = FALSE;
		}
		UpdateIRQLine();
		break;

	case 0x2210:
		m_State.TimerMatch = FALSE;
		break;

	case 0x2211:
		m_State.HCounter = 0;
		m_State.VCounter = 0;
		m_State.TimerMatch = FALSE;
		break;

	case 0x2230:
		if (!(uData & 0x80))
			m_State.CharConvLine = 0;
		break;

	case 0x2231:
		if (uData & 0x80)
			m_State.CC1Active = FALSE;
		break;

	case 0x2236:
		if ((m_State.Registers[0x030] & 0xA4) == 0x80)
			ExecuteDMA();
		else if ((m_State.Registers[0x030] & 0xB0) == 0xB0)
			StartCC1();
		break;

	case 0x2237:
		if ((m_State.Registers[0x030] & 0xA4) == 0x84)
			ExecuteDMA();
		break;

	case 0x2247:
	case 0x224F:
		if ((m_State.Registers[0x030] & 0xB0) == 0xA0)
			ExecuteCC2();
		break;

	case 0x2250:
		if (uData & 0x02)
		{
			m_State.ArithmeticResult = 0;
			m_State.ArithmeticOverflow = FALSE;
		}
		break;

	case 0x2251:
		m_State.ArithmeticOp1 = (Uint16)((m_State.ArithmeticOp1 & 0xFF00) | uData);
		break;
	case 0x2252:
		m_State.ArithmeticOp1 = (Uint16)((m_State.ArithmeticOp1 & 0x00FF) |
		                                    ((Uint16)uData << 8));
		break;
	case 0x2253:
		m_State.ArithmeticOp2 = (Uint16)((m_State.ArithmeticOp2 & 0xFF00) | uData);
		break;
	case 0x2254:
		m_State.ArithmeticOp2 = (Uint16)((m_State.ArithmeticOp2 & 0x00FF) |
		                                    ((Uint16)uData << 8));
		ExecuteArithmetic();
		break;

	case 0x2258:
		UpdateVariableData(TRUE, FALSE);
		break;
	case 0x2259:
	case 0x225A:
	case 0x225B:
		m_State.VariableBitPos = 0;
		UpdateVariableData(FALSE, TRUE);
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

Bool SNSA1::CanWriteIRAM(Uint16 uAddr, Bool bSA1Side) const
{
	Uint8 uProtect = m_State.Registers[bSA1Side ? 0x02A : 0x029];
	Uint8 uPage = (Uint8)((uAddr & (SNSA1_IRAM_SIZE - 1)) >> 8);
	return (uProtect & (1u << uPage)) ? FALSE : TRUE;
}

void SNSA1::WriteIRAM(Uint16 uAddr, Uint8 uData)
{
	if (CanWriteIRAM(uAddr, FALSE))
		m_IRAM[uAddr & (SNSA1_IRAM_SIZE - 1)] = uData;
}

void SNSA1::WriteIRAMSA1(Uint16 uAddr, Uint8 uData)
{
	if (CanWriteIRAM(uAddr, TRUE))
		m_IRAM[uAddr & (SNSA1_IRAM_SIZE - 1)] = uData;
}

Uint32 SNSA1::MirrorBWRAM(Uint32 uOffset) const
{
	if (!m_uBWRAMBytes)
		return 0;
	return uOffset % m_uBWRAMBytes;
}

Bool SNSA1::CanWriteBWRAM(Uint32 uOffset, Bool bSA1Side) const
{
	Uint8 uEnable = m_State.Registers[bSA1Side ? 0x027 : 0x026];
	Uint32 uProtected = 0x100u << (m_State.Registers[0x028] & 0x0F);

	if (!(uEnable & 0x80))
		return FALSE;

	// BWPA compares against the logical 256 KiB BW-RAM address before
	// mirroring to the physically installed RAM.
	uOffset &= 0x3FFFFu;
	return (uOffset < uProtected) ? FALSE : TRUE;
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
	if (CanWriteBWRAM(uOffset, FALSE))
		m_pBWRAM[MirrorBWRAM(uOffset)] = uData;
}

Uint8 SNSA1::ReadSA1BWRAMWindow(Uint16 uAddr) const
{
	Uint8 uMap = m_State.Registers[0x025];
	Uint32 uOffset;
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return 0xFF;

	if (uMap & 0x80)
	{
		uOffset = ((Uint32)(uMap & 0x7F) << 13) | (uAddr & 0x1FFF);
		return ReadBitmap(uOffset);
	}

	uOffset = ((Uint32)(uMap & 0x1F) << 13) | (uAddr & 0x1FFF);
	return m_pBWRAM[MirrorBWRAM(uOffset)];
}

void SNSA1::WriteSA1BWRAMWindow(Uint16 uAddr, Uint8 uData)
{
	Uint8 uMap = m_State.Registers[0x025];
	Uint32 uOffset;
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return;

	if (uMap & 0x80)
	{
		uOffset = ((Uint32)(uMap & 0x7F) << 13) | (uAddr & 0x1FFF);
		WriteBitmap(uOffset, uData);
		return;
	}

	uOffset = ((Uint32)(uMap & 0x1F) << 13) | (uAddr & 0x1FFF);
	if (CanWriteBWRAM(uOffset, TRUE))
		m_pBWRAM[MirrorBWRAM(uOffset)] = uData;
}

Uint8 SNSA1::ReadBitmap(Uint32 uVirtualAddr) const
{
	Uint32 uPhysical;
	Uint8 uPacked;

	if (!m_pBWRAM || !m_uBWRAMBytes)
		return 0xFF;

	if (m_State.Registers[0x03F] & 0x80)
	{
		uPhysical = MirrorBWRAM(uVirtualAddr >> 2);
		uPacked = m_pBWRAM[uPhysical];
		return (Uint8)((uPacked >> ((uVirtualAddr & 3) * 2)) & 0x03);
	}

	uPhysical = MirrorBWRAM(uVirtualAddr >> 1);
	uPacked = m_pBWRAM[uPhysical];
	return (Uint8)((uPacked >> ((uVirtualAddr & 1) * 4)) & 0x0F);
}

void SNSA1::WriteBitmap(Uint32 uVirtualAddr, Uint8 uData)
{
	Uint32 uLogical;
	Uint32 uPhysical;
	Uint8 uShift, uMask, uPacked;

	if (!m_pBWRAM || !m_uBWRAMBytes)
		return;

	if (m_State.Registers[0x03F] & 0x80)
	{
		uLogical = (uVirtualAddr >> 2) & 0x3FFFF;
		if (!CanWriteBWRAM(uLogical, TRUE))
			return;
		uPhysical = MirrorBWRAM(uLogical);
		uShift = (Uint8)((uVirtualAddr & 3) * 2);
		uMask = (Uint8)(0x03u << uShift);
		uPacked = m_pBWRAM[uPhysical];
		m_pBWRAM[uPhysical] = (Uint8)((uPacked & ~uMask) |
		                              ((uData & 0x03) << uShift));
		return;
	}

	uLogical = (uVirtualAddr >> 1) & 0x3FFFF;
	if (!CanWriteBWRAM(uLogical, TRUE))
		return;
	uPhysical = MirrorBWRAM(uLogical);
	uShift = (Uint8)((uVirtualAddr & 1) * 4);
	uMask = (Uint8)(0x0Fu << uShift);
	uPacked = m_pBWRAM[uPhysical];
	m_pBWRAM[uPhysical] = (Uint8)((uPacked & ~uMask) |
	                              ((uData & 0x0F) << uShift));
}

Uint8 SNSA1::ReadBWRAMDirect(Uint32 uAddr) const
{
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return 0xFF;
	return m_pBWRAM[MirrorBWRAM(uAddr & 0x3FFFF)];
}

void SNSA1::WriteBWRAMDirect(Uint32 uAddr, Uint8 uData)
{
	Uint32 uOffset = uAddr & 0x3FFFF;
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return;
	if (CanWriteBWRAM(uOffset, FALSE))
		m_pBWRAM[MirrorBWRAM(uOffset)] = uData;
}

void SNSA1::WriteBWRAMDirectSA1(Uint32 uAddr, Uint8 uData)
{
	Uint32 uOffset = uAddr & 0x3FFFF;
	if (!m_pBWRAM || !m_uBWRAMBytes)
		return;
	if (CanWriteBWRAM(uOffset, TRUE))
		m_pBWRAM[MirrorBWRAM(uOffset)] = uData;
}

void SNSA1::StartCC1()
{
	if (!(m_State.Registers[0x030] & 0x80) ||
	    !(m_State.Registers[0x030] & 0x20) ||
	    !(m_State.Registers[0x030] & 0x10))
		return;

	m_State.CC1Active = TRUE;
	m_State.Registers[0x100] |= 0x20;
}

void SNSA1::ConvertCC1Tile(Uint32 uAddr)
{
	Uint8 uFormat = m_State.Registers[0x031] & 0x03;
	Uint8 uWidth = (m_State.Registers[0x031] >> 2) & 0x07;
	Uint32 uBpp;
	Uint32 uTilesPerLine;
	Uint32 uBytesPerLine;
	Uint32 uSourceBase;
	Uint32 uDestBase;
	Uint32 uOffset;
	Uint32 uTileNumber;
	Uint32 uTileX;
	Uint32 uTileY;
	Uint32 uSrc;
	Uint32 y;

	if (!m_pBWRAM || !m_uBWRAMBytes)
		return;
	if (uFormat > 2)
		uFormat = 2;
	if (uWidth > 5)
		uWidth = 5;

	uBpp = 8u >> uFormat;
	uTilesPerLine = 1u << uWidth;
	uBytesPerLine = (uTilesPerLine * 8u) >> uFormat;
	uSourceBase = ((Uint32)m_State.Registers[0x032] |
	              ((Uint32)m_State.Registers[0x033] << 8) |
	              ((Uint32)m_State.Registers[0x034] << 16)) & 0x3FFFFu;
	uDestBase = ((Uint32)m_State.Registers[0x035] |
	            ((Uint32)m_State.Registers[0x036] << 8) |
	            ((Uint32)m_State.Registers[0x037] << 16)) & 0x07FFu;
	uOffset = uAddr & 0x3FFFFu;
	uTileNumber = ((uOffset - uSourceBase) & 0x3FFFFu) >> (6 - uFormat);
	uTileX = uTileNumber & (uTilesPerLine - 1);
	uTileY = uTileNumber >> uWidth;
	uSrc = uSourceBase + uTileY * 8u * uBytesPerLine + uTileX * uBpp;

	for (y = 0; y < 8; y++)
	{
		Uint64 uPixels = 0;
		Uint8 uPlane[8] = {0,0,0,0,0,0,0,0};
		Uint32 p, x;

		for (p = 0; p < uBpp; p++)
			uPixels |= (Uint64)m_pBWRAM[MirrorBWRAM(uSrc + p)] << (p * 8);
		uSrc += uBytesPerLine;

		for (x = 0; x < 8; x++)
		{
			for (p = 0; p < uBpp; p++)
			{
				uPlane[p] |= (Uint8)((uPixels & 1) << (7 - x));
				uPixels >>= 1;
			}
		}

		for (p = 0; p < uBpp; p++)
		{
			Uint32 uPlanar = (y << 1) + ((p >> 1) << 4) + (p & 1);
			m_IRAM[(uDestBase + uPlanar) & (SNSA1_IRAM_SIZE - 1)] = uPlane[p];
		}
	}
}

Uint8 SNSA1::ReadCC1Byte(Uint32 uAddr)
{
	Uint8 uFormat = m_State.Registers[0x031] & 0x03;
	Uint32 uMask;
	Uint32 uOffset;
	Uint32 uDest;

	if (!m_State.CC1Active)
		return ReadBWRAMDirect(uAddr);
	if (uFormat > 2)
		uFormat = 2;

	uMask = (1u << (6 - uFormat)) - 1u;
	uOffset = uAddr & 0x3FFFFu;
	if ((uOffset & uMask) == 0)
		ConvertCC1Tile(uOffset);

	uDest = ((Uint32)m_State.Registers[0x035] |
	        ((Uint32)m_State.Registers[0x036] << 8) |
	        ((Uint32)m_State.Registers[0x037] << 16)) & 0x07FFu;
	return m_IRAM[(uDest + (uOffset & uMask)) & (SNSA1_IRAM_SIZE - 1)];
}

void SNSA1::ExecuteCC2()
{
	Uint8 uFormat = m_State.Registers[0x031] & 0x03;
	Uint32 uBpp;
	Uint32 uDest;
	Uint32 uRegBase;
	Uint32 p, x;

	if (uFormat > 2)
		uFormat = 2;
	uBpp = 8u >> uFormat;
	uDest = ((Uint32)m_State.Registers[0x035] |
	        ((Uint32)m_State.Registers[0x036] << 8) |
	        ((Uint32)m_State.Registers[0x037] << 16)) & 0x07FFu;
	uDest &= ~((uBpp << 4) - 1u);
	uDest += (m_State.CharConvLine & 7u) * 2u;
	uDest += (m_State.CharConvLine & 8u) * uBpp;
	uRegBase = (m_State.CharConvLine & 1u) ? 0x048u : 0x040u;

	for (p = 0; p < uBpp; p++)
	{
		Uint8 uOut = 0;
		Uint32 uPlanar = ((p >> 1) << 4) + (p & 1);
		for (x = 0; x < 8; x++)
			uOut |= (Uint8)(((m_State.Registers[uRegBase + x] >> p) & 1u) << (7 - x));
		m_IRAM[(uDest + uPlanar) & (SNSA1_IRAM_SIZE - 1)] = uOut;
	}

	m_State.CharConvLine = (Uint8)((m_State.CharConvLine + 1) & 15);
}

void SNSA1::ExecuteDMA()
{
	Uint32 uSrc = (Uint32)m_State.Registers[0x032] |
	              ((Uint32)m_State.Registers[0x033] << 8) |
	              ((Uint32)m_State.Registers[0x034] << 16);
	Uint32 uDst = (Uint32)m_State.Registers[0x035] |
	              ((Uint32)m_State.Registers[0x036] << 8) |
	              ((Uint32)m_State.Registers[0x037] << 16);
	Uint32 uLen = (Uint32)m_State.Registers[0x038] |
	              ((Uint32)m_State.Registers[0x039] << 8);
	Uint8 uSource = m_State.Registers[0x030] & 0x03;
	Bool bDestBWRAM = (m_State.Registers[0x030] & 0x04) ? TRUE : FALSE;
	Uint32 i;

	if (!(m_State.Registers[0x030] & 0x80) ||
	    (m_State.Registers[0x030] & 0x20))
		return;

	for (i = 0; i < uLen; i++)
	{
		Uint8 uData;
		switch (uSource)
		{
		default:
		case 0:
			uData = SNCPURead8(&m_Cpu, (uSrc + i) & 0xFFFFFF);
			break;
		case 1:
			uData = (!m_pBWRAM || !m_uBWRAMBytes) ? 0xFF :
			        m_pBWRAM[MirrorBWRAM(uSrc + i)];
			break;
		case 2:
			uData = m_IRAM[(uSrc + i) & (SNSA1_IRAM_SIZE - 1)];
			break;
		}

		if (bDestBWRAM)
		{
			if (m_pBWRAM && m_uBWRAMBytes)
				m_pBWRAM[MirrorBWRAM(uDst + i)] = uData;
		}
		else
		{
			m_IRAM[(uDst + i) & (SNSA1_IRAM_SIZE - 1)] = uData;
		}
	}

	m_State.Registers[0x101] |= 0x20;
	UpdateIRQLine();
}

void SNSA1::ExecuteArithmetic()
{
	Uint8 uMode = m_State.Registers[0x050] & 0x03;
	Int32 nA = (Int16)m_State.ArithmeticOp1;
	Int32 nB = (Int16)m_State.ArithmeticOp2;
	const Uint64 uMask40 = (((Uint64)1 << 40) - 1);

	if (uMode & 0x02)
	{
		Int64 nOld = (Int64)(m_State.ArithmeticResult & uMask40);
		Int64 nSum;
		if (nOld & ((Int64)1 << 39))
			nOld |= ~((Int64)uMask40);
		nSum = nOld + (Int64)nA * (Int64)nB;
		m_State.ArithmeticOverflow =
			(nSum < -((Int64)1 << 39) || nSum > (((Int64)1 << 39) - 1)) ?
			TRUE : FALSE;
		m_State.ArithmeticResult = ((Uint64)nSum) & uMask40;
		return;
	}

	if (uMode & 0x01)
	{
		Uint16 uDivisor = m_State.ArithmeticOp2;
		Int32 nDividend = (Int16)m_State.ArithmeticOp1;
		Uint16 uQuotient;
		Uint16 uRemainder;

		if (!uDivisor)
		{
			uQuotient = (nDividend < 0) ? 0x0001 : 0xFFFF;
			uRemainder = (Uint16)(nDividend < 0 ? -nDividend : nDividend);
		}
		else
		{
			Int32 nQ = nDividend / (Int32)uDivisor;
			Int32 nAbs = nDividend < 0 ? -nDividend : nDividend;
			uQuotient = (Uint16)nQ;
			uRemainder = (Uint16)(nAbs % uDivisor);
		}
		m_State.ArithmeticResult = (Uint64)uQuotient |
		                           ((Uint64)uRemainder << 16);
		m_State.ArithmeticOverflow = FALSE;
		return;
	}

	m_State.ArithmeticResult = ((Uint64)((Int64)nA * (Int64)nB)) & uMask40;
	m_State.ArithmeticOverflow = FALSE;
}

void SNSA1::UpdateVariableData(Bool bIncrement, Bool bNoShift)
{
	Uint32 uAddr = (Uint32)m_State.Registers[0x059] |
	               ((Uint32)m_State.Registers[0x05A] << 8) |
	               ((Uint32)m_State.Registers[0x05B] << 16);
	Uint8 uShift = m_State.Registers[0x058] & 0x0F;
	Uint8 uBit;
	Uint32 uData;

	if (bNoShift)
		uShift = 0;
	else if (!uShift)
		uShift = 16;

	uBit = (Uint8)(uShift + m_State.VariableBitPos);
	if (uBit >= 16)
	{
		uAddr = (uAddr + ((Uint32)(uBit >> 4) << 1)) & 0xFFFFFF;
		uBit &= 15;
	}

	uData = (Uint32)SNCPURead8(&m_Cpu, uAddr) |
	        ((Uint32)SNCPURead8(&m_Cpu, (uAddr + 1) & 0xFFFFFF) << 8) |
	        ((Uint32)SNCPURead8(&m_Cpu, (uAddr + 2) & 0xFFFFFF) << 16) |
	        ((Uint32)SNCPURead8(&m_Cpu, (uAddr + 3) & 0xFFFFFF) << 24);
	m_State.VariableData = (Uint16)(uData >> uBit);

	if (bIncrement)
	{
		m_State.VariableBitPos = (Uint8)((m_State.VariableBitPos + uShift) & 15);
		m_State.Registers[0x059] = (Uint8)uAddr;
		m_State.Registers[0x05A] = (Uint8)(uAddr >> 8);
		m_State.Registers[0x05B] = (Uint8)(uAddr >> 16);
	}
}

void SNSA1::UpdateTimer(Uint32 uMasterCycles)
{
	Uint32 uLineClocks = (m_State.Registers[0x010] & 0x80) ?
	                     0x800u : (Uint32)SNES_CYCLESPERLINE;
	Uint32 uOldH = m_State.HCounter;
	Uint16 uOldV = m_State.VCounter;
	Uint16 uHCompare = (Uint16)(m_State.Registers[0x012] |
	                           ((Uint16)(m_State.Registers[0x013] & 1) << 8));
	Uint16 uVCompare = (Uint16)(m_State.Registers[0x014] |
	                           ((Uint16)(m_State.Registers[0x015] & 1) << 8));
	Uint32 uCompareMaster = (Uint32)uHCompare * 4u;
	Bool bMatch = TRUE;

	m_State.HCounter += uMasterCycles;
	while (m_State.HCounter >= uLineClocks)
	{
		m_State.HCounter -= uLineClocks;
		m_State.VCounter++;
		if (m_State.VCounter >= ((m_State.Registers[0x010] & 0x80) ?
		                        0x200u : (Uint32)(SNES_CYCLESPERFRAME / SNES_CYCLESPERLINE)))
			m_State.VCounter = 0;
	}

	if (!(m_State.Registers[0x010] & 0x03))
		bMatch = FALSE;
	if (m_State.Registers[0x010] & 0x01)
	{
		Bool bCrossed = FALSE;
		if (uMasterCycles >= uLineClocks)
			bCrossed = (uCompareMaster < uLineClocks);
		else if (m_State.HCounter >= uOldH)
			bCrossed = (uCompareMaster >= uOldH && uCompareMaster < m_State.HCounter);
		else
			bCrossed = (uCompareMaster >= uOldH || uCompareMaster < m_State.HCounter);
		if (!bCrossed)
			bMatch = FALSE;
	}
	if (m_State.Registers[0x010] & 0x02)
	{
		Bool bVSeen = (uOldV == uVCompare || m_State.VCounter == uVCompare);
		if (!bVSeen)
			bMatch = FALSE;
	}

	if (bMatch && !m_State.TimerMatch)
	{
		m_State.Registers[0x101] |= 0x40;
		UpdateIRQLine();
	}
	m_State.TimerMatch = bMatch;
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

Uint8 SNSA1::ReadCpuBus(Uint32 uAddr)
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

	if (uBank >= 0x40 && uBank <= 0x5F)
		return ReadBWRAMDirect(uAddr);
	if (uBank >= 0x60 && uBank <= 0x6F)
		return ReadBitmap(uAddr & 0x0FFFFF);

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
			WriteIRAMSA1(uLow, uData);
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

	if (uBank >= 0x40 && uBank <= 0x5F)
		WriteBWRAMDirectSA1(uAddr, uData);
	else if (uBank >= 0x60 && uBank <= 0x6F)
		WriteBitmap(uAddr & 0x0FFFFF, uData);
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

Bool SNSA1::ServiceNMI()
{
	Uint16 uVector;

	if (!m_State.NMIPending || !(m_State.Registers[0x00A] & 0x10))
		return FALSE;

	m_State.NMIPending = FALSE;
	m_Cpu.uSignal &= (Uint8)~SNCPU_SIGNAL_WAI;
	uVector = (Uint16)(m_State.Registers[0x005] |
	                   ((Uint16)m_State.Registers[0x006] << 8));

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
		ServiceNMI();
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
	UpdateTimer((Uint32)nMasterCycles);
	if (!m_State.Running)
		return;

	uTotal = (Uint32)nMasterCycles + m_State.MasterRemainder;
	uTicks = uTotal / SNSA1_MASTER_PER_TICK;
	m_State.MasterRemainder = (Uint8)(uTotal % SNSA1_MASTER_PER_TICK);
	m_State.LastSliceCycles = uTicks;
	m_State.ScheduledCycles += uTicks;

	RunScheduled(uTicks);
}
