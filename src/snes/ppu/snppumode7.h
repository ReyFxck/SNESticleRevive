#ifndef _SNPPUMODE7_H
#define _SNPPUMODE7_H

#include "types.h"

/*
 * Coordenadas 24.8 usadas pelo Mode 7 para o primeiro pixel da linha.
 *
 * O PPU nao faz uma multiplicacao matricial comum: cada produto perde os
 * seis bits inferiores antes da soma.  Jogos que alimentam A/B/C/D por HDMA,
 * como Pilotwings, dependem desse arredondamento em todas as scanlines.
 */
struct SnesPPUMode7LineT
{
	Int32 x;
	Int32 y;
	Int32 dx;
	Int32 dy;
};

_INLINE Int32 SnesPPUMode7Sign13(Uint16 uValue)
{
	Int32 nValue = (Int32)(uValue & 0x1FFFu);
	return (nValue & 0x1000) ? nValue - 0x2000 : nValue;
}

/* Reproduz o recorte interno de 10 bits do PPU, inclusive o bit de sinal. */
_INLINE Int32 SnesPPUMode7Clip(Int32 nValue)
{
	Int32 nLow = nValue & 0x03FF;
	return (nValue & 0x2000) ? nLow - 0x0400 : nLow;
}

_INLINE Int32 SnesPPUMode7Product(Int32 nLeft, Int32 nRight)
{
	return (nLeft * nRight) & ~63;
}

_INLINE SnesPPUMode7LineT SnesPPUMode7MakeLine(
	Uint16 uHOffset, Uint16 uVOffset,
	Uint16 uCenterX, Uint16 uCenterY,
	Int16 nA, Int16 nB, Int16 nC, Int16 nD,
	Uint8 uMode7Select, Int32 iLine)
{
	SnesPPUMode7LineT Line;
	Int32 nHOffset = SnesPPUMode7Sign13(uHOffset);
	Int32 nVOffset = SnesPPUMode7Sign13(uVOffset);
	Int32 nCenterX = SnesPPUMode7Sign13(uCenterX);
	Int32 nCenterY = SnesPPUMode7Sign13(uCenterY);
	Int32 nRealY = (uMode7Select & 0x02) ? 255 - iLine : iLine;

	Line.x =
		SnesPPUMode7Product((Int32)nA,
			SnesPPUMode7Clip(nHOffset - nCenterX)) +
		SnesPPUMode7Product((Int32)nB, nRealY) +
		SnesPPUMode7Product((Int32)nB,
			SnesPPUMode7Clip(nVOffset - nCenterY)) +
		nCenterX * 256;

	Line.y =
		SnesPPUMode7Product((Int32)nC,
			SnesPPUMode7Clip(nHOffset - nCenterX)) +
		SnesPPUMode7Product((Int32)nD, nRealY) +
		SnesPPUMode7Product((Int32)nD,
			SnesPPUMode7Clip(nVOffset - nCenterY)) +
		nCenterY * 256;

	Line.dx = (Int32)nA;
	Line.dy = (Int32)nC;

	if (uMode7Select & 0x01)
	{
		/* Comeca no pixel 255 e caminha para tras. */
		Line.x += Line.dx * 255;
		Line.y += Line.dy * 255;
		Line.dx = -Line.dx;
		Line.dy = -Line.dy;
	}

	return Line;
}

#endif
