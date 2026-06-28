/*
 * sndsp4.cpp - DSP-4 (NEC uPD7725) coprocessor HLE  [EXPERIMENTAL]
 *
 * Veja sndsp4.h para o contexto completo (e a nota de honestidade
 * tecnica: os comandos de renderizacao do DSP-4 NAO tem spec publica).
 *
 * Aqui esta implementado o PROTOCOLO de barramento (clean-room a partir
 * da doc publica: transfers de 16 bits LSB-first, RQM sempre pronto,
 * terminador 0xFFFF) + o comando de versao (real).  Os comandos
 * funcionais ficam inertes e logam o opcode visto, para engenharia
 * reversa observacional a partir do proprio jogo.
 */

#include "types.h"
#include "sndsp4.h"
#include "console.h"

#include <string.h>
#include <stdio.h>

SNDSP4::SNDSP4()
{
    memset(m_SeenCmd, 0, sizeof(m_SeenCmd));
    Reset();
}

void SNDSP4::Reset()
{
    m_bWaitCmd  = TRUE;
    m_uCommand  = 0;
    m_bHaveLo   = FALSE;
    m_uWrLo     = 0;
    m_nIn = m_iIn = 0;
    m_nOut      = 0;
    m_iOutByte  = 0;

    memset(m_In,  0, sizeof(m_In));
    memset(m_Out, 0, sizeof(m_Out));
    // m_SeenCmd NAO e' limpo no Reset de propósito: queremos logar cada
    // opcode so' uma vez por sessao, nao a cada reset do jogo.
}

//==========================================================================
//  Numero de palavras de parametro esperadas por comando.
//
//  So' os comandos de TESTE tem comportamento documentado.  Os comandos
//  de renderizacao sao "Unknown": retornamos 0 (sem parametros) para que
//  cada palavra escrita pelo jogo seja tratada como um novo comando e
//  apareca no log -- assim capturamos o stream real para RE futura, sem
//  travar nem dessincronizar de forma destrutiva.
//==========================================================================
Int32 SNDSP4::CommandInWords(Uint16 uCmd)
{
    switch (uCmd & 0xFF)
    {
    case 0x13:  // Test Transfer DATA ROM
    case 0x14:  // Test ROM Version
        return 0;
    default:
        return 0;
    }
}

//==========================================================================
//  Dispatch
//==========================================================================
void SNDSP4::Execute()
{
    Uint8 op = (Uint8)(m_uCommand & 0xFF);

    m_nOut     = 0;
    m_iOutByte = 0;

    switch (op)
    {
    case 0x14:  // Test ROM Version -> 0x0400 identifica o DSP-4 (REAL)
        m_Out[0] = 0x0400;
        m_nOut   = 1;
        break;

    case 0x13:  // Test Transfer DATA ROM
        // O HLE nao embute a Data ROM do silicio; sem dados para devolver.
        // ReadData servira 0xFFFF (terminador), que e' o esperado ao ler
        // alem do fim de uma transferencia valida.
        m_nOut = 0;
        break;

    default:
        // ------------------------------------------------------------------
        // Comando de renderizacao do DSP-4 (projecao de pista / OAM).
        // Sem spec publica -> INERTE: nao trava o jogo (devolve 0xFFFF) e
        // loga o opcode UMA vez para reconstruirmos a matematica observando
        // o proprio jogo nas proximas iteracoes (clean-room observacional).
        // ------------------------------------------------------------------
        if (!m_SeenCmd[op])
        {
            m_SeenCmd[op] = 1;
            ConDebug("[dsp4] opcode 0x%02X visto (sem impl ainda; inerte)\n",
                     (unsigned)op);
        }
        m_nOut = 0;
        break;
    }
}

//==========================================================================
//  Interface de barramento (ISNDSP)
//==========================================================================

void SNDSP4::WriteData(Uint32 /*uAddr*/, Uint8 uData)
{
    // monta a palavra de 16 bits (LSB primeiro, igual ao SNES real)
    if (!m_bHaveLo)
    {
        m_uWrLo   = uData;
        m_bHaveLo = TRUE;
        return;
    }
    Uint16 uWord = (Uint16)(m_uWrLo | ((Uint16)uData << 8));
    m_bHaveLo = FALSE;

    if (m_bWaitCmd)
    {
        m_uCommand = uWord;
        m_iIn      = 0;
        m_nIn      = CommandInWords(uWord);
        if (m_nIn <= 0)
        {
            // comando sem parametros: executa imediatamente
            Execute();
        }
        else
        {
            m_bWaitCmd = FALSE;
        }
    }
    else
    {
        if (m_iIn < (Int32)(sizeof(m_In) / sizeof(m_In[0])))
            m_In[m_iIn] = uWord;
        m_iIn++;

        if (m_iIn >= m_nIn)
        {
            m_bWaitCmd = TRUE;
            Execute();
        }
    }
}

Uint8 SNDSP4::ReadData(Uint32 /*uAddr*/)
{
    // serve a saida byte a byte (LSB depois MSB de cada palavra)
    Int32 nTotalBytes = m_nOut * 2;
    if (m_iOutByte < nTotalBytes)
    {
        Uint16 uWord = m_Out[m_iOutByte >> 1];
        Uint8  uByte = (m_iOutByte & 1) ? (Uint8)(uWord >> 8)
                                        : (Uint8)(uWord & 0xFF);
        m_iOutByte++;
        return uByte;
    }
    // alem do fim -> terminador 0xFFFF (DR deve conter 0xFFFF ao
    // completar um comando valido, conforme a doc do DSP-4)
    return 0xFF;
}

Uint8 SNDSP4::ReadStatus(Uint32 /*uAddr*/)
{
    // DSP-4 HLE: sempre pronto (RQM=1 -> bit7).
    return 0x80;
}
