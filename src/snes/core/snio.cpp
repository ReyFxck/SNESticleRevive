/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snio behavior for the SNES emulation core.
 */

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "snio.h"

#define SNIO_VERSION_5A22 (0x02)

static Uint8 _SnesMouseMotion(Int8 iDelta)
{
	Int32 iValue = (Int32)iDelta;
	if (iValue < 0)
	{
		iValue = -iValue;
		if (iValue > 127) iValue = 127;
		return (Uint8)(0x80 | iValue);
	}
	if (iValue > 127) iValue = 127;
	return (Uint8)iValue;
}

Bool SnesIO::IsMouseMode() const
{
	return m_Input.uPad[4] == EMUSYS_SNES_SPECIAL_MOUSE ? TRUE : FALSE;
}

Bool SnesIO::IsSuperScopeMode() const
{
	return m_Input.uPad[4] == EMUSYS_SNES_SPECIAL_SUPERSCOPE ? TRUE : FALSE;
}

Bool SnesIO::IsJustifierMode() const
{
	return (m_Input.uPad[4] == EMUSYS_SNES_SPECIAL_JUSTIFIER ||
	        m_Input.uPad[4] == EMUSYS_SNES_SPECIAL_JUSTIFIERS) ? TRUE : FALSE;
}

Bool SnesIO::IsDualJustifierMode() const
{
	return m_Input.uPad[4] == EMUSYS_SNES_SPECIAL_JUSTIFIERS ? TRUE : FALSE;
}

void SnesIO::LatchMouseSerial()
{
	Int8 iDX = (Int8)(m_Input.uPad[2] & 0xFF);
	Int8 iDY = (Int8)((m_Input.uPad[2] >> 8) & 0xFF);
	Uint16 uButtons = m_Input.uPad[3];
	Uint8 uStatus = (Uint8)(0x01 | ((m_uMouseSensitivity & 3) << 4));

	if (uButtons & EMUSYS_SNES_MOUSE_LEFT)  uStatus |= 0x40;
	if (uButtons & EMUSYS_SNES_MOUSE_RIGHT) uStatus |= 0x80;

	m_uMouseSerial =
		((Uint32)uStatus << 16) |
		((Uint32)_SnesMouseMotion(iDY) << 8) |
		(Uint32)_SnesMouseMotion(iDX);
}

void SnesIO::LatchScopeSerial()
{
	Uint16 uButtons = m_Input.uPad[3];
	Uint16 uReport = 0x00FF;

	if (uButtons & EMUSYS_SNES_SCOPE_FIRE)   uReport |= 0x8000;
	if (uButtons & EMUSYS_SNES_SCOPE_CURSOR) uReport |= 0x4000;
	if (uButtons & EMUSYS_SNES_SCOPE_TURBO)  uReport |= 0x2000;
	if (uButtons & EMUSYS_SNES_SCOPE_PAUSE)  uReport |= 0x1000;

	/* Noise/null remain clear: the emulated pointer always represents an
	   on-screen receiver hit. */
	m_uScopeSerial = uReport;
}

void SnesIO::LatchJustifierSerial()
{
	Uint16 uButtons = m_Input.uPad[1] & 0x000F;
	Uint32 uReport = 0x000E5500u;

	if (uButtons & EMUSYS_SNES_JUSTIFIER1_TRIGGER) uReport |= 0x00000080u;
	if (IsDualJustifierMode() &&
	    (uButtons & EMUSYS_SNES_JUSTIFIER2_TRIGGER)) uReport |= 0x00000040u;
	if (uButtons & EMUSYS_SNES_JUSTIFIER1_START) uReport |= 0x00000020u;
	if (IsDualJustifierMode() &&
	    (uButtons & EMUSYS_SNES_JUSTIFIER2_START)) uReport |= 0x00000010u;
	if (m_uJustifierActive & 1) uReport |= 0x00000008u;

	m_uJustifierSerial = uReport;
}

Uint8 SnesIO::ReadMouseSerial()
{
	Uint8 uData = (Uint8)((m_uMouseSerial >> 31) & 1);

	if (m_Regs.joydata & 1)
	{
		/* The original mouse cycles sensitivity when clocked while OUT0 is
		   high. Rebuild the report immediately so the next normal read sees
		   the new sensitivity bits. */
		m_uMouseSensitivity = (Uint8)((m_uMouseSensitivity + 1) % 3);
		LatchMouseSerial();
	}
	else
	{
		/* Official mouse reports ones forever after its 32 data bits. */
		m_uMouseSerial = (m_uMouseSerial << 1) | 1u;
	}
	return uData;
}

