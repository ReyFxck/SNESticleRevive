/*
 * sndsp2.cpp - DSP-2 (NEC uPD7725) coprocessor HLE
 *
 * Implementacao clean-room a partir do comportamento documentado do
 * chip (engenharia reversa publica).  Usado so em Dungeon Master.
 *
 * Protocolo HLE byte-oriented: a CPU escreve opcode + N bytes de
 * parametro e le M bytes de saida; o Status Register esta sempre
 * pronto (0x80 = RQM).  Os comandos 0x05/0x06/0x0D tem tamanho
 * dinamico: o(s) primeiro(s) byte(s) e' o comprimento.
 */

#include "types.h"
#include "sndsp2.h"

#include <string.h>

SNDSP2::SNDSP2()
{
    Reset();
}

void SNDSP2::Reset()
{
    m_uCommand   = 0;
    m_bWaitCmd   = TRUE;
    m_nIn = m_iIn = 0;
    m_nOut = m_iOut = 0;

    memset(m_Param, 0, sizeof(m_Param));
    memset(m_Out,   0, sizeof(m_Out));

    m_Op05Transparent = 0;
    m_bOp05HasLen = FALSE; m_Op05Len = 0;
    m_bOp06HasLen = FALSE; m_Op06Len = 0;
    m_bOp0DHasLen = FALSE; m_Op0DInLen = m_Op0DOutLen = 0;
}

//==========================================================================
//  Comandos
//==========================================================================

// op01: rearranjo de bitplane (32 bytes in -> 32 bytes out).  Cada bloco
// de 4 bytes de entrada gera 2 bytes na metade baixa e 2 na metade alta
// da saida (comportamento factual do silicio).
void SNDSP2::DoOp01()
{
    Uint8 *p1  = m_Param;
    Uint8 *pa  = &m_Out[0];
    Uint8 *pb  = &m_Out[16];
    Int32 j;

    for (j = 0; j < 8; j++)
    {
        Uint8 c0 = *p1++, c1 = *p1++, c2 = *p1++, c3 = *p1++;
        *pa++ = (Uint8)((c0&0x10)<<3 | (c0&0x01)<<6 | (c1&0x10)<<1 | (c1&0x01)<<4 | (c2&0x10)>>1 | (c2&0x01)<<2 | (c3&0x10)>>3 | (c3&0x01));
        *pa++ = (Uint8)((c0&0x20)<<2 | (c0&0x02)<<5 | (c1&0x20)    | (c1&0x02)<<3 | (c2&0x20)>>2 | (c2&0x02)<<1 | (c3&0x20)>>4 | (c3&0x02)>>1);
        *pb++ = (Uint8)((c0&0x40)<<1 | (c0&0x04)<<4 | (c1&0x40)>>1 | (c1&0x04)<<2 | (c2&0x40)>>3 | (c2&0x04)    | (c3&0x40)>>5 | (c3&0x04)>>2);
        *pb++ = (Uint8)((c0&0x80)    | (c0&0x08)<<3 | (c1&0x80)>>2 | (c1&0x08)<<1 | (c2&0x80)>>4 | (c2&0x08)>>1 | (c3&0x80)>>6 | (c3&0x08)>>3);
    }
}

// op05: overlay de dois bitmaps de 4-bit com cor transparente.
// Param[0..Len-1] = bitmap base, Param[Len..2Len-1] = bitmap por cima.
// Onde o pixel de cima == cor transparente, usa o de baixo.
void SNDSP2::DoOp05()
{
    Uint8 color = (Uint8)(m_Op05Transparent & 0x0F);
    Uint8 *p1 = m_Param;
    Uint8 *p2 = &m_Param[m_Op05Len];
    Uint8 *p3 = m_Out;
    Int32 n;

    for (n = 0; n < m_Op05Len; n++)
    {
        Uint8 c1 = *p1++;
        Uint8 c2 = *p2++;
        Uint8 hi = ((c2 >> 4)   == color) ? (Uint8)(c1 & 0xF0) : (Uint8)(c2 & 0xF0);
        Uint8 lo = ((c2 & 0x0F) == color) ? (Uint8)(c1 & 0x0F) : (Uint8)(c2 & 0x0F);
        *p3++ = (Uint8)(hi | lo);
    }
}

// op06: inverte a ordem dos bytes e troca os nibbles de cada byte.
void SNDSP2::DoOp06()
{
    Int32 i, j;
    for (i = 0, j = m_Op06Len - 1; i < m_Op06Len; i++, j--)
        m_Out[j] = (Uint8)((m_Param[i] << 4) | (m_Param[i] >> 4));
}

