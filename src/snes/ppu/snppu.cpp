
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "console.h"
#include "snppu.h"
#include "prof.h"
#include "sntiming.h"
#include "sndebug.h"
#include "sndbglog.h"

#define SNPPU_VERSION_5C77 (0x01)
#define SNPPU_VERSION_5C78 (0x01)

void SnesPPU::WriteCGDATA(Uint8 uData)
{
#if SNDBG_LOG
	g_DbgCGRAMWrites++;
#endif
	Uint32 uCGAddr = (m_Regs.cgadd.w >> 1) &
	                 (SNESPPU_CGRAM_NUM - 1);

	/* $2122 is a latched 15-bit port.  The low byte is not visible in
	   CGRAM until the following high byte commits the complete color. */
	if (!(m_Regs.cgadd.w & 1))
	{
		m_CGRAMLatch = uData;
	}
	else
	{
		Uint16 uColor = (Uint16)m_CGRAMLatch |
		                ((Uint16)(uData & 0x7F) << 8);
		Bool bChanged = m_CGRAM[uCGAddr] != uColor;

		m_CGRAM[uCGAddr] = uColor;
#if SNDBG_LOG
		g_DbgCGRAMCommits++;
		if (!bChanged)
			g_DbgCGRAMUnchanged++;
#endif
		/* Preserve every emulated write/address side effect, but avoid
		   converting and uploading a host palette entry that did not change. */
		if (bChanged)
			m_pRender->UpdateCGRAM(uCGAddr, uColor);
	}

	// The internal byte phase advances after every port access.
	m_Regs.cgadd.w++;
}


Uint8 SnesPPU::ReadCGDATA()
{
	Uint32 uCGAddr;
	Uint8 uData;

	uCGAddr = m_Regs.cgadd.w >> 1;
	uCGAddr&= SNESPPU_CGRAM_NUM-1;
	if (!(m_Regs.cgadd.w&1))
	{
		// lower byte
		uData =  m_CGRAM[uCGAddr] & 0xFF;
	} else
	{
		// upper byte
		uData =  (m_CGRAM[uCGAddr] >> 8);
	}

	// increment color address
	m_Regs.cgadd.w++;

	return uData;
}


static Uint32 _SwizzleVramAddr(Uint32 uVramAddr, Uint32 uFullGraphic)
{
	switch (uFullGraphic)
	{
	case 0:
	default:
		//	0: 0aaaaaaaaaaaaaaa -> 0aaaaaaaaaaaaaaa
		return uVramAddr & 0x7FFF;

	case 1:
		// 32:  0aaaaaaabbbccccc -> 0aaaaaaacccccbbb
		return (uVramAddr & 0x7F00) | ((uVramAddr &0x1F)<<3) | ((uVramAddr>>5) & 0x07);

	case 2:
		//	64:  0aaaaaabbbcccccc -> 0aaaaaaccccccbbb
		return (uVramAddr & 0x7E00) | ((uVramAddr &0x3F)<<3) | ((uVramAddr>>6) & 0x07);

	case 3:
		//	128: 0aaaaabbbccccccc -> 0aaaaacccccccbbb
		return (uVramAddr & 0x7C00) | ((uVramAddr &0x7F)<<3) | ((uVramAddr>>7) & 0x07);
	}
}


void SnesPPU::WriteVMDATAL(Uint8 uData)
{
#if SNDBG_LOG
	g_DbgVRAMWrites++;
#endif
	SnesReg16T *pVram = (SnesReg16T *)m_VRAM;
	Uint32 uVramAddr;

	// calculate vram address
	uVramAddr = _SwizzleVramAddr(m_Regs.vmaddr.w, (m_Regs.vmain >> 2) & 3);

	// set read latch
	m_Regs.vmreadlatch.w  = m_Regs.vmaddr.w;

	// write to vram
	pVram[uVramAddr].b.l = uData;

	// increment vram addr
	m_Regs.vmaddr.w += m_Regs.vminc[0];

	m_pRender->UpdateVRAM(uVramAddr);
}

