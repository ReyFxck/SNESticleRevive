/*
 * sndsp2.h - DSP-2 (NEC uPD7725) coprocessor HLE
 *
 * O DSP-2 e' da mesma familia do DSP-1, mas a microcode e os comandos
 * sao outros.  Usado APENAS em "Dungeon Master" (SNES).  Os comandos
 * fazem manipulacao de bitmap (conversao de bitplane, overlay com
 * transparencia, swap de nibble, escala) e uma multiplicacao 16x16.
 *
 * Implementacao clean-room a partir do comportamento documentado do
 * chip (engenharia reversa publica - Overload et al.):
 *   - https://www.sneslab.net/wiki/DSP-2
 *
 * Protocolo de barramento HLE (mais simples que o do DSP-1): orientado
 * a BYTES, com o Status Register sempre pronto (0x80 = RQM).  A CPU
 * escreve o opcode + N bytes de parametro e le M bytes de saida.
 *
 * Reaproveita o trap/decode de DSP do snes.cpp (ReadDSP1/WriteDSP1 +
 * _SnesDsp1IsStatus): basta apontar m_pDsp para uma instancia desta
 * classe quando o cartucho for DSP-2.
 */
#ifndef _SNDSP2_H
#define _SNDSP2_H

#include "types.h"
#include "sndsp.h"

class SNDSP2 : public ISNDSP
{
public:
    SNDSP2();

    // ISNDSP
    void  Reset();
    void  WriteData(Uint32 uAddr, Uint8 uData);
    Uint8 ReadData (Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);

private:
    // ---- estado do protocolo (byte-oriented) ----
    Uint8 m_uCommand;       // opcode atual
    Bool  m_bWaitCmd;       // esperando byte de comando
    Int32 m_nIn,  m_iIn;    // bytes de entrada esperados / recebidos
    Int32 m_nOut, m_iOut;   // bytes de saida disponiveis / lidos

    Uint8 m_Param[512];     // buffer de parametros
    Uint8 m_Out  [512];     // buffer de saida

    // ---- estado por comando ----
    Uint8 m_Op05Transparent;          // cor transparente (op03)
    Bool  m_bOp05HasLen; Int32 m_Op05Len;
    Bool  m_bOp06HasLen; Int32 m_Op06Len;
    Bool  m_bOp0DHasLen; Int32 m_Op0DInLen, m_Op0DOutLen;

    void Execute(Uint8 uLastByte);
    void DoOp01();   // conversao de bitplane (32->32)
    void DoOp05();   // overlay com transparencia
    void DoOp06();   // swap de nibble + reverse
    void DoOp0D();   // escala de bitmap
};

#endif