Uint8 SnesIO::ReadScopeSerial()
{
	Uint8 uData = (Uint8)((m_uScopeSerial >> 15) & 1);
	if (!(m_Regs.joydata & 1))
		m_uScopeSerial = (Uint16)((m_uScopeSerial << 1) | 1u);
	return uData;
}

Uint8 SnesIO::ReadJustifierSerial()
{
	Uint8 uData = (Uint8)((m_uJustifierSerial >> 31) & 1);
	if (!(m_Regs.joydata & 1))
		m_uJustifierSerial = (m_uJustifierSerial << 1) | 1u;
	return uData;
}

Bool SnesIO::GetSuperScopePosition(Uint16 *pX, Uint16 *pY) const
{
	if (!IsSuperScopeMode())
		return FALSE;

	if (pX) *pX = (Uint16)(m_Input.uPad[2] & 0xFF);
	if (pY) *pY = (Uint16)((m_Input.uPad[2] >> 8) & 0xFF);
	return TRUE;
}

Bool SnesIO::GetJustifierPosition(Uint16 *pX, Uint16 *pY) const
{
	Uint16 uPacked;

	if (!IsJustifierMode())
		return FALSE;

	/* A single Justifier still toggles the active selector. When selector 1
	   is active there is no chained second gun, so no optical hit exists. */
	if ((m_uJustifierActive & 1) && !IsDualJustifierMode())
		return FALSE;

	uPacked = (m_uJustifierActive & 1) ? m_Input.uPad[3] : m_Input.uPad[2];
	if (pX) *pX = (Uint16)(uPacked & 0xFF);
	if (pY) *pY = (Uint16)((uPacked >> 8) & 0xFF);
	return TRUE;
}

Bool SnesIO::GetLightGunPosition(Uint16 *pX, Uint16 *pY) const
{
	if (GetSuperScopePosition(pX, pY))
		return TRUE;
	return GetJustifierPosition(pX, pY);
}


Uint8 SnesIO::ReadSerialPad(Uint32 uPad)
{
	// read top-most joypad bit
	return (m_Regs.joyserial[uPad] >> 15) & 1;
}

void SnesIO::ShiftSerialPad(Uint32 uPad)
{
	// shift pad data
	m_Regs.joyserial[uPad] <<= 1;

	// if joystick connected
	if (m_Input.uPad[uPad]!=EMUSYS_DEVICE_DISCONNECTED)
	{
		// set connected status
		m_Regs.joyserial[uPad] |= 1;
	}
}

Uint8 SnesIO::ReadSerial0()
{
	Uint32 uData;

	if (IsMouseMode())
		return ReadMouseSerial();

	uData  = ReadSerialPad(0) << 0;

	// confirmed:
	// if strobe is left on, then bitposition never shifts
	// all bits returned are button B
	if (!(m_Regs.joydata&1))
	{
		ShiftSerialPad(0);
	}

	return uData;
}

Uint8 SnesIO::ReadSerial1()
{
	Uint32 uData;

	if (IsJustifierMode())
		return (Uint8)(ReadJustifierSerial() | 0x1C);
	if (IsSuperScopeMode())
		return (Uint8)(ReadScopeSerial() | 0x1C);

	// Special peripherals and multitap cannot coexist on real hardware.
	// Mouse mode therefore keeps port 2 as a direct standard controller even
	// though uPad[2]/uPad[3] carry the mouse payload.
	if (IsMouseMode())
	{
		uData = ReadSerialPad(1);
		if (!(m_Regs.joydata & 1))
			ShiftSerialPad(1);
		return (Uint8)(uData | 0x1C);
	}

	// if joypads 2,3,4 are all disconnected then assume no multitap is installed
	if (
		m_Input.uPad[2]==EMUSYS_DEVICE_DISCONNECTED &&
		m_Input.uPad[3]==EMUSYS_DEVICE_DISCONNECTED &&
		m_Input.uPad[4]==EMUSYS_DEVICE_DISCONNECTED
		)
	{
		// no multitap!

		// read serial bit
		uData  = ReadSerialPad(1) << 0;

		// confirmed:
		// if strobe is left on, then bitposition never shifts
		// all bits returned are button B
		if (!(m_Regs.joydata&1))
		{
			ShiftSerialPad(1);
		}

	} else
	{
		// multitap

		// confirmed:
		// if stobe is left on, then bit is returned if multitap is connected
		if (m_Regs.joydata&1)
		{
			// signal presence of multitap
			uData = 0x02;
		}
		else
		{
			// multitap port enabled?
			if (m_Regs.wrio & 0x80)
			{
				// use controllers 2 and 3
				uData  = ReadSerialPad(1) << 0;
				uData |= ReadSerialPad(2) << 1;

				ShiftSerialPad(1);
				ShiftSerialPad(2);
			} else
			{
				// use controllers 4 and 5
				uData  = ReadSerialPad(3) << 0;
				uData |= ReadSerialPad(4) << 1;

				ShiftSerialPad(3);
				ShiftSerialPad(4);
			}
		}
	}

	// confirmed:
	// this port always returns with 1C bits on
	// havent tested with multitap yet though
	return uData | 0x1C;
}