void SnesPPU::WriteVMDATAH(Uint8 uData)
{
#if SNDBG_LOG
	g_DbgVRAMWrites++;
#endif
	SnesReg16T *pVram = (SnesReg16T *)m_VRAM;
	Uint32 uVramAddr;

	// calculate vram address
	uVramAddr = _SwizzleVramAddr(m_Regs.vmaddr.w, (m_Regs.vmain >> 2) & 3);

	// set read latch
	m_Regs.vmreadlatch.w  = m_Regs.vmaddr.w;

	// write to vram
	pVram[uVramAddr].b.h = uData;

	// increment vram addr
	m_Regs.vmaddr.w += m_Regs.vminc[1];

	m_pRender->UpdateVRAM(uVramAddr);
}



void SnesPPU::WriteVMDATALH(Uint8 uDataL, Uint8 uDataH)
{
#if SNDBG_LOG
	g_DbgVRAMWrites += 2;
#endif
	SnesReg16T *pVram = (SnesReg16T *)m_VRAM;
	Uint32 uVramAddr, uFirstVramAddr;

	// calculate vram address
	uVramAddr = _SwizzleVramAddr(m_Regs.vmaddr.w, (m_Regs.vmain >> 2) & 3);
	uFirstVramAddr = uVramAddr;
	m_Regs.vmreadlatch.w = m_Regs.vmaddr.w;

	// write to vram
	pVram[uVramAddr].b.l = uDataL;

	if (m_Regs.vminc[0])
	{
		// increment vram addr
		m_Regs.vmaddr.w += m_Regs.vminc[0];
		m_Regs.vmreadlatch.w = m_Regs.vmaddr.w;

		// re-calculate vram address
		uVramAddr = _SwizzleVramAddr(m_Regs.vmaddr.w, (m_Regs.vmain >> 2) & 3);
	}

	// write to vram
	pVram[uVramAddr].b.h = uDataH;

	// increment vram addr
	m_Regs.vmaddr.w += m_Regs.vminc[1];

	m_pRender->UpdateVRAM(uFirstVramAddr);
	if (uVramAddr != uFirstVramAddr)
		m_pRender->UpdateVRAM(uVramAddr);
}


void SnesPPU::WriteVMDATABlock(const Uint8 *pData, Int32 nBytes)
{
	/* DMA mode 1 to $2118/$2119 is by far the most common path for tile
	   uploads. With normal address mapping and increment-after-high, each
	   byte pair is one consecutive VRAM word. Copy those words here and
	   invalidate the renderer once for the whole burst instead of making
	   thousands of calls through WriteVMDATALH(). */
	if (nBytes >= 2 &&
	    ((m_Regs.vmain >> 2) & 3) == 0 &&
	    m_Regs.vminc[0] == 0 && m_Regs.vminc[1] == 1)
	{
		Int32 nWords = nBytes >> 1;
		Int32 nWordsLeft = nWords;
		Uint16 uLastAddress = m_Regs.vmaddr.w;
		Uint32 uFirstPhysical = m_Regs.vmaddr.w & 0x7FFF;

		while (nWordsLeft > 0)
		{
			Uint32 uPhysical = m_Regs.vmaddr.w & 0x7FFF;
			Int32 nChunk = 0x8000 - (Int32)uPhysical;
			Int32 iWord;

			if (nChunk > nWordsLeft)
				nChunk = nWordsLeft;

			for (iWord = 0; iWord < nChunk; iWord++)
			{
				m_VRAM[uPhysical + iWord] =
					(Uint16)pData[0] | ((Uint16)pData[1] << 8);
				pData += 2;
			}

			uLastAddress = (Uint16)(m_Regs.vmaddr.w + nChunk - 1);
			m_Regs.vmaddr.w = (Uint16)(m_Regs.vmaddr.w + nChunk);
			nWordsLeft -= nChunk;
		}

		m_Regs.vmreadlatch.w = uLastAddress;
#if SNDBG_LOG
		g_DbgVRAMWrites += nWords * 2;
#endif
		m_pRender->UpdateVRAMRange(uFirstPhysical, (Uint32)nWords);

		nBytes -= nWords * 2;
	}
	else
	{
		while (nBytes >= 2)
		{
			WriteVMDATALH(pData[0], pData[1]);
			pData += 2;
			nBytes -= 2;
		}
	}

	/* An odd DMA length ends on $2118, so preserve the low-port increment
	   and latch behaviour for its final byte. */
	if (nBytes > 0)
		WriteVMDATAL(*pData);
}