// op0D: escala um bitmap de 4-bit de InLen para OutLen pixels.
void SNDSP2::DoOp0D()
{
    Int32 i;
    for (i = 0; i < m_Op0DOutLen; i++)
    {
        Int32 j = i << 1;
        Int32 offLo = ((j     * m_Op0DInLen) / m_Op0DOutLen) >> 1;
        Int32 offHi = (((j+1) * m_Op0DInLen) / m_Op0DOutLen) >> 1;
        Uint8 pLo = (Uint8)(m_Param[offLo] >> 4);
        Uint8 pHi = (Uint8)(m_Param[offHi] & 0x0F);
        m_Out[i] = (Uint8)((pLo << 4) | pHi);
    }
}

//==========================================================================
//  Dispatch
//==========================================================================

void SNDSP2::Execute(Uint8 uLastByte)
{
    switch (m_uCommand)
    {
    case 0x01:  // bitplane convert (fixo 32->32)
        m_nOut = 32;
        DoOp01();
        break;

    case 0x03:  // set transparent color
        m_Op05Transparent = m_Param[0];
        break;

    case 0x05:  // overlay com transparencia (2 fases)
        if (m_bOp05HasLen)
        {
            m_bOp05HasLen = FALSE;
            m_nOut = m_Op05Len;
            DoOp05();
        }
        else
        {
            m_Op05Len = m_Param[0];
            m_iIn = 0;
            m_nIn = 2 * m_Op05Len;
            m_bOp05HasLen = TRUE;
            if (uLastByte) m_bWaitCmd = FALSE;
        }
        break;

    case 0x06:  // swap nibble + reverse (2 fases)
        if (m_bOp06HasLen)
        {
            m_bOp06HasLen = FALSE;
            m_nOut = m_Op06Len;
            DoOp06();
        }
        else
        {
            m_Op06Len = m_Param[0];
            m_iIn = 0;
            m_nIn = m_Op06Len;
            m_bOp06HasLen = TRUE;
            if (uLastByte) m_bWaitCmd = FALSE;
        }
        break;

    case 0x09:  // multiplicacao 16x16 -> 32 (unsigned)
    {
        Uint16 w1 = (Uint16)(m_Param[0] | (m_Param[1] << 8));
        Uint16 w2 = (Uint16)(m_Param[2] | (m_Param[3] << 8));
        Uint32 r  = (Uint32)w1 * (Uint32)w2;
        m_nOut = 4;
        m_Out[0] = (Uint8)(r & 0xFF);
        m_Out[1] = (Uint8)((r >> 8)  & 0xFF);
        m_Out[2] = (Uint8)((r >> 16) & 0xFF);
        m_Out[3] = (Uint8)((r >> 24) & 0xFF);
        break;
    }

    case 0x0D:  // escala de bitmap (2 fases)
        if (m_bOp0DHasLen)
        {
            m_bOp0DHasLen = FALSE;
            m_nOut = m_Op0DOutLen;
            DoOp0D();
        }
        else
        {
            m_Op0DInLen  = m_Param[0];
            m_Op0DOutLen = m_Param[1];
            m_iIn = 0;
            m_nIn = (m_Op0DInLen + 1) >> 1;   // bytes de nibbles empacotados
            m_bOp0DHasLen = TRUE;
            if (uLastByte) m_bWaitCmd = FALSE;
        }
        break;

    default:
        break;
    }
}

//==========================================================================
//  Interface de barramento (ISNDSP)
//==========================================================================

void SNDSP2::WriteData(Uint32 /*uAddr*/, Uint8 uData)
{
    if (m_bWaitCmd)
    {
        m_uCommand = uData;
        m_iIn      = 0;
        m_bWaitCmd = FALSE;
        switch (uData)
        {
        case 0x03: case 0x05: case 0x06: m_nIn = 1;  break;
        case 0x0D:                       m_nIn = 2;  break;
        case 0x09:                       m_nIn = 4;  break;
        case 0x01:                       m_nIn = 32; break;
        default:                         m_nIn = 0;  break;
        }
    }
    else
    {
        if (m_iIn < (Int32)sizeof(m_Param))
            m_Param[m_iIn] = uData;
        m_iIn++;
    }

    if (m_nIn == m_iIn)
    {
        m_bWaitCmd = TRUE;
        m_iOut     = 0;
        Execute(uData);
    }
}

Uint8 SNDSP2::ReadData(Uint32 /*uAddr*/)
{
    if (m_nOut)
    {
        Uint8 t = m_Out[m_iOut];
        m_iOut++;
        if (m_nOut == m_iOut) m_nOut = 0;
        return t;
    }
    return 0xFF;
}

Uint8 SNDSP2::ReadStatus(Uint32 /*uAddr*/)
{
    // DSP-2 HLE: sempre pronto (RQM=1).
    return 0x80;
}
