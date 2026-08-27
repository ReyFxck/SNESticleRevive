#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "types.h"
#include "sncpu.h"
#include "sncpu_c.h"
}

static Uint32 g_TrapReadAddress;
static Uint32 g_TrapWriteAddress;
static Uint8 g_TrapWriteData;

extern "C" Uint8 SNCPU_TRAPFUNC TestTrapRead(SNCpuT *, Uint32 address)
{
	g_TrapReadAddress = address;
	return 0xA5;
}

extern "C" void SNCPU_TRAPFUNC TestTrapWrite(SNCpuT *, Uint32 address,
	Uint8 data)
{
	g_TrapWriteAddress = address;
	g_TrapWriteData = data;
}

static bool Check24BitBusWrap(SNCpuT *cpu, Uint8 *memory)
{
	bool ok = true;

	memory[0x0123] = 0x5A;
	ok &= SNCPURead8(cpu, 0x1000123) == 0x5A;
	SNCPUWrite8(cpu, 0x1000123, 0xC3);
	ok &= memory[0x0123] == 0xC3;

	/* The overflow page must also forward I/O/trap accesses with the masked
	   24-bit address, rather than exposing $100:xxxx to the device. */
	SNCPUSetTrap(cpu, 0x2000, SNCPU_BANK_SIZE, TestTrapRead, TestTrapWrite);
	SNCPUMirror24BitBus(cpu);
	g_TrapReadAddress = 0xFFFFFFFF;
	g_TrapWriteAddress = 0xFFFFFFFF;
	g_TrapWriteData = 0;
	ok &= SNCPURead8(cpu, 0x1002345) == 0xA5;
	ok &= g_TrapReadAddress == 0x2345;
	SNCPUWrite8(cpu, 0x1002345, 0x7E);
	ok &= g_TrapWriteAddress == 0x2345 && g_TrapWriteData == 0x7E;

	SNCPUSetBank(cpu, 0x2000, SNCPU_BANK_SIZE, memory + 0x2000, TRUE);
	SNCPUSetMemSpeed(cpu, 0x2000, SNCPU_BANK_SIZE, SNCPU_CYCLE_FAST);
	SNCPUMirror24BitBus(cpu);
	memory[0x0123] = 0;

	std::printf("24-bit bus wrap: %s\n", ok ? "PASS" : "FAIL");
	return ok;
}

