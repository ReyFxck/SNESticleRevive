/*
 * sndsp4.h - DSP-4 (NEC uPD7725) coprocessor HLE
 *
 * O DSP-4 e' da mesma familia do DSP-1/2/3 (NEC uPD7725); so' muda a
 * microcode interna.  E' usado APENAS por "Top Gear 3000" (USA) e
 * "The Planet's Champ TG3000" (Japan).  Ele gera, em tempo real, a
 * projecao em perspectiva da pista (tabelas de HDMA para scroll/janela
 * de BG1/BG2 linha-a-linha) e a posicao dos sprites (OAM) ao longo
 * dela -- incluindo a pista que se divide em dois caminhos (turnoff).
 *
 * Implementacao HLE clean-room:
 *   O PROTOCOLO de barramento e os comandos de teste (0x13/0x14) sao
 *   documentados publicamente (nesdev/sneslab/nocash).  A MATEMATICA dos
 *   comandos de renderizacao aparece como "Unknown" em toda fonte
 *   publica -- a unica descricao do algoritmo esta em codigo de emulador.
 *   Aqui o ALGORITMO (comportamento do silicio, nao protegivel) foi
 *   estudado e REIMPLEMENTADO do zero, com estrutura/nomes/comentarios
 *   proprios e otimizado para o PS2 (apenas inteiros 16/32 bits), do
 *   mesmo jeito que foi feito para CX4/OBC1/S-DD1/DSP-1/DSP-2 neste
 *   projeto.  Nenhum bloco de codigo foi copiado.
 *
 * Self-contained: NAO precisa de firmware externo (dsp4.rom).  Reaproveita
 * o trap de DSP do snes.cpp (ReadDSP1/WriteDSP1 + _SnesDsp1IsStatus):
 * basta apontar m_pDsp para uma instancia desta classe (snmemmap.cpp).
 *
 * --------------------------------------------------------------------------
 * Modelo de barramento (igual ao DSP-1, mas SEMPRE 16-bit / orientado a byte):
 *
 *   A CPU acessa o DR (Data Register) um BYTE por vez (LSB depois MSB).
 *   - Fase de COMANDO: 2 bytes montam a palavra de comando (LSB-first).
 *   - Fase de PARAMETROS: cada byte vai para um buffer de bytes; o
 *     numero de bytes esperados (m_nInBytes) depende do comando.
 *   - Quando o buffer enche, o comando executa e enche o buffer de SAIDA.
 *   - A CPU le a saida byte a byte; ler alem do fim devolve 0xFF (a doc
 *     diz que o DR deve conter 0xFFFF ao fim de um comando valido).
 *   - O Status (SR) le sempre 0x80 (RQM=1, pronto).
 *
 *   Comandos de projecao sao "streaming": eles processam um bloco,
 *   devolvem saida, e PEDEM mais bytes (mudando m_nInBytes no meio).
 *   Isso e' modelado com um estado de retomada (m_iResume) por comando.
 * --------------------------------------------------------------------------
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
    //----------------------------------------------------------------------
    // FSM de barramento
    //----------------------------------------------------------------------
    Bool   m_bWaitCmd;        // TRUE: esperando palavra de comando
    Bool   m_bHalfCmd;        // TRUE: ja recebeu o byte baixo do comando
    Uint16 m_uCommand;        // comando corrente (16 bits)

    Int32  m_nInBytes;        // bytes de parametro esperados
    Int32  m_iInByte;         // cursor de escrita/leitura no buffer de params
    Int32  m_nOutBytes;       // bytes de saida disponiveis
    Int32  m_iOutByte;        // cursor de leitura no buffer de saida

    Int32  m_iResume;         // ponto de retomada do comando streaming (0=inicio)

    Uint8  m_Param[512];      // buffer de parametros (bytes, little-endian)
    Uint8  m_Out  [2048];     // buffer de saida (bytes, little-endian)

    // diagnostico: opcodes ja logados (uma vez por sessao)
    Uint8  m_SeenCmd[256];

    //----------------------------------------------------------------------
    // Estado de projecao da pista (fixed-point 1.15.16 nos *_x/_y de mundo)
    //----------------------------------------------------------------------
    Int32  m_WorldX,  m_WorldY;       // posicao no mundo (1.15.16)
    Int32  m_WorldDX, m_WorldDY;      // delta por linha (1.15.16)
    Int16  m_WorldDDX, m_WorldDDY;    // aceleracao (1.7.8)
    Int32  m_WorldXEnv;               // envelope X (1.15.16)
    Int16  m_WorldYOfs;               // offset Y do mundo

    Int16  m_ViewX1, m_ViewY1;        // ponto projetado anterior
    Int16  m_ViewX2, m_ViewY2;        // ponto projetado atual
    Int16  m_ViewXOfs1, m_ViewYOfs1;  // scroll projetado anterior
    Int16  m_ViewXOfs2, m_ViewYOfs2;  // scroll projetado atual
    Int16  m_ViewDX, m_ViewDY;        // passo de "shaping"
    Int16  m_ViewYOfsEnv;             // envelope do offset Y de view
    Int16  m_ViewTurnoffX, m_ViewTurnoffDX;  // desvio da pista (bifurcacao)

    Int16  m_Distance;                // fator de perspectiva (escala)
    Int16  m_Segments;                // linhas de raster nesta iteracao
    Int16  m_Lcv;                     // contador de loop

    // poligonos: [forma][lado]
    Int16  m_PolyBottom[2][2];        // primeira linha de raster (abaixo do horizonte)
    Int16  m_PolyTop   [2][2];        // linha superior de corte
    Int16  m_PolyCX    [2][2];        // centragem horizontal
    Int16  m_PolyPtr   [2][2];        // ponteiro da tabela HDMA
    Int16  m_PolyRaster[2][2];        // cursor de raster corrente
    Int16  m_PolyClipLf[2][2];        // limite de corte esquerdo
    Int16  m_PolyClipRt[2][2];        // limite de corte direito
    Int16  m_PolyStart [2];           // base X de projecao
    Int16  m_PolyPlane [2];           // distancia (plano) corrente

    //----------------------------------------------------------------------
    // Estado de sprites / OAM
    //----------------------------------------------------------------------
    Int16  m_ViewportCX, m_ViewportCY;
    Int16  m_ViewportLeft, m_ViewportRight, m_ViewportTop, m_ViewportBottom;
    Int16  m_Raster;
    Int16  m_SpriteX, m_SpriteY, m_SpriteAttr;
    Int16  m_SpriteSize, m_SpriteClipY;
    Int16  m_SpriteCount;

    Int16  m_OAMRowMax;
    Uint8  m_OAMRow [64];     // contador de tiles por faixa de 8px (idx 0..31)
    Uint16 m_OAMAttr[16];     // bits de tamanho/MSB de X empacotados (16 words)
    Int16  m_OAMIndex;
    Int16  m_OAMBits;

    //----------------------------------------------------------------------
    // Helpers de buffer e matematica
    //----------------------------------------------------------------------
    Int16  ReadWord();                // le 16 bits (LE) do buffer de params
    Int32  ReadDword();               // le 32 bits (LE) do buffer de params
    void   WriteWord(Int32 d);        // escreve 16 bits (LE) na saida
    void   WriteByte(Int32 d);        // escreve 1 byte na saida
    void   ClearOut();                // reinicia o buffer de saida

    static Int16 Inverse(Int16 v);    // 1/v aproximado (tabela)

    //----------------------------------------------------------------------
    // Comandos
    //----------------------------------------------------------------------
    Int32  CommandInBytes(Uint16 uCmd);  // bytes de parametro por comando
    void   Execute();                    // despacha o comando corrente

    // ops de projecao / OAM (streaming = re-entrantes via m_iResume)
    void   Op01();   // projecao de pista (1 jogador)
    void   Op03();   // selecao de sprite (1 jogador)
    void   Op05();   // limpa OAM
    void   Op06();   // transfere OAM
    void   Op07();   // projecao da bifurcacao de pista (1 jogador)
    void   Op08();   // poligono solido (2 formas)
    void   Op09();   // projecao de sprites
    void   Op0A(Int16 n, Int16 *o1, Int16 *o2, Int16 *o3, Int16 *o4);
    void   Op0B(Bool *bDraw, Int16 spx, Int16 spy, Int16 attr, Bool bSize, Bool bStop);
    void   Op0D();   // projecao de pista (multiplayer)
    void   Op0E();   // selecao (multiplayer)
    void   Op0F();   // projecao de pista com iluminacao (1 jogador)
    void   Op10();   // projecao da bifurcacao com iluminacao (1 jogador)
    void   Op11(Int16 a, Int16 b, Int16 c, Int16 d, Int16 *m);
};

#endif
