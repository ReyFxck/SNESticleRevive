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

/*
 * Busca escalar dos pixels do Mode 7.
 *
 * A VRAM nao pode mudar enquanto RenderLine() produz uma scanline. Portanto,
 * pixels consecutivos que caem no mesmo tile 8x8 podem reutilizar o byte do
 * tilemap sem alterar o resultado emulado. As variantes clamp/black tambem
 * evitam leituras cujo valor o PPU descartaria por estar fora de 0..1023.
 */
_INLINE void SnesPPUMode7FetchRepeat(
	Uint8 *pLine, Int32 nPixels, const Uint8 *pVram,
	Int32 x, Int32 y, Int32 dx, Int32 dy)
{
	Uint32 uLastTileAddr = 0xFFFFFFFFu;
	Uint32 uChrBase = 0;

	while (nPixels-- > 0)
	{
		Int32 x2 = (x >> 8) & 0x3FF;
		Int32 y2 = (y >> 8) & 0x3FF;
		Uint32 uTileAddr = ((Uint32)(y2 >> 3) << 7) |
		                       (Uint32)(x2 >> 3);

		x += dx;
		y += dy;

		if (uTileAddr != uLastTileAddr)
		{
			uChrBase = (Uint32)pVram[uTileAddr * 2] << 6;
			uLastTileAddr = uTileAddr;
		}

		Uint32 uChrAddr = uChrBase + (Uint32)(x2 & 7) +
		                  ((Uint32)(y2 & 7) << 3);
		*pLine++ = pVram[uChrAddr * 2 + 1];
	}
}

_INLINE void SnesPPUMode7FetchClamp(
	Uint8 *pLine, Int32 nPixels, const Uint8 *pVram,
	Int32 x, Int32 y, Int32 dx, Int32 dy)
{
	Uint32 uLastTileAddr = 0xFFFFFFFFu;
	Uint32 uCachedChrBase = 0;

	while (nPixels-- > 0)
	{
		Int32 x2 = x >> 8;
		Int32 y2 = y >> 8;
		Uint32 uChrBase;

		x += dx;
		y += dy;

		if ((Uint32)x2 > 0x3FFu || (Uint32)y2 > 0x3FFu)
		{
			/* Character zero is repeated outside the tilemap. */
			uChrBase = 0;
		}
		else
		{
			Uint32 uTileAddr = ((Uint32)(y2 >> 3) << 7) |
			                       (Uint32)(x2 >> 3);
			if (uTileAddr != uLastTileAddr)
			{
				uCachedChrBase = (Uint32)pVram[uTileAddr * 2] << 6;
				uLastTileAddr = uTileAddr;
			}
			uChrBase = uCachedChrBase;
		}

		Uint32 uChrAddr = uChrBase + (Uint32)(x2 & 7) +
		                  ((Uint32)(y2 & 7) << 3);
		*pLine++ = pVram[uChrAddr * 2 + 1];
	}
}

_INLINE void SnesPPUMode7FetchBlack(
	Uint8 *pLine, Int32 nPixels, const Uint8 *pVram,
	Int32 x, Int32 y, Int32 dx, Int32 dy)
{
	Uint32 uLastTileAddr = 0xFFFFFFFFu;
	Uint32 uCachedChrBase = 0;

	while (nPixels-- > 0)
	{
		Int32 x2 = x >> 8;
		Int32 y2 = y >> 8;

		x += dx;
		y += dy;

		if ((Uint32)x2 > 0x3FFu || (Uint32)y2 > 0x3FFu)
		{
			*pLine++ = 0;
		}
		else
		{
			Uint32 uTileAddr = ((Uint32)(y2 >> 3) << 7) |
			                       (Uint32)(x2 >> 3);
			if (uTileAddr != uLastTileAddr)
			{
				uCachedChrBase = (Uint32)pVram[uTileAddr * 2] << 6;
				uLastTileAddr = uTileAddr;
			}

			Uint32 uChrAddr = uCachedChrBase + (Uint32)(x2 & 7) +
			                  ((Uint32)(y2 & 7) << 3);
			*pLine++ = pVram[uChrAddr * 2 + 1];
		}
	}
}

#endif