void SnesIO::WriteSerial(Uint8 uData)
{
	Uint8 uOldStrobe = m_Regs.joydata & 1;
	Uint8 uNewStrobe = uData & 1;

	// strobe?
	if (uNewStrobe && !uOldStrobe)
	{
		int iPad;

		// strobe!
		for (iPad=0; iPad < SNESIO_DEVICE_NUM; iPad++)
		{
			if (m_Input.uPad[iPad] != EMUSYS_DEVICE_DISCONNECTED)
			{
				// latch joypad position
				m_Regs.joyserial[iPad] = m_Input.uPad[iPad] & 0xFFF0;
			} else
			{
				// disconnected joypads return 0's
				m_Regs.joyserial[iPad] = 0;
			}
		}

		if (IsMouseMode())
			LatchMouseSerial();
		if (IsSuperScopeMode())
			LatchScopeSerial();
	}

	/* Justifier resets its 32-bit stream on either strobe edge and toggles
	   the selected gun on the falling edge, even for a single-gun chain. */
	if (IsJustifierMode() && uOldStrobe != uNewStrobe)
	{
		if (uOldStrobe && !uNewStrobe)
			m_uJustifierActive ^= 1;
		LatchJustifierSerial();
	}

	m_Regs.joydata = uData;
}

// this function gets called about 3 scanlines after vblank, it performs reads from the serial
// port and loads them into each register
void SnesIO::UpdateJoyPads()
{

	// strobe joypads
	WriteSerial(0);
	WriteSerial(1);
	WriteSerial(0);

	if (IsMouseMode() || IsSuperScopeMode() || IsJustifierMode())
	{
		m_Regs.joy1.w = 0;
		m_Regs.joy2.w = 0;
		m_Regs.joy3.w = 0;
		m_Regs.joy4.w = 0;
		for (int i=0; i<16; i++)
		{
			Uint8 uPort0 = ReadSerial0();
			Uint8 uPort1 = ReadSerial1();
			m_Regs.joy1.w = (Uint16)((m_Regs.joy1.w << 1) | (uPort0 & 1));
			m_Regs.joy3.w = (Uint16)((m_Regs.joy3.w << 1) | ((uPort0 >> 1) & 1));
			m_Regs.joy2.w = (Uint16)((m_Regs.joy2.w << 1) | (uPort1 & 1));
			m_Regs.joy4.w = (Uint16)((m_Regs.joy4.w << 1) | ((uPort1 >> 1) & 1));
		}
		return;
	}

	m_Regs.joy1.w = m_Regs.joyserial[0];
	m_Regs.joy2.w = m_Regs.joyserial[1];

	// multitap enabled?
	if (m_Regs.wrio & 0x80)
	{
		// ??
		m_Regs.joy3.w = m_Regs.joyserial[1];
		m_Regs.joy4.w = m_Regs.joyserial[2];
	} else
	{
		// ??
		m_Regs.joy3.w = m_Regs.joyserial[3];
		m_Regs.joy4.w = m_Regs.joyserial[4];
	}

	// perform dummy reads
	for (int i=0; i<16; i++)
	{
		ReadSerial0();
		ReadSerial1();
	}
}

SnesIO::SnesIO()
{
	Reset();
}

void SnesIO::Reset()
{
	memset(this, 0, sizeof(*this));

	/* These are 5A22 power-on values, not ordinary zero-initialized RAM.
	   WRIO.7 being high matters: reads of $2137 are allowed to latch the
	   live PPU counters, and a later high-to-low transition latches them too.
	   Starting the IRQ compare registers at $1FF also keeps an unwritten
	   H/V timer outside the visible picture, as on hardware. */
	m_Regs.rdnmi = SNIO_VERSION_5A22;
	m_Regs.wrio = 0xFF;
	m_Regs.htime.w = 0x01FF;
	m_Regs.vtime.w = 0x01FF;
	/* Power-on values of the 5A22 ALU input latches. */
	m_Regs.wrmpya = 0xFF;
	m_Regs.wrdiv.w = 0xFFFF;
	ResetALUTiming(0);
	m_uAutoReadClockStart = 0;
	m_uAutoReadNextClock = 0;
	m_uAutoReadPort1Value = 0;
	m_uAutoReadPort2Value = 0;
	m_bAutoReadActive = FALSE;
	m_bAutoReadDisabled = TRUE;
}

