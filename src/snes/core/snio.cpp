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
}

void SnesIO::ResetALUTiming(Uint32 uCpuCycle)
{
	m_uMultCounter = 0;
	m_uDivCounter = 0;
	m_uAluShift = 0;
	m_uAluPrevCpuCycle = uCpuCycle;
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