static bool CheckInterruptSemantics(SNCpuT *cpu, Uint8 *memory)
{
	bool ok = true;

	/* Emulation NMI: two-byte PC + status, I set, D cleared, 7 cycles. */
	memory[SNCPU_VECTORE_NMI] = 0x34;
	memory[SNCPU_VECTORE_NMI + 1] = 0x12;
	cpu->Regs.rPC = 0x12ABCD;
	cpu->Regs.rS.w = 0x01FF;
	cpu->Regs.rP = SNCPU_FLAG_M | SNCPU_FLAG_X | SNCPU_FLAG_D | SNCPU_FLAG_C;
	cpu->Regs.rE = 1;
	cpu->uSignal = 0;
	cpu->Cycles = 1000;
	SNCPUNMI(cpu);
	ok &= cpu->Regs.rPC == 0x001234;
	ok &= cpu->Regs.rS.w == 0x01FC;
	ok &= memory[0x01FF] == 0xAB && memory[0x01FE] == 0xCD;
	ok &= memory[0x01FD] == (SNCPU_FLAG_M | SNCPU_FLAG_D | SNCPU_FLAG_C);
	ok &= (cpu->Regs.rP & (SNCPU_FLAG_I | SNCPU_FLAG_D)) == SNCPU_FLAG_I;
	ok &= cpu->Cycles == 1000 - (5 * SNCPU_CYCLE_SLOW + 2 * SNCPU_CYCLE_FAST);

	/* Native IRQ: bank + PC + status, 8 cycles. */
	memory[SNCPU_VECTOR_IRQ] = 0x78;
	memory[SNCPU_VECTOR_IRQ + 1] = 0x56;
	cpu->Regs.rPC = 0x56ABCD;
	cpu->Regs.rS.w = 0x0200;
	cpu->Regs.rP = SNCPU_FLAG_D | SNCPU_FLAG_C;
	cpu->Regs.rE = 0;
	cpu->uSignal = SNCPU_SIGNAL_IRQ;
	cpu->Cycles = 1000;
	SNCPUIRQ(cpu);
	ok &= cpu->Regs.rPC == 0x005678;
	ok &= cpu->Regs.rS.w == 0x01FC;
	ok &= memory[0x0200] == 0x56 && memory[0x01FF] == 0xAB;
	ok &= memory[0x01FE] == 0xCD && memory[0x01FD] == (SNCPU_FLAG_D | SNCPU_FLAG_C);
	ok &= (cpu->Regs.rP & (SNCPU_FLAG_I | SNCPU_FLAG_D)) == SNCPU_FLAG_I;
	ok &= cpu->Cycles == 1000 - (6 * SNCPU_CYCLE_SLOW + 2 * SNCPU_CYCLE_FAST);

	/* An asserted but masked IRQ still releases WAI without taking a vector. */
	/* The architectural PC has already advanced past WAI while halted. */
	cpu->Regs.rPC = 0x008001;
	cpu->Regs.rS.w = 0x01AA;
	cpu->Regs.rP = SNCPU_FLAG_M | SNCPU_FLAG_X | SNCPU_FLAG_I;
	cpu->Regs.rE = 1;
	cpu->uSignal = SNCPU_SIGNAL_IRQ | SNCPU_SIGNAL_WAI;
	cpu->Cycles = 1000;
	SNCPUIRQ(cpu);
	ok &= cpu->Regs.rPC == 0x008001;
	ok &= cpu->Regs.rS.w == 0x01AA;
	ok &= !(cpu->uSignal & SNCPU_SIGNAL_WAI);
	ok &= cpu->Cycles == 1000;

	/* Clearing RDNMI or disabling NMI lowers the input line, but must not
	   cancel an edge which the CPU has already latched. */
	cpu->uSignal = 0;
	SNCPUSignalNMI(cpu, 1);
	ok &= (cpu->uSignal & (SNCPU_SIGNAL_NMI | SNCPU_SIGNAL_NMIEDGE)) ==
		(SNCPU_SIGNAL_NMI | SNCPU_SIGNAL_NMIEDGE);
	SNCPUSignalNMI(cpu, 0);
	ok &= !(cpu->uSignal & SNCPU_SIGNAL_NMI);
	ok &= (cpu->uSignal & SNCPU_SIGNAL_NMIEDGE) != 0;

	/* WAI and STP are four-cycle instructions and leave architectural PC
	   pointing at the following opcode.  IRQ wakes WAI without an extra PC
	   increment; STP records a reset-only halt. */
	const Uint32 haltPc = 0x008100;
	memory[haltPc] = 0xCB;
	cpu->Regs.rPC = haltPc;
	cpu->Regs.rP = SNCPU_FLAG_M | SNCPU_FLAG_X | SNCPU_FLAG_I;
	cpu->Regs.rE = 1;
	cpu->uSignal = 0;
	cpu->Cycles = 1000;
	cpu->uTestCycles = 0;
	SNCPUExecuteOne(cpu);
	ok &= cpu->Regs.rPC == haltPc + 1;
	ok &= (cpu->uSignal & SNCPU_SIGNAL_WAI) != 0;
	ok &= cpu->uTestCycles == 4;
	SNCPUSignalIRQ(cpu, 1);
	SNCPUIRQ(cpu);
	ok &= cpu->Regs.rPC == haltPc + 1;
	ok &= !(cpu->uSignal & SNCPU_SIGNAL_WAI);

	memory[haltPc] = 0xDB;
	cpu->Regs.rPC = haltPc;
	cpu->uSignal = 0;
	cpu->Cycles = 1000;
	cpu->uTestCycles = 0;
	SNCPUExecuteOne(cpu);
	ok &= cpu->Regs.rPC == haltPc + 1;
	ok &= (cpu->uSignal & SNCPU_SIGNAL_STP) != 0;
	ok &= cpu->uTestCycles == 4;

	/* An IRQ already asserted while I=1 must be taken immediately after CLI
	   unmasks it, not after the interpreter burns the rest of its scanline. */
	memory[haltPc] = 0x58; /* CLI */
	memory[haltPc + 1] = 0xEA;
	cpu->Regs.rPC = haltPc;
	cpu->Regs.rP = SNCPU_FLAG_M | SNCPU_FLAG_X | SNCPU_FLAG_I;
	cpu->Regs.rE = 1;
	cpu->uSignal = SNCPU_SIGNAL_IRQ;
	cpu->Cycles = 100;
	cpu->uTestCycles = 0;
	SNCPUExecute(cpu);
	ok &= cpu->Regs.rPC == haltPc + 1;
	ok &= !(cpu->Regs.rP & SNCPU_FLAG_I);
	ok &= cpu->Cycles == 100 - 2 * SNCPU_CYCLE_FAST;
	ok &= cpu->uTestCycles == 2;

	/* Aero 2's line-zero V-IRQ must let two polling opcodes retire before
	   the opcode-based core vectors.  The delay is bypassed for an existing
	   WAI and is discarded when the IRQ line is lowered. */
	memory[haltPc] = 0xEA;
	memory[haltPc + 1] = 0xEA;
	memory[haltPc + 2] = 0xEA;
	cpu->Regs.rPC = haltPc;
	cpu->Regs.rP = SNCPU_FLAG_M | SNCPU_FLAG_X;
	cpu->Regs.rE = 1;
	cpu->uSignal = 0;
	cpu->Cycles = 100;
	cpu->uTestCycles = 0;
	SNCPUSetIRQDelay(cpu, 2);
	SNCPUSignalIRQ(cpu, 1);
	ok &= SNCPUExecuteIRQDelay(cpu);
	ok &= cpu->Regs.rPC == haltPc + 1 && cpu->uIrqPending == 1;
	ok &= SNCPUExecuteIRQDelay(cpu);
	ok &= cpu->Regs.rPC == haltPc + 2 && cpu->uIrqPending == 0;
	ok &= !SNCPUExecuteIRQDelay(cpu);

	cpu->uSignal = SNCPU_SIGNAL_IRQ | SNCPU_SIGNAL_WAI;
	SNCPUSetIRQDelay(cpu, 2);
	ok &= !SNCPUExecuteIRQDelay(cpu) && cpu->uIrqPending == 0;
	SNCPUIRQ(cpu);
	ok &= !(cpu->uSignal & SNCPU_SIGNAL_WAI);

	SNCPUSetIRQDelay(cpu, 2);
	SNCPUSignalIRQ(cpu, 0);
	ok &= cpu->uIrqPending == 0;

	memory[SNCPU_VECTORE_NMI] = memory[SNCPU_VECTORE_NMI + 1] = 0;
	memory[SNCPU_VECTOR_IRQ] = memory[SNCPU_VECTOR_IRQ + 1] = 0;
	memory[0x01FD] = memory[0x01FE] = memory[0x01FF] = memory[0x0200] = 0;
	memory[haltPc] = memory[haltPc + 1] = 0;
	cpu->uSignal = 0;
	std::printf("interrupt semantics: %s\n", ok ? "PASS" : "FAIL");
	return ok;
}

