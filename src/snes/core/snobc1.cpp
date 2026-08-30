/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Implements snobc1 behavior for the SNES emulation core.
 */

#include "types.h"
#include "snobc1.h"

#include <string.h>

SNOBC1::SNOBC1()
{
    Reset();
}

void SNOBC1::Reset()
{
    m_Address = 0;
    m_BasePtr = 0x1C00;
    m_Shift   = 0;
    memset(m_Ram, 0, sizeof(m_Ram));
}

Uint8 SNOBC1::Read(Uint32 uAddr)
{
    switch (uAddr)
    {
    case 0x7FF0: return m_Ram[m_BasePtr + (m_Address << 2)];
    case 0x7FF1: return m_Ram[m_BasePtr + (m_Address << 2) + 1];
    case 0x7FF2: return m_Ram[m_BasePtr + (m_Address << 2) + 2];
    case 0x7FF3: return m_Ram[m_BasePtr + (m_Address << 2) + 3];
    case 0x7FF4: return m_Ram[m_BasePtr + (m_Address >> 2) + 0x200];
    }
    return m_Ram[uAddr & 0x1FFF];
}

void SNOBC1::Write(Uint32 uAddr, Uint8 uData)
{
    switch (uAddr)
    {
    case 0x7FF0: m_Ram[m_BasePtr + (m_Address << 2)]     = uData; break;
    case 0x7FF1: m_Ram[m_BasePtr + (m_Address << 2) + 1] = uData; break;
    case 0x7FF2: m_Ram[m_BasePtr + (m_Address << 2) + 2] = uData; break;
    case 0x7FF3: m_Ram[m_BasePtr + (m_Address << 2) + 3] = uData; break;

    case 0x7FF4:
    {
        Int32 i = m_BasePtr + (m_Address >> 2) + 0x200;
        Uint8 t = m_Ram[i];
        t = (Uint8)((t & ~(3 << m_Shift)) | ((uData & 3) << m_Shift));
        m_Ram[i] = t;
        break;
    }

    case 0x7FF5:
        m_BasePtr = (uData & 1) ? 0x1800 : 0x1C00;
        m_Ram[0x1FF5] = uData;
        break;

    case 0x7FF6:
        m_Address = uData & 0x7F;
        m_Shift   = (uData & 3) << 1;
        break;

    default:
        m_Ram[uAddr & 0x1FFF] = uData;
        break;
    }
}
