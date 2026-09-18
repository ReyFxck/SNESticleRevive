/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * NEC DSP execution core adapted for SNESticleRevive from MesenCE:
 *   Core/SNES/Coprocessors/DSP/NecDsp.*
 *   Copyright (C) 2014-2026 Sour, 2026 MesenCE contributors
 *
 * Licensed under GNU GPL v3 or later. See LICENSE and NOTICE.
 *
 * Description:
 *   Emulates the NEC uPD7725 used as DSP-4 by Top Gear 3000.
 *
 *   A real firmware image may still be loaded for validation, but normal
 *   SNESticleRevive builds use the self-contained replacement-program path.
 *   That path reproduces the public DSP-4 DR/SR command protocol without
 *   embedding Nintendo/NEC microcode.
 */

#ifndef _SNDSP4_H
#define _SNDSP4_H

#include "types.h"
#include "sndsp.h"

class SNDSP4 : public ISNDSP
{
public:
    SNDSP4();

    Bool LoadFirmware(const Uint8 *pImage, Uint32 nBytes);
    void UseReplacementProgram();

    Bool IsLoaded() const { return m_bLoaded; }
    Bool IsReady() const;

    void  Reset();
    void  WriteData(Uint32 uAddr, Uint8 uData);
    Uint8 ReadData(Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);

private:
    enum
    {
        PROGRAM_WORDS = 0x800,
        PROGRAM_BYTES = 0x1800,
        DATA_WORDS    = 0x400,
        DATA_BYTES    = 0x0800,
        FIRMWARE_BYTES= 0x2000,
        RAM_WORDS     = 0x100,
        STACK_WORDS   = 4,
        BUS_CYCLE_BUDGET = 0x40000,

        REPL_INPUT_BYTES  = 512,
        REPL_OUTPUT_BYTES = 4096
    };

    enum StatusFlags
    {
        SR_RQM = 0x8000,
        SR_USF1 = 0x4000,
        SR_USF0 = 0x2000,
        SR_DRS = 0x1000,
        SR_DMA = 0x0800,
        SR_DRC = 0x0400,
        SR_SOC = 0x0200,
        SR_SIC = 0x0100,
        SR_EI  = 0x0080
    };

    struct AccFlags
    {
        Uint8 Carry;
        Uint8 Zero;
        Uint8 Overflow0;
        Uint8 Overflow1;
        Uint8 Sign0;
        Uint8 Sign1;
    };

    struct State
    {
        Uint64 CycleCount;
        Uint16 A;
        Uint16 B;
        AccFlags FlagsA;
        AccFlags FlagsB;
        Uint16 TR;
        Uint16 TRB;
        Uint16 PC;
        Uint16 RP;
        Uint16 DP;
        Uint16 DR;
        Uint16 SR;
        Uint16 K;
        Uint16 L;
        Uint16 M;
        Uint16 N;
        Uint16 SerialOut;
        Uint16 SerialIn;
        Uint8 SP;
    };

    struct ReplacementState
    {
        Bool WaitingCommand;
        Bool HalfCommand;
        Uint16 Command;
        Uint16 Need;
        Uint16 InPos;
        Uint16 OutCount;
        Uint16 OutPos;
        Uint8 Phase;
        Uint8 Input[REPL_INPUT_BYTES];
        Uint8 Output[REPL_OUTPUT_BYTES];

        Int32 WorldX;
        Int32 WorldY;
        Int32 WorldDx;
        Int32 WorldDy;
        Int32 WorldXEnv;

        Int16 WorldYOfs;
        Int16 WorldDdx;
        Int16 WorldDdy;
        Int16 ViewYOfsEnv;
        Int16 Distance;

        Int16 PolyBottom;
        Int16 PolyTop;
        Int16 PolyCxX;
        Int16 PolyCxY;
        Int16 PolyPtr;
        Int16 PolyRaster;
        Int16 ViewportBottom;

        Int16 ViewX1;
        Int16 ViewY1;
        Int16 ViewX2;
        Int16 ViewY2;
        Int16 ViewXOfs1;
        Int16 ViewYOfs1;
        Int16 ViewXOfs2;
        Int16 ViewYOfs2;
        Int16 ViewDx;
        Int16 ViewDy;
        Int16 TurnX;
        Int16 TurnDx;
        Int16 Segments;
        Uint8 LightIndex;

