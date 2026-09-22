/*
 * Experimental SA-1 support for SNESticle Revive.
 *
 * Experimental correctness-first SA-1 implementation around an independent
 * 65C816 context. Hardware engines stay in readable C++ while timing, mapping
 * and shared-bus behavior are validated before any SA-1-specific MIPS tuning.
 */

#include <string.h>
#include "snsa1.h"
#include "sntiming.h"
#include "sndbglog.h"

extern "C" {
#include "sncpu_c.h"
#if defined(__mips__)
Int32 SNCPUExecute_ASM(SNCpuT *pCpu);
#endif
}

static const Uint8 _SA1LoBankBase[4] = { 0x00, 0x20, 0x80, 0xA0 };

#if SNDBG_DEEP
static Uint32 _SA1TraceFrame = (Uint32)-1;
static Uint32 _SA1TraceCount = 0;
#define SNSA1_TRACE_MAX 64u
#endif

SNSA1::SNSA1()
{
	m_pRom = NULL;
	m_uRomBytes = 0;
	m_pBWRAM = NULL;
	m_uBWRAMBytes = 0;
	m_uIdleFastForwardTicks = 0;
	m_uIdleSleepSlices = 0;
	m_bIdlePollSleeping = FALSE;
	m_uIdlePollIRAM = 0;
	m_uIdlePollValue = 0;
	m_uIdlePollLoopPC = 0;
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
	UpdateFastMemorySidecars();
}

void SNSA1::UpdateFastMemorySidecars()
{
	Uint32 uProtected = 0x100u << (m_State.Registers[0x028] & 0x0F);
	Bool bWriteEnabled =
		((m_State.Registers[0x026] | m_State.Registers[0x027]) & 0x80) ?
		TRUE : FALSE;

	SNCPUSA1FastMemConfig(m_IRAM, m_State.Registers[0x02A],
	                     m_pBWRAM, m_uBWRAMBytes,
	                     bWriteEnabled, uProtected);
}

void SNSA1::SetVideoRegion(Bool bPAL)
{
	m_State.TimerScanlines = bPAL ? 312 : 262;
}

void SNSA1::SaveState(SA1SaveState *pState) const
{
	if (!pState)
		return;

	memset(pState, 0, sizeof(*pState));
	pState->State = m_State;
	pState->CpuRegs = m_Cpu.Regs;
	pState->CpuCycles = m_Cpu.Cycles;
	memcpy(pState->CpuCounter, m_Cpu.Counter, sizeof(pState->CpuCounter));
	pState->CpuAbortCycles = m_Cpu.nAbortCycles;
	pState->CpuSignal = m_Cpu.uSignal;
	pState->CpuNmiDmaDelay = m_Cpu.uNmiDmaDelay;
	pState->CpuIrqPending = m_Cpu.uIrqPending;
	memcpy(pState->IRAM, m_IRAM, sizeof(pState->IRAM));
}

void SNSA1::RestoreState(const SA1SaveState *pState)
{
	if (!pState)
		return;

	m_State = pState->State;
	memcpy(m_IRAM, pState->IRAM, sizeof(m_IRAM));

	m_Cpu.Regs = pState->CpuRegs;
	m_Cpu.Cycles = pState->CpuCycles;
	memcpy(m_Cpu.Counter, pState->CpuCounter, sizeof(m_Cpu.Counter));
	m_Cpu.nAbortCycles = pState->CpuAbortCycles;
	m_Cpu.bRunning = FALSE;
	m_Cpu.uSignal = pState->CpuSignal;
	m_Cpu.uNmiDmaDelay = pState->CpuNmiDmaDelay;
	m_Cpu.uIrqPending = pState->CpuIrqPending;
	m_Cpu.pUserData = this;

	// The idle latch is a derived optimization, not architectural state.
	// Reconstruct it naturally after restore instead of serializing it.
	ClearIdlePollSleep();

	// Bank pointers and trap callbacks are process-local and are never
	// serialized. Rebuild them from the restored MMC/register state.
	MapCpuMemory();
	UpdateFastMemorySidecars();
	UpdateIRQLine();
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
	m_State.HCounterLatch = 0;
	m_State.VCounterLatch = 0;
	m_State.TimerScanlines = 262;
	m_State.TimerRemainder = 0;
	m_State.DMASource = 0;
	m_State.DMADest = 0;
	m_State.DMARemaining = 0;
	m_State.DMAStallTicks = 0;
	m_State.DMATransferredBytes = 0;
	m_State.DMAWaitTicks = 0;
	m_State.DMARunning = FALSE;
	m_State.ArithmeticResult = 0;
	m_State.ArithmeticOverflow = FALSE;
	m_State.VariableData = 0;
	m_State.VariableBitPos = 0;
	m_State.TimerMatch = FALSE;
	m_State.NMIPending = FALSE;
	m_State.CharConvLine = 0;
	m_State.CC1Active = FALSE;
	m_State.Running = FALSE;
	m_uIdleFastForwardTicks = 0;
	m_uIdleSleepSlices = 0;
	ClearIdlePollSleep();

	ResetCPUContext();
	MapCpuMemory();
	UpdateFastMemorySidecars();
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
	Uint32 uElapsedTicks;
	Int32 nElapsedUnits;

	ClearIdlePollSleep();

	// Releasing CCNT.RESET resets the SA-1-side I-RAM write-enable mask.
	// Software must reprogram CIWP ($222A) after the CPU leaves reset.
	m_State.Registers[0x02A] = 0;

	// Reset the architectural CPU state, but keep its clock on the same
	// free-running epoch as the rest of the SA-1. MesenCE resets the SA-1 CPU
	// here and then aligns its cycle count to the current master clock / 2.
	uElapsedTicks = (Uint32)(m_State.MasterCycles / SNSA1_MASTER_PER_TICK);
	nElapsedUnits = (Int32)(uElapsedTicks * SNCPU_CYCLE_FAST);
	ResetCPUContext();
	for (Int32 i = 0; i < SNCPU_COUNTER_NUM; i++)
		m_Cpu.Counter[i] = nElapsedUnits;

	m_State.LastResetVector = GetResetVector();
	m_State.ResetEpoch++;
	// Preserve MasterRemainder: reset/wait gate instruction execution but do
	// not reset the phase of the free-running 10.74 MHz SA-1 clock.
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
	if (uAddr == 0x2302)
	{
		m_State.HCounterLatch = (Uint16)(m_State.HCounter >> 2);
		m_State.VCounterLatch = m_State.VCounter;
		return (Uint8)m_State.HCounterLatch;
	}
	if (uAddr == 0x2303)
		return (Uint8)(m_State.HCounterLatch >> 8);
	if (uAddr == 0x2304 || uAddr == 0x2305)
		return (uAddr == 0x2304) ? (Uint8)m_State.VCounterLatch :
		                            (Uint8)(m_State.VCounterLatch >> 8);
	if (uAddr >= 0x2306 && uAddr <= 0x230A)
	{
		ProcessArithmetic();
		return (Uint8)(m_State.ArithmeticResult >> ((uAddr - 0x2306) * 8));
	}
	if (uAddr == 0x230B)
	{
		ProcessArithmetic();
		return m_State.ArithmeticOverflow ? 0x80 : 0x00;
	}
	if (uAddr == 0x230C)
	{
		LoadVariableData();
		return (Uint8)m_State.VariableData;
	}
	if (uAddr == 0x230D)
	{
		LoadVariableData();
		uValue = (Uint8)(m_State.VariableData >> 8);
		if (m_State.Registers[0x058] & 0x80)
			IncrementVariablePosition();
		return uValue;
	}
	// $230E is often documented as a version register, but real SA-1
	// hardware does not implement it.  MesenCE and bsnes both leave it as
	// open bus.  This core currently approximates unmapped/open bus as $FF.
	if (uAddr == 0x230E)
		return 0xFF;

	return m_State.Registers[uAddr - SNSA1_REGISTER_BASE];
}