void SnesIO::ResetALUTiming(Uint32 uCpuCycle)
{
	m_uMultCounter = 0;
	m_uDivCounter = 0;
	m_uAluShift = 0;
	m_uAluPrevCpuCycle = uCpuCycle;
}

void SnesIO::BeginAutoJoypad(Uint32 uMasterClock)
{
	/* MesenCE/5A22: start at the first 256-master-clock boundary after
	   H=130, then back up 128 clocks so OUT0 rises one half-period before
	   the busy flag.  Keeping the clock absolute preserves the 256-clock
	   phase across frame boundaries. */
	Uint32 uRangeStart = uMasterClock + 130u;
	Uint32 uPhase = uRangeStart & 0xFFu;
	m_uAutoReadClockStart = uRangeStart + (uPhase ? (256u - uPhase) : 0u) - 128u;
	m_uAutoReadNextClock = m_uAutoReadClockStart;
	m_bAutoReadActive = FALSE;
	m_bAutoReadDisabled = FALSE;
	m_uAutoReadPort1Value = 0;
	m_uAutoReadPort2Value = 0;
}

void SnesIO::ProcessAutoJoypad(Uint32 uMasterClock)
{
	/* Signed deltas are safe here because every interesting interval is only
	   a few thousand clocks long, even if the 32-bit master clock wraps. */
	if ((Int32)(uMasterClock - m_uAutoReadClockStart) < 0)
		return;

	if (m_bAutoReadDisabled)
	{
		if ((Uint32)(uMasterClock - m_uAutoReadClockStart) >= 256u)
			WriteSerial(0);
		return;
	}

	while ((Int32)(uMasterClock - m_uAutoReadNextClock) >= 0)
	{
		Uint32 uClock = m_uAutoReadNextClock;
		Int32 iStep = (Int32)((uClock - m_uAutoReadClockStart) >> 7);
		m_uAutoReadNextClock = uClock + 128u;

		switch (iStep)
		{
		case 0:
			WriteSerial((m_Regs.nmitimen & 0x01) ? 1 : 0);
			break;

		case 1:
			if (!(m_Regs.nmitimen & 0x01))
			{
				m_bAutoReadDisabled = TRUE;
				m_bAutoReadActive = FALSE;
			}
			else
			{
				m_bAutoReadActive = TRUE;
				m_Regs.joy1.w = 0;
				m_Regs.joy2.w = 0;
				m_Regs.joy3.w = 0;
				m_Regs.joy4.w = 0;
			}
			break;

		case 2:
			WriteSerial(0);
			break;

		default:
			if (!(m_Regs.nmitimen & 0x01))
			{
				iStep = 34;
			}
			else if (iStep & 1)
			{
				/* The serial helpers advance the controller shift registers at
				   the read phase; the next 128-clock phase commits those bits to
				   $4218-$421F. */
				m_uAutoReadPort1Value = ReadSerial0();
				m_uAutoReadPort2Value = ReadSerial1();
			}
			else
			{
				m_Regs.joy1.w = (Uint16)((m_Regs.joy1.w << 1) |
					(m_uAutoReadPort1Value & 0x01));
				m_Regs.joy2.w = (Uint16)((m_Regs.joy2.w << 1) |
					(m_uAutoReadPort2Value & 0x01));
				m_Regs.joy3.w = (Uint16)((m_Regs.joy3.w << 1) |
					((m_uAutoReadPort1Value & 0x02) >> 1));
				m_Regs.joy4.w = (Uint16)((m_Regs.joy4.w << 1) |
					((m_uAutoReadPort2Value & 0x02) >> 1));
			}
			break;
		}

		if (iStep >= 34)
		{
			m_bAutoReadDisabled = TRUE;
			m_bAutoReadActive = FALSE;
			WriteSerial(0);
			return;
		}
	}

	if (!(m_Regs.nmitimen & 0x01) &&
		(Uint32)(uMasterClock - m_uAutoReadClockStart) >= 128u * 3u)
	{
		m_bAutoReadDisabled = TRUE;
		m_bAutoReadActive = FALSE;
		WriteSerial(0);
	}
}

