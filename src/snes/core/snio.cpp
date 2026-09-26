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

void SnesIO::ResetAluTransient()
{
	m_uAluLastClock = 0;
	m_uAluShift = 0;
	m_uAluMulCounter = 0;
	m_uAluDivCounter = 0;
	m_uAluCycleClocks = 0;
}

void SnesIO::RunAlu(Uint32 uClock)
{
	Uint32 uSteps;
	Uint32 uDone = 0;

	if ((!m_uAluMulCounter && !m_uAluDivCounter) || !m_uAluCycleClocks)
		return;

	uSteps = (uClock - m_uAluLastClock) / m_uAluCycleClocks;
	while (uSteps-- > 0 && (m_uAluMulCounter || m_uAluDivCounter))
	{
		if (m_uAluMulCounter)
		{
			m_uAluMulCounter--;
			if (m_Regs.rddiv.w & 1)
				m_Regs.rdmpy.w = (Uint16)(m_Regs.rdmpy.w + m_uAluShift);
			m_uAluShift <<= 1;
			m_Regs.rddiv.w >>= 1;
		}

		if (m_uAluDivCounter)
		{
			m_uAluDivCounter--;
			m_uAluShift >>= 1;
			m_Regs.rddiv.w <<= 1;
			if ((Uint32)m_Regs.rdmpy.w >= m_uAluShift)
			{
				m_Regs.rdmpy.w = (Uint16)((Uint32)m_Regs.rdmpy.w - m_uAluShift);
				m_Regs.rddiv.w |= 1;
			}
		}
		uDone++;
	}
	m_uAluLastClock += uDone * m_uAluCycleClocks;
}

void SnesIO::WriteAlu(Uint16 uAddr, Uint8 uData, Uint32 uBeforeClock,
                      Uint32 uAfterClock, Uint8 uCycleClocks)
{
	Bool bBlocked;

	/* Match the 5A22 ordering: determine whether an operation was active
	   before this write cycle, then allow that cycle to advance the old op. */
	RunAlu(uBeforeClock);
	bBlocked = (m_uAluMulCounter || m_uAluDivCounter) ? TRUE : FALSE;
	RunAlu(uAfterClock);

	if (uCycleClocks != 6 && uCycleClocks != 8)
		uCycleClocks = 6;

	switch (uAddr)
	{
	case 0x4202:
		m_Regs.wrmpya = uData;
		break;

	case 0x4203:
		m_Regs.rdmpy.w = 0;
		if (!bBlocked)
		{
			m_uAluMulCounter = 8;
			m_uAluDivCounter = 0;
			m_Regs.wrmpyb = uData;
			m_Regs.rddiv.w = ((Uint16)uData << 8) | m_Regs.wrmpya;
			m_uAluShift = uData;
			m_uAluCycleClocks = uCycleClocks;
			m_uAluLastClock = uAfterClock;
		}
		else if (!m_uAluMulCounter && !m_uAluDivCounter)
		{
			/* A write on the exact final clock corrupts/overwrites the internal
			   shift value but does not start a second multiplication. */
			m_Regs.rddiv.w = ((Uint16)uData << 8) | m_Regs.wrmpya;
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
		if (!bBlocked)
		{
			m_uAluDivCounter = 16;
			m_uAluMulCounter = 0;
			m_Regs.wrdivb = uData;
			m_uAluShift = (Uint32)uData << 16;
			m_uAluCycleClocks = uCycleClocks;
			m_uAluLastClock = uAfterClock;
		}
		break;
	}
}

Uint8 SnesIO::ReadAlu(Uint16 uAddr, Uint32 uBeforeClock)
{
	RunAlu(uBeforeClock);
	switch (uAddr)
	{
	case 0x4214: return m_Regs.rddiv.b.l;
	case 0x4215: return m_Regs.rddiv.b.h;
	case 0x4216: return m_Regs.rdmpy.b.l;
	case 0x4217: return m_Regs.rdmpy.b.h;
	default: return 0;
	}
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
	// strobe?
	if ((uData&1) && !(m_Regs.joydata&1))
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
	m_Regs.rdnmi  =  SNIO_VERSION_5A22;
	m_Regs.wrmpya = 0xFF;
	m_Regs.wrdiv.w = 0xFFFF;
	ResetAluTransient();
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
