/*
 * snsdd1.cpp - S-DD1 (Super Data Decompression 1) coprocessor HLE
 *
 * Veja snsdd1.h para a descricao geral.
 *
 * Implementacao clean-room do descompressor (Golomb + modelo de contexto
 * adaptativo por bitplane), a partir da engenharia reversa publica do
 * Andreas Naive. As tabelas (evolucao de estado / run-length) sao constantes
 * factuais do hardware.
 */

#include "types.h"
#include "snsdd1.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Tabelas factuais
// ---------------------------------------------------------------------------

// tabela de evolucao de estado da estimativa de probabilidade:
// { tamanho do codigo Golomb, proximo estado se MPS, proximo estado se LPS }
struct SDD1EvoT { Uint8 uCodeSize; Uint8 uMPSNext; Uint8 uLPSNext; };

static const SDD1EvoT s_EvoTable[33] =
{
    { 0, 25, 25 }, { 0,  2,  1 }, { 0,  3,  1 }, { 0,  4,  2 },
    { 0,  5,  3 }, { 1,  6,  4 }, { 1,  7,  5 }, { 1,  8,  6 },
    { 1,  9,  7 }, { 2, 10,  8 }, { 2, 11,  9 }, { 2, 12, 10 },
    { 2, 13, 11 }, { 3, 14, 12 }, { 3, 15, 13 }, { 3, 16, 14 },
    { 3, 17, 15 }, { 4, 18, 16 }, { 4, 19, 17 }, { 5, 20, 18 },
    { 5, 21, 19 }, { 6, 22, 20 }, { 6, 23, 21 }, { 7, 24, 22 },
    { 7, 24, 23 }, { 0, 26,  1 }, { 1, 27,  2 }, { 2, 28,  4 },
    { 3, 29,  8 }, { 4, 30, 12 }, { 5, 31, 16 }, { 6, 32, 18 },
    { 7, 24, 22 }
};

// tabela de run-length (bit-reversal da contagem de zeros)
static const Uint8 s_RunTable[128] =
{
    128,  64,  96,  32, 112,  48,  80,  16, 120,  56,  88,  24, 104,  40,  72,
      8, 124,  60,  92,  28, 108,  44,  76,  12, 116,  52,  84,  20, 100,  36,
     68,   4, 126,  62,  94,  30, 110,  46,  78,  14, 118,  54,  86,  22, 102,
     38,  70,   6, 122,  58,  90,  26, 106,  42,  74,  10, 114,  50,  82,  18,
     98,  34,  66,   2, 127,  63,  95,  31, 111,  47,  79,  15, 119,  55,  87,
     23, 103,  39,  71,   7, 123,  59,  91,  27, 107,  43,  75,  11, 115,  51,
     83,  19,  99,  35,  67,   3, 125,  61,  93,  29, 109,  45,  77,  13, 117,
     53,  85,  21, 101,  37,  69,   5, 121,  57,  89,  25, 105,  41,  73,   9,
    113,  49,  81,  17,  97,  33,  65,   1
};

// ---------------------------------------------------------------------------
// Construcao / reset
// ---------------------------------------------------------------------------

SNSDD1::SNSDD1()
{
    Reset();
}

void SNSDD1::Reset()
{
    memset(m_Reg, 0, sizeof(m_Reg));
    // mapeamento padrao: segmento i no grupo i (linear)
    for (Int32 i = 0; i < 4; i++)
        m_Reg[4 + i] = (Uint8)i;
    m_bMapDirty = FALSE;

    m_ValidBits    = 0;
    m_InStream     = 0;
    m_pInBuf       = 0;
    m_BitplaneType = 0;
    m_HighCtxBits  = 0;
    m_LowCtxBits   = 0;
    memset(m_BitCtr,   0, sizeof(m_BitCtr));
    memset(m_CtxState, 0, sizeof(m_CtxState));
    memset(m_CtxMPS,   0, sizeof(m_CtxMPS));
    memset(m_PrevBits, 0, sizeof(m_PrevBits));
}

// ---------------------------------------------------------------------------
// Registradores
// ---------------------------------------------------------------------------

Uint8 SNSDD1::ReadReg(Uint32 uAddr)
{
    return m_Reg[uAddr & 7];
}

void SNSDD1::WriteReg(Uint32 uAddr, Uint8 uData)
{
    Uint32 r = uAddr & 7;
    m_Reg[r] = uData;
    if (r >= 4)
        m_bMapDirty = TRUE;   // $4804-$4807: o mapa de bancos mudou
}

// ---------------------------------------------------------------------------
// Descompressor
// ---------------------------------------------------------------------------

Uint8 SNSDD1::GetCodeword(Int32 nBits)
{
    Uint8 tmp;

    if (!m_ValidBits)
    {
        m_InStream |= *(m_pInBuf++);
        m_ValidBits = 8;
    }
    m_InStream <<= 1;
    m_ValidBits--;
    m_InStream ^= 0x8000;
    if (m_InStream & 0x8000)
        return (Uint8)(0x80 + (1 << nBits));

    tmp = (Uint8)((m_InStream >> 8) | (0x7f >> nBits));
    m_InStream <<= nBits;
    m_ValidBits -= nBits;
    if (m_ValidBits < 0)
    {
        m_InStream |= (Uint16)((*(m_pInBuf++)) << (-m_ValidBits));
        m_ValidBits += 8;
    }
    return s_RunTable[tmp];
}

