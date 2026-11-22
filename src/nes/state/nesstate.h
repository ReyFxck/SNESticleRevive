/* nesstate.h - persistent, pointer-free InfoNES snapshot. */

#ifndef _NESSTATE_H
#define _NESSTATE_H

#include "types.h"

#define NES_STATE_MAGIC            0x4E535354UL /* "NSST" */
#define NES_STATE_VERSION          1
#define NES_STATE_CPU_BYTES        64
#define NES_STATE_APU_BYTES        (16 * 1024)
#define NES_STATE_MAPPER_BYTES     (432 * 1024)
#define NES_STATE_RAM_BYTES        0x2000
#define NES_STATE_SRAM_BYTES       0x2000
#define NES_STATE_PPURAM_BYTES     0x4000
#define NES_STATE_SPRRAM_BYTES     256
#define NES_STATE_CHRRAM_BYTES     0x2000
#define NES_STATE_CHRBUF_BYTES     (256 * 2 * 8 * 8)
#define NES_STATE_PPU_BANKS        16

/* A bank reference is portable across emulator restarts. Raw pointers
   would only be valid until the ROM buffer was allocated again. */
enum NesStateRegionE
{
    NES_STATE_REGION_NONE = 0,
    NES_STATE_REGION_ROM,
    NES_STATE_REGION_VROM,
    NES_STATE_REGION_RAM,
    NES_STATE_REGION_SRAM,
    NES_STATE_REGION_PPURAM,
    NES_STATE_REGION_SPRRAM,
    NES_STATE_REGION_CHRBUF,
    NES_STATE_REGION_MAPPER
};

struct NesStateBankRefT
{
    Uint32 eRegion;
    Uint32 uOffset;
};

struct NesStateT
{
    Uint32 uMagic;
    Uint32 uVersion;
    Uint32 nStateBytes;
    Uint32 uMapper;
    Uint32 nCpuStateBytes;
    Uint32 nApuStateBytes;
    Uint32 nMapperStateBytes;
    Uint32 bChrRam;

    Uint32 uFrameTick;
    Uint32 uFrame;
    Uint32 uLine;

    Uint8 aCpuState[NES_STATE_CPU_BYTES];
    Uint8 aApuState[NES_STATE_APU_BYTES];

    Uint8 aRam[NES_STATE_RAM_BYTES];
    Uint8 aSram[NES_STATE_SRAM_BYTES];
    Uint8 aPpuRam[NES_STATE_PPURAM_BYTES];
    Uint8 aSpriteRam[NES_STATE_SPRRAM_BYTES];
    Uint8 aChrRam[NES_STATE_CHRRAM_BYTES];
    Uint8 aChrBuf[NES_STATE_CHRBUF_BYTES];
    Uint32 aPalette[32];
    Uint8 aPpuScanTable[263];
    Uint8 aApuRegisters[0x18];

    Uint8 PPU_R0, PPU_R1, PPU_R2, PPU_R3, PPU_R7;
    Uint8 PPU_Scr_V, PPU_Scr_V_Next;
    Uint8 PPU_Scr_V_Byte, PPU_Scr_V_Byte_Next;
    Uint8 PPU_Scr_V_Bit, PPU_Scr_V_Bit_Next;
    Uint8 PPU_Scr_H, PPU_Scr_H_Next;
    Uint8 PPU_Scr_H_Byte, PPU_Scr_H_Byte_Next;
    Uint8 PPU_Scr_H_Bit, PPU_Scr_H_Bit_Next;
    Uint8 PPU_Latch_Flag, PPU_UpDown_Clip;
    Uint8 PPU_NameTableBank, byVramWriteEnable;
    Uint8 FrameIRQ_Enable, ChrBufUpdate;

    Uint16 PPU_Addr, PPU_Temp, PPU_Increment;
    Uint16 PPU_Scanline, PPU_SP_Height;
    Uint16 FrameStep, FrameSkip, FrameCnt;
    Int32 SpriteJustHit;
    Int32 APU_Mute;

    Uint32 PAD1_Latch, PAD2_Latch, PAD_System;
    Uint32 PAD1_Bit, PAD2_Bit;

    NesStateBankRefT RomBanks[4];
    NesStateBankRefT SramBank;
    NesStateBankRefT PpuBanks[NES_STATE_PPU_BANKS];
    NesStateBankRefT PpuBgBase;
    NesStateBankRefT PpuSpriteBase;

    /* Opaque mapper globals (RAM, IRQ counters, latches and registers).
       nMapperStateBytes says how much of this capacity belongs to the
       current build; the remainder stays zero and compresses cheaply. */
    Uint8 aMapperState[NES_STATE_MAPPER_BYTES];
};

#endif
