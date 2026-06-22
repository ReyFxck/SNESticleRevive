/*
 * snsrtc.h - S-RTC (Sharp Real-Time Clock) coprocessor HLE
 *
 * Relogio de tempo real usado em Daikaijuu Monogatari II (Super Shell
 * Monsters Story II). O jogo le data/hora reais para um ciclo dia/noite e
 * eventos baseados em tempo. Sem o chip, o jogo trava/comporta-se errado ao
 * acessar os registradores do relogio.
 *
 * Interface (bancos $00-$3F / $80-$BF):
 *   $2800 (leitura): porta de dados; sequencia de 13 nibbles de data/hora
 *   $2801 (escrita): porta de comando/dados (FSM de 4 modos)
 *
 * Protocolo (engenharia reversa publica, byuu):
 *   - escrever 0x0D em $2801: entra em modo leitura, reinicia o indice
 *   - escrever 0x0E: entra em modo comando
 *   - em modo comando: 0x00 -> modo escrita; 0x04 -> limpa/pronto
 *   - em modo escrita: os nibbles sao gravados em sequencia; o dia-da-semana
 *     e' calculado automaticamente
 *   - ler $2800 em modo leitura: devolve 0x0F (inicio), os 13 nibbles, 0x0F (fim)
 *
 * Implementacao clean-room. A fonte de tempo e' o relogio do host (time()).
 * No reset, semeia os registradores a partir de gmtime() para que o jogo veja
 * a data/hora reais (UTC); update_time avanca a partir do timestamp salvo.
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
