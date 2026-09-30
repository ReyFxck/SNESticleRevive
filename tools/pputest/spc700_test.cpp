/*
 * Host-side SPC700 correctness smoke tests.
 * Cross-checks instruction families that historically diverged from Mesen CE.
 */
#include <cstdio>
#include <cstring>
#include "types.h"
#include "snspc.h"
#include "snspc_c.h"

static int g_Failures;
static Uint8 TestRead(SNSpcT *pSpc, Uint32 uAddr) { return pSpc->Mem[uAddr & 0xFFFF]; }
static void TestWrite(SNSpcT *pSpc, Uint32 uAddr, Uint8 uData) { pSpc->Mem[uAddr & 0xFFFF] = uData; }

static void Check(const char *pName, int nGot, int nExpected)
{
	if (nGot != nExpected) {
		std::printf("FAIL %s: %d != %d\n", pName, nGot, nExpected);
		g_Failures++;
	}
}
static void Init(SNSpcT &cpu)
{
	std::memset(&cpu, 0, sizeof(cpu));
	cpu.Regs.rSP = 0xFF;
	cpu.pReadTrapFunc = TestRead;
	cpu.pWriteTrapFunc = TestWrite;
	cpu.bRomEnable = FALSE;
	cpu.uTestReg = 0x0A;
}
static void PutOp(SNSpcT &cpu, Uint8 op) { cpu.Regs.rPC = 0x0200; cpu.Mem[0x0200] = op; }
static void Run(SNSpcT &cpu, int nSpcCycles) { cpu.Cycles = nSpcCycles * SNSPC_CYCLE; SNSPCExecute_C(&cpu); }
static void PutBitOperand(SNSpcT &cpu, Uint16 addr, Uint8 bit)
{
	Uint16 operand = (addr & 0x1FFF) | ((bit & 7) << 13);
	cpu.Mem[0x0201] = operand & 0xFF;
	cpu.Mem[0x0202] = operand >> 8;
}

