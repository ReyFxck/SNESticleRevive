/*
 * sndsp4.h - DSP-4 (NEC uPD7725) coprocessor HLE  [EXPERIMENTAL]
 *
 * O DSP-4 e' da mesma familia do DSP-1/2/3 (NEC uPD7725); so' muda a
 * microcode interna.  E' usado APENAS por "Top Gear 3000" (USA) e
 * "The Planet's Champ TG3000" (Japan).  Ele gera, em tempo real, a
 * projecao em perspectiva da pista e a posicao dos sprites (OAM) ao
 * longo dela -- incluindo a pista que se divide em dois caminhos.
 *
 * IMPORTANTE / honestidade tecnica:
 *   Ao contrario do DSP-1 e do DSP-2, os comandos FUNCIONAIS do DSP-4
 *   NAO tem especificacao publica.  A doc disponivel
 *     - https://snes.nesdev.org/wiki/DSP-4
 *     - https://sneslab.net/wiki/DSP4
 *     - https://problemkaputt.de/fullsnes.htm (nocash)
 *   documenta apenas o PROTOCOLO de barramento (transfers de 16 bits,
 *   RQM, terminador 0xFFFF) e dois comandos de teste:
 *     0x13 = Test Transfer DATA ROM
 *     0x14 = Test ROM Version  -> 0x0400 (=DSP4)
 *   Os comandos de renderizacao aparecem como "Unknown" em toda fonte
 *   publica; a unica referencia da matematica e' codigo de emulador com
 *   licenca incompativel com a MIT deste projeto (snes9x = nao-comercial,
 *   bsnes = GPL) -- portanto NAO foi copiado.
 *
 *   Esta classe implementa o protocolo correto e self-contained (sem
 *   precisar de firmware dsp4.rom).  O comando de versao e' real.  Os
 *   comandos de renderizacao ficam INERTES (devolvem o terminador
 *   0xFFFF, sem travar o jogo) e LOGam o opcode visto uma unica vez,
 *   para reconstruirmos a matematica observando o PROPRIO jogo
 *   (clean-room observacional) nas proximas iteracoes.
 *
 * Reaproveita o trap/decode de DSP do snes.cpp (ReadDSP1/WriteDSP1 +
 * _SnesDsp1IsStatus): basta apontar m_pDsp para uma instancia desta
 * classe quando o cartucho for DSP-4.
 */
#ifndef _SNDSP4_H
#define _SNDSP4_H

#include "types.h"
#include "sndsp.h"

class SNDSP4 : public ISNDSP
{
public:
    SNDSP4();

    // ISNDSP
    void  Reset();
    void  WriteData(Uint32 uAddr, Uint8 uData);
    Uint8 ReadData (Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);

private:
    // ------------------------------------------------------------------
    // Protocolo de 16 bits.  A CPU do SNES escreve/le o DR um BYTE por
    // vez (LSB primeiro, depois MSB); aqui montamos/desmontamos as
    // palavras de 16 bits.  O Status Register fica sempre pronto
    // (bit7 RQM = 1 -> 0x80), igual ao HLE do DSP-2.
    // ------------------------------------------------------------------
    Bool   m_bWaitCmd;        // esperando a palavra de comando
    Uint16 m_uCommand;        // comando atual (palavra de 16 bits)

    // montagem da palavra na ESCRITA (LSB depois MSB)
    Bool   m_bHaveLo;
    Uint8  m_uWrLo;

    Int32  m_nIn,  m_iIn;     // palavras de parametro esperadas / recebidas
    Int32  m_nOut;            // palavras de saida disponiveis
    Int32  m_iOutByte;        // indice de BYTE corrente na saida

    Uint16 m_In [256];        // buffer de parametros (palavras)
    Uint16 m_Out[256];        // buffer de saida (palavras)

    // diagnostico: marca opcodes ja logados para nao floodar o log
    Uint8  m_SeenCmd[256];

    Int32 CommandInWords(Uint16 uCmd);   // quantas palavras de parametro
    void  Execute();
};

#endif