static bool CheckBlockMove(SNCpuT *cpu, Uint8 *memory)
{
	bool ok = true;
	const Uint32 pc = 0x008000;

	memory[pc + 0] = 0x54; /* MVN destination, source */
	memory[pc + 1] = 0x02;
	memory[pc + 2] = 0x01;
	memory[0x01FFFE] = 0x11;
	memory[0x01FFFF] = 0x22;
	memory[0x010000] = 0x33;
	cpu->Regs.rPC = pc;
	cpu->Regs.rA.w = 2; /* A+1 bytes */
	cpu->Regs.rX.w = 0xFFFE;
	cpu->Regs.rY.w = 0x0100;
	cpu->Regs.rS.w = 0x01FF;
	cpu->Regs.rP = SNCPU_FLAG_M; /* native, 16-bit indexes */
	cpu->Regs.rE = 0;
	cpu->uSignal = 0;
	cpu->Cycles = 1000;

	for (unsigned i = 0; i < 3; i++)
	{
		cpu->uTestCycles = 0;
		SNCPUExecuteOne(cpu);
		ok &= cpu->uTestCycles == 7;
		ok &= (cpu->Regs.rPC & 0xFFFFFF) == (i == 2 ? pc + 3 : pc);
	}
	ok &= cpu->Regs.rA.w == 0xFFFF;
	ok &= cpu->Regs.rX.w == 0x0001 && cpu->Regs.rY.w == 0x0103;
	ok &= cpu->Regs.rDB == 0x020000;
	ok &= memory[0x020100] == 0x11;
	ok &= memory[0x020101] == 0x22;
	ok &= memory[0x020102] == 0x33;

	memory[pc + 0] = memory[pc + 1] = memory[pc + 2] = 0;
	memory[0x01FFFE] = memory[0x01FFFF] = memory[0x010000] = 0;
	memory[0x020100] = memory[0x020101] = memory[0x020102] = 0;
	std::printf("interruptible MVN: %s\n", ok ? "PASS" : "FAIL");
	return ok;
}