Uint8 SNSA1::ReadSCPURegister(Uint16 uAddr)
{
	// The host CPU can read SFR. $230E is not implemented on real SA-1
	// hardware and falls through to the core's open-bus approximation.
	if (uAddr == 0x2300)
		return ReadRegister(uAddr);
	return 0xFF;
}

Uint8 SNSA1::ReadSA1Register(Uint16 uAddr)
{
	// CFR/counters/math/VBR are visible to the SA-1 CPU itself.
	if (uAddr >= 0x2301 && uAddr <= 0x230D)
		return ReadRegister(uAddr);
	return 0xFF;
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
	if (uAddr >= 0x2250 && uAddr <= 0x2254)
		ProcessArithmetic();
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
		m_State.TimerRemainder = 0;
		m_State.TimerMatch = FALSE;
		break;

	case 0x2213:
		m_State.Registers[0x013] &= 0x01;
		break;

	case 0x2215:
		m_State.Registers[0x015] &= 0x01;
		break;

	case 0x2230:
		if (!(uData & 0x80))
		{
			m_State.CharConvLine = 0;
			m_State.DMARunning = FALSE;
			m_State.DMAWaitTicks = 0;
		}
		break;

	case 0x2231:
		{
			Uint8 uFormat = uData & 0x03;
			Uint8 uWidth = (uData >> 2) & 0x07;
			if (uFormat > 2) uFormat = 2;
			if (uWidth > 5) uWidth = 5;
			m_State.Registers[0x031] =
				(Uint8)((uData & 0x80) | (uWidth << 2) | uFormat);
			if (uData & 0x80)
				m_State.CC1Active = FALSE;
		}
		break;

	case 0x2236:
		if ((m_State.Registers[0x030] & 0xA4) == 0x80)
			StartDMA();
		else if ((m_State.Registers[0x030] & 0xB0) == 0xB0)
			StartCC1();
		break;

	case 0x2237:
		if ((m_State.Registers[0x030] & 0xA4) == 0x84)
			StartDMA();
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
		// Multiplication/division complete after five SA-1 clocks; cumulative
		// multiplication takes six. Keep the previous result visible meanwhile.
		m_State.ArithmeticStartClock =
			(Uint32)SNCPUGetCounter(&m_Cpu, SNCPU_COUNTER_FRAME);
		m_State.ArithmeticPending = TRUE;
		break;

	case 0x2258:
		if (!(uData & 0x80))
			IncrementVariablePosition();
		break;
	case 0x2259:
	case 0x225A:
		break;
	case 0x225B:
		m_State.VariableBitPos = 0;
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

	/* Register writes can change CIWP/BWPA/write enable while the R5900
	   interpreter is still active, so refresh the direct-memory sidecar now. */
	UpdateFastMemorySidecars();
}

void SNSA1::WriteSCPURegister(Uint16 uAddr, Uint8 uData)
{
	Bool bAllowed =
		(uAddr >= 0x2200 && uAddr <= 0x2208) ||
		(uAddr >= 0x2220 && uAddr <= 0x2224) ||
		uAddr == 0x2226 || uAddr == 0x2228 || uAddr == 0x2229 ||
		(uAddr >= 0x2231 && uAddr <= 0x2237);

	if (bAllowed)
		WriteRegister(uAddr, uData);
}

void SNSA1::WriteSA1Register(Uint16 uAddr, Uint8 uData)
{
	Bool bAllowed =
		(uAddr >= 0x2209 && uAddr <= 0x2215) ||
		uAddr == 0x2225 || uAddr == 0x2227 || uAddr == 0x222A ||
		uAddr == 0x2230 ||
		(uAddr >= 0x2231 && uAddr <= 0x2239) ||
		uAddr == 0x223F ||
		(uAddr >= 0x2240 && uAddr <= 0x2254) ||
		(uAddr >= 0x2258 && uAddr <= 0x225B);

	if (bAllowed)
		WriteRegister(uAddr, uData);
}

