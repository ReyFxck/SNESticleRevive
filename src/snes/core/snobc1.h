/*
 * snobc1.h - OBC1 coprocessor (Metal Combat: Falcon's Revenge)
 *
 * O OBC1 nao e' um DSP: e' um controladorzinho que ajuda o jogo a montar
 * a tabela de OAM (sprites). Ele expoe 8KB de RAM em $6000-$7FFF, com
 * alguns registradores no topo ($7FF0-$7FF6) que dao uma "janela" para
 * escrever os dados de um sprite por vez; o jogo depois faz DMA dessa RAM
 * para a OAM.
 *
 * Implementacao clean-room a partir do comportamento documentado.
 * Unico jogo: Metal Combat.
 */
#ifndef _SNOBC1_H
#define _SNOBC1_H

#include "types.h"

class SNOBC1
{
public:
    SNOBC1();

    void  Reset();
    Uint8 Read (Uint32 uAddr);             // uAddr = endereco baixo ($6000-$7FFF)
    void  Write(Uint32 uAddr, Uint8 uData);

private:
    Uint8 m_Ram[0x2000];   // 8KB de RAM interna ($6000-$7FFF)
    Int32 m_Address;       // indice do sprite atual (0..0x7F)
    Int32 m_BasePtr;       // base da tabela: 0x1C00 ou 0x1800
    Int32 m_Shift;         // deslocamento (0/2/4/6) no byte da tabela alta
};

#endif
