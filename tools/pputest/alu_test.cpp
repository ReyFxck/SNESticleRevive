/*
 * Host regression tests for the S-CPU 5A22 multiplication/division unit.
 */
#include <cstdio>

#include "types.h"
#include "snio.h"

static int g_Failures;

static void Check(const char *name, unsigned got, unsigned expected)
{
	if (got != expected)
	{
		std::printf("FAIL %s: %u != %u\n", name, got, expected);
		g_Failures++;
	}
}

int main()
{
	SnesIO io;

	io.Reset();
	Check("power-on WRMPYA", io.m_Regs.wrmpya, 0xFF);
	Check("power-on WRDIV", io.m_Regs.wrdiv.w, 0xFFFF);

	io.WriteAlu(0x4202, 3, 0, 6, 6);
	io.WriteAlu(0x4203, 5, 6, 12, 6);
	Check("multiply is not immediate", io.ReadAlu(0x4216, 12), 0);
	Check("multiply first serial step", io.ReadAlu(0x4216, 18), 5);
	Check("multiply final low", io.ReadAlu(0x4216, 60), 15);
	Check("multiply final high", io.ReadAlu(0x4217, 60), 0);

	io.Reset();
	io.WriteAlu(0x4204, 0xE8, 0, 8, 8);
	io.WriteAlu(0x4205, 0x03, 8, 16, 8);
	io.WriteAlu(0x4206, 7, 16, 24, 8);
	Check("division initial quotient", io.ReadAlu(0x4214, 24), 0);
	Check("division 15-cycle quotient", io.ReadAlu(0x4214, 24 + 15 * 8), 71);
	Check("division final quotient low", io.ReadAlu(0x4214, 24 + 16 * 8), 142);
	Check("division final quotient high", io.ReadAlu(0x4215, 24 + 16 * 8), 0);
	Check("division final remainder", io.ReadAlu(0x4216, 24 + 16 * 8), 6);

	io.Reset();
	io.WriteAlu(0x4204, 0x34, 0, 6, 6);
	io.WriteAlu(0x4205, 0x12, 6, 12, 6);
	io.WriteAlu(0x4206, 0, 12, 18, 6);
	Check("div0 quotient low", io.ReadAlu(0x4214, 18 + 16 * 6), 0xFF);
	Check("div0 quotient high", io.ReadAlu(0x4215, 18 + 16 * 6), 0xFF);
	Check("div0 remainder low", io.ReadAlu(0x4216, 18 + 16 * 6), 0x34);
	Check("div0 remainder high", io.ReadAlu(0x4217, 18 + 16 * 6), 0x12);

	std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
	return g_Failures ? 1 : 0;
}
