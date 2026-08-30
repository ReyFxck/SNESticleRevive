/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snmemmap behavior for the SNES emulation core.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "snes.h"
#include "console.h"
#include "snmemmap.h"
#include "sndebug.h"
#include "sndbglog.h"
#include "file.h"

static SnesMemMapT	_SnesMemMap_LoRom[]=
{
	// map slow rom (estendido $00-$7D para cobrir 4MB; $7E-$7F e' WRAM)
	{0x00, 0x7D, 0x8000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_ROM},

	// map fast rom (estendido $80-$FF: cobre os 4MB inteiros via FastROM;
	// o wrap usa o tamanho real da ROM, entao ROMs menores continuam
	// espelhando certo)
	{0x80, 0xFF, 0x8000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_ROM},

	// mirror rom in lower 32k: a metade baixa ($0000-7FFF) de cada banco
	// espelha a metade ALTA ($8000-FFFF) do MESMO banco (LoROM). O offset
	// inicial 0x200000 faz o banco $40/$C0 casar com o offset da metade
	// alta (0x40*0x8000 = 0x200000). Sem isso, ROMs grandes (>~2MB) liam
	// dados do offset 0 -> graficos/niveis embaralhados (ex.: SMW 4MB
	// "12 Magic Orbs"). Em ROMs pequenas o wrap escondia o bug.
	{0x40, 0x6F, 0x0000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_ROM, 0x200000},
	{0xC0, 0xFF, 0x0000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_ROM, 0x200000},

	// map sram areas
	{0x70, 0x77, 0x0000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},

	// map ram
	{0x7E, 0x7F, 0x0000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_RAM},

	// map lo-ram / ppu areas
	{0x00, 0x3F, 0x0000, 0x1FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_LORAM},
	{0x00, 0x3F, 0x2000, 0x3FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU0},
	{0x00, 0x3F, 0x4000, 0x5FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU1},

	{0x80, 0xBF, 0x0000, 0x1FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_LORAM},
	{0x80, 0xBF, 0x2000, 0x3FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU0},
	{0x80, 0xBF, 0x4000, 0x5FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU1},

	{0, 0, 0, 0, SNESMEM_TYPE_NONE}
};

static SnesMemMapT	_SnesMemMap_HiRom[]=
{
	// map slow rom
	{0x00, 0x3F, 0x0000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_ROM},
	{0x40, 0x6F, 0x0000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_ROM},

	// map fast rom
	{0x80, 0xBF, 0x0000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_ROM},
	{0xC0, 0xFF, 0x0000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_ROM},

	// map sram areas
	{0x70, 0x77, 0x0000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0x00, 0x0F, 0x6000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0x10, 0x1F, 0x6000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0x20, 0x2F, 0x6000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0x30, 0x3F, 0x6000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0x80, 0x8F, 0x6000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0x90, 0x9F, 0x6000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0xA0, 0xAF, 0x6000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0xB0, 0xBF, 0x6000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},

	// map ram
	{0x7E, 0x7F, 0x0000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_RAM},

	// map lo-ram / ppu areas
	{0x00, 0x3F, 0x0000, 0x1FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_LORAM},
	{0x00, 0x3F, 0x2000, 0x3FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU0},
	{0x00, 0x3F, 0x4000, 0x5FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU1},

	{0x80, 0xBF, 0x0000, 0x1FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_LORAM},
	{0x80, 0xBF, 0x2000, 0x3FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU0},
	{0x80, 0xBF, 0x4000, 0x5FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU1},

	{0, 0, 0, 0, SNESMEM_TYPE_NONE}
};

#if SNES_DSP1
static SnesMemMapT	_SnesMemMap_HiRom_DSP1[]=
{
	{0x00, 0x1F, 0x6000, 0x7FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_DSP1},
	{0x80, 0x9F, 0x6000, 0x7FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_DSP1},

	{0, 0, 0, 0, SNESMEM_TYPE_NONE}
};
#endif

