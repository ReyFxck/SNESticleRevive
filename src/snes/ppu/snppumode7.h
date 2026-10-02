/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snppumode7 interface for SNES picture processing.
 */

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
_INLINE Uint32 SnesPPUMode7TileByteAddress(Int32 x, Int32 y)
{
	return (((Uint32)y >> 3) & 0x7F00u) |
	       (((Uint32)x >> 10) & 0xFEu);
}

_INLINE Uint32 SnesPPUMode7PixelByteOffset(Int32 x, Int32 y)
{
	return (((Uint32)x >> 7) & 0x0Eu) |
	       (((Uint32)y >> 4) & 0x70u) | 1u;
}

_INLINE Uint8 SnesPPUMode7ByteHighBits(Uint64 uData)
{
	Uint32 lo = (Uint32)uData & 0x80808080u;
	Uint32 hi = (Uint32)(uData >> 32) & 0x80808080u;
	return (Uint8)(((lo * 0x00204081u) >> 28) |
		(((hi * 0x00204081u) >> 28) << 4));
}

_INLINE Uint8 SnesPPUMode7RepeatPixel(const Uint8 *pVram,
	Uint32 uTileAddr, Int32 x, Int32 y,
	Uint32 &uLastTileAddr, Uint32 &uChrBase)
{
	if (uTileAddr != uLastTileAddr)
	{
		uChrBase = (Uint32)pVram[uTileAddr] << 7;
		uLastTileAddr = uTileAddr;
	}
	return pVram[uChrBase | SnesPPUMode7PixelByteOffset(x, y)];
}

_INLINE void SnesPPUMode7FetchRepeat(
	Uint8 *pLine, Int32 nPixels, const Uint8 *pVram,
	Int32 x, Int32 y, Int32 dx, Int32 dy)
{
	Uint32 uLastTileAddr = 0xFFFFFFFFu;
	Uint32 uChrBase = 0;

	/* Pack x/y into one 64-bit accumulator. Bias x by a multiple of the
	   1024-pixel wrap period, then compensate the low-word carry of a
	   negative dx in the high-word increment. The guard leaves ample
	   headroom for all 256 signed-16-bit steps; no cross-word overflow can
	   change y. Endpoint equality still proves a shared tile for four dots.
	   Unusual caller ranges retain the scalar path below. */
	if ((Uint32)dx + 32768u <= 65536u && (Uint32)dy + 32768u <= 65536u &&
	    nPixels <= 256 && (Uint32)x + 0x20000000u < 0x40000000u)
	{
		Uint64 xy = ((Uint64)(Uint32)y << 32) | ((Uint32)x + 0x40000000u);
		Uint64 step = ((Uint64)(Uint32)(dy - (dx < 0)) << 32) | (Uint32)dx;
		while (nPixels >= 4)
		{
#if defined(__GNUC__)
			/* Prevent GCC from replacing the induction with __muldi3 on
			   R5900, which has native 64-bit add but no DMULT instruction. */
			__asm__ __volatile__("" : "+r"(xy));
#endif
			Uint64 p1 = xy + step, p2 = p1 + step, p3 = p2 + step;
			Uint32 first = ((xy >> 35) & 0x7F00u) | ((xy >> 10) & 0xFEu);
			Uint32 last = ((p3 >> 35) & 0x7F00u) | ((p3 >> 10) & 0xFEu);
			if (first == last)
			{
				if (first != uLastTileAddr)
				{
					uChrBase = (Uint32)pVram[first] << 7;
					uLastTileAddr = first;
				}
				pLine[0] = pVram[uChrBase | ((xy >> 7) & 0x0Eu) | ((xy >> 36) & 0x70u) | 1u];
				pLine[1] = pVram[uChrBase | ((p1 >> 7) & 0x0Eu) | ((p1 >> 36) & 0x70u) | 1u];
				pLine[2] = pVram[uChrBase | ((p2 >> 7) & 0x0Eu) | ((p2 >> 36) & 0x70u) | 1u];
				pLine[3] = pVram[uChrBase | ((p3 >> 7) & 0x0Eu) | ((p3 >> 36) & 0x70u) | 1u];
			}
			else
			{
				Uint32 tile0 = ((xy >> 35) & 0x7F00u) | ((xy >> 10) & 0xFEu);
				if (tile0 != uLastTileAddr)
				{
					uChrBase = (Uint32)pVram[tile0] << 7;
					uLastTileAddr = tile0;
				}
				pLine[0] = pVram[uChrBase | ((xy >> 7) & 0x0Eu) | ((xy >> 36) & 0x70u) | 1u];
				Uint32 tile1 = ((p1 >> 35) & 0x7F00u) | ((p1 >> 10) & 0xFEu);
				if (tile1 != uLastTileAddr)
				{
					uChrBase = (Uint32)pVram[tile1] << 7;
					uLastTileAddr = tile1;
				}
				pLine[1] = pVram[uChrBase | ((p1 >> 7) & 0x0Eu) | ((p1 >> 36) & 0x70u) | 1u];
				Uint32 tile2 = ((p2 >> 35) & 0x7F00u) | ((p2 >> 10) & 0xFEu);
				if (tile2 != uLastTileAddr)
				{
					uChrBase = (Uint32)pVram[tile2] << 7;
					uLastTileAddr = tile2;
				}
				pLine[2] = pVram[uChrBase | ((p2 >> 7) & 0x0Eu) | ((p2 >> 36) & 0x70u) | 1u];
				Uint32 tile3 = ((p3 >> 35) & 0x7F00u) | ((p3 >> 10) & 0xFEu);
				if (tile3 != uLastTileAddr)
				{
					uChrBase = (Uint32)pVram[tile3] << 7;
					uLastTileAddr = tile3;
				}
				pLine[3] = pVram[uChrBase | ((p3 >> 7) & 0x0Eu) | ((p3 >> 36) & 0x70u) | 1u];

			}
			xy = p3 + step;
			pLine += 4;
			nPixels -= 4;
		}
		x = (Int32)((Uint32)xy - 0x40000000u);
		y = (Int32)(xy >> 32);
	}

	while (nPixels-- > 0)
	{
		/* Select byte-address fields directly from the 24.8 coordinates.
		   Map bytes are even; CHR bytes are odd. The fields do not overlap,
		   so OR replaces the pixel/tile shifts and additions without changing
		   wrapping, fractional bits or the PPU's transform rounding. */
		*pLine++ = SnesPPUMode7RepeatPixel(pVram,
			SnesPPUMode7TileByteAddress(x, y), x, y,
			uLastTileAddr, uChrBase);
		x += dx;
		y += dy;
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
