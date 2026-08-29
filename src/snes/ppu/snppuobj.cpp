

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "console.h"
#include "snppu.h"
#include "snppurender.h"
#include "rendersurface.h"
#include "snmask.h"
#include "prof.h"
#include "sndbglog.h"



// OBSEL.5-7 escolhe dois tamanhos. Os modos 6/7 sao retangulares e nao
// podem ser representados por um unico shift, como fazia o renderer antigo.
static const Uint8 _SnesPPU_OAMWidth[8][2]=
{
	{ 8, 16}, { 8, 32}, { 8, 64}, {16, 32},
	{16, 64}, {32, 64}, {16, 32}, {16, 32}
};

static const Uint8 _SnesPPU_OAMHeight[8][2]=
{
	{ 8, 16}, { 8, 32}, { 8, 64}, {16, 32},
	{16, 64}, {32, 64}, {32, 64}, {32, 32}
};

Bool _SnesPPUOBJVisibleX(Uint16 uPosX, Uint8 uWidth)
{
	uPosX &= 0x1FF;

	/* X=256 is the hardware's special counted-but-hidden position. Other
	   negative positions count only while at least one pixel reaches x=0. */
	if (uPosX == 0x100)
		return TRUE;

	if (uPosX & 0x100)
		return ((Int32)uPosX - 512) > -(Int32)uWidth;

	return TRUE;
}

Bool _SnesPPUOBJTileCountedX(Uint16 uObjectX, Int32 iTileX)
{
	/* OBJ X=256 is a hardware quirk: its tiles consume the 34-tile budget
	   even though no pixel is visible. A tile ending exactly at x=-1 is the
	   first ordinary off-left tile that counts. */
	return ((uObjectX & 0x1FF) == 0x100) ||
	       (iTileX > -8 && iTileX < 256);
}


#if SNDBG_DEEP
static Uint32 _ObjCountBits8(Uint32 v)
{
	v &= 0xFF;
	v = v - ((v >> 1) & 0x55);
	v = (v & 0x33) + ((v >> 2) & 0x33);
	return (v + (v >> 4)) & 0x0F;
}
#endif


