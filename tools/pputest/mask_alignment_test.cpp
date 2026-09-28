/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Ai, ChatGPT
 *
 * Description:
 *   Verifies SNMask shifts with the 64-bit-aligned raw rows used by the BG
 *   cache.  Those rows are deliberately not required to be qword-aligned.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "types.h"
#include "snmask.h"

static int g_Failures;

static void CheckMask(const char *pName, Int32 nBits,
	const SNMaskT &Got, const Uint64 *pExpected)
{
	for (Uint32 i = 0; i < 4; i++)
	{
		if (Got.uMask64[i] != pExpected[i])
		{
			std::printf("FAIL %s(%d)[%u]: %016llX != %016llX\n",
				pName, (int)nBits, (unsigned)i,
				(unsigned long long)Got.uMask64[i],
				(unsigned long long)pExpected[i]);
			g_Failures++;
		}
	}
}

int main()
{
	struct RawRowT
	{
		Uint8 uPad[8];
		Uint8 uData[40];
	} _ALIGN(16) Row;
	Uint64 Source[5];
	SNMaskT Got;
	Uint64 Expected[4];

	for (Uint32 i = 0; i < sizeof(Row.uData); i++)
		Row.uData[i] = (Uint8)(i * 37u + 11u);
	std::memcpy(Source, Row.uData, sizeof(Source));

	if (((uintptr_t)Row.uData & 15u) != 8u)
	{
		std::puts("FAIL fixture is not 8-byte aligned / 16-byte unaligned");
		return 1;
	}

	for (Int32 nBits = 0; nBits <= 7; nBits++)
	{
		if (nBits == 0)
		{
			for (Uint32 i = 0; i < 4; i++) Expected[i] = Source[i];
		}
		else
		{
			for (Uint32 i = 0; i < 4; i++)
				Expected[i] = (Source[i] >> nBits) |
					(Source[i + 1] << (64 - nBits));
		}
		SNMaskSHL(&Got, Row.uData, nBits);
		CheckMask("SHL", nBits, Got, Expected);

		if (nBits == 0)
		{
			for (Uint32 i = 0; i < 4; i++) Expected[i] = Source[i];
		}
		else
		{
			Expected[0] = Source[0] << nBits;
			for (Uint32 i = 1; i < 4; i++)
				Expected[i] = (Source[i] << nBits) |
					(Source[i - 1] >> (64 - nBits));
		}
		SNMaskSHR(&Got, Row.uData, nBits);
		CheckMask("SHR", nBits, Got, Expected);
	}

	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