void SnesIO::PrepareAutoJoypadEnableChange(Uint32 uMasterClock, Bool bEnable)
{
	Bool bOldEnable = (m_Regs.nmitimen & 0x01) ? TRUE : FALSE;
	if (bOldEnable == bEnable)
		return;

	/* Catch up with the old enable value before changing the signal. During
	   the first 256 clocks, OUT0 follows a mid-sequence enable change. */
	ProcessAutoJoypad(uMasterClock);
	if ((Int32)(uMasterClock - m_uAutoReadClockStart) >= 0 &&
		(Uint32)(uMasterClock - m_uAutoReadClockStart) < 256u)
	{
		WriteSerial(bEnable ? 1 : 0);
	}
}

void SnesIO::RunALU(SNCpuT *pCpu, Bool bReadPhase)
{
	Uint32 uCpuCycle = pCpu ? pCpu->uCpuCycleCount : 0;
	Uint32 nCycles;

	/* On the real 5A22 a read samples before the write point of the current
	   machine cycle. MesenCE represents that by advancing the ALU to C-1 for
	   reads (and the first phase of writes), then to C for writes. */
	if (bReadPhase && uCpuCycle)
		uCpuCycle--;

	nCycles = uCpuCycle - m_uAluPrevCpuCycle;
	while (nCycles-- && (m_uMultCounter || m_uDivCounter))
	{
		if (m_uMultCounter)
		{
			m_uMultCounter--;
			if (m_Regs.rddiv.w & 1)
				m_Regs.rdmpy.w =
					(Uint16)(m_Regs.rdmpy.w + (Uint16)m_uAluShift);
			m_uAluShift <<= 1;
			m_Regs.rddiv.w >>= 1;
		}

		if (m_uDivCounter)
		{
			m_uDivCounter--;
			m_uAluShift >>= 1;
			m_Regs.rddiv.w = (Uint16)(m_Regs.rddiv.w << 1);
			if ((Uint32)m_Regs.rdmpy.w >= m_uAluShift)
			{
				m_Regs.rdmpy.w =
					(Uint16)((Uint32)m_Regs.rdmpy.w - m_uAluShift);
				m_Regs.rddiv.w |= 1;
			}
		}
	}

	m_uAluPrevCpuCycle = uCpuCycle;
}

Uint8 SnesIO::ReadALU(SNCpuT *pCpu, Uint32 uAddr)
{
	RunALU(pCpu, TRUE);
	switch (uAddr & 0xFFFF)
	{
	case 0x4214: return m_Regs.rddiv.b.l;
	case 0x4215: return m_Regs.rddiv.b.h;
	case 0x4216: return m_Regs.rdmpy.b.l;
	case 0x4217: return m_Regs.rdmpy.b.h;
	default: return 0;
	}
}

void SnesIO::WriteALU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
	Bool bBlockWrite;

	RunALU(pCpu, TRUE);
	/* A unit that was active at the read point of this cycle blocks a new
	   operation even when its final shift completes at the write point. */
	bBlockWrite = (m_uDivCounter || m_uMultCounter) ? TRUE : FALSE;
	RunALU(pCpu, FALSE);

	switch (uAddr & 0xFFFF)
	{
	case 0x4202:
		m_Regs.wrmpya = uData;
		break;

	case 0x4203:
		m_Regs.rdmpy.w = 0;
		if (!bBlockWrite)
		{
			m_uMultCounter = 8;
			m_Regs.wrmpyb = uData;
			m_Regs.rddiv.w =
				(Uint16)(((Uint16)uData << 8) | m_Regs.wrmpya);
			m_uAluShift = uData;
		}
		else if (!m_uDivCounter && !m_uMultCounter)
		{
			/* Last-cycle write: operand reaches the quotient shift register,
			   but hardware does not launch another multiplication. */
			m_Regs.rddiv.w =
				(Uint16)(((Uint16)uData << 8) | m_Regs.wrmpya);
		}
		break;

	case 0x4204:
		m_Regs.wrdiv.b.l = uData;
		break;

	case 0x4205:
		m_Regs.wrdiv.b.h = uData;
		break;

	case 0x4206:
		m_Regs.rdmpy.w = m_Regs.wrdiv.w;
		if (!bBlockWrite)
		{
			m_uDivCounter = 16;
			m_Regs.wrdivb = uData;
			m_uAluShift = (Uint32)uData << 16;
		}
		break;
	}
}

void SnesIO::LatchInput(Emu::SysInputT  *pInput)
{
	if (pInput)
	{
		m_Input = *pInput;
	} else
	{
		// not connected
		m_Input.uPad[0] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[1] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[2] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[3] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[4] = EMUSYS_DEVICE_DISCONNECTED;
	}
}