Uint8 SNSA1::ReadIRAM(Uint16 uAddr) const
{
	return m_IRAM[uAddr & (SNSA1_IRAM_SIZE - 1)];
}

Bool SNSA1::CanWriteIRAM(Uint16 uAddr, Bool bSA1Side) const
{
	Uint8 uWriteEnable = m_State.Registers[bSA1Side ? 0x02A : 0x029];
	Uint8 uPage = (Uint8)((uAddr & (SNSA1_IRAM_SIZE - 1)) >> 8);
	// SIWP/CIWP bits are per-256-byte write-enable bits: 1 = writable.
	return (uWriteEnable & (1u << uPage)) ? TRUE : FALSE;
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
	Uint32 uProtected = 0x100u << (m_State.Registers[0x028] & 0x0F);
	(void)bSA1Side;

	// Hardware BWPA protection is active only while BOTH write-enable bits
	// are clear. This detail is required by games such as Kirby's Dream Land 3.
	if ((m_State.Registers[0x026] & 0x80) ||
	    (m_State.Registers[0x027] & 0x80))
		return TRUE;

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

Uint8 SNSA1::ReadSCPUBWRAMWindow(Uint16 uAddr)
{
	Uint32 uOffset;

	if (!m_pBWRAM || !m_uBWRAMBytes)
		return 0xFF;

	uOffset = ((Uint32)(m_State.Registers[0x024] & 0x1F) << 13) |
	          (uAddr & 0x1FFF);
	return m_State.CC1Active ? ReadCC1Byte(uOffset) :
	                           m_pBWRAM[MirrorBWRAM(uOffset)];
}

Uint8 SNSA1::ReadSCPUBWRAMDirect(Uint32 uAddr)
{
	Uint32 uOffset = uAddr & 0x3FFFFu;

	if (!m_pBWRAM || !m_uBWRAMBytes)
		return 0xFF;
	return m_State.CC1Active ? ReadCC1Byte(uOffset) :
	                           m_pBWRAM[MirrorBWRAM(uOffset)];
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

void SNSA1::StartDMA()
{
	if (!(m_State.Registers[0x030] & 0x80) ||
	    (m_State.Registers[0x030] & 0x20))
		return;

	m_State.DMASource = (Uint32)m_State.Registers[0x032] |
	                    ((Uint32)m_State.Registers[0x033] << 8) |
	                    ((Uint32)m_State.Registers[0x034] << 16);
	m_State.DMADest = (Uint32)m_State.Registers[0x035] |
	                  ((Uint32)m_State.Registers[0x036] << 8) |
	                  ((Uint32)m_State.Registers[0x037] << 16);
	m_State.DMARemaining = (Uint16)(m_State.Registers[0x038] |
	                                ((Uint16)m_State.Registers[0x039] << 8));
	m_State.DMAWaitTicks = 0;
	m_State.DMARunning = m_State.DMARemaining ? TRUE : FALSE;

	if (m_State.DMARunning)
	{
		Uint8 uSource = m_State.Registers[0x030] & 0x03;
		Bool bDestBWRAM = (m_State.Registers[0x030] & 0x04) ? TRUE : FALSE;
		Bool bValidPair =
			(uSource == 0) ||
			(uSource == 1 && !bDestBWRAM) ||
			(uSource == 2 && bDestBWRAM);

		if (!bValidPair)
		{
			// Invalid source/destination combinations still consume DTC and
			// advance DSA/DDA, but no bus transfer or DMA step occurs.
			m_State.DMASource =
				(m_State.DMASource + m_State.DMARemaining) & 0xFFFFFF;
			m_State.DMADest =
				(m_State.DMADest + m_State.DMARemaining) & 0xFFFFFF;
			m_State.Registers[0x032] = (Uint8)m_State.DMASource;
			m_State.Registers[0x033] = (Uint8)(m_State.DMASource >> 8);
			m_State.Registers[0x034] = (Uint8)(m_State.DMASource >> 16);
			m_State.Registers[0x035] = (Uint8)m_State.DMADest;
			m_State.Registers[0x036] = (Uint8)(m_State.DMADest >> 8);
			m_State.Registers[0x037] = (Uint8)(m_State.DMADest >> 16);
			CompleteDMA();
			return;
		}
	}

	if (!m_State.DMARunning)
		CompleteDMA();
}

Uint8 SNSA1::ReadDMASource(Uint8 uSource, Uint32 uAddr)
{
	switch (uSource)
	{
	default:
	case 0:
		// Program-ROM DMA must not charge the SA-1 CPU's instruction budget.
		return SNCPUPeek8(&m_Cpu, uAddr & 0xFFFFFF);
	case 1:
		return (!m_pBWRAM || !m_uBWRAMBytes) ? 0xFF :
		       m_pBWRAM[MirrorBWRAM(uAddr)];
	case 2:
		return m_IRAM[uAddr & (SNSA1_IRAM_SIZE - 1)];
	}
}

void SNSA1::CompleteDMA()
{
	m_State.DMARunning = FALSE;
	m_State.DMAWaitTicks = 0;
	m_State.DMARemaining = 0;
	m_State.Registers[0x038] = 0;
	m_State.Registers[0x039] = 0;
	m_State.Registers[0x101] |= 0x20;
	UpdateIRQLine();
}

Uint32 SNSA1::RunDMA(Uint32 uSA1Ticks)
{
	Uint8 uSource = m_State.Registers[0x030] & 0x03;
	Bool bDestBWRAM = (m_State.Registers[0x030] & 0x04) ? TRUE : FALSE;
	Uint8 uBaseCost = (uSource == 0 && !bDestBWRAM) ? 1 : 2;

	while (m_State.DMARunning && uSA1Ticks)
	{
		Uint8 uData;
		Uint32 uConsume;
		Int32 iCounter;

		if (!m_State.DMAWaitTicks)
		{
			Uint32 uConflict =
				SNCPUSA1BusDMAPenaltyTicks(&m_Cpu, uSource, bDestBWRAM);
			m_State.DMAWaitTicks = (Uint8)(uBaseCost + uConflict);
		}

		uConsume = uSA1Ticks;
		if (uConsume > m_State.DMAWaitTicks)
			uConsume = m_State.DMAWaitTicks;
		uSA1Ticks -= uConsume;
		m_State.DMAWaitTicks = (Uint8)(m_State.DMAWaitTicks - uConsume);
		m_State.DMAStallTicks += uConsume;

		for (iCounter = 0; iCounter < SNCPU_COUNTER_NUM; iCounter++)
			m_Cpu.Counter[iCounter] += (Int32)(uConsume * SNCPU_CYCLE_FAST);

		if (m_State.DMAWaitTicks)
			return 0;

		uData = ReadDMASource(uSource, m_State.DMASource);
		if (bDestBWRAM)
		{
			if (m_pBWRAM && m_uBWRAMBytes)
				m_pBWRAM[MirrorBWRAM(m_State.DMADest)] = uData;
		}
		else
		{
			m_IRAM[m_State.DMADest & (SNSA1_IRAM_SIZE - 1)] = uData;
		}

		m_State.DMASource = (m_State.DMASource + 1) & 0xFFFFFF;
		m_State.DMADest = (m_State.DMADest + 1) & 0xFFFFFF;
		m_State.DMARemaining--;
		m_State.DMATransferredBytes++;

		m_State.Registers[0x032] = (Uint8)m_State.DMASource;
		m_State.Registers[0x033] = (Uint8)(m_State.DMASource >> 8);
		m_State.Registers[0x034] = (Uint8)(m_State.DMASource >> 16);
		m_State.Registers[0x035] = (Uint8)m_State.DMADest;
		m_State.Registers[0x036] = (Uint8)(m_State.DMADest >> 8);
		m_State.Registers[0x037] = (Uint8)(m_State.DMADest >> 16);
		m_State.Registers[0x038] = (Uint8)m_State.DMARemaining;
		m_State.Registers[0x039] = (Uint8)(m_State.DMARemaining >> 8);

		if (!m_State.DMARemaining)
			CompleteDMA();
	}
	return uSA1Ticks;
}

void SNSA1::ProcessArithmetic()
{
	Uint32 uNow;
	Uint32 uElapsed;
	Uint32 uRequired;

	if (!m_State.ArithmeticPending)
		return;

	uNow = (Uint32)SNCPUGetCounter(&m_Cpu, SNCPU_COUNTER_FRAME);
	uElapsed = uNow - m_State.ArithmeticStartClock;
	uRequired = ((m_State.Registers[0x050] & 0x02) ? 6u : 5u) *
	            (Uint32)SNCPU_CYCLE_FAST;
	if (uElapsed < uRequired)
		return;

	m_State.ArithmeticPending = FALSE;
	ExecuteArithmetic();
}

void SNSA1::ExecuteArithmetic()
{
	Uint8 uMode = m_State.Registers[0x050] & 0x03;
	Int32 nA = (Int16)m_State.ArithmeticOp1;
	Int32 nB = (Int16)m_State.ArithmeticOp2;
	const Uint64 uMask40 = (((Uint64)1 << 40) - 1);

	if (uMode & 0x02)
	{
		Uint64 uRaw = m_State.ArithmeticResult +
		              (Uint64)((Int64)nA * (Int64)nB);
		m_State.ArithmeticOverflow = (uRaw >> 40) ? TRUE : FALSE;
		m_State.ArithmeticResult = uRaw & uMask40;
		m_State.ArithmeticOp2 = 0;
		m_State.Registers[0x053] = 0;
		m_State.Registers[0x054] = 0;
		return;
	}

	if (uMode & 0x01)
	{
		Uint16 uDivisor = m_State.ArithmeticOp2;
		Int32 nDividend = (Int16)m_State.ArithmeticOp1;
		Uint16 uQuotient = 0;
		Uint16 uRemainder = 0;

		if (uDivisor)
		{
			Int32 nRem = nDividend % (Int32)uDivisor;
			if (nRem < 0)
				nRem += uDivisor;
			uRemainder = (Uint16)nRem;
			uQuotient = (Uint16)((nDividend - nRem) / (Int32)uDivisor);
		}
		m_State.ArithmeticResult = (Uint64)uQuotient |
		                           ((Uint64)uRemainder << 16);
		m_State.ArithmeticOverflow = FALSE;
		m_State.ArithmeticOp1 = 0;
		m_State.ArithmeticOp2 = 0;
		m_State.Registers[0x051] = 0;
		m_State.Registers[0x052] = 0;
		m_State.Registers[0x053] = 0;
		m_State.Registers[0x054] = 0;
		return;
	}

	// Normal multiplication is a signed 16x16 -> 32-bit result.  MR is
	// physically 40 bits wide, but the upper byte is cleared rather than
	// sign-extended; cumulative mode is the only path that uses all 40 bits.
	m_State.ArithmeticResult =
		(Uint32)((Int32)nA * (Int32)nB);
	m_State.ArithmeticOverflow = FALSE;
	m_State.ArithmeticOp2 = 0;
	m_State.Registers[0x053] = 0;
	m_State.Registers[0x054] = 0;
}
Uint8 SNSA1::ReadVariableBus(Uint32 uAddr)
{
	uAddr &= 0xFFFFFF;
	Uint8 uBank = (Uint8)(uAddr >> 16);
	Uint16 uLow = (Uint16)uAddr;
	Bool bSystemBank = (uBank <= 0x3F || (uBank >= 0x80 && uBank <= 0xBF));

	// VBR intentionally cannot access MMIO. ROM follows the active MMC map.
	if ((bSystemBank && uLow >= 0x8000) || uBank >= 0xC0)
		return SNCPURead8(&m_Cpu, uAddr);

	if (bSystemBank)
	{
		if (uLow < 0x1000)
			return (uLow < 0x0800) ?
			       m_IRAM[uLow & (SNSA1_IRAM_SIZE - 1)] : 0;
		if (uLow >= 0x3000 && uLow <= 0x3FFF)
			return (uLow < 0x3800) ?
			       m_IRAM[uLow & (SNSA1_IRAM_SIZE - 1)] : 0;
		if (uLow >= 0x6000 && uLow <= 0x7FFF)
			return ReadSA1BWRAMWindow(uLow);
	}

	if (uBank >= 0x40 && uBank <= 0x5F)
		return ReadBWRAMDirect(uAddr);
	if (uBank >= 0x60 && uBank <= 0x6F)
		return ReadBitmap(uAddr & 0x0FFFFF);

	return 0;
}

void SNSA1::LoadVariableData()
{
	Uint32 uAddr = (Uint32)m_State.Registers[0x059] |
	               ((Uint32)m_State.Registers[0x05A] << 8) |
	               ((Uint32)m_State.Registers[0x05B] << 16);
	Uint32 uData = (Uint32)ReadVariableBus(uAddr) |
	               ((Uint32)ReadVariableBus((uAddr + 1) & 0xFFFFFF) << 8) |
	               ((Uint32)ReadVariableBus((uAddr + 2) & 0xFFFFFF) << 16);
	m_State.VariableData = (Uint16)(uData >> m_State.VariableBitPos);
}

void SNSA1::IncrementVariablePosition()
{
	Uint8 uBits = m_State.Registers[0x058] & 0x0F;
	Uint32 uAddr = (Uint32)m_State.Registers[0x059] |
	               ((Uint32)m_State.Registers[0x05A] << 8) |
	               ((Uint32)m_State.Registers[0x05B] << 16);
	Uint8 uBit;

	if (!uBits)
		uBits = 16;

	uBit = (Uint8)(m_State.VariableBitPos + uBits);
	uAddr = (uAddr + (uBit >> 3)) & 0xFFFFFF;
	m_State.VariableBitPos = uBit & 7;
	m_State.Registers[0x059] = (Uint8)uAddr;
	m_State.Registers[0x05A] = (Uint8)(uAddr >> 8);
	m_State.Registers[0x05B] = (Uint8)(uAddr >> 16);
}

void SNSA1::UpdateTimer(Uint32 uMasterCycles)
{
	Uint32 uAdvance;
	Uint32 uTotal;
	Uint32 uLineClocks;
	Uint32 uPeriod;
	Uint32 uStart;
	Uint32 uEnd;
	Uint32 uDistance;
	Uint16 uHCompare;
	Uint16 uVCompare;
	Bool bTriggered = FALSE;
	Bool bLinear = (m_State.Registers[0x010] & 0x80) ? TRUE : FALSE;
	Bool bH = (m_State.Registers[0x010] & 0x01) ? TRUE : FALSE;
	Bool bV = (m_State.Registers[0x010] & 0x02) ? TRUE : FALSE;

	// SA-1 timer counters advance on the same 2-master-clock cadence as the
	// coprocessor. Preserve odd synchronization fragments for the next call.
	uTotal = uMasterCycles + m_State.TimerRemainder;
	uAdvance = (uTotal / 2u) * 2u;
	m_State.TimerRemainder = (Uint8)(uTotal & 1u);
	if (!uAdvance)
		return;

	uHCompare = (Uint16)(m_State.Registers[0x012] |
	                    ((Uint16)(m_State.Registers[0x013] & 1) << 8));
	uVCompare = (Uint16)(m_State.Registers[0x014] |
	                    ((Uint16)(m_State.Registers[0x015] & 1) << 8));

	if (bLinear)
	{
		uLineClocks = 0x800u;
		uPeriod = uLineClocks * 0x200u;
	}
	else
	{
		uLineClocks = (Uint32)SNES_CYCLESPERLINE;
		uPeriod = uLineClocks *
		          (m_State.TimerScanlines ? m_State.TimerScanlines : 262u);
	}

	uStart = ((Uint32)m_State.VCounter * uLineClocks +
	          m_State.HCounter) % uPeriod;

	if (bH && !bV)
	{
		Uint32 uTargetH = ((Uint32)uHCompare * 4u) % uLineClocks;
		uDistance = (uTargetH + uLineClocks -
		             (m_State.HCounter % uLineClocks)) % uLineClocks;
		if (!uDistance) uDistance = uLineClocks;
		bTriggered = (uDistance <= uAdvance) ? TRUE : FALSE;
	}
	else if (bV)
	{
		Uint32 uTargetH = bH ? (((Uint32)uHCompare * 4u) % uLineClocks) : 0u;
		Uint32 uTarget = (((Uint32)uVCompare & 0x1FFu) * uLineClocks +
		                  uTargetH) % uPeriod;
		uDistance = (uTarget + uPeriod - uStart) % uPeriod;
		if (!uDistance) uDistance = uPeriod;
		bTriggered = (uDistance <= uAdvance) ? TRUE : FALSE;
	}

	uEnd = (uStart + uAdvance) % uPeriod;
	m_State.VCounter = (Uint16)(uEnd / uLineClocks);
	m_State.HCounter = uEnd % uLineClocks;

	if (bTriggered)
	{
		m_State.Registers[0x101] |= 0x40;
		UpdateIRQLine();
	}
	m_State.TimerMatch = bTriggered;
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
	ClearIdlePollSleep();
	Uint32 i, uBank;

	SNCPUSetTrap(&m_Cpu, 0, SNCPU_MEM_SIZE, CpuReadTrap, CpuWriteTrap);
	SNCPUSetMemSpeed(&m_Cpu, 0, SNCPU_MEM_SIZE, SNCPU_CYCLE_FAST);

	for (i = 0; i < 4; i++)
		MapRomGroup(i, m_State.Registers[0x020 + i]);

	// ROM/I-RAM/MMIO run at one SA-1 tick; BW-RAM and bitmap accesses take
	// two ticks (~5.37 MHz). SNCpu timing units use FAST=6 for one tick.
	for (uBank = 0; uBank <= 0x3F; uBank++)
	{
		SNCPUSetMemSpeed(&m_Cpu, (uBank << 16) | 0x6000,
		                  0x2000, SNCPU_CYCLE_FAST * 2);
		SNCPUSetMemSpeed(&m_Cpu, ((uBank + 0x80) << 16) | 0x6000,
		                  0x2000, SNCPU_CYCLE_FAST * 2);
	}
	// The SA-1 CPU sees direct BW-RAM through $40-$5F.  The S-CPU only
	// exposes $40-$4F; do not reuse the narrower host-CPU map here.
	SNCPUSetMemSpeed(&m_Cpu, 0x400000, 0x200000, SNCPU_CYCLE_FAST * 2);
	SNCPUSetMemSpeed(&m_Cpu, 0x600000, 0x100000, SNCPU_CYCLE_FAST * 2);

	SNCPUMirror24BitBus(&m_Cpu);
	SNCPUSA1BusTagCpu(&m_Cpu);
}

Uint8 SNSA1::ReadCpuBus(Uint32 uAddr)
{
	Uint8 uBank = (Uint8)(uAddr >> 16);
	Uint16 uLow = (Uint16)uAddr;
	Bool bSystemBank = (uBank <= 0x3F || (uBank >= 0x80 && uBank <= 0xBF));

	if (bSystemBank)
	{
		// I-RAM is decoded over 4 KiB windows but only the lower 2 KiB is
		// implemented.  The upper half reads as zero rather than mirroring.
		if (uLow < 0x1000)
			return (uLow < 0x0800) ? ReadIRAM(uLow) : 0;
		if (uLow >= 0x3000 && uLow <= 0x3FFF)
			return (uLow < 0x3800) ? ReadIRAM(uLow) : 0;
		if (uLow >= 0x2200 && uLow <= 0x23FF)
			return ReadSA1Register(uLow);
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
		if (uLow < 0x1000)
		{
			if (uLow < 0x0800)
				WriteIRAMSA1(uLow, uData);
			return;
		}
		if (uLow >= 0x3000 && uLow <= 0x3FFF)
		{
			if (uLow < 0x3800)
				WriteIRAMSA1(uLow, uData);
			return;
		}
		if (uLow >= 0x2200 && uLow <= 0x23FF)
		{
			WriteSA1Register(uLow, uData);
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

Bool SNSA1::ExecuteCpuC()
{
	m_Cpu.nAbortCycles = 0;
	m_Cpu.bRunning = TRUE;
	SNCPUSetSA1TimingCPU(&m_Cpu);
	SNCPUExecute_C(&m_Cpu);
	SNCPUSetSA1TimingCPU(NULL);
	m_Cpu.bRunning = FALSE;

	if (m_Cpu.nAbortCycles != 0)
	{
		m_Cpu.Cycles = m_Cpu.nAbortCycles;
		m_Cpu.nAbortCycles = 0;
		return FALSE;
	}
	return TRUE;
}

Bool SNSA1::ExecuteCpuFast()
{
#if defined(__mips__)
	m_Cpu.nAbortCycles = 0;
	m_Cpu.bRunning = TRUE;
	UpdateFastMemorySidecars();
	SNCPUSA1BusSetExecCpu(&m_Cpu);
	SNCPUExecute_ASM(&m_Cpu);
	SNCPUSA1BusSetExecCpu(NULL);
	SNCPUSA1BusSetIRAM(NULL);
	m_Cpu.bRunning = FALSE;
	if (m_Cpu.nAbortCycles != 0)
	{
		m_Cpu.Cycles = m_Cpu.nAbortCycles;
		m_Cpu.nAbortCycles = 0;
		return FALSE;
	}
	return TRUE;
#else
	return ExecuteCpuC();
#endif
}

Bool SNSA1::PeekMappedCpuByte(Uint32 uAddr, Uint8 *pValue) const
{
	const SNCpuBankT *pBank;
	uAddr &= 0xFFFFFFu;
	pBank = &m_Cpu.Bank[uAddr >> SNCPU_BANK_SHIFT];
	if (!pValue || !pBank->pMem)
		return FALSE;
	*pValue = pBank->pMem[uAddr];
	return TRUE;
}

void SNSA1::ClearIdlePollSleep()
{
	m_bIdlePollSleeping = FALSE;
	m_uIdlePollIRAM = 0;
	m_uIdlePollValue = 0;
	m_uIdlePollLoopPC = 0;
}

Bool SNSA1::FastForwardSleepingIdle(Uint32 uSA1Cycles)
{
	Int32 nIdleUnits;
	Int32 i;

	if (!m_bIdlePollSleeping)
		return FALSE;

	/* Anything that can change architectural execution wakes the CPU. */
	if (m_State.DMARunning || m_State.ArithmeticPending ||
	    m_State.NMIPending || m_State.TimerMatch ||
	    (m_Cpu.uSignal & (SNCPU_SIGNAL_IRQ | SNCPU_SIGNAL_NMI |
	                      SNCPU_SIGNAL_NMIEDGE | SNCPU_SIGNAL_WAI |
	                      SNCPU_SIGNAL_STP)))
	{
		ClearIdlePollSleep();
		return FALSE;
	}

	/* Shared I-RAM is the wake event.  Resume at the LDA rather than at a
	   cached BEQ/BNE phase so the new byte is observed immediately. */
	if (m_IRAM[m_uIdlePollIRAM] != m_uIdlePollValue)
	{
		m_Cpu.Regs.rPC = m_uIdlePollLoopPC;
		ClearIdlePollSleep();
		return FALSE;
	}

	/* Once latched, do not decode the polling loop on every S-CPU sync.
	   Advance the free-running SA-1 timebase directly. */
	nIdleUnits = (Int32)(uSA1Cycles * SNCPU_CYCLE_FAST);
	for (i = 0; i < SNCPU_COUNTER_NUM; i++)
		m_Cpu.Counter[i] += nIdleUnits;
	m_Cpu.Cycles = 0;
	m_uIdleFastForwardTicks += uSA1Cycles;
	m_uIdleSleepSlices++;
	return TRUE;
}

Bool SNSA1::TryFastForwardIdleLoop()
{
	Uint32 uPC;
	Uint32 uLoopPC;
	Uint32 uTarget;
	Uint16 uDirect;
	Uint16 uIRAM;
	Uint8 uOp0, uArg, uBranch, uRel;
	Uint8 uValue;
	Bool bZero;
	Bool bNegative;
	Bool bBranchTaken;
	Uint32 uSkippedUnits;

	/* This optimization is deliberately narrow: a two-instruction LDA dp /
	   BEQ-or-BNE polling loop in mapped program ROM, reading only SA-1 I-RAM.
	   S-CPU accesses to shared I-RAM synchronize SA-1 first, so while one
	   scheduler slice is executing this byte cannot change externally. */
	if (m_State.DMARunning || m_State.ArithmeticPending ||
	    m_State.NMIPending || m_State.TimerMatch ||
	    (m_Cpu.uSignal & (SNCPU_SIGNAL_IRQ | SNCPU_SIGNAL_NMI |
	                      SNCPU_SIGNAL_NMIEDGE | SNCPU_SIGNAL_WAI |
	                      SNCPU_SIGNAL_STP)))
		return FALSE;

	if (!(m_Cpu.Regs.rP & SNCPU_FLAG_M))
		return FALSE;

	uPC = m_Cpu.Regs.rPC & 0xFFFFFFu;
	if (!PeekMappedCpuByte(uPC, &uOp0))
		return FALSE;

	if (uOp0 == 0xA5)
	{
		/* Slice ended at the load half of: LDA dp / BEQ|BNE loop. */
		uLoopPC = uPC;
		if (!PeekMappedCpuByte((uPC & 0xFF0000u) | ((uPC + 1u) & 0xFFFFu), &uArg) ||
		    !PeekMappedCpuByte((uPC & 0xFF0000u) | ((uPC + 2u) & 0xFFFFu), &uBranch) ||
		    !PeekMappedCpuByte((uPC & 0xFF0000u) | ((uPC + 3u) & 0xFFFFu), &uRel))
			return FALSE;
		if (uBranch != 0xF0 && uBranch != 0xD0)
			return FALSE;
		uTarget = (uPC & 0xFF0000u) |
		          (((uPC + 4u) + (Int8)uRel) & 0xFFFFu);
		if (uTarget != uLoopPC)
			return FALSE;
	}
	else if (uOp0 == 0xF0 || uOp0 == 0xD0)
	{
		/* A full-speed R5900 slice often expires at the branch half instead.
		   Deep diagnostics single-step and happened to move execution back to
		   LDA before the next idle check, which made diagnostic builds faster.
		   Recognize the same loop from either architectural phase. */
		uBranch = uOp0;
		if (!PeekMappedCpuByte((uPC & 0xFF0000u) | ((uPC + 1u) & 0xFFFFu), &uRel))
			return FALSE;
		uLoopPC = (uPC & 0xFF0000u) |
		          (((uPC + 2u) + (Int8)uRel) & 0xFFFFu);
		if (!PeekMappedCpuByte(uLoopPC, &uOp0) || uOp0 != 0xA5 ||
		    !PeekMappedCpuByte((uLoopPC & 0xFF0000u) | ((uLoopPC + 1u) & 0xFFFFu), &uArg))
			return FALSE;
		/* The load must be immediately followed by this exact branch. */
		if (((uLoopPC + 2u) & 0xFFFFu) != (uPC & 0xFFFFu))
			return FALSE;
	}
	else
	{
		return FALSE;
	}

	uDirect = (Uint16)(m_Cpu.Regs.rDP + uArg);
	if (uDirect < 0x0800)
		uIRAM = uDirect;
	else if (uDirect >= 0x3000 && uDirect < 0x3800)
		uIRAM = (Uint16)(uDirect & 0x07FF);
	else
		return FALSE;

	uValue = m_IRAM[uIRAM];
	bZero = (uValue == 0) ? TRUE : FALSE;
	bNegative = (uValue & 0x80) ? TRUE : FALSE;
	bBranchTaken = (uBranch == 0xF0) ? bZero : !bZero;
	if (!bBranchTaken)
		return FALSE;

	/* Only skip when the architectural A/N/Z state already agrees with the
	   polled byte. If the S-CPU changed I-RAM at the sync boundary, this fails
	   and normal execution observes the new value immediately. */
	if ((Uint8)m_Cpu.Regs.rA.w != uValue)
		return FALSE;
	if (((m_Cpu.Regs.rP & SNCPU_FLAG_Z) ? TRUE : FALSE) != bZero)
		return FALSE;
	if (((m_Cpu.Regs.rP & SNCPU_FLAG_N) ? TRUE : FALSE) != bNegative)
		return FALSE;

	uSkippedUnits = (m_Cpu.Cycles > 0) ? (Uint32)m_Cpu.Cycles : 0;
	m_bIdlePollSleeping = TRUE;
	m_uIdlePollIRAM = uIRAM;
	m_uIdlePollValue = uValue;
	m_uIdlePollLoopPC = uLoopPC;
	m_uIdleFastForwardTicks += uSkippedUnits / SNCPU_CYCLE_FAST;
	m_Cpu.Cycles = 0;
	return TRUE;
}

void SNSA1::RunScheduled(Uint32 uSA1Cycles)
{
	Int32 nExecUnits;
	Int32 nGuard;
	Int32 nGuardLimit = 4;
	Uint32 uCpuCycles = uSA1Cycles;

#if SNDBG_DEEP
	if (g_DbgCaptureActive)
		nGuardLimit = (Int32)SNSA1_TRACE_MAX + 4;
#endif

	if (!uSA1Cycles || !m_State.Running)
		return;

	// Normal DMA owns the SA-1 bus and stalls instruction execution.  DMA can
	// touch shared memory, so it always invalidates a latched polling sleep.
	if (m_State.DMARunning)
	{
		ClearIdlePollSleep();
		uCpuCycles = RunDMA(uCpuCycles);
	}

	if (!uCpuCycles || (m_Cpu.uSignal & SNCPU_SIGNAL_STP))
	{
		m_State.ExecutionSlices++;
		m_State.ExecutedCycles += uSA1Cycles;
		return;
	}

	if (FastForwardSleepingIdle(uCpuCycles))
	{
		m_State.ExecutionSlices++;
		m_State.ExecutedCycles += uSA1Cycles;
		return;
	}

	nExecUnits = (Int32)(uCpuCycles * SNCPU_CYCLE_FAST);
	SNCPUAddCycles(&m_Cpu, nExecUnits);

	for (nGuard = 0; nGuard < nGuardLimit && m_Cpu.Cycles > 0; nGuard++)
	{
		ServiceNMI();
		ServiceIRQ();

		if (TryFastForwardIdleLoop())
			break;

		if ((m_Cpu.uSignal & SNCPU_SIGNAL_STP) ||
		    ((m_Cpu.uSignal & SNCPU_SIGNAL_WAI) &&
		     !(m_Cpu.uSignal & SNCPU_SIGNAL_IRQ)))
		{
			m_Cpu.Cycles = 0;
			break;
		}

#if SNDBG_DEEP
		if (g_DbgCaptureActive)
		{
			if (_SA1TraceFrame != g_DbgCaptureFrameNo)
			{
				_SA1TraceFrame = g_DbgCaptureFrameNo;
				_SA1TraceCount = 0;
			}
			if (_SA1TraceCount < SNSA1_TRACE_MAX)
			{
				Uint32 uPC = m_Cpu.Regs.rPC & 0xFFFFFFu;
				SNCpuBankT *pBank = &m_Cpu.Bank[uPC >> SNCPU_BANK_SHIFT];
				Uint8 uOpcode = pBank->pMem ? pBank->pMem[uPC] : 0xFF;
				Int32 nDelta = m_Cpu.Cycles - 1;
				Int32 iCounter;

				DLog("[snes-sa1-op] f=%u n=%u pc=%06X op=%02X a/x/y/s=%04X/%04X/%04X/%04X p/e=%02X/%u cyc=%d irq=%02X dma=%u/%u",
					(unsigned)g_DbgCaptureFrameNo, (unsigned)_SA1TraceCount,
					(unsigned)uPC, (unsigned)uOpcode,
					(unsigned)m_Cpu.Regs.rA.w, (unsigned)m_Cpu.Regs.rX.w,
					(unsigned)m_Cpu.Regs.rY.w, (unsigned)m_Cpu.Regs.rS.w,
					(unsigned)m_Cpu.Regs.rP, (unsigned)m_Cpu.Regs.rE,
					(int)m_Cpu.Cycles, (unsigned)m_Cpu.uSignal,
					(unsigned)m_State.DMARunning,
					(unsigned)m_State.DMARemaining);
				_SA1TraceCount++;

				// Portable equivalent of SNCPUExecuteOne(): temporarily expose one
				// cycle of budget, but call the C interpreter directly so the
				// S-CPU's global MIPS backend selection cannot leak into SA-1.
				m_Cpu.Cycles -= nDelta;
				for (iCounter = 0; iCounter < SNCPU_COUNTER_NUM; iCounter++)
					m_Cpu.Counter[iCounter] -= nDelta;
				ExecuteCpuC();
				m_Cpu.Cycles += nDelta;
				for (iCounter = 0; iCounter < SNCPU_COUNTER_NUM; iCounter++)
					m_Cpu.Counter[iCounter] += nDelta;
				continue;
			}
		}
#endif

		// PS2 executes this independent SA-1 context on the hand-written R5900
		// 65C816 backend; host tests stay on the portable reference core.
		if (ExecuteCpuFast())
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

	// The SA-1 internal clock keeps running while CCNT WAIT/RESET prevents
	// instruction execution.  Keep the independent 65C816 timebase moving so
	// clocked units (notably arithmetic) observe elapsed SA-1 ticks exactly as
	// they do while the core is executing instructions.
	uTotal = (Uint32)nMasterCycles + m_State.MasterRemainder;
	uTicks = uTotal / SNSA1_MASTER_PER_TICK;
	m_State.MasterRemainder = (Uint8)(uTotal % SNSA1_MASTER_PER_TICK);

	if (!m_State.Running)
	{
		if (uTicks)
		{
			Int32 nIdleUnits = (Int32)(uTicks * SNCPU_CYCLE_FAST);
			for (Int32 i = 0; i < SNCPU_COUNTER_NUM; i++)
				m_Cpu.Counter[i] += nIdleUnits;
		}
		return;
	}

	m_State.LastSliceCycles = uTicks;
	m_State.ScheduledCycles += uTicks;

	RunScheduled(uTicks);
}