int main()
{
	SNSpcT cpu;

	Init(cpu); PutOp(cpu, 0x0A); PutBitOperand(cpu, 0x0300, 3); cpu.Mem[0x0300] = 0x08; Run(cpu, 5);
	Check("OR1 sets carry", cpu.Regs.rPSW & SNSPC_FLAG_C, SNSPC_FLAG_C);
	Init(cpu); PutOp(cpu, 0x2A); PutBitOperand(cpu, 0x0300, 3); cpu.Mem[0x0300] = 0x00; Run(cpu, 5);
	Check("OR1-not sets carry", cpu.Regs.rPSW & SNSPC_FLAG_C, SNSPC_FLAG_C);
	Init(cpu); PutOp(cpu, 0x4A); PutBitOperand(cpu, 0x0300, 3); cpu.Regs.rPSW = SNSPC_FLAG_C; Run(cpu, 4);
	Check("AND1 clears carry", cpu.Regs.rPSW & SNSPC_FLAG_C, 0);
	Init(cpu); PutOp(cpu, 0x6A); PutBitOperand(cpu, 0x0300, 3); cpu.Regs.rPSW = SNSPC_FLAG_C; Run(cpu, 4);
	Check("AND1-not preserves carry", cpu.Regs.rPSW & SNSPC_FLAG_C, SNSPC_FLAG_C);

	Init(cpu); PutOp(cpu, 0x7F); cpu.Regs.rSP = 0xFC;
	cpu.Mem[0x01FD] = SNSPC_FLAG_P | SNSPC_FLAG_C; cpu.Mem[0x01FE] = 0x34; cpu.Mem[0x01FF] = 0x12; Run(cpu, 6);
	Check("RETI PC", cpu.Regs.rPC, 0x1234); Check("RETI PSW", cpu.Regs.rPSW, SNSPC_FLAG_P | SNSPC_FLAG_C);

	Init(cpu); PutOp(cpu, 0xDF); cpu.Regs.rA = 0x9A; Run(cpu, 3);
	Check("DAA result", cpu.Regs.rA, 0x00); Check("DAA carry", cpu.Regs.rPSW & SNSPC_FLAG_C, SNSPC_FLAG_C);
	Check("DAA zero", cpu.Regs.rPSW & SNSPC_FLAG_Z, SNSPC_FLAG_Z);
	Init(cpu); PutOp(cpu, 0xBE); cpu.Regs.rA = 0x00; Run(cpu, 3);
	Check("DAS result", cpu.Regs.rA, 0x9A); Check("DAS carry clear", cpu.Regs.rPSW & SNSPC_FLAG_C, 0);

	Init(cpu); PutOp(cpu, 0x9E); cpu.Regs.rA = 0x00; cpu.Regs.rY = 0x20; cpu.Regs.rX = 0x10; Run(cpu, 12);
	Check("DIV overflow A", cpu.Regs.rA, 0xFF); Check("DIV overflow Y", cpu.Regs.rY, 0x10);
	Check("DIV overflow V", cpu.Regs.rPSW & SNSPC_FLAG_V, SNSPC_FLAG_V);

	Init(cpu); PutOp(cpu, 0x40); cpu.Regs.rPSW = SNSPC_FLAG_I; Run(cpu, 2);
	Check("SETP keeps I", cpu.Regs.rPSW & SNSPC_FLAG_I, SNSPC_FLAG_I);
	Check("SETP sets P", cpu.Regs.rPSW & SNSPC_FLAG_P, SNSPC_FLAG_P);

	Init(cpu); PutOp(cpu, 0xCF); cpu.Regs.rA = 0x10; cpu.Regs.rY = 0x08; Run(cpu, 9);
	Check("MUL A", cpu.Regs.rA, 0x80); Check("MUL Y", cpu.Regs.rY, 0x00);
	Check("MUL zero from Y", cpu.Regs.rPSW & SNSPC_FLAG_Z, SNSPC_FLAG_Z);

	Init(cpu); PutOp(cpu, 0x0E); cpu.Mem[0x0201] = 0x00; cpu.Mem[0x0202] = 0x03; cpu.Regs.rA = 0x0F; cpu.Mem[0x0300] = 0x0F; Run(cpu, 6);
	Check("TSET1 compare zero", cpu.Regs.rPSW & SNSPC_FLAG_Z, SNSPC_FLAG_Z);
	Init(cpu); PutOp(cpu, 0x4E); cpu.Mem[0x0201] = 0x00; cpu.Mem[0x0202] = 0x03; cpu.Regs.rA = 0x0F; cpu.Mem[0x0300] = 0x0F; Run(cpu, 6);
	Check("TCLR1 compare zero", cpu.Regs.rPSW & SNSPC_FLAG_Z, SNSPC_FLAG_Z); Check("TCLR1 memory", cpu.Mem[0x0300], 0x00);

	Init(cpu); PutOp(cpu, 0xBA); cpu.Mem[0x0201] = 0xFF; cpu.Mem[0x00FF] = 0x34; cpu.Mem[0x0000] = 0x12; cpu.Mem[0x0100] = 0x99; Run(cpu, 5);
	Check("DP wrap low", cpu.Regs.rA, 0x34); Check("DP wrap high", cpu.Regs.rY, 0x12);

	/* The SPC700 PC is 16-bit: instruction bytes wrap at $ffff.
	   Check opcode+operand, a two-byte operand, and a negative branch. */
	Init(cpu); cpu.Regs.rPC = 0xFFFF; cpu.Mem[0xFFFF] = 0xE8;
	cpu.Mem[0x0000] = 0x42; Run(cpu, 2);
	Check("fetch8 wraps operand", cpu.Regs.rA, 0x42);
	Check("fetch8 wraps PC", cpu.Regs.rPC, 0x0001);

	Init(cpu); cpu.Regs.rPC = 0xFFFE; cpu.Mem[0xFFFE] = 0x5F;
	cpu.Mem[0xFFFF] = 0x56; cpu.Mem[0x0000] = 0x34; Run(cpu, 3);
	Check("fetch16 wraps operand", cpu.Regs.rPC, 0x3456);

	Init(cpu); cpu.Regs.rPC = 0xFFFF; cpu.Mem[0xFFFF] = 0x2F;
	cpu.Mem[0x0000] = 0x80; Run(cpu, 4);
	Check("branch wraps PC", cpu.Regs.rPC, 0xFF81);

	/* TEST controls APURAM visibility and write permission. */
	Init(cpu); cpu.Mem[0x0300] = 0x12;
	SNSPCWrite8(&cpu, 0x0300, 0x34);
	Check("TEST default RAM writable", cpu.Mem[0x0300], 0x34);
	cpu.uTestReg = SNSPC_TEST_TIMERS_ENABLE; /* RAM writable=0 */
	SNSPCWrite8(&cpu, 0x0300, 0x56);
	Check("TEST RAM write disable", cpu.Mem[0x0300], 0x34);
	cpu.uTestReg = SNSPC_TEST_TIMERS_ENABLE | SNSPC_TEST_RAM_WRITABLE |
		SNSPC_TEST_RAM_DISABLE;
	Check("TEST RAM read disable", SNSPCRead8(&cpu, 0x0300), 0x5A);

	Init(cpu); PutOp(cpu, 0xEF); Run(cpu, 3);
	Check("SLEEP leaves PC after opcode", cpu.Regs.rPC, 0x0201);
	Check("SLEEP state latched", cpu.Regs.uPad & SNSPC_HALT_SLEEP, SNSPC_HALT_SLEEP);
	cpu.Mem[0x0201] = 0xE8; cpu.Mem[0x0202] = 0x77; Run(cpu, 20);
	Check("SLEEP does not execute next opcode", cpu.Regs.rA, 0);
	Check("SLEEP PC remains stable", cpu.Regs.rPC, 0x0201);

	Init(cpu); PutOp(cpu, 0xFF); Run(cpu, 3);
	Check("STOP leaves PC after opcode", cpu.Regs.rPC, 0x0201);
	Check("STOP state latched", cpu.Regs.uPad & SNSPC_HALT_STOP, SNSPC_HALT_STOP);
	Run(cpu, 20);
	Check("STOP PC remains stable", cpu.Regs.rPC, 0x0201);

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