static unsigned ParseNumber(const std::string &text)
{
	return (unsigned)std::strtoul(text.c_str(), NULL, 10);
}

static void Split(const std::string &text, char separator,
	std::vector<std::string> &parts)
{
	std::string part;
	std::istringstream input(text);

	parts.clear();
	while (std::getline(input, part, separator))
		parts.push_back(part);
}

static void LoadRam(Uint8 *memory, const std::string &spec,
	std::vector<Uint32> &touched)
{
	std::vector<std::string> pairs;
	Split(spec, ';', pairs);
	for (size_t i = 0; i < pairs.size(); i++)
	{
		size_t equals = pairs[i].find('=');
		if (equals == std::string::npos)
			continue;
		Uint32 address = ParseNumber(pairs[i].substr(0, equals)) & 0xFFFFFF;
		memory[address] = (Uint8)ParseNumber(pairs[i].substr(equals + 1));
		touched.push_back(address);
	}
}

static bool CheckRam(const Uint8 *memory, const std::string &spec)
{
	std::vector<std::string> pairs;
	Split(spec, ';', pairs);
	for (size_t i = 0; i < pairs.size(); i++)
	{
		size_t equals = pairs[i].find('=');
		if (equals == std::string::npos)
			continue;
		Uint32 address = ParseNumber(pairs[i].substr(0, equals)) & 0xFFFFFF;
		Uint8 expected = (Uint8)ParseNumber(pairs[i].substr(equals + 1));
		if (memory[address] != expected)
			return false;
	}
	return true;
}

static void ClearRam(Uint8 *memory, const std::string &spec)
{
	std::vector<std::string> pairs;
	Split(spec, ';', pairs);
	for (size_t i = 0; i < pairs.size(); i++)
	{
		size_t equals = pairs[i].find('=');
		if (equals != std::string::npos)
			memory[ParseNumber(pairs[i].substr(0, equals)) & 0xFFFFFF] = 0;
	}
}

static std::string TestGroup(const std::string &name)
{
	std::istringstream input(name);
	std::string opcode;
	std::string mode;
	input >> opcode >> mode;
	return opcode + "." + mode;
}

struct TestStats
{
	unsigned Tests;
	unsigned StateFailures;
	unsigned CycleFailures;

	TestStats() : Tests(0), StateFailures(0), CycleFailures(0) {}
};

