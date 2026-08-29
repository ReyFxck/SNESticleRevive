#include <cstddef>
#include <cstdio>
#include <cstring>

#include "types.h"
#include "snppu.h"
#include "snppurender.h"

void _DecodeOBJEX(Uint8 *pObjEx, SnesRenderObjT *pObjs, Int32 nObjs,
                  Uint32 uBaseSize);
void _DecodeOBJ(SnesPPUOBJT *pPPUObj, SnesRenderObjT *pObjs, Int32 nObjs,
                Uint8 *pObjY, Uint8 *pObjSize);
Bool _SnesPPUOBJVisibleX(Uint16 uPosX, Uint8 uWidth);
Bool _SnesPPUOBJTileCountedX(Uint16 uObjectX, Int32 iTileX);

static int g_Failures;

static Uint32 NextRandom(Uint32 *pState)
{
	Uint32 x = *pState;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*pState = x;
	return x;
}

static Bool MaskBit(const SNMaskT *pMask, Int32 iX)
{
	return pMask && ((pMask->uMask32[iX >> 5] >> (iX & 31)) & 1u);
}

static void RenderOBJReference(Uint8 *pLine8, const SNMaskT *pLine,
	const SnesRenderObj8T *pObjLine, Int32 nObjLine,
	const SNMaskT *pWindow, const SNMaskT *pMask,
	SNMaskT *pAddSubMask, Bool bAddSubMask)
{
	Uint8 blocked[256];
	Int32 iX;

	for (iX = 0; iX < 256; iX++)
		blocked[iX] = (Uint8)(MaskBit(pWindow, iX) || MaskBit(pMask, iX));

	while (--nObjLine >= 0)
	{
		const SnesRenderObj8T *pObj = pObjLine + nObjLine;
		Uint32 uOpaque = pObj->uData[SNPPU_BGPLANE_OPAQUE];
		Int32 iPixel;

		for (iPixel = 0; iPixel < 8; iPixel++)
		{
			Bool bBGBlocked;
			Uint32 uBit;

			if (!(uOpaque & (1u << iPixel)))
				continue;
			iX = pObj->iPosX + iPixel;
			if ((Uint32)iX >= 256u)
				continue;

			uBit = 1u << (iX & 31);
			switch (pObj->uPri)
			{
			case 0:
				bBGBlocked = MaskBit(&pLine[SNPPU_BGPLANE_LAYER0], iX) ||
				             MaskBit(&pLine[SNPPU_BGPLANE_LAYER1], iX);
				break;
			case 1:
				bBGBlocked = MaskBit(&pLine[SNPPU_BGPLANE_LAYER1], iX);
				break;
			case 2:
				bBGBlocked = MaskBit(&pLine[SNPPU_BGPLANE_LAYER0], iX) &&
				             MaskBit(&pLine[SNPPU_BGPLANE_LAYER1], iX);
				break;
			default:
				bBGBlocked = FALSE;
				break;
			}

			if (blocked[iX])
				continue;
			blocked[iX] = TRUE;
			if (bBGBlocked)
				continue;

			pLine8[iX] = pObj->uData[iPixel];
			if (pAddSubMask)
			{
				if ((bAddSubMask & 1) &&
				    ((pObj->uPal | bAddSubMask) & 0x4))
					pAddSubMask->uMask32[iX >> 5] |= uBit;
				else
					pAddSubMask->uMask32[iX >> 5] &= ~uBit;
			}
		}
	}
}

static void Check(const char *pName, int nGot, int nExpected)
{
    if (nGot != nExpected)
    {
        std::printf("FAIL %s: %d != %d\n", pName, nGot, nExpected);
        g_Failures++;
    }
}

static void CheckFill(const char *pName, const Uint8 *pData, int nBytes,
                      Uint8 uExpected)
{
    int i;
    for (i = 0; i < nBytes; i++)
    {
        if (pData[i] != uExpected)
        {
            std::printf("FAIL %s[%d]: %02X != %02X\n", pName, i,
                        (unsigned)pData[i], (unsigned)uExpected);
            g_Failures++;
            return;
        }
    }
}

static void InitRenderTile(SnesRenderObj8T *pObj, Int32 iPosX)
{
    int i;
    std::memset(pObj, 0, sizeof(*pObj));
    pObj->iPosX = (Int16)iPosX;
    pObj->uPri = 3;
    pObj->uPal = 4;
    for (i = 0; i < 8; i++)
        pObj->uData[i] = (Uint8)(0x40 + i);
    pObj->uData[SNPPU_BGPLANE_OPAQUE] = 0xFF;
}

