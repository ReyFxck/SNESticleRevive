/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Declares the snsrtc interface for the SNES emulation core.
 */

#ifndef _SNSRTC_H
#define _SNSRTC_H

#include "types.h"

class SNSRTC
{
public:
    SNSRTC();

    void  Reset();

    Uint8 ReadReg();          // leitura de $2800
    void  WriteReg(Uint8 d);  // escrita em $2801

#ifdef SNSRTC_TESTHOOK
    // para a bancada de testes: injeta um tempo fixo e expoe os registradores
    typedef Uint32 (*TimeFnT)(void);
    void   SetTimeFn(TimeFnT fn) { m_pTimeFn = fn; }
    Uint8 *TestReg() { return m_Reg; }
    void   TestUpdateTime() { UpdateTime(); }
    Uint32 TestWeekday(Uint32 y, Uint32 m, Uint32 d) { return Weekday(y, m, d); }
    void   SetModeIndex(Int32 mode, Int32 idx) { m_Mode = mode; m_Index = idx; }
#endif

private:
    enum { RTCM_Ready = 0, RTCM_Command, RTCM_Read, RTCM_Write };

    Uint8 m_Reg[20];   // 0..12 = nibbles de data/hora; 16..19 = timestamp
    Int32 m_Mode;
    Int32 m_Index;

#ifdef SNSRTC_TESTHOOK
    TimeFnT m_pTimeFn;
#endif

    Uint32 GetTime();
    void   UpdateTime();
    Uint32 Weekday(Uint32 year, Uint32 month, Uint32 day);
    void   SyncFromHost();
};

#endif