int main(int argc, char **argv)
{
	const unsigned maxFailures = argc > 1 ? ParseNumber(argv[1]) : 20;
	Uint8 *memory = (Uint8 *)std::calloc(SNCPU_MEM_SIZE, 1);
	SNCpuT cpu;
	std::string line;
	unsigned tests = 0;
	unsigned failures = 0;
	unsigned cycleFailures = 0;
	std::map<std::string, TestStats> stats;

	if (!memory)
		return 2;
	SNCPUNew(&cpu);
	SNCPUSetBank(&cpu, 0, SNCPU_MEM_SIZE, memory, TRUE);
	SNCPUSetMemSpeed(&cpu, 0, SNCPU_MEM_SIZE, SNCPU_CYCLE_FAST);
	SNCPUMirror24BitBus(&cpu);
	SNCPUSetExecuteFunc(SNCPUExecute_C);
	if (!Check24BitBusWrap(&cpu, memory))
	{
		std::free(memory);
		return 1;
	}
	if (!CheckInterruptSemantics(&cpu, memory) || !CheckBlockMove(&cpu, memory))
	{
		std::free(memory);
		return 1;
	}

	while (std::getline(std::cin, line))
	{
		std::vector<std::string> field;
		std::vector<Uint32> touched;
		Split(line, '\t', field);
		if (field.size() != 23 && field.size() != 24)
		{
			std::fprintf(stderr, "bad input: got %u fields\n", (unsigned)field.size());
			return 2;
		}

		LoadRam(memory, field[21], touched);
		cpu.Regs.rPC = (ParseNumber(field[9]) << 16) | ParseNumber(field[1]);
		cpu.Regs.rS.w = (Uint16)ParseNumber(field[2]);
		cpu.Regs.rP = (Uint8)ParseNumber(field[3]);
		cpu.Regs.rA.w = (Uint16)ParseNumber(field[4]);
		cpu.Regs.rX.w = (Uint16)ParseNumber(field[5]);
		cpu.Regs.rY.w = (Uint16)ParseNumber(field[6]);
		cpu.Regs.rDB = ParseNumber(field[7]) << 16;
		cpu.Regs.rDP = (Uint16)ParseNumber(field[8]);
		cpu.Regs.rE = (Uint8)ParseNumber(field[10]);
		cpu.uSignal = 0;
		cpu.Cycles = 1000;
		cpu.uTestCycles = 0;
		SNCPUExecuteOne(&cpu);

		bool ok =
			(cpu.Regs.rPC & 0xFFFF) == ParseNumber(field[11]) &&
			cpu.Regs.rS.w == ParseNumber(field[12]) &&
			cpu.Regs.rP == ParseNumber(field[13]) &&
			cpu.Regs.rA.w == ParseNumber(field[14]) &&
			cpu.Regs.rX.w == ParseNumber(field[15]) &&
			cpu.Regs.rY.w == ParseNumber(field[16]) &&
			((cpu.Regs.rDB >> 16) & 0xFF) == ParseNumber(field[17]) &&
			cpu.Regs.rDP == ParseNumber(field[18]) &&
			((cpu.Regs.rPC >> 16) & 0xFF) == ParseNumber(field[19]) &&
			cpu.Regs.rE == ParseNumber(field[20]) &&
			CheckRam(memory, field[22]);

		const bool cyclesOk = field.size() != 24 ||
			cpu.uTestCycles == ParseNumber(field[23]);
		TestStats &group = stats[TestGroup(field[0])];
		tests++;
		group.Tests++;
		if (!ok)
		{
			failures++;
			group.StateFailures++;
			if (failures <= maxFailures)
			{
				std::printf("FAIL %s got pc=%02X:%04X s=%04X p=%02X a=%04X x=%04X y=%04X db=%02X d=%04X e=%u; expected pc=%02X:%04X s=%04X p=%02X a=%04X x=%04X y=%04X db=%02X d=%04X e=%u\n",
					field[0].c_str(), (unsigned)(cpu.Regs.rPC >> 16) & 0xFF,
					(unsigned)cpu.Regs.rPC & 0xFFFF, (unsigned)cpu.Regs.rS.w,
					(unsigned)cpu.Regs.rP, (unsigned)cpu.Regs.rA.w,
					(unsigned)cpu.Regs.rX.w, (unsigned)cpu.Regs.rY.w,
					(unsigned)(cpu.Regs.rDB >> 16) & 0xFF,
					(unsigned)cpu.Regs.rDP, (unsigned)cpu.Regs.rE,
					ParseNumber(field[19]), ParseNumber(field[11]),
					ParseNumber(field[12]), ParseNumber(field[13]),
					ParseNumber(field[14]), ParseNumber(field[15]),
					ParseNumber(field[16]), ParseNumber(field[17]),
					ParseNumber(field[18]), ParseNumber(field[20]));
			}
		}
		if (!cyclesOk)
		{
			cycleFailures++;
			group.CycleFailures++;
			if (cycleFailures <= maxFailures)
			{
				std::printf("CYCLES %s got=%u expected=%u\n",
					field[0].c_str(), (unsigned)cpu.uTestCycles,
					ParseNumber(field[23]));
			}
		}

		ClearRam(memory, field[21]);
		ClearRam(memory, field[22]);
	}

	for (std::map<std::string, TestStats>::const_iterator it = stats.begin();
		it != stats.end(); ++it)
	{
		if (it->second.StateFailures || it->second.CycleFailures)
		{
			std::printf("SUMMARY %s tests=%u state=%u cycles=%u\n",
				it->first.c_str(), it->second.Tests,
				it->second.StateFailures, it->second.CycleFailures);
		}
	}
	std::printf("CPU tests: %u, state failures: %u, cycle failures: %u\n",
		tests, failures, cycleFailures);
	std::free(memory);
	return (failures || cycleFailures) ? 1 : 0;
}