Uint8 SNSDD1::GolombGetBit(Int32 nCodeSize)
{
    if (!m_BitCtr[nCodeSize])
        m_BitCtr[nCodeSize] = GetCodeword(nCodeSize);
    m_BitCtr[nCodeSize]--;
    if (m_BitCtr[nCodeSize] == 0x80)
    {
        m_BitCtr[nCodeSize] = 0;
        return 2;   // codigo especial: "ultimo zero" (uns sao sempre o ultimo)
    }
    return (m_BitCtr[nCodeSize] == 0) ? 1 : 0;
}

Uint8 SNSDD1::ProbGetBit(Uint8 uContext)
{
    Uint8 uState = m_CtxState[uContext];
    Uint8 uBit   = GolombGetBit(s_EvoTable[uState].uCodeSize);

    if (uBit & 1)
    {
        m_CtxState[uContext] = s_EvoTable[uState].uLPSNext;
        if (uState < 2)
        {
            m_CtxMPS[uContext] ^= 1;
            return (Uint8)m_CtxMPS[uContext];   // acabou de inverter, retorna direto
        }
        else
            return (Uint8)(m_CtxMPS[uContext] ^ 1);   // bit e' 1
    }
    else if (uBit)
    {
        m_CtxState[uContext] = s_EvoTable[uState].uMPSNext;
        // zero aqui, zero ali: cai fora
    }
    return (Uint8)m_CtxMPS[uContext];   // bit e' 0
}

Uint8 SNSDD1::GetBit(Uint8 uCurBitplane)
{
    Uint8 uBit = ProbGetBit((Uint8)(((uCurBitplane & 1) << 4)
                | ((m_PrevBits[uCurBitplane] & m_HighCtxBits) >> 5)
                | (m_PrevBits[uCurBitplane] & m_LowCtxBits)));

    m_PrevBits[uCurBitplane] <<= 1;
    m_PrevBits[uCurBitplane] |= uBit;
    return uBit;
}

void SNSDD1::Decompress(Uint8 *pOut, const Uint8 *pIn, Int32 len)
{
    Uint8 bit, byte1, byte2;
    Uint8 i, plane;

    if (len == 0)
        len = 0x10000;

    m_BitplaneType = pIn[0] >> 6;

    switch (pIn[0] & 0x30)
    {
    case 0x00: m_HighCtxBits = 0x01c0; m_LowCtxBits = 0x0001; break;
    case 0x10: m_HighCtxBits = 0x0180; m_LowCtxBits = 0x0001; break;
    case 0x20: m_HighCtxBits = 0x00c0; m_LowCtxBits = 0x0001; break;
    case 0x30: m_HighCtxBits = 0x0180; m_LowCtxBits = 0x0003; break;
    }

    m_InStream  = (Uint16)((pIn[0] << 11) | (pIn[1] << 3));
    m_ValidBits = 5;
    m_pInBuf    = pIn + 2;
    memset(m_BitCtr,   0, sizeof(m_BitCtr));
    memset(m_CtxState, 0, sizeof(m_CtxState));
    memset(m_CtxMPS,   0, sizeof(m_CtxMPS));
    memset(m_PrevBits, 0, sizeof(m_PrevBits));

    switch (m_BitplaneType)
    {
    case 0:
        while (1)
        {
            for (byte1 = byte2 = 0, bit = 0x80; bit; bit >>= 1)
            {
                if (GetBit(0)) byte1 |= bit;
                if (GetBit(1)) byte2 |= bit;
            }
            *(pOut++) = byte1; if (!--len) return;
            *(pOut++) = byte2; if (!--len) return;
        }
        break;

    case 1:
        i = plane = 0;
        while (1)
        {
            for (byte1 = byte2 = 0, bit = 0x80; bit; bit >>= 1)
            {
                if (GetBit(plane))     byte1 |= bit;
                if (GetBit(plane + 1)) byte2 |= bit;
            }
            *(pOut++) = byte1; if (!--len) return;
            *(pOut++) = byte2; if (!--len) return;
            if (!(i += 32)) plane = (plane + 2) & 7;
        }
        break;

    case 2:
        i = plane = 0;
        while (1)
        {
            for (byte1 = byte2 = 0, bit = 0x80; bit; bit >>= 1)
            {
                if (GetBit(plane))     byte1 |= bit;
                if (GetBit(plane + 1)) byte2 |= bit;
            }
            *(pOut++) = byte1; if (!--len) return;
            *(pOut++) = byte2; if (!--len) return;
            if (!(i += 32)) plane ^= 2;
        }
        break;

    case 3:
        do
        {
            for (byte1 = plane = 0, bit = 1; bit; bit <<= 1, plane++)
            {
                if (GetBit(plane)) byte1 |= bit;
            }
            *(pOut++) = byte1;
        } while (--len);
        break;
    }
}