Uint8 SnesPPU::ReadVMDATAL()
{
	Uint8 uData;
	SnesReg16T *pVram = (SnesReg16T *)m_VRAM;
	Uint32 uVramAddr;

	// calculate vram address
	uVramAddr = _SwizzleVramAddr(m_Regs.vmreadlatch.w, (m_Regs.vmain >> 2) & 3);

	if (m_Regs.vminc[0])
	{
		m_Regs.vmreadlatch.w  = m_Regs.vmaddr.w;

		// increment vram addr
		m_Regs.vmaddr.w += m_Regs.vminc[0];
	}

	// fetch data from latch
	uData = pVram[uVramAddr].b.l;

	return uData;
}

Uint8 SnesPPU::ReadVMDATAH()
{
	Uint8 uData;
	SnesReg16T *pVram = (SnesReg16T *)m_VRAM;
	Uint32 uVramAddr;

	// calculate vram address
	uVramAddr = _SwizzleVramAddr(m_Regs.vmreadlatch.w, (m_Regs.vmain >> 2) & 3);

	if (m_Regs.vminc[1])
	{
		m_Regs.vmreadlatch.w  = m_Regs.vmaddr.w;

		// increment vram addr
		m_Regs.vmaddr.w += m_Regs.vminc[1];
	}

	// fetch data from latch
	uData = pVram[uVramAddr].b.h;

	return uData;
}




static Uint32 _MapOAMAddress(Uint32 uAddress)
{
	uAddress &= 0x3FF;
	return (uAddress < 0x200) ? uAddress : 0x200 | (uAddress & 0x1F);
}

#if SNDBG_DEEP
static void _TraceOAMAddressWrite(Uint32 uPort, Uint8 uData,
	Uint16 uOldAddress, Uint16 uOldBase, const SnesPPURegsT *pRegs)
{
	static Uint32 s_uFrame = (Uint32)-1;
	static Uint32 s_uWrites = 0;

	if (!g_DbgCaptureActive)
		return;

	if (s_uFrame != g_DbgCaptureFrameNo)
	{
		s_uFrame = g_DbgCaptureFrameNo;
		s_uWrites = 0;
	}

	/* Jogos normalmente escrevem o par uma vez por frame. O limite evita
	   transformar um caso patologico em milhares de linhas de diagnostico. */
	if (s_uWrites < 12)
	{
		DLog("[snes-oam-reg] f=%u port=%04X data=%02X addr=%04X>%04X base=%04X>%04X first=%u",
			(unsigned)g_DbgCaptureFrameNo, (unsigned)uPort,
			(unsigned)uData, (unsigned)uOldAddress,
			(unsigned)pRegs->oamaddr.w, (unsigned)uOldBase,
			(unsigned)pRegs->oamaddrlatch.w,
			(unsigned)pRegs->oampri.w);
	}
	s_uWrites++;
}
#endif

void SnesPPU::UpdateOAMPriority()
{
	Uint16 uOldPriority = m_Regs.oampri.w;

	m_Regs.oampri.w = (m_Regs.oamaddr.w & 0x8000)
		? ((m_Regs.oamaddr.w & 0x1FF) >> 2) : 0;

	if (m_Regs.oampri.w != uOldPriority && m_pRender)
		m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_OBJ);
}

void SnesPPU::WriteOAMDATA(Uint8 uData)
{
#if SNDBG_LOG
	g_DbgOAMWrites++;
#endif
	Uint8	*pOamData = (Uint8 *)&m_OAM;
	Uint32 uAddress = m_Regs.oamaddr.w & 0x3FF;
	Bool bChanged = FALSE;

	/* Low OAM is a 16-bit write port: the even byte is latched and the pair
	   is committed only by the odd byte. High OAM writes immediately and is
	   mirrored every 32 bytes throughout the logical $200-$3ff range. */
	if (!(uAddress & 1))
		m_OAMLatch = uData;

	if (uAddress & 0x200)
	{
		Uint32 uPhysical = _MapOAMAddress(uAddress);
		bChanged = pOamData[uPhysical] != uData;
		pOamData[uPhysical] = uData;
	}
	else if (uAddress & 1)
	{
		Uint32 uEven = uAddress & ~1;
		bChanged = pOamData[uEven] != m_OAMLatch ||
		           pOamData[uAddress] != uData;
		pOamData[uEven] = m_OAMLatch;
		pOamData[uAddress] = uData;
	}

	m_Regs.oamaddr.w = (m_Regs.oamaddr.w & 0x8000) |
	                     ((uAddress + 1) & 0x3FF);
	UpdateOAMPriority();

	if (bChanged)
		m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_OBJ);
}