#if SNES_DSP1
static SnesMemMapT _SnesMemMap_LoRom_DSP1[]={
    {0x20,0x3F,0x8000,0xBFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0xA0,0xBF,0x8000,0xBFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    /* DR mirror $C0-$CF (regiao FastROM): jogos como Super Mario Kart
       apontam o HDMA do Mode-7 para o espelho do DSP em $C0-$CF.
       Sem isto o HDMA le ROM em vez do registrador de dados do DSP e a
       matriz Mode-7 vira lixo -> pista achatada (mas a CPU, que usa
       $30-$3F, funciona, por isso o jogo "roda" mesmo assim). Veja o
       mapa de memoria do DSP-1 LoROM ($30-$3F / $C0-$CF). */
    {0xC0,0xCF,0x8000,0xBFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    /* SR (Status Register): $20-$3F:C000-FFFF + mirror $A0-$BF + $C0-$CF
       Sem isto a CPU le ROM em vez do status e o DSP-1 trava */
    {0x20,0x3F,0xC000,0xFFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0xA0,0xBF,0xC000,0xFFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0xC0,0xCF,0xC000,0xFFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0,0,0,0,SNESMEM_TYPE_NONE}
};

// DSP-4 decodes only $30-$3F/$B0-$BF:$8000-$FFFF. Each row is one
// 8 KiB CPU page because MapMem clamps trapped devices to SNCPU_BANK_SIZE.
static SnesMemMapT _SnesMemMap_LoRom_DSP4[]={
    {0x30,0x3F,0x8000,0x9FFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0x30,0x3F,0xA000,0xBFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0x30,0x3F,0xC000,0xDFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0x30,0x3F,0xE000,0xFFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0xB0,0xBF,0x8000,0x9FFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0xB0,0xBF,0xA000,0xBFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0xB0,0xBF,0xC000,0xDFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0xB0,0xBF,0xE000,0xFFFF,SNCPU_CYCLE_FAST,SNESMEM_TYPE_DSP1},
    {0,0,0,0,SNESMEM_TYPE_NONE}
};
#endif

// OBC1 (Metal Combat): 8KB de RAM + registradores em $6000-$7FFF,
// nos bancos LoROM $00-$3F e espelho FastROM $80-$BF.
static SnesMemMapT _SnesMemMap_OBC1[]={
    {0x00,0x3F,0x6000,0x7FFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_OBC1},
    {0x80,0xBF,0x6000,0x7FFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_OBC1},
    {0,0,0,0,SNESMEM_TYPE_NONE}
};

// CX4 (Mega Man X2/X3): C4RAM + registradores em $6000-$7FFF, nos bancos
// LoROM $00-$3F e espelho FastROM $80-$BF.
static SnesMemMapT _SnesMemMap_CX4[]={
    {0x00,0x3F,0x6000,0x7FFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_CX4},
    {0x80,0xBF,0x6000,0x7FFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_CX4},
    {0,0,0,0,SNESMEM_TYPE_NONE}
};

// SuperFX Game Pak RAM. Diferente da SRAM LoROM comum, os bancos $70-$71
// inteiros sao RAM; o tamanho fisico (normalmente 32/64 KiB) espelha dentro
// destes 128 KiB de espaco.
static SnesMemMapT _SnesMemMap_SuperFXRAM[]={
    {0x70,0x71,0x0000,0xFFFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_SRAM},
    {0,0,0,0,SNESMEM_TYPE_NONE}
};

// GSU1 acrescenta espelhos altos de Game Pak RAM em $F0-$F1.
static SnesMemMapT _SnesMemMap_SuperFXGSU1RAM[]={
    {0x70,0x71,0x0000,0xFFFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_SRAM},
    {0xF0,0xF1,0x0000,0xFFFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_SRAM},
    {0,0,0,0,SNESMEM_TYPE_NONE}
};

// O Mario Chip 1 do Star Fox usa uma decodificacao anterior: os 32 KiB de
// Game Pak RAM aparecem, espelhados, em todos os bancos $60-$7D/$E0-$FF.
// Sem este mapa o 65816 le ROM/open-bus no lugar do framebuffer/work RAM e o
// jogo para logo no boot. Os GSU1/GSU2 posteriores usam $70-$71.
static SnesMemMapT _SnesMemMap_SuperFXMC1RAM[]={
    {0x60,0x7D,0x0000,0xFFFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_SRAM},
    {0xE0,0xFF,0x0000,0xFFFF,SNCPU_CYCLE_SLOW,SNESMEM_TYPE_SRAM},
    {0,0,0,0,SNESMEM_TYPE_NONE}
};

enum
{
    SNES_SUPERFX_BOARD_MC1 = 1,
    SNES_SUPERFX_BOARD_GSU1,
    SNES_SUPERFX_BOARD_GSU2
};

/* Os headers nao identificam a revisao do GSU. A lista de cartuchos e' fixa,
   entao o titulo interno distingue MC1/GSU1/GSU2 sem depender do nome do ZIP.
   Desconhecidos/homebrews usam o mapa GSU2, que e' o mais novo. */
static Int32 _SnesSuperFXBoardType(const Char *pTitle)
{
    Char compact[22];
    Int32 n = 0;
    if (!pTitle) return SNES_SUPERFX_BOARD_GSU2;

    while (*pTitle && n < 21)
    {
        Char c = *pTitle++;
        if (c >= 'a' && c <= 'z') c = (Char)(c - ('a' - 'A'));
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            compact[n++] = c;
    }
    compact[n] = 0;

    // Star Fox 2 e' a excecao de 1 MiB que ja usa GSU2.
    if (n >= 8 && !memcmp(compact, "STARFOX2", 8))
        return SNES_SUPERFX_BOARD_GSU2;

    if ((n >= 7 && !memcmp(compact, "STARFOX", 7)) ||
        (n >= 8 && !memcmp(compact, "STARWING", 8)))
        return SNES_SUPERFX_BOARD_MC1;

    if ((n >= 9  && !memcmp(compact, "DIRTRACER", 9)) ||
        (n >= 10 && !memcmp(compact, "DIRTTRAXFX", 10)) ||
        (n >= 10 && !memcmp(compact, "POWERSLIDE", 10)) ||
        (n >= 11 && !memcmp(compact, "STUNTRACEFX", 11)) ||
        (n >= 8  && !memcmp(compact, "WILDTRAX", 8)) ||
        (n >= 6  && !memcmp(compact, "VORTEX", 6)))
        return SNES_SUPERFX_BOARD_GSU1;

    return SNES_SUPERFX_BOARD_GSU2;
}

/* SNES ROM address lines do not mirror a non-power-of-two image with a
   simple modulo.  A 12-Mbit (1.5 MiB) cart, for example, mirrors its final
   4 Mbit into the next 4-Mbit window.  This is the recursive mirror used by
   real cartridge decoders (and by bsnes/snes9x). */
static Uint32 _SnesMirrorRomOffset(Uint32 uSize, Uint32 uPos)
{
	Uint32 uMask;

	if (uSize == 0 || uPos < uSize)
		return (uSize == 0) ? 0 : uPos;

	uMask = 0x80000000u;
	while (!(uPos & uMask))
		uMask >>= 1;

	if (uSize <= (uPos & uMask))
		return _SnesMirrorRomOffset(uSize, uPos - uMask);

	return uMask + _SnesMirrorRomOffset(uSize - uMask, uPos - uMask);
}

/* Sobrepoe o mapa generico LoROM com as duas visoes do Program ROM usadas
   pelo GSU: 32 KiB/banco em $00-$3F e 64 KiB/banco em $40-$5F. Os espelhos
   altos sao mantidos porque os jogos comerciais tambem os enxergam. */
static void _MapSuperFXRom(SNCpuT *pCpu, Uint8 *pRom, Uint32 uRomBytes)
{
	Uint32 uBank, uPage;
	if (!pRom || !uRomBytes) return;

	for (uBank = 0; uBank <= 0x3F; uBank++)
	{
		Uint32 uOffset = _SnesMirrorRomOffset(uRomBytes, uBank * 0x8000);
		Uint32 uAddr = uBank << 16;
		SNCPUSetMemSpeed(pCpu, uAddr | 0x8000, 0x8000, SNCPU_CYCLE_SLOW);
		SNCPUSetBank(pCpu, uAddr | 0x8000, 0x8000, pRom + uOffset, FALSE);
		SNCPUSetMemSpeed(pCpu, (uAddr | 0x800000) | 0x8000,
		                  0x8000, SNCPU_CYCLE_SLOW);
		SNCPUSetBank(pCpu, (uAddr | 0x800000) | 0x8000,
		             0x8000, pRom + uOffset, FALSE);
	}

	for (uBank = 0; uBank <= 0x1F; uBank++)
	{
		Uint32 uAddr = (0x40 + uBank) << 16;
		Uint32 uMirrorAddr = (0xC0 + uBank) << 16;
		for (uPage = 0; uPage < 0x10000; uPage += SNCPU_BANK_SIZE)
		{
			Uint32 uOffset = _SnesMirrorRomOffset(
				uRomBytes, uBank * 0x10000 + uPage);
			SNCPUSetMemSpeed(pCpu, uAddr + uPage,
			                  SNCPU_BANK_SIZE, SNCPU_CYCLE_SLOW);
			SNCPUSetBank(pCpu, uAddr + uPage,
			             SNCPU_BANK_SIZE, pRom + uOffset, FALSE);
			SNCPUSetMemSpeed(pCpu, uMirrorAddr + uPage,
			                  SNCPU_BANK_SIZE, SNCPU_CYCLE_SLOW);
			SNCPUSetBank(pCpu, uMirrorAddr + uPage,
			             SNCPU_BANK_SIZE, pRom + uOffset, FALSE);
		}
	}
}

void SnesSystem::MapMem(SnesMemMapT *pMemMap)
{
	SNCpuT *pCpu = &m_Cpu;
	Uint32 uSize[SNESMEM_TYPE_NUM];
	SnesRom *pRom = m_pRom;

	// determine size of SRAM in bytes
	m_uSramSize = pRom->GetSRAMBytes();
	if (m_uSramSize > SNES_SRAMSIZE) m_uSramSize = SNES_SRAMSIZE;

	// calculate size of each map type
	Int32 i;
	for (i=0; i < SNESMEM_TYPE_NUM; i++)
	{
		uSize[i] = SNCPU_BANK_SIZE;
	}

	uSize[SNESMEM_TYPE_NONE]	 = 0;
	uSize[SNESMEM_TYPE_UNMAPPED] = 0;
	uSize[SNESMEM_TYPE_ROM]      = pRom->GetBytes();
	uSize[SNESMEM_TYPE_RAM]      = SNES_RAMSIZE;
	uSize[SNESMEM_TYPE_SRAM]     = m_uSramSize;

	while (pMemMap->eMemType!=SNESMEM_TYPE_NONE)
	{
		Uint32 uBank;
		Uint32 uOffset;

		uOffset = pMemMap->uOffset;

		// iterate through bank range
		for (uBank = pMemMap->uStartBank; uBank <= pMemMap->uEndBank; uBank++)
		{
			Uint32 uStartAddr, uEndAddr, nBytes;

			// determine range to map for this bank
			uStartAddr = (uBank << 16) | (Uint32)pMemMap->uStartAddr;
			uEndAddr   = (uBank << 16) | (Uint32)pMemMap->uEndAddr;
			uEndAddr++;

			assert(uStartAddr <= uEndAddr);
			nBytes     = uEndAddr - uStartAddr;

			/* ROM keeps its logical offset here: each 8 KiB CPU page is
			   mirrored below with the cartridge address-line rule.  Other
			   memory types retain the legacy linear wrapping behavior. */
			if (pMemMap->eMemType != SNESMEM_TYPE_ROM)
			{
				// wrap address to size of memory
				if (uOffset >= uSize[pMemMap->eMemType])
					uOffset = 0;

				// wrap byte count to size of memory
				if (nBytes >= uSize[pMemMap->eMemType])
					nBytes = uSize[pMemMap->eMemType];
			}

			if (nBytes > 0)
			{
				Uint32 uAlignedBytes = (nBytes + SNCPU_BANK_SIZE - 1)  & ~(SNCPU_BANK_MASK);

				// set memory speed for region
				SNCPUSetMemSpeed(pCpu, uStartAddr, uAlignedBytes, pMemMap->uSpeed);

					switch (pMemMap->eMemType)
					{
					case SNESMEM_TYPE_ROM:
						{
							Uint32 uPage;
							Uint32 uRomBytes = pRom->GetBytes();
							for (uPage = 0; uPage < nBytes; uPage += SNCPU_BANK_SIZE)
							{
								Uint32 uRomOffset = _SnesMirrorRomOffset(
									uRomBytes, uOffset + uPage);
								SNCPUSetBank(pCpu, uStartAddr + uPage,
								              SNCPU_BANK_SIZE,
								              pRom->GetData() + uRomOffset, FALSE);
							}
						}
						break;
				case SNESMEM_TYPE_SRAM:
					if (nBytes & (SNCPU_BANK_SIZE - 1))
					{
						// size of sram wont map evenly to our bank size, so we must use a traphandler for reads/writes
						SNCPUSetTrap(pCpu, uStartAddr, uAlignedBytes, ReadSRAM, WriteSRAM);
					}
					else
					{
						// map mirrored
						while (uStartAddr < uEndAddr)
						{
							SNCPUSetBank(pCpu, uStartAddr, nBytes, m_SRam + uOffset, TRUE);
							uStartAddr += nBytes;
						}
					}
					break;
				case SNESMEM_TYPE_RAM:
				case SNESMEM_TYPE_LORAM:
					SNCPUSetBank(pCpu, uStartAddr, nBytes, m_Ram + uOffset, TRUE);
					break;
				case SNESMEM_TYPE_PPU0:
#if SNES_DEBUG
					SNCPUSetTrap(pCpu, uStartAddr, nBytes, Read2000Debug, Write2000Debug);
#else
                    SNCPUSetTrap(pCpu, uStartAddr, nBytes, Read2000, Write2000);
#endif
					break;
				case SNESMEM_TYPE_PPU1:
#if SNES_DEBUG
					SNCPUSetTrap(pCpu, uStartAddr, nBytes, Read4000Debug, Write4000Debug);
#else
                    SNCPUSetTrap(pCpu, uStartAddr, nBytes, Read4000, Write4000);
#endif
					break;
				case SNESMEM_TYPE_DSP1:
#ifdef SNES_DSP1
					SNCPUSetTrap(&m_Cpu, uStartAddr, nBytes, ReadDSP1, WriteDSP1);
#endif
					break;
				case SNESMEM_TYPE_OBC1:
					SNCPUSetTrap(&m_Cpu, uStartAddr, nBytes, ReadOBC1, WriteOBC1);
					break;
				case SNESMEM_TYPE_CX4:
					SNCPUSetTrap(&m_Cpu, uStartAddr, nBytes, ReadCX4, WriteCX4);
					break;
				case SNESMEM_TYPE_GSU:
					SNCPUSetTrap(&m_Cpu, uStartAddr, nBytes, ReadGSU, WriteGSU);
					break;
				default:
					break;
				}
			}
			uOffset += nBytes;
		}

		pMemMap++;
	}
}

#if CODE_DEBUG
void SnesSystem::DumpMemMap()
{
	SNCpuT *pCpu = GetCpu();
	Uint32 uAddr;
	for (uAddr=0; uAddr < SNCPU_MEM_SIZE; uAddr+= SNCPU_BANK_SIZE)
	{
		SNCpuBankT *pBank =  &pCpu->Bank[uAddr >> SNCPU_BANK_SHIFT];
		ConDebug("%06X %p %p %p %d %d\n",
			uAddr,
			pBank->pMem,
			pBank->pReadTrapFunc,
			pBank->pWriteTrapFunc,
			pBank->uBankCycle,
			pBank->bRAM
			);
	}
}
#endif

/* Areas de sistema (SRAM/WRAM/LoRAM/PPU) do ExLoROM - iguais ao LoROM.
   Aplicadas DEPOIS do mapeamento de ROM para sobrepor (WRAM em $7E-$7F
   tem que vencer a ROM que a regiao $40-$7F mapeia ali). */
static SnesMemMapT _SnesMemMap_ExLoRom_Sys[]=
{
	{0x70, 0x77, 0x0000, 0x7FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_SRAM},
	{0x7E, 0x7F, 0x0000, 0xFFFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_RAM},
	{0x00, 0x3F, 0x0000, 0x1FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_LORAM},
	{0x00, 0x3F, 0x2000, 0x3FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU0},
	{0x00, 0x3F, 0x4000, 0x5FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU1},
	{0x80, 0xBF, 0x0000, 0x1FFF, SNCPU_CYCLE_SLOW, SNESMEM_TYPE_LORAM},
	{0x80, 0xBF, 0x2000, 0x3FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU0},
	{0x80, 0xBF, 0x4000, 0x5FFF, SNCPU_CYCLE_FAST, SNESMEM_TYPE_PPU1},
	{0, 0, 0, 0, SNESMEM_TYPE_NONE}
};

/* Mapeia uma faixa de bancos no estilo LoROM (32KB por banco). Se
   fullBank, a metade baixa ($0000-7FFF) espelha a alta ($8000-FFFF) do
   mesmo chunk. Replica o map_lorom_offset do snes9x. */
static void _MapExLoRomRegion(SNCpuT *pCpu, Uint8 *pRom, Uint32 romBytes,
                              Uint32 bankS, Uint32 bankE, Bool fullBank,
                              Uint32 baseOffset)
{
	Uint32 c;
	if (romBytes == 0) return;
	for (c = bankS; c <= bankE; c++)
	{
		Uint32 chunk = baseOffset + ((c - bankS) * 0x8000);
		Uint8 *pMem;
		Uint32 bankAddr;
		chunk = _SnesMirrorRomOffset(romBytes, chunk);
		pMem     = pRom + chunk;
		bankAddr = c << 16;

		SNCPUSetMemSpeed(pCpu, bankAddr | 0x8000, 0x8000, SNCPU_CYCLE_SLOW);
		SNCPUSetBank    (pCpu, bankAddr | 0x8000, 0x8000, pMem, FALSE);
		if (fullBank)
		{
			SNCPUSetMemSpeed(pCpu, bankAddr | 0x0000, 0x8000, SNCPU_CYCLE_SLOW);
			SNCPUSetBank    (pCpu, bankAddr | 0x0000, 0x8000, pMem, FALSE);
		}
	}
}

void SnesSystem::MapMemExLoRom(void)
{
	SNCpuT *pCpu     = &m_Cpu;
	Uint8  *pRom     = m_pRom->GetData();
	Uint32  romBytes = m_pRom->GetBytes();

	// Bancos de ROM (replica snes9x Map_JumboLoROMMap, com as metades ja
	// normalizadas no loader: metade-com-header em 0x400000, 4MB em 0):
	//   $00-$3F:8000 -> 0x400000  (metade com header/vetores -> $00 le aqui)
	//   $40-$7F:0000 -> 0x600000  (full bank; em geral fora de range)
	//   $80-$BF:8000 -> 0x000000  (4MB principal, parte 1)
	//   $C0-$FF:0000 -> 0x200000  (4MB principal, parte 2; full bank)
	_MapExLoRomRegion(pCpu, pRom, romBytes, 0x00, 0x3F, FALSE, 0x400000);
	_MapExLoRomRegion(pCpu, pRom, romBytes, 0x40, 0x7F, TRUE,  0x600000);
	_MapExLoRomRegion(pCpu, pRom, romBytes, 0x80, 0xBF, FALSE, 0x000000);
	_MapExLoRomRegion(pCpu, pRom, romBytes, 0xC0, 0xFF, TRUE,  0x200000);

	// areas de sistema por cima
	MapMem(_SnesMemMap_ExLoRom_Sys);
}

void SnesSystem::MapMem(SNRomMappingE eRomMapping, Uint32 uFlags)
{
	// set default traps
	SNCPUSetTrap(&m_Cpu,     0, SNCPU_MEM_SIZE, ReadMem, WriteMem);
	SNCPUSetMemSpeed(&m_Cpu, 0, SNCPU_MEM_SIZE, SNCPU_CYCLE_SLOW);

	m_bSDD1 = FALSE;
	m_bSRTC = (uFlags & SNROM_FLAG_SRTC) ? TRUE : FALSE;
	m_bSuperFX = (uFlags & SNROM_FLAG_SUPERFX) ? TRUE : FALSE;

	switch (eRomMapping)
	{
		default:

		// mode 20h
		case SNROM_MAPPING_LOROM:
			MapMem(_SnesMemMap_LoRom);

#if SNES_DSP1
			if (uFlags & SNROM_FLAG_DSP1) { MapMem(_SnesMemMap_LoRom_DSP1); m_pDsp = &m_DSP1; }
			if (uFlags & SNROM_FLAG_DSP2) { MapMem(_SnesMemMap_LoRom_DSP1); m_pDsp = &m_DSP2; }
			// DSP-3 (SD Gundam GX): ainda SEM HLE proprio.  Mapeia a
			// regiao do DSP, mas m_pDsp fica NULL -> a guarda anti-crash
			// em ReadDSP1/WriteDSP1 mantem o emulador estavel (status
			// "pronto", dados 0).  O jogo nao renderiza certo, mas nao
			// trava.  (Sem dependencia de firmware externo.)
			if (uFlags & SNROM_FLAG_DSP3)
			{
				MapMem(_SnesMemMap_LoRom_DSP1);
				// m_pDsp permanece NULL (inerte)
			}
			// DSP-4 (Top Gear 3000): HLE self-contained -- NAO precisa de
			// firmware.  O chip esta sempre disponivel, entao a regiao do
			// DSP e' sempre mapeada e m_pDsp aponta para o HLE.
			if (uFlags & SNROM_FLAG_DSP4)
			{
				MapMem(_SnesMemMap_LoRom_DSP4);
				m_pDsp = &m_DSP4;
			}
#endif
			if (uFlags & SNROM_FLAG_OBC1) { MapMem(_SnesMemMap_OBC1); }
			if (uFlags & SNROM_FLAG_CX4)
			{
				MapMem(_SnesMemMap_CX4);
				m_CX4.SetMemReader(CX4ReadMem, &m_Cpu);
			}
			// SuperFX / GSU (Star Fox, Yoshi's Island, etc.).  So' ativa para
			// cartucho SuperFX (via m_bSuperFX), entao boot e jogos sem
			// SuperFX ficam 100% intactos.
			if (uFlags & SNROM_FLAG_SUPERFX)
			{
				Uint32 uBank;
				Int32 nBoard = _SnesSuperFXBoardType(m_pRom->GetRomTitle());
				Bool bMarioChip1 = (nBoard == SNES_SUPERFX_BOARD_MC1);

				_MapSuperFXRom(&m_Cpu, m_pRom->GetData(), m_pRom->GetBytes());
				if (bMarioChip1)
					MapMem(_SnesMemMap_SuperFXMC1RAM);
				else if (nBoard == SNES_SUPERFX_BOARD_GSU1)
					MapMem(_SnesMemMap_SuperFXGSU1RAM);
				else
					MapMem(_SnesMemMap_SuperFXRAM);

				// GSU1/2: $00-$3F/$80-$BF:6000-$7FFF espelha os primeiros
				// 8 KiB de $70:0000. O Mario Chip 1 nao possui esta janela.
				if (!bMarioChip1)
				{
					for (uBank = 0; uBank <= 0x3F; uBank++)
					{
						Uint32 uAddr = (uBank << 16) | 0x6000;
						SNCPUSetMemSpeed(&m_Cpu, uAddr, 0x2000, SNCPU_CYCLE_SLOW);
						SNCPUSetBank(&m_Cpu, uAddr, 0x2000, m_SRam, TRUE);
						uAddr |= 0x800000;
						SNCPUSetMemSpeed(&m_Cpu, uAddr, 0x2000, SNCPU_CYCLE_SLOW);
						SNCPUSetBank(&m_Cpu, uAddr, 0x2000, m_SRam, TRUE);
					}
				}

				// A MMIO do GSU ($3000-$34FF) compartilha a pagina do PPU e
				// continua roteada por Read2000/Write2000.
				m_GSU.SetVersion(bMarioChip1 ? 0x01 : 0x04);
				m_GSU.SetMemory(m_pRom->GetData(), m_pRom->GetBytes(),
				                m_SRam, m_uSramSize);
			}
			if (uFlags & SNROM_FLAG_SDD1)
			{
				m_bSDD1 = TRUE;
				RemapSDD1();   // sobrepoe $C0-$FF com os 4 segmentos
			}
			break;

		// mode 21h
		case SNROM_MAPPING_HIROM:
			MapMem(_SnesMemMap_HiRom);

#if SNES_DSP1
			if (uFlags & SNROM_FLAG_DSP1)
			{
				MapMem(_SnesMemMap_HiRom_DSP1);
				m_pDsp = &m_DSP1;;
			}
			if (uFlags & SNROM_FLAG_DSP2)
			{
				MapMem(_SnesMemMap_HiRom_DSP1);
				m_pDsp = &m_DSP2;
			}
#endif
			if (uFlags & SNROM_FLAG_OBC1) { MapMem(_SnesMemMap_OBC1); }
			break;

		// LoROM > 4MB (Jumbo / ExLoROM, ate 8MB)
		case SNROM_MAPPING_EXLOROM:
			MapMemExLoRom();
			break;
	}

	/* Indexed/16-bit accesses can transiently carry past $FFFFFF.  Publish
	   bank $00 into the overflow page after every cartridge/system override
	   has been installed, preserving the 24-bit bus wrap in the ASM core. */
	SNCPUMirror24BitBus(&m_Cpu);

#if CODE_DEBUG
#endif

}

// (Re)mapeia os bancos $C0-$FF conforme os registradores de segmento do
// S-DD1 ($4804-$4807). Cada registrador escolhe um segmento de 1MB da ROM
// para um grupo de 16 bancos: $4804->$C0-$CF, $4805->$D0-$DF, $4806->$E0-$EF,
// $4807->$F0-$FF. Star Ocean troca esses segmentos para enxergar seus 6MB.
void SnesSystem::RemapSDD1(void)
{
	Uint8 *pRomData  = m_pRom->GetData();
	Uint32 uRomBytes = m_pRom->GetBytes();
	Uint32 g;

	if (!pRomData || uRomBytes == 0)
		return;

#if SNDBG_LOG
	g_DbgSDD1Remaps++;
#endif

	for (g = 0; g < 4; g++)
	{
		Uint32 uSeg     = m_SDD1.BankSegment(g);
		Uint32 uRomOff  = (uSeg * 0x100000) % uRomBytes;
		Uint32 uBankBase = (0xC0 + g * 0x10) << 16;   // $C00000 / $D00000 / ...

		SNCPUSetMemSpeed(&m_Cpu, uBankBase, 0x100000, SNCPU_CYCLE_SLOW);
		SNCPUSetBank    (&m_Cpu, uBankBase, 0x100000, pRomData + uRomOff, FALSE);
	}

#if SNDBG_DEEP
	if (g_DbgCaptureActive)
	{
		static Uint32 s_uFrame = (Uint32)-1;
		static Uint32 s_uCount = 0;
		if (s_uFrame != g_DbgCaptureFrameNo)
		{
			s_uFrame = g_DbgCaptureFrameNo;
			s_uCount = 0;
		}
		if (s_uCount++ < 8u)
		{
			DLog("[snes-sdd1-map] f=%u seg=%u/%u/%u/%u rom-bytes=%u",
				(unsigned)g_DbgCaptureFrameNo,
				(unsigned)m_SDD1.BankSegment(0), (unsigned)m_SDD1.BankSegment(1),
				(unsigned)m_SDD1.BankSegment(2), (unsigned)m_SDD1.BankSegment(3),
				(unsigned)uRomBytes);
		}
	}
#endif
}

void SnesSystem::SetFastRom()
{
	SNCPUSetRomSpeed(&m_Cpu, 0x800000, 0x800000, SNCPU_CYCLE_FAST);
}

void SnesSystem::SetSlowRom()
{
	SNCPUSetRomSpeed(&m_Cpu, 0x800000, 0x800000, SNCPU_CYCLE_SLOW);
}