void _SnesPPURenderOBJ8(Uint8 *pLine8, SNMaskT *pLine,
	const SnesRenderObj8T *pObjLine, Int32 nObjLine,
	const SNMaskT *pWindow, const SNMaskT *pMask,
	SNMaskT *pAddSubMask, Bool bAddSubMask)
{
	SNMaskT ObjMask;
	SNMaskT PriorityMask[4];
	Int32 iWord;

	if (nObjLine <= 0)
		return;

	PROF_ENTER("_RenderOBJPlanar");

	/* Os buffers de destino possuem exatamente oito palavras. O renderer
	   antigo criava guardas somente para ObjMask, mas ainda formava ponteiros
	   antes de pLine8/pLine e escrevia pAddSubMask[-1] ou [8] quando um tile
	   cruzava x=0/255. Final Fight 2 usa OBJ em x negativo durante o gameplay.

	   Tiles inteiramente visiveis continuam no caminho de mascara por palavra
	   (o caso quente). Somente os dois recortes de borda usam o caminho por
	   pixel, evitando tanto o acesso fora do buffer quanto uma regressao de
	   desempenho para todos os OBJ da scanline. */
	/* SNMask usa dois qwords MMI no EE. Alem de ser mais curto que oito
	   loads/stores escalares, mantem a mascara inteira alinhada no caminho
	   quente de jogos com muitos OBJ. O recorte seguro continua abaixo. */
	if (pWindow)
		SNMaskCopy(&ObjMask, pWindow);
	else
		SNMaskClear(&ObjMask);
	if (pMask)
		SNMaskOR(&ObjMask, &ObjMask, pMask);

	/* As quatro prioridades usam sempre as mesmas combinacoes dos dois
	   planos de BG. Calcula-las uma vez com MMI evita repetir OR/AND e o
	   switch para cada um dos ate 34 tiles OBJ da scanline. */
	SNMaskOR(&PriorityMask[0], &pLine[SNPPU_BGPLANE_LAYER0],
		&pLine[SNPPU_BGPLANE_LAYER1]);
	SNMaskCopy(&PriorityMask[1], &pLine[SNPPU_BGPLANE_LAYER1]);
	SNMaskAND(&PriorityMask[2], &pLine[SNPPU_BGPLANE_LAYER0],
		&pLine[SNPPU_BGPLANE_LAYER1]);
	SNMaskClear(&PriorityMask[3]);

	while (--nObjLine >= 0)
	{
		const SnesRenderObj8T *pObj = pObjLine + nObjLine;
		Uint32 uOpaque = pObj->uData[SNPPU_BGPLANE_OPAQUE];

		if (!uOpaque || pObj->iPosX <= -8 || pObj->iPosX >= 256)
			continue;

#if SNDBG_DEEP
		g_DbgObjCandidatePixels += _ObjCountBits8(uOpaque);
#endif

		if (pObj->iPosX >= 0 && pObj->iPosX <= 248)
		{
			const SNMaskT *pPriorityMask = &PriorityMask[pObj->uPri];
			Uint32 uShift = pObj->iPosX & 31;
			Uint32 uInvShift = 32 - uShift;
			Uint32 uMask0 = uOpaque << uShift;
			Uint32 uMask1 = uShift ? (uOpaque >> uInvShift) : 0;
			Uint32 uBlocked0;
			Uint32 uBlocked1;
			Uint32 uVisible;
			Uint8 *pDest8 = pLine8 + pObj->iPosX;

			iWord = pObj->iPosX >> 5;
			uBlocked0 = ObjMask.uMask32[iWord];
			uBlocked1 = uMask1 ? ObjMask.uMask32[iWord + 1] : 0;

			/* Um OBJ de maior prioridade impede os seguintes mesmo quando o
			   proprio pixel fica atras de BG, igual ao caminho planar antigo. */
			ObjMask.uMask32[iWord] |= uMask0;
			if (uMask1)
				ObjMask.uMask32[iWord + 1] |= uMask1;

			uBlocked0 |= pPriorityMask->uMask32[iWord];
			if (uMask1)
				uBlocked1 |= pPriorityMask->uMask32[iWord + 1];

			uMask0 &= ~uBlocked0;
			uMask1 &= ~uBlocked1;

			if (pAddSubMask)
			{
				if ((bAddSubMask & 1) && ((pObj->uPal | bAddSubMask) & 0x4))
				{
					pAddSubMask->uMask32[iWord] |= uMask0;
					if (uMask1)
						pAddSubMask->uMask32[iWord + 1] |= uMask1;
				} else
				{
					pAddSubMask->uMask32[iWord] &= ~uMask0;
					if (uMask1)
						pAddSubMask->uMask32[iWord + 1] &= ~uMask1;
				}
			}

			uVisible = uMask0 >> uShift;
			if (uShift)
				uVisible |= uMask1 << uInvShift;
			uVisible &= 0xFF;

#if SNDBG_DEEP
			g_DbgObjDrawnPixels += _ObjCountBits8(uVisible);
#endif
			if (uVisible & 0x01) pDest8[0] = pObj->uData[0];
			if (uVisible & 0x02) pDest8[1] = pObj->uData[1];
			if (uVisible & 0x04) pDest8[2] = pObj->uData[2];
			if (uVisible & 0x08) pDest8[3] = pObj->uData[3];
			if (uVisible & 0x10) pDest8[4] = pObj->uData[4];
			if (uVisible & 0x20) pDest8[5] = pObj->uData[5];
			if (uVisible & 0x40) pDest8[6] = pObj->uData[6];
			if (uVisible & 0x80) pDest8[7] = pObj->uData[7];
		} else
		{
			const SNMaskT *pPriorityMask = &PriorityMask[pObj->uPri];
			Int32 iPixel;
#if SNDBG_DEEP
			g_DbgObjClippedTiles++;
#endif
			for (iPixel = 0; iPixel < 8; iPixel++)
			{
				Int32 iX;
				Uint32 uBit;
				Uint32 uBlocked;

				if (!(uOpaque & (1u << iPixel)))
					continue;

				iX = pObj->iPosX + iPixel;
				if ((Uint32)iX >= 256u)
					continue;

				iWord = iX >> 5;
				uBit = 1u << (iX & 31);
				uBlocked = ObjMask.uMask32[iWord] & uBit;
				ObjMask.uMask32[iWord] |= uBit;

				uBlocked |= pPriorityMask->uMask32[iWord] & uBit;

				if (uBlocked)
					continue;

				if (pAddSubMask)
				{
					if ((bAddSubMask & 1) && ((pObj->uPal | bAddSubMask) & 0x4))
						pAddSubMask->uMask32[iWord] |= uBit;
					else
						pAddSubMask->uMask32[iWord] &= ~uBit;
				}

				pLine8[iX] = pObj->uData[iPixel];
#if SNDBG_DEEP
				g_DbgObjDrawnPixels++;
#endif
			}
		}
	}

	PROF_LEAVE("_RenderOBJPlanar");
}