void SnesPPU::WriteOAMBlock(const Uint8 *pData, Int32 nBytes)
{
	Uint8 *pOamData = (Uint8 *)&m_OAM;
	Uint32 uAddress = m_Regs.oamaddr.w & 0x3FF;
	Bool bChanged = FALSE;

#if SNDBG_LOG
	g_DbgOAMWrites += nBytes;
#endif

	/* OAM is normally refreshed by one 544-byte DMA every frame. Preserve
	   the low-table latch and high-table mirroring exactly, but publish the
	   final address/priority and renderer invalidation only once. */
	while (nBytes-- > 0)
	{
		Uint8 uData = *pData++;

		if (!(uAddress & 1))
			m_OAMLatch = uData;

		if (uAddress & 0x200)
		{
			Uint32 uPhysical = _MapOAMAddress(uAddress);
			if (pOamData[uPhysical] != uData)
				bChanged = TRUE;
			pOamData[uPhysical] = uData;
		}
		else if (uAddress & 1)
		{
			Uint32 uEven = uAddress & ~1;
			if (pOamData[uEven] != m_OAMLatch ||
			    pOamData[uAddress] != uData)
				bChanged = TRUE;
			pOamData[uEven] = m_OAMLatch;
			pOamData[uAddress] = uData;
		}

		uAddress = (uAddress + 1) & 0x3FF;
	}

	m_Regs.oamaddr.w = (m_Regs.oamaddr.w & 0x8000) | uAddress;
	UpdateOAMPriority();
	if (bChanged)
		m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_OBJ);
}

Uint8 SnesPPU::ReadOAMDATA()
{
	Uint8	*pOamData = (Uint8 *)&m_OAM;
	Uint32 uAddress = m_Regs.oamaddr.w & 0x3FF;
	Uint8 uData = pOamData[_MapOAMAddress(uAddress)];

	m_Regs.oamaddr.w = (m_Regs.oamaddr.w & 0x8000) |
	                     ((uAddress + 1) & 0x3FF);
	UpdateOAMPriority();

	return uData;
}


void SnesPPU::UpdateMatMul()
{
	Int32 iMulA, iMulB;
	Int32 iProduct;

	iMulA = (Int16)m_Regs.m7a.w;
	iMulB = (Int16)m_Regs.m7b.w;

	iProduct = iMulA * (iMulB >> 8);

	m_Regs.mpyl = (Uint8)(iProduct  >> 0);
	m_Regs.mpym = (Uint8)(iProduct  >> 8);
	m_Regs.mpyh = (Uint8)(iProduct  >> 16);
}






