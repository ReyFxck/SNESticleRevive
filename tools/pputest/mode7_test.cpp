#include <cstdio>

#include "types.h"
#include "snppumode7.h"

static int g_Failures;

static void Check(const char *pName, Int32 nGot, Int32 nExpected)
{
	if (nGot != nExpected)
	{
		std::printf("FAIL %s: %d != %d\n", pName, nGot, nExpected);
		g_Failures++;
	}
}

static Int32 ReferenceSign13(Uint16 uValue)
{
	Int32 nValue = uValue & 0x1FFF;
	return nValue >= 0x1000 ? nValue - 0x2000 : nValue;
}

static Int32 ReferenceClip(Int32 nValue)
{
	return (nValue & 0x2000) ? (nValue | ~0x03FF) : (nValue & 0x03FF);
}

static SnesPPUMode7LineT ReferenceLine(
	Uint16 uHOffset, Uint16 uVOffset, Uint16 uCenterX, Uint16 uCenterY,
	Int16 nA, Int16 nB, Int16 nC, Int16 nD, Uint8 uSelect, Int32 iLine)
{
	SnesPPUMode7LineT Result;
	Int32 nH = ReferenceSign13(uHOffset);
	Int32 nV = ReferenceSign13(uVOffset);
	Int32 nX = ReferenceSign13(uCenterX);
	Int32 nY = ReferenceSign13(uCenterY);
	Int32 nRealY = (uSelect & 2) ? 255 - iLine : iLine;

	Result.x =
		((nA * ReferenceClip(nH - nX)) & ~63) +
		((nB * nRealY) & ~63) +
		((nB * ReferenceClip(nV - nY)) & ~63) + nX * 256;
	Result.y =
		((nC * ReferenceClip(nH - nX)) & ~63) +
		((nD * nRealY) & ~63) +
		((nD * ReferenceClip(nV - nY)) & ~63) + nY * 256;
	Result.dx = nA;
	Result.dy = nC;

	if (uSelect & 1)
	{
		Result.x += Result.dx * 255;
		Result.y += Result.dy * 255;
		Result.dx = -Result.dx;
		Result.dy = -Result.dy;
	}
	return Result;
}

static Uint32 NextRandom(Uint32 *pState)
{
	Uint32 x = *pState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*pState = x;
	return x;
}

int main()
{
	Uint32 uRandom = 0x4D374B91u;
	Int32 i;

	Check("sign13 max", SnesPPUMode7Sign13(0x0FFF), 4095);
	Check("sign13 min", SnesPPUMode7Sign13(0x1000), -4096);
	Check("clip positive", SnesPPUMode7Clip(0x1234), 0x234);
	Check("clip negative", SnesPPUMode7Clip(0x2001), -1023);

	for (i = 0; i < 1000000 && !g_Failures; i++)
	{
		Uint16 uH = (Uint16)NextRandom(&uRandom);
		Uint16 uV = (Uint16)NextRandom(&uRandom);
		Uint16 uX = (Uint16)NextRandom(&uRandom);
		Uint16 uY = (Uint16)NextRandom(&uRandom);
		Int16 nA = (Int16)NextRandom(&uRandom);
		Int16 nB = (Int16)NextRandom(&uRandom);
		Int16 nC = (Int16)NextRandom(&uRandom);
		Int16 nD = (Int16)NextRandom(&uRandom);
		Uint8 uSelect = (Uint8)NextRandom(&uRandom);
		Int32 iLine = (Int32)(NextRandom(&uRandom) % 240);
		SnesPPUMode7LineT Got = SnesPPUMode7MakeLine(
			uH, uV, uX, uY, nA, nB, nC, nD, uSelect, iLine);
		SnesPPUMode7LineT Expected = ReferenceLine(
			uH, uV, uX, uY, nA, nB, nC, nD, uSelect, iLine);

		if (Got.x != Expected.x || Got.y != Expected.y ||
			Got.dx != Expected.dx || Got.dy != Expected.dy)
		{
			std::printf("FAIL randomized Mode 7 transform at vector %d\n", i);
			g_Failures++;
		}
	}

	std::puts(g_Failures ? "FAIL" : "PASS");
	return g_Failures ? 1 : 0;
}