void _DecodeOBJEX(Uint8 *pObjEx, SnesRenderObjT *pObjs, Int32 nObjs, Uint32 uBaseSize)
{
	uBaseSize &= 7;
	while (nObjs > 0)
	{
		Uint8	uObjEx;
		Uint8  uLarge;

		// fetch obj byte
		uObjEx = *pObjEx++;

		//uObjEx|=0xAA;

		pObjs->uPosX	   = (uObjEx & 1) << 8;
		uObjEx>>=1;
		uLarge = uObjEx & 1;
		pObjs->uWidth  = _SnesPPU_OAMWidth [uBaseSize][uLarge];
		pObjs->uHeight = _SnesPPU_OAMHeight[uBaseSize][uLarge];
		uObjEx>>=1;
		pObjs++;

		pObjs->uPosX	   = (uObjEx & 1) << 8;
		uObjEx>>=1;
		uLarge = uObjEx & 1;
		pObjs->uWidth  = _SnesPPU_OAMWidth [uBaseSize][uLarge];
		pObjs->uHeight = _SnesPPU_OAMHeight[uBaseSize][uLarge];
		uObjEx>>=1;
		pObjs++;

		pObjs->uPosX	   = (uObjEx & 1) << 8;
		uObjEx>>=1;
		uLarge = uObjEx & 1;
		pObjs->uWidth  = _SnesPPU_OAMWidth [uBaseSize][uLarge];
		pObjs->uHeight = _SnesPPU_OAMHeight[uBaseSize][uLarge];
		uObjEx>>=1;
		pObjs++;

		pObjs->uPosX	   = (uObjEx & 1) << 8;
		uObjEx>>=1;
		uLarge = uObjEx & 1;
		pObjs->uWidth  = _SnesPPU_OAMWidth [uBaseSize][uLarge];
		pObjs->uHeight = _SnesPPU_OAMHeight[uBaseSize][uLarge];
		uObjEx>>=1;
		pObjs++;

		nObjs-=4;
	}

}


void _DecodeOBJ(SnesPPUOBJT *pPPUObj, SnesRenderObjT *pObjs, Int32 nObjs, Uint8 *pObjY, Uint8 *pObjSize)
{
	// xxxxxxxx
	// yyyyyyyy
	// CCCCCCCC
	// vhppcccC


	while (nObjs > 0)
	{
		Uint32 uTile;
		Uint8 uAttrib;

		uAttrib = pPPUObj->uAttrib;

		uTile =  pPPUObj->uTile;
		uTile|= ((uAttrib&1)<<8);

		pObjs->uPosX   |= pPPUObj->uX;
		pObjs->uPosY    = pPPUObj->uY + 1;
		pObjs->uPal   = (uAttrib >> 1) & 7;
		pObjs->uPri   = (uAttrib >> 4) & 3;
		pObjs->bHFlip = (uAttrib >> 6) & 1;
		if (uAttrib & 0x80)
		{
			// Nos modos H=2*W, o PPU vira duas metades W x W em vez de
			// espelhar o retangulo inteiro. Isso equivale a XOR com W-1.
			pObjs->uVXOR = pObjs->uWidth - 1;
		} else
		{
			pObjs->uVXOR = 0;
		}

		pObjs->uTile = uTile;

        *pObjY++    = pObjs->uPosY;
        *pObjSize++ = pObjs->uHeight;

		// next obj
		pPPUObj++;
		pObjs++;
		nObjs--;
	}
}