        Uint8 OamRow[32];
        Uint16 OamAttr[16];
        Uint16 OamIndex;
        Uint16 OamBits;
        Uint16 SpriteCount;
        Uint16 OamRowMax;

        /* Op 0008 solid-polygon state. */
        Int16 Poly8ClipRt[2][2];
        Int16 Poly8ClipLf[2][2];
        Int16 Poly8Cx[2][2];
        Int16 Poly8Ptr[2][2];
        Int16 Poly8Bottom[2][2];
        Int16 Poly8Top[2][2];
        Int16 Poly8Raster[2][2];
        Int16 Poly8Start[2];
        Int16 Poly8Plane[2];
        Int16 Poly8ViewX[2];
        Int16 Poly8ViewY[2];
        Int16 Poly8Envelope[2][2];

        /* Op 0009 sprite-projection state. */
        Int16 ViewportCx;
        Int16 ViewportCy;
        Int16 ViewportLeft;
        Int16 ViewportRight;
        Int16 ViewportTop;
        Int16 SpriteClipY;
        Int16 SpriteRaster;
        Int16 SpriteX;
        Int16 SpriteY;
        Int16 SpriteAttr;
        Uint8 SpriteSize;

        Uint32 UnsupportedMask;

        Uint32 TraceSeq;
        Uint32 TraceLines;
        Uint32 TraceCommands;
        Uint32 TraceReads;
        Uint32 TraceWrites;
    };

    State  m_State;
    Uint32 m_OpCode;
    Uint32 m_Program[PROGRAM_WORDS];
    Uint16 m_DataRom[DATA_WORDS];
    Uint16 m_Ram[RAM_WORDS];
    Uint16 m_Stack[STACK_WORDS];

    Bool m_bLoaded;
    Bool m_bFaulted;
    Bool m_bLittleEndianFirmware;
    Bool m_bReplacementProgram;
    ReplacementState m_Repl;

    /* Real uPD7725 execution path (debug/reference firmware mode). */
    void DecodeFirmware(const Uint8 *pImage, Bool bLittleEndian);
    void RunUntilRqm();
    void StepOne();

    void RunAluOp(Uint8 uOperation, Uint16 uSource);
    void UpdateDataPointer();
    void ExecOp();
    void ExecAndReturn();
    void Jump();
    void Load(Uint8 uDest, Uint16 uValue);
    Uint16 GetSourceValue(Uint8 uSource);

    Uint16 ReadRom(Uint32 uAddr) const;
    Uint16 ReadRam(Uint32 uAddr) const;
    void WriteRam(Uint32 uAddr, Uint16 uValue);

    /* Self-contained replacement-program path.  This is intentionally
       implemented behind the same DR/SR interface as the real uPD7725. */
    void ReplacementReset();
    void ReplacementWrite(Uint8 uData);
    Uint8 ReplacementRead();
    Uint8 ReplacementStatus() const;

    void ReplacementBeginCommand(Uint16 uCommand);
    void ReplacementDispatch();
    void ReplacementFinish();
    void ReplacementExpect(Uint16 nBytes, Uint8 uPhase);

    Uint16 ReplacementReadWord(Uint16 uOffset) const;
    Int16 ReplacementReadSWord(Uint16 uOffset) const;
    Int32 ReplacementReadDword(Uint16 uOffset) const;
    void ReplacementClearOutput();
    void ReplacementWriteByte(Uint8 uValue);
    void ReplacementWriteWord(Uint16 uValue);

    Int16 ReplacementInverse(Int16 nLines) const;
    void ReplacementProject01();
    void ReplacementProject07();
    void ReplacementProject0F();
    void ReplacementProject10();
    void ReplacementProject08();
    void ReplacementSprite09Tile();
    void ReplacementAppendRaster();
    void ReplacementPostRoad(Bool bAdvanceWorld);
    Uint16 ReplacementLightColor(Int16 nDistance, Uint16 uColor) const;
    void ReplacementOp0B(Int16 x, Int16 y, Int16 attr, Bool bLarge, Bool bEmitStop);
};

#endif