void SnesPPU::Write8(Uint32 uAddr, Uint8 uData)
{
	//if (uAddr!= 0x2118 && uAddr!= 0x2119 && uAddr!= 0x2122  && uAddr!= 0x2104)
	//ConDebug("write8[%06X]:ppu.%s=%02X\n", uAddr, GetRegName(uAddr), uData);

	switch (uAddr)
	{
	case 0x2100:	// inidisp (screen display)
		m_Regs.inidisp = uData;
		break;

	case 0x2101:	// obsel (oam size)
		if (m_Regs.obsel != uData)
		{
			m_Regs.obsel = uData;
			m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_OBJ);
#if SNDBG_LOG
			// DLog("[snes-obj] OBSEL=%02X base=%d nameSel=%d baseSize=%d",
			// 	(int)uData, (int)(uData & 7), (int)((uData >> 3) & 3), (int)((uData >> 5) & 7));
#endif
		}
		break;

	case 0x2102:	// oamaddl (oam address low)
	{
	#if SNDBG_DEEP
		Uint16 uOldAddress = m_Regs.oamaddr.w;
		Uint16 uOldBase = m_Regs.oamaddrlatch.w;
#endif

		/* $2102/$2103 formam um endereco BASE separado do endereco interno
		   que $2104/$2138 incrementam. Escrever qualquer metade recarrega o
		   endereco interno a partir das duas ultimas metades gravadas. Usar
		   o endereco ja incrementado aqui desloca a OAM quando um jogo muda
		   apenas uma metade; esse e' o caso historico do Final Fight 2. */
		m_Regs.oamaddrlatch.w =
			(m_Regs.oamaddrlatch.w & 0x8200) | ((Uint16)uData << 1);
		m_Regs.oamaddr.w = m_Regs.oamaddrlatch.w;
		m_OAMLatch = 0;
		UpdateOAMPriority();
	#if SNDBG_DEEP
		_TraceOAMAddressWrite(uAddr, uData, uOldAddress, uOldBase, &m_Regs);
#endif
		break;
	}
	case 0x2103:	// oamaddh (oam address high)
	{
	#if SNDBG_DEEP
		Uint16 uOldAddress = m_Regs.oamaddr.w;
		Uint16 uOldBase = m_Regs.oamaddrlatch.w;
#endif

		m_Regs.oamaddrlatch.w =
			(m_Regs.oamaddrlatch.w & 0x01FE) |
			((uData & 0x01) << 9) | ((uData & 0x80) << 8);
		m_Regs.oamaddr.w = m_Regs.oamaddrlatch.w;
		m_OAMLatch = 0;
		UpdateOAMPriority();
	#if SNDBG_DEEP
		_TraceOAMAddressWrite(uAddr, uData, uOldAddress, uOldBase, &m_Regs);
#endif
		break;
	}

	case 0x2104:	// oamdata (oam data)
		WriteOAMDATA(uData);
		break;

	case 0x2105:	// bgmode (screen mode)
        if (m_Regs.bgmode!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_BGSCR | SNESPPURENDER_UPDATE_BGCHR);
		m_Regs.bgmode = uData;
		break;

	case 0x2106:	// mosaic (screen pixelation)
		m_Regs.mosaic = uData;
		break;

	case 0x2107:	// bg1sc (BG1 vram location)
        if (m_Regs.bg1sc!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_BGSCR);
		m_Regs.bg1sc = uData;
		break;
	case 0x2108:	// bg2sc (BG2 vram location)
        if (m_Regs.bg2sc!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_BGSCR);
		m_Regs.bg2sc = uData;
		break;
	case 0x2109:	// bg3sc (BG3 vram location)
        if (m_Regs.bg3sc!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_BGSCR);
		m_Regs.bg3sc = uData;
		break;
	case 0x210A:	// bg4sc (BG4 vram location)
        if (m_Regs.bg4sc!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_BGSCR);
		m_Regs.bg4sc = uData;
		break;
	case 0x210B:	// bg12nba (BG1 & BG2 vram location)
        if (m_Regs.bg12nba !=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_BGCHR);
		m_Regs.bg12nba = uData;
		break;
	case 0x210C:	// bg34nba (BG3 & BG4 vram location)
        if (m_Regs.bg34nba !=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_BGCHR);
		m_Regs.bg34nba = uData;
		break;

	case 0x210D:	// m7hofs + bg1hofs
		/* Mode 7 has its own 13-bit scroll and shares one byte latch with
		   every Mode 7 matrix/centre register. */
		m_Regs.m7hofs.w = ((uData << 8) | m_Regs.m7latch) & 0x1FFF;
		m_Regs.m7latch = uData;
		/* Horizontal BG scroll combines bits 3-7 from the common H/V latch
		   with bits 0-2 from the previous horizontal write. */
		m_Regs.bg1hofs.w = ((uData << 8) |
			(m_Regs.bgofslo & 0xF8) | (m_Regs.bghofslo & 0x07)) & 0x03FF;
		m_Regs.bgofslo = uData;
		m_Regs.bghofslo = uData;
		break;
	case 0x210E:	// m7vofs + bg1vofs
		m_Regs.m7vofs.w = ((uData << 8) | m_Regs.m7latch) & 0x1FFF;
		m_Regs.m7latch = uData;
		m_Regs.bg1vofs.w = ((uData << 8) | m_Regs.bgofslo) & 0x03FF;
		m_Regs.bgofslo = uData;
		break;
	case 0x210F:	// bg2hofs 
		m_Regs.bg2hofs.w = ((uData << 8) |
			(m_Regs.bgofslo & 0xF8) | (m_Regs.bghofslo & 0x07)) & 0x03FF;
		m_Regs.bgofslo = uData;
		m_Regs.bghofslo = uData;
		break;
	case 0x2110:	// bg2vofs 
		m_Regs.bg2vofs.w = ((uData << 8) | m_Regs.bgofslo) & 0x03FF;
		m_Regs.bgofslo = uData;
		break;
	case 0x2111:	// bg3hofs 
		m_Regs.bg3hofs.w = ((uData << 8) |
			(m_Regs.bgofslo & 0xF8) | (m_Regs.bghofslo & 0x07)) & 0x03FF;
		m_Regs.bgofslo = uData;
		m_Regs.bghofslo = uData;
		break;
	case 0x2112:	// bg3vofs 
		m_Regs.bg3vofs.w = ((uData << 8) | m_Regs.bgofslo) & 0x03FF;
		m_Regs.bgofslo = uData;
		break;
	case 0x2113:	// bg4hofs 
		m_Regs.bg4hofs.w = ((uData << 8) |
			(m_Regs.bgofslo & 0xF8) | (m_Regs.bghofslo & 0x07)) & 0x03FF;
		m_Regs.bgofslo = uData;
		m_Regs.bghofslo = uData;
		break;
	case 0x2114:	// bg4vofs 
		m_Regs.bg4vofs.w = ((uData << 8) | m_Regs.bgofslo) & 0x03FF;
		m_Regs.bgofslo = uData;
		break;

	case 0x2115:	// vmain (video port control)
		{
			static Uint8 _SNPPU_VramInc[4]={1,32,128,128};
			//ConDebug("write8[%06X]:ppu.%s=%02X\n", uAddr, GetRegName(uAddr), uData);
			m_Regs.vmain = uData;
			if (uData & 0x80)
			{	// auto-inc on 2119
				m_Regs.vminc[0] = 0;
				m_Regs.vminc[1] = _SNPPU_VramInc[uData & 3];
			} else
			{
				m_Regs.vminc[0] = _SNPPU_VramInc[uData & 3];
				m_Regs.vminc[1] = 0;
			}
		}
		break;

	case 0x2116:	// vmaddl (video port address low)
		m_Regs.vmaddr.b.l = uData;
		m_Regs.vmreadlatch.b.l  = uData;
		break;
	case 0x2117:	// vmaddh (video port address hi)
		m_Regs.vmaddr.b.h = uData;
		m_Regs.vmreadlatch.b.h  = uData;
		break;

	case 0x2118:	// vmdatal (video port data low)
		WriteVMDATAL(uData);
		break;
	case 0x2119:	// vmdatah (video port data hi)
		WriteVMDATAH(uData);
		break;

	case 0x211A:	// m7sel (mode 7 setting)
		m_Regs.m7sel = uData;
		break;

	case 0x211B:	// m7a
		m_Regs.m7a.w = (uData << 8) | m_Regs.m7latch;
		m_Regs.m7latch = uData;
		UpdateMatMul();
		break;
	case 0x211C:	// m7b
		m_Regs.m7b.w = (uData << 8) | m_Regs.m7latch;
		m_Regs.m7latch = uData;
		UpdateMatMul();
		break;
	case 0x211D:	// m7c
		m_Regs.m7c.w = (uData << 8) | m_Regs.m7latch;
		m_Regs.m7latch = uData;
		break;
	case 0x211E:	// m7d
		m_Regs.m7d.w = (uData << 8) | m_Regs.m7latch;
		m_Regs.m7latch = uData;
		break;
	case 0x211F:	// m7x
		m_Regs.m7x.w = (uData << 8) | m_Regs.m7latch;
		m_Regs.m7latch = uData;
		break;
	case 0x2120:	// m7y
		m_Regs.m7y.w = (uData << 8) | m_Regs.m7latch;
		m_Regs.m7latch = uData;
		break;

	case 0x2121:	// cgadd (color address)
		m_Regs.cgadd.w = uData << 1;
		break;
	case 0x2122:	// cgdata (color data)
		WriteCGDATA(uData);
		break;

	case 0x2123:	// window registers
        if (m_Regs.w12sel!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.w12sel = uData;
		break;
	case 0x2124:	
        if (m_Regs.w34sel!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.w34sel = uData;
		break;
	case 0x2125:
        if (m_Regs.wobjsel!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.wobjsel = uData;
		break;
	case 0x2126:	
        if (m_Regs.wh0!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.wh0 = uData;
		break;
	case 0x2127:	
        if (m_Regs.wh1!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.wh1 = uData;
		break;
	case 0x2128:	
        if (m_Regs.wh2!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.wh2 = uData;
		break;
	case 0x2129:	
        if (m_Regs.wh3!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.wh3 = uData;
		break;
	case 0x212A:	
        if (m_Regs.wbglog!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.wbglog = uData;
		break;
	case 0x212B:	
        if (m_Regs.wobjlog!=uData) 
		    m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_WINDOW);
		m_Regs.wobjlog = uData;
		break;

	case 0x212C:	// TM (main screen designation)
		m_Regs.tm = uData;
		break;
	case 0x212D:	// TS (sub screen designation)
		m_Regs.ts = uData;
		break;
	case 0x212E:	// TMW (window mask main screen designation)
		m_Regs.tmw = uData;
		break;
	case 0x212F:	// TSW (window mask sub screen designation)
		m_Regs.tsw = uData;
		break;

	case 0x2130:	// CGWSEL 
		m_Regs.cgwsel = uData;
		break;
	case 0x2131:	// CGADSUB 
		m_Regs.cgadsub = uData;
		break;
	case 0x2132:	// COLDATA 
		//m_Regs.coldata = 0;
		if (uData & 0x20)
		{
			// red
			m_Regs.coldata &= ~(0x1F << 0);
			m_Regs.coldata |= (uData & 0x1F) << 0;
		}

		if (uData & 0x40)
		{
			// green
			m_Regs.coldata &= ~(0x1F << 5);
			m_Regs.coldata |= (uData & 0x1F) << 5;
		}

		if (uData & 0x80)
		{
			// blue
			m_Regs.coldata &= ~(0x1F << 10);
			m_Regs.coldata |= (uData & 0x1F) << 10;
		}
		break;

	case 0x2133:	// SETINI (screen mode)
		m_Regs.setini = uData;
		break;

	case 0x2134:
	    break;

	case 0x2135: //mpym
		break;

	case 0x2139:
		break;
	case 0x213A:
		break;

	default:
		#if SNES_DEBUG
        if (Snes_bDebugUnhandledIO)
            SnesDebugRead(uAddr);
		#endif
		break;

	}
}



/*
Uint8 SnesPPU::Read8(Uint32 uAddr)
{
	switch (uAddr)
	{
	case 0x2137: // slhv
		ConDebug("readppu_slhv\n");
		return 0;
	case 0x213c: // ophct
		return m_Regs.ophct.Read8();

	case 0x213d: // opvct
		return m_Regs.opvct.Read8();

	case 0x213e: // stat77
		return m_Regs.stat77;

	case 0x213f: // stat78
		m_Regs.ophct.Reset();
		m_Regs.opvct.Reset();
		return m_Regs.stat78;
	case 0x2134: //mpyl
		return m_Regs.mpyl;
	case 0x2135: //mpym
		return m_Regs.mpym;
	case 0x2136: //mpyh
		return m_Regs.mpyh;
	default:
		ConDebug("readppu[%06X]\n", uAddr);
	}
	return 0;
}
*/




void SnesPPU::BeginFrame()
{
	m_uLine   = 0;
    m_bVBlank = FALSE;
}

void SnesPPU::EndFrame()
{
    m_bVBlank = TRUE;

	// toggle field
	m_Regs.stat78^=0x80;

    // forced blanking?
    if (!(m_Regs.inidisp & 0x80))
    {
        // reset oam addr to latched value
        // RTYPE-3 needs this
        m_Regs.oamaddr.w = m_Regs.oamaddrlatch.w;
		m_OAMLatch = 0;
		UpdateOAMPriority();
    }
}


void SnesPPU::Sync(Uint32 uLine)
{
	SNQueueElementT *pElement;
#if SNDBG_LOG
	Uint32 uAppliedWrites = 0;
#endif

    // are we rendering?
	if (!m_bVBlank)
	{
		while (m_uLine <= uLine)
		{
			// dequeue all pending writes before this line
			while ( (pElement=m_Queue.Dequeue(m_uLine)) != NULL)
			{
				// perform write
				Write8(pElement->uAddr, pElement->uData);
#if SNDBG_LOG
				uAppliedWrites++;
#endif
			}

            // are we within a frame?
            if (m_uLine > 0 && m_uLine < (224 + 1))
            {
                // render a line
				PROF_ENTER("PPURender");
#if SNDBG_LOG
				Uint32 _tPPU = ProfCtrGetCycle();
				g_DbgPPURenderLines++;
#endif
				m_pRender->RenderLine(m_uLine);;
#if SNDBG_LOG
				g_TmgCycPPU += ProfCtrGetCycle() - _tPPU;
#endif
				PROF_LEAVE("PPURender");
			}

			m_uLine  ++;
		}
	}

	// dequeue all pending writes
	while ( (pElement=m_Queue.Dequeue()) != NULL)
	{
		// perform write
		Write8(pElement->uAddr, pElement->uData);
#if SNDBG_LOG
		uAppliedWrites++;
#endif
	}
#if SNDBG_LOG
	g_DbgPPUAppliedWrites += uAppliedWrites;
#endif
}

void SnesPPU::Reset()
{
	m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_ALL);
	m_Queue.Reset();
	m_uLine = 0;

	memset(&m_Regs, 0, sizeof(m_Regs));
	memset(&m_CGRAM, 0, sizeof(m_CGRAM));
	memset(&m_VRAM, 0, sizeof(m_VRAM));
	memset(&m_OAM, 0, sizeof(m_OAM));
	m_pRender->UpdateVRAMRange(0, SNESPPU_VRAM_NUMWORDS);
	m_OAMLatch = 0;
	m_CGRAMLatch = 0;

	// confirmed:
	m_Regs.stat77 =  SNPPU_VERSION_5C77;
	m_Regs.stat78 =  SNPPU_VERSION_5C78; 
}

SnesPPU::SnesPPU()
{
	m_pRender = NULL;
	m_OAMLatch = 0;
	m_CGRAMLatch = 0;
}

#ifdef SNES_DEBUG

char *SnesPPU::GetRegName(Uint32 uAddr)
{
    switch (uAddr)
    {
	    case 0x2100: return (char *)"inidisp";
	    case 0x2101: return (char *)"obsel";
	    case 0x2102: return (char *)"oamaddl";
	    case 0x2103: return (char *)"oamaddh";
	    case 0x2104: return (char *)"oamdata";
	    case 0x2105: return (char *)"bgmode";
	    case 0x2106: return (char *)"mosaic";
	    case 0x2107: return (char *)"bg1sc";
	    case 0x2108: return (char *)"bg2sc";
	    case 0x2109: return (char *)"bg3sc";
	    case 0x210A: return (char *)"bg4sc";
	    case 0x210B: return (char *)"bg12nba";
	    case 0x210C: return (char *)"bg34nba";
	    case 0x210D: return (char *)"bg1hofs";
	    case 0x210E: return (char *)"bg1vofs";
	    case 0x210F: return (char *)"bg2hofs";
	    case 0x2110: return (char *)"bg2vofs";
	    case 0x2111: return (char *)"bg3hofs";
	    case 0x2112: return (char *)"bg3vofs";
	    case 0x2113: return (char *)"bg4hofs";
	    case 0x2114: return (char *)"bg4vofs";
	    case 0x2115: return (char *)"vmain";
	    case 0x2116: return (char *)"vmaddl";
	    case 0x2117: return (char *)"vmaddh";
	    case 0x2118: return (char *)"vmdatal";
	    case 0x2119: return (char *)"vmdatah";
	    case 0x211A: return (char *)"m7sel";
	    case 0x211B: return (char *)"m7a";
	    case 0x211C: return (char *)"m7b";
	    case 0x211D: return (char *)"m7c";
	    case 0x211E: return (char *)"m7d";
	    case 0x211F: return (char *)"m7x";
	    case 0x2120: return (char *)"m7y";
	    case 0x2121: return (char *)"cgadd";
	    case 0x2122: return (char *)"cgdata";
	    case 0x2123: return (char *)"w12sel";
	    case 0x2124: return (char *)"w34sel";
	    case 0x2125: return (char *)"wobjsel";
	    case 0x2126: return (char *)"wh0";
	    case 0x2127: return (char *)"wh1";
	    case 0x2128: return (char *)"wh2";
	    case 0x2129: return (char *)"wh3";
	    case 0x212A: return (char *)"wbglog";
	    case 0x212B: return (char *)"wobjlog";
	    case 0x212C: return (char *)"tm";
	    case 0x212D: return (char *)"ts";
	    case 0x212E: return (char *)"tmw";
	    case 0x212F: return (char *)"tsw";
	    case 0x2130: return (char *)"cgwsel";
	    case 0x2131: return (char *)"cgadsub";
	    case 0x2132: return (char *)"coldata";
	    case 0x2133: return (char *)"setini";
		default:
			return NULL;
    }
}
#endif