int main()
{
    SnesRenderObjT objs[4];
    SnesPPUOBJT raw[4];
    Uint8 objEx[1];
    Uint8 objY[4];
    Uint8 objHeight[4];

    std::memset(objs, 0, sizeof(objs));
    std::memset(raw, 0, sizeof(raw));

    // Alterna small/large nos quatro objetos (bits de size 1, 3, 5 e 7).
    objEx[0] = 0x88;
    _DecodeOBJEX(objEx, objs, 4, 6);
    Check("mode 6 small width",  objs[0].uWidth, 16);
    Check("mode 6 small height", objs[0].uHeight, 32);
    Check("mode 6 large width",  objs[1].uWidth, 32);
    Check("mode 6 large height", objs[1].uHeight, 64);

    raw[0].uAttrib = 0x80;
    raw[1].uAttrib = 0x80;
    _DecodeOBJ(raw, objs, 4, objY, objHeight);
    Check("rect small vflip xor",  objs[0].uVXOR, 15);
    Check("rect large vflip xor",  objs[1].uVXOR, 31);
    Check("rect small visibility", objHeight[0], 32);
    Check("rect large visibility", objHeight[1], 64);

    std::memset(objs, 0, sizeof(objs));
    objEx[0] = 0x88;
    _DecodeOBJEX(objEx, objs, 4, 7);
    Check("mode 7 small width",  objs[0].uWidth, 16);
    Check("mode 7 small height", objs[0].uHeight, 32);
    Check("mode 7 large width",  objs[1].uWidth, 32);
    Check("mode 7 large height", objs[1].uHeight, 32);

	Check("x 0 visible",       _SnesPPUOBJVisibleX(0, 8), TRUE);
	Check("x 255 visible",     _SnesPPUOBJVisibleX(255, 8), TRUE);
	Check("x 256 counted",     _SnesPPUOBJVisibleX(256, 8), TRUE);
	Check("x -1 visible",      _SnesPPUOBJVisibleX(511, 8), TRUE);
	Check("x -7 visible",      _SnesPPUOBJVisibleX(505, 8), TRUE);
	Check("x -8 hidden",       _SnesPPUOBJVisibleX(504, 8), FALSE);
	Check("x -31 visible",     _SnesPPUOBJVisibleX(481, 32), TRUE);
	Check("x -32 hidden",      _SnesPPUOBJVisibleX(480, 32), FALSE);
	Check("tile x -7 counted", _SnesPPUOBJTileCountedX(505, -7), TRUE);
	Check("tile x -8 skipped", _SnesPPUOBJTileCountedX(504, -8), FALSE);
	Check("tile x 255 counted", _SnesPPUOBJTileCountedX(255, 255), TRUE);
	Check("tile x 256 skipped", _SnesPPUOBJTileCountedX(257, 256), FALSE);
	Check("object x 256 quirk", _SnesPPUOBJTileCountedX(256, -256), TRUE);
	Check("OBSEL name select 0", _SnesPPUOBJNameSelect(0x00), 0x1000);
	Check("OBSEL name select 1", _SnesPPUOBJNameSelect(0x08), 0x2000);
	Check("OBSEL name select 2", _SnesPPUOBJNameSelect(0x10), 0x3000);
	Check("OBSEL name select 3", _SnesPPUOBJNameSelect(0x18), 0x4000);
	Check("OBSEL ignores size/base", _SnesPPUOBJNameSelect(0xE7), 0x1000);
	Check("normal tile column 0", _SnesPPUOBJSourceColumn(0, 32, FALSE), 0);
	Check("normal tile column 3", _SnesPPUOBJSourceColumn(3, 32, FALSE), 3);
	Check("hflip left fetches right", _SnesPPUOBJSourceColumn(0, 32, TRUE), 3);
	Check("hflip right fetches left", _SnesPPUOBJSourceColumn(3, 32, TRUE), 0);

	/* A faixa recortada deve ser bit-a-bit equivalente ao teste antigo para
	   todo X de 9 bits e todos os tamanhos horizontais do SNES. */
	{
		static const Uint8 widths[] = { 8, 16, 32, 64 };
		Int32 iRawX;
		Int32 iWidth;
		for (iRawX = 0; iRawX < 512; iRawX++)
		{
			Int32 iSignedX = (iRawX & 0x100) ? iRawX - 512 : iRawX;
			for (iWidth = 0; iWidth < 4; iWidth++)
			{
				Int32 iFirst;
				Int32 nCount;
				Int32 iTile;
				Int32 nExpected = 0;
				Int32 iExpectedFirst = widths[iWidth] >> 3;

				_SnesPPUOBJCountedTileRange((Uint16)iRawX, iSignedX,
					widths[iWidth], &iFirst, &nCount);
				for (iTile = 0; iTile < (widths[iWidth] >> 3); iTile++)
				{
					if (_SnesPPUOBJTileCountedX((Uint16)iRawX,
						iSignedX + (iTile << 3)))
					{
						if (!nExpected) iExpectedFirst = iTile;
						nExpected++;
					}
				}
				if (iFirst != iExpectedFirst || nCount != nExpected)
				{
					std::printf("FAIL tile range x=%d width=%u: %d/%d != %d/%d\n",
						iRawX, (unsigned)widths[iWidth], iFirst, nCount,
						iExpectedFirst, nExpected);
					g_Failures++;
				}
			}
		}
	}

    // Tiles parcialmente fora da tela nao podem tocar os buffers vizinhos.
    // Final Fight 2 mantem OBJ em X negativo durante o gameplay.
    {
        Uint8 guardedLine[256 + 16];
        Uint8 *pLine8 = guardedLine + 8;
        SNMaskT planes[SNPPU_BGPLANE_NUM];
        SNMaskT guardedAddSub[3];
        SnesRenderObj8T obj;

        std::memset(guardedLine, 0xCD, sizeof(guardedLine));
        std::memset(pLine8, 0, 256);
        std::memset(planes, 0, sizeof(planes));
        std::memset(guardedAddSub, 0xA5, sizeof(guardedAddSub));
        std::memset(&guardedAddSub[1], 0, sizeof(SNMaskT));
        InitRenderTile(&obj, -7);

        _SnesPPURenderOBJ8(pLine8, planes, &obj, 1, NULL, NULL,
                           &guardedAddSub[1], 1);
        Check("left clip visible pixel", pLine8[0], 0x47);
        Check("left clip next pixel untouched", pLine8[1], 0x00);
        Check("left clip add/sub", guardedAddSub[1].uMask32[0], 0x00000001);
        CheckFill("left line guard before", guardedLine, 8, 0xCD);
        CheckFill("left line guard after", guardedLine + 264, 8, 0xCD);
        CheckFill("left mask guard before", (const Uint8 *)&guardedAddSub[0],
                  sizeof(SNMaskT), 0xA5);
        CheckFill("left mask guard after", (const Uint8 *)&guardedAddSub[2],
                  sizeof(SNMaskT), 0xA5);

        std::memset(guardedLine, 0xCD, sizeof(guardedLine));
        std::memset(pLine8, 0, 256);
        std::memset(planes, 0, sizeof(planes));
        std::memset(guardedAddSub, 0xA5, sizeof(guardedAddSub));
        std::memset(&guardedAddSub[1], 0, sizeof(SNMaskT));
        InitRenderTile(&obj, 255);

        _SnesPPURenderOBJ8(pLine8, planes, &obj, 1, NULL, NULL,
                           &guardedAddSub[1], 1);
        Check("right clip visible pixel", pLine8[255], 0x40);
        Check("right clip previous pixel untouched", pLine8[254], 0x00);
        Check("right clip add/sub", guardedAddSub[1].uMask32[7],
              (int)0x80000000u);
        CheckFill("right line guard before", guardedLine, 8, 0xCD);
        CheckFill("right line guard after", guardedLine + 264, 8, 0xCD);
        CheckFill("right mask guard before", (const Uint8 *)&guardedAddSub[0],
                  sizeof(SNMaskT), 0xA5);
        CheckFill("right mask guard after", (const Uint8 *)&guardedAddSub[2],
                  sizeof(SNMaskT), 0xA5);

        // Prioridade de BG e janela continuam bloqueando somente o pixel alvo.
        std::memset(pLine8, 0, 256);
        std::memset(planes, 0, sizeof(planes));
        std::memset(&guardedAddSub[1], 0, sizeof(SNMaskT));
        InitRenderTile(&obj, 10);
        obj.uPri = 0;
        planes[SNPPU_BGPLANE_LAYER0].uMask32[0] = 1u << 10;
        _SnesPPURenderOBJ8(pLine8, planes, &obj, 1, NULL, NULL,
                           &guardedAddSub[1], 1);
        Check("BG priority blocks first pixel", pLine8[10], 0x00);
        Check("BG priority leaves next pixel", pLine8[11], 0x41);

		// The fast path must split an unaligned tile across adjacent words
		// without changing pixel order or priority masking.
		std::memset(pLine8, 0, 256);
		std::memset(planes, 0, sizeof(planes));
		std::memset(&guardedAddSub[1], 0, sizeof(SNMaskT));
		InitRenderTile(&obj, 29);
		obj.uPri = 0;
		planes[SNPPU_BGPLANE_LAYER0].uMask32[1] = 1u << 1; // x=33
		_SnesPPURenderOBJ8(pLine8, planes, &obj, 1, NULL, NULL,
		                   &guardedAddSub[1], 1);
		Check("word split first pixel", pLine8[29], 0x40);
		Check("word split last low pixel", pLine8[31], 0x42);
		Check("word split first high pixel", pLine8[32], 0x43);
		Check("word split BG priority", pLine8[33], 0x00);
		Check("word split last pixel", pLine8[36], 0x47);
		Check("word split add/sub low", guardedAddSub[1].uMask32[0],
		      (int)0xE0000000u);
		Check("word split add/sub high", guardedAddSub[1].uMask32[1],
		      0x0000001D);
    }

	/* Compara o compositor otimizado com um modelo por pixel independente.
	   Isso cobre prioridades, sobreposicao OBJ, janela, BG3 e add/sub. */
	{
		Uint32 state = 0x4F424A38u;
		Int32 iCase;
		for (iCase = 0; iCase < 1000; iCase++)
		{
			SnesRenderObj8T obj[SNPPU_MAXOBJCHR];
			SNMaskT planes[SNPPU_BGPLANE_NUM];
			SNMaskT window;
			SNMaskT mask;
			SNMaskT addSubGot;
			SNMaskT addSubExpected;
			Uint8 lineGot[256];
			Uint8 lineExpected[256];
			Int32 nObj = (Int32)(NextRandom(&state) %
				(SNPPU_MAXOBJCHR + 1));
			Int32 i;
			const SNMaskT *pWindow = (NextRandom(&state) & 1) ? &window : NULL;
			const SNMaskT *pMask = (NextRandom(&state) & 1) ? &mask : NULL;
			SNMaskT *pAddSubGot = (NextRandom(&state) & 1) ? &addSubGot : NULL;
			SNMaskT *pAddSubExpected = pAddSubGot ? &addSubExpected : NULL;
			static const Uint8 addSubModes[] = { 0, 1, 5 };
			Uint8 uAddSubMode = addSubModes[NextRandom(&state) % 3];

			for (i = 0; i < 8; i++)
			{
				planes[SNPPU_BGPLANE_LAYER0].uMask32[i] = NextRandom(&state);
				planes[SNPPU_BGPLANE_LAYER1].uMask32[i] = NextRandom(&state);
				window.uMask32[i] = NextRandom(&state);
				mask.uMask32[i] = NextRandom(&state);
				addSubGot.uMask32[i] = NextRandom(&state);
			}
			std::memcpy(&addSubExpected, &addSubGot, sizeof(addSubGot));
			for (i = 0; i < 256; i++)
				lineGot[i] = (Uint8)NextRandom(&state);
			std::memcpy(lineExpected, lineGot, sizeof(lineGot));

			for (i = 0; i < nObj; i++)
			{
				Int32 iPixel;
				obj[i].iPosX = (Int16)((Int32)(NextRandom(&state) % 280) - 12);
				obj[i].uPri = (Uint8)(NextRandom(&state) & 3);
				obj[i].uPal = (Uint8)(NextRandom(&state) & 7);
				for (iPixel = 0; iPixel < 8; iPixel++)
					obj[i].uData[iPixel] = (Uint8)NextRandom(&state);
				obj[i].uData[SNPPU_BGPLANE_OPAQUE] = (Uint8)NextRandom(&state);
			}

			RenderOBJReference(lineExpected, planes, obj, nObj,
				pWindow, pMask, pAddSubExpected, uAddSubMode);
			_SnesPPURenderOBJ8(lineGot, planes, obj, nObj,
				pWindow, pMask, pAddSubGot, uAddSubMode);

			if (std::memcmp(lineGot, lineExpected, sizeof(lineGot)) ||
			    (pAddSubGot && std::memcmp(pAddSubGot, pAddSubExpected,
			                              sizeof(*pAddSubGot))))
			{
				std::printf("FAIL randomized OBJ compositor case %d\n", iCase);
				g_Failures++;
				break;
			}
		}
	}

    std::printf(g_Failures ? "FAIL (%d)\n" : "PASS\n", g_Failures);
    return g_Failures ? 1 : 0;
}