Int32 SnesPPURender::CheckOBJ(SnesRenderObjT *pObjs, Int32 iObj, Int32 nObjs, Uint8 *pObjList, Int32 MaxObjLine, Int32 iLine)
{
	Int32 nObjLine = 0;

	while (nObjs > 0)
	{
		SnesRenderObjT *pObj;
		Uint32 uObjY;

		// get pointer to object
		pObj   = &pObjs[iObj & 0x7F];

		uObjY = iLine - pObj->uPosY;
		uObjY&= 0xFF;

		if (uObjY < pObj->uHeight &&
		    _SnesPPUOBJVisibleX(pObj->uPosX, pObj->uWidth))
		{
			// we got an obj
			*pObjList =  iObj;
			pObjList++;

			nObjLine++;
			if (nObjLine >= MaxObjLine) break;
		}

		
		iObj++;
		nObjs--;
	}

	return nObjLine;
}







Int32 SnesPPURender::CheckOBJ(Uint8 *pObjY, Uint8 *pObjSize, Int32 iObj, Int32 nObjs, Uint8 *pObjList, Int32 MaxObjLine, Int32 iLine)
{
	Int32 nObjLine = 0;

	while (nObjs > 0)
	{
		Uint32 uObjY, uObjSize;

		iObj &= 0x7F;

		// get pointer to object
        uObjSize = pObjSize[iObj];
		uObjY    = pObjY[iObj];

		uObjY = iLine - uObjY;
		uObjY&= 0xFF;

		if (uObjY < uObjSize) 
		{
			// we got an obj
			*pObjList =  iObj;
			pObjList++;

			nObjLine++;
			if (nObjLine >= MaxObjLine) break;
		}

		iObj++;
		nObjs--;
	}

	return nObjLine;
}



Int32 SnesPPURender::CheckOBJ(Uint8 *pObjList, Int32 iLine)
{
    if (iLine >= 0 && iLine < SNPPU_MAXLINE)
    {
        memcpy(pObjList, m_ObjLine[iLine], SNPPU_MAXOBJ);
        return m_nObjLine[iLine];
    } else
    {
        return 0;
    }
}



void SnesPPURender::UpdateOBJVisibility(Uint8 *pObjY, Uint8 *pObjSize, Int32 iObj, Int32 nObjs)
{
    memset(m_nObjLine, 0, sizeof(m_nObjLine));

	while (nObjs > 0)
	{
		Uint32 uObjY, uObjSize;

		iObj &= 0x7F;

		// get pointer to object
        uObjSize = pObjSize[iObj];
		uObjY    = pObjY[iObj];

		if (_SnesPPUOBJVisibleX(m_Objs[iObj].uPosX,
		                           m_Objs[iObj].uWidth))
		while (uObjSize > 0)
        {
            if (uObjY < SNPPU_MAXLINE)
            {
                if (m_nObjLine[uObjY] < SNPPU_MAXOBJ)
                {
                    m_ObjLine[uObjY][m_nObjLine[uObjY]] = (Uint8)iObj;
                    m_nObjLine[uObjY]++;
                }
            }

            uObjY++;
            uObjSize--;
    		uObjY&= 0xFF;
        }

		iObj++;
		nObjs--;
	}
}






void SnesPPURender::UpdateOBJ(Uint8 *pObjY, Uint8 *pObjSize)
{
	SnesOAMT *pOAM = m_pPPU->GetOAM();
	const SnesPPURegsT *pRegs  = m_pPPU->GetRegs();

	// decode objs
	_DecodeOBJEX(pOAM->ObjEx, m_Objs, SNESPPU_OBJ_NUM,
	             (pRegs->obsel >> 5) & 7);
	_DecodeOBJ(pOAM->Objs, m_Objs, SNESPPU_OBJ_NUM, pObjY, pObjSize);
}
