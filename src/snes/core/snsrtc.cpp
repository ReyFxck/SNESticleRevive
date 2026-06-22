/*
 * snsrtc.cpp - S-RTC (Sharp Real-Time Clock) coprocessor HLE
 *
 * Veja snsrtc.h. Implementacao clean-room a partir do comportamento
 * documentado do chip (engenharia reversa publica, byuu).
 */

#include "types.h"
#include "snsrtc.h"

#include <string.h>
#include <time.h>

// dias por mes (fevereiro tratado como 28 + ajuste de ano bissexto)
static const Uint32 s_Months[12] =
{
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static Bool _IsLeap(Uint32 year)
{
    if ((year % 4) != 0)
        return FALSE;
    if ((year % 100) == 0 && (year % 400) != 0)
        return FALSE;
    return TRUE;
}

SNSRTC::SNSRTC()
{
#ifdef SNSRTC_TESTHOOK
    m_pTimeFn = 0;
#endif
    memset(m_Reg, 0, sizeof(m_Reg));
    m_Mode  = RTCM_Read;
    m_Index = -1;
}

Uint32 SNSRTC::GetTime()
{
#ifdef SNSRTC_TESTHOOK
    if (m_pTimeFn)
        return m_pTimeFn();
#endif
    return (Uint32)time(0);
}

void SNSRTC::Reset()
{
    m_Mode  = RTCM_Read;
    m_Index = -1;
    SyncFromHost();
}

// Semeia os registradores com a data/hora atual do host (UTC), e grava o
// timestamp atual. Assim o jogo le a hora real, e UpdateTime() avanca
// corretamente a partir desse ponto.
void SNSRTC::SyncFromHost()
{
    Uint32 t = GetTime();
    time_t  tt = (time_t)t;
    struct tm *g = gmtime(&tt);

    memset(m_Reg, 0, sizeof(m_Reg));

    if (g)
    {
        Uint32 second  = (Uint32)g->tm_sec;
        Uint32 minute  = (Uint32)g->tm_min;
        Uint32 hour    = (Uint32)g->tm_hour;
        Uint32 day     = (Uint32)g->tm_mday;
        Uint32 month   = (Uint32)(g->tm_mon + 1);
        Uint32 year    = (Uint32)(g->tm_year + 1900);
        Uint32 weekday = (Uint32)g->tm_wday;

        if (second > 59) second = 59;   // segundo bissexto -> clampa

        m_Reg[0]  = (Uint8)(second % 10);
        m_Reg[1]  = (Uint8)(second / 10);
        m_Reg[2]  = (Uint8)(minute % 10);
        m_Reg[3]  = (Uint8)(minute / 10);
        m_Reg[4]  = (Uint8)(hour % 10);
        m_Reg[5]  = (Uint8)(hour / 10);
        m_Reg[6]  = (Uint8)(day % 10);
        m_Reg[7]  = (Uint8)(day / 10);
        m_Reg[8]  = (Uint8)month;
        m_Reg[9]  = (Uint8)(year % 10);
        m_Reg[10] = (Uint8)((year / 10) % 10);
        m_Reg[11] = (Uint8)(year / 100);
        m_Reg[12] = (Uint8)(weekday % 7);
    }

    m_Reg[16] = (Uint8)(t >> 0);
    m_Reg[17] = (Uint8)(t >> 8);
    m_Reg[18] = (Uint8)(t >> 16);
    m_Reg[19] = (Uint8)(t >> 24);
}

void SNSRTC::UpdateTime()
{
    Uint32 rtc_time = (Uint32)(m_Reg[16] | (m_Reg[17] << 8) | (m_Reg[18] << 16) | (m_Reg[19] << 24));
    Uint32 current  = GetTime();

    // diff em segundos, compensando overflow/underflow do contador de 32 bits
    Uint32 diff = (current >= rtc_time)
                ? (current - rtc_time)
                : (0xFFFFFFFFu - rtc_time + current + 1);
    if (diff > 0x7FFFFFFFu)
        diff = 0;

    if (diff > 0)
    {
        Uint32 second  = m_Reg[0] + m_Reg[1] * 10;
        Uint32 minute  = m_Reg[2] + m_Reg[3] * 10;
        Uint32 hour    = m_Reg[4] + m_Reg[5] * 10;
        Uint32 day     = m_Reg[6] + m_Reg[7] * 10;
        Uint32 month   = m_Reg[8];
        Uint32 year    = m_Reg[9] + m_Reg[10] * 10 + m_Reg[11] * 100;
        Uint32 weekday = m_Reg[12];

        day--;
        month--;
        year += 1000;

        second += diff;
        while (second >= 60)
        {
            second -= 60;

            minute++;
            if (minute < 60) continue;
            minute = 0;

            hour++;
            if (hour < 24) continue;
            hour = 0;

            day++;
            weekday = (weekday + 1) % 7;
            {
                Uint32 days = s_Months[month % 12];
                if (days == 28 && _IsLeap(year))
                    days++;
                if (day < days) continue;
            }
            day = 0;

            month++;
            if (month < 12) continue;
            month = 0;

            year++;
        }

        day++;
        month++;
        year -= 1000;

        m_Reg[0]  = (Uint8)(second % 10);
        m_Reg[1]  = (Uint8)(second / 10);
        m_Reg[2]  = (Uint8)(minute % 10);
        m_Reg[3]  = (Uint8)(minute / 10);
        m_Reg[4]  = (Uint8)(hour % 10);
        m_Reg[5]  = (Uint8)(hour / 10);
        m_Reg[6]  = (Uint8)(day % 10);
        m_Reg[7]  = (Uint8)(day / 10);
        m_Reg[8]  = (Uint8)month;
        m_Reg[9]  = (Uint8)(year % 10);
        m_Reg[10] = (Uint8)((year / 10) % 10);
        m_Reg[11] = (Uint8)(year / 100);
        m_Reg[12] = (Uint8)(weekday % 7);
    }

    m_Reg[16] = (Uint8)(current >> 0);
    m_Reg[17] = (Uint8)(current >> 8);
    m_Reg[18] = (Uint8)(current >> 16);
    m_Reg[19] = (Uint8)(current >> 24);
}

// dia da semana (0=domingo .. 6=sabado). Epoca: 1900-01-01 (segunda).
Uint32 SNSRTC::Weekday(Uint32 year, Uint32 month, Uint32 day)
{
    Uint32 y = 1900, m = 1;
    Uint32 sum = 0;

    if (year < 1900) year = 1900;
    if (month < 1)  month = 1;
    if (month > 12) month = 12;
    if (day < 1)  day = 1;
    if (day > 31) day = 31;

    while (y < year)
    {
        sum += _IsLeap(y) ? 366 : 365;
        y++;
    }

    while (m < month)
    {
        Uint32 days = s_Months[m - 1];
        if (days == 28 && _IsLeap(y))
            days++;
        sum += days;
        m++;
    }

    sum += day - 1;
    return (sum + 1) % 7;   // 1900-01-01 foi uma segunda-feira
}

Uint8 SNSRTC::ReadReg()
{
    if (m_Mode != RTCM_Read)
        return 0x00;

    if (m_Index < 0)
    {
        UpdateTime();
        m_Index++;
        return 0x0f;
    }
    else if (m_Index > 12)
    {
        m_Index = -1;
        return 0x0f;
    }
    else
    {
        return m_Reg[m_Index++];
    }
}

void SNSRTC::WriteReg(Uint8 d)
{
    d &= 0x0f;   // so' os 4 bits baixos importam

    if (d == 0x0d)
    {
        m_Mode  = RTCM_Read;
        m_Index = -1;
        return;
    }

    if (d == 0x0e)
    {
        m_Mode = RTCM_Command;
        return;
    }

    if (d == 0x0f)
        return;   // comportamento desconhecido

    if (m_Mode == RTCM_Write)
    {
        if (m_Index >= 0 && m_Index < 12)
        {
            m_Reg[m_Index++] = d;

            if (m_Index == 12)
            {
                // o dia da semana e' calculado e gravado automaticamente
                Uint32 day   = m_Reg[6] + m_Reg[7] * 10;
                Uint32 month = m_Reg[8];
                Uint32 year  = m_Reg[9] + m_Reg[10] * 10 + m_Reg[11] * 100;
                year += 1000;

                m_Reg[m_Index++] = (Uint8)Weekday(year, month, day);
            }
        }
    }
    else if (m_Mode == RTCM_Command)
    {
        if (d == 0)
        {
            m_Mode  = RTCM_Write;
            m_Index = 0;
        }
        else if (d == 4)
        {
            m_Mode  = RTCM_Ready;
            m_Index = -1;
            for (Int32 i = 0; i < 13; i++)
                m_Reg[i] = 0;
        }
        else
        {
            m_Mode = RTCM_Ready;
        }
    }
}
