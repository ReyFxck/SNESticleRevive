/*
 * snsdd1.h - S-DD1 (Super Data Decompression 1) coprocessor HLE
 *
 * O S-DD1 e' um chip da Ricoh que descomprime graficos em tempo real
 * durante transferencias de DMA. Usado em:
 *   - Star Ocean (Japan)
 *   - Street Fighter Alpha 2 / Street Fighter Zero 2
 *
 * Sem o chip, esses jogos nao conseguem carregar os graficos comprimidos
 * (telas pretas / lixo) e tambem nao enxergam toda a ROM (Star Ocean tem
 * 48 Mbit, acessados pela janela remapeavel $C0-$FF).
 *
 * O chip tem tres funcoes:
 *   1. Registradores $4800-$4807
 *      - $4801: habilita descompressao no proximo DMA (bit por canal,
 *        disparo unico, limpo apos o uso)
 *      - $4804-$4807: selecionam o segmento de 1MB da ROM mapeado nos
 *        grupos de bancos $C0-$CF, $D0-$DF, $E0-$EF, $F0-$FF
 *   2. Remapeamento dos bancos $C0-$FF (feito pelo core via SNCPUSetBank)
 *   3. Descompressao: quando um DMA num canal habilitado em $4801 dispara,
 *      o chip le os dados comprimidos da ROM e devolve bytes descomprimidos.
 *
 * O algoritmo de descompressao (codificacao Golomb + modelo de contexto
 * adaptativo sobre bitplanes) e' a engenharia reversa publica do Andreas
 * Naive. Implementacao clean-room; as tabelas numericas (evolucao de estado
 * e run-length) sao constantes factuais do hardware.
 *   - https://wiki.superfamicom.org/s-dd1
 */
#ifndef _SNSDD1_H
#define _SNSDD1_H

#include "types.h"

class SNSDD1
{
public:
    SNSDD1();

    void  Reset();

    // ---- registradores $4800-$4807 ----
    Uint8 ReadReg(Uint32 uAddr);
    void  WriteReg(Uint32 uAddr, Uint8 uData);

    // disparo de DMA: o S-DD1 descomprime quando ha' um DMA com endereco-A
    // fixo e $4801 != 0 (mesma condicao do snes9x). $4801 e' limpo apos o uso.
    Bool  DmaActive() const { return m_Reg[1] != 0; }
    Bool  DmaEnabled(Uint32 uChan) const { return (m_Reg[1] >> uChan) & 1; }
    void  ClearDmaEnable() { m_Reg[1] = 0; }

    // segmento de 1MB do grupo de bancos (0=$C0-$CF .. 3=$F0-$FF)
    Uint8 BankSegment(Uint32 uGroup) const { return m_Reg[4 + (uGroup & 3)]; }

    // verdadeiro se a ultima escrita foi num registrador de mapeamento
    // ($4804-$4807); o core deve refazer o mapa de bancos nesse caso.
    Bool  MapDirty() const { return m_bMapDirty; }
    void  ClearMapDirty() { m_bMapDirty = FALSE; }

    // ---- descompressao ----
    // descomprime 'len' bytes (0 = 0x10000) de 'pIn' para 'pOut'
    void  Decompress(Uint8 *pOut, const Uint8 *pIn, Int32 len);

private:
    Uint8 m_Reg[8];        // $4800-$4807
    Bool  m_bMapDirty;

    // ---- estado do descompressor ----
    Int32        m_ValidBits;
    Uint16       m_InStream;
    const Uint8 *m_pInBuf;
    Uint8        m_BitCtr[8];
    Uint8        m_CtxState[32];
    Int32        m_CtxMPS[32];
    Int32        m_BitplaneType;
    Int32        m_HighCtxBits;
    Int32        m_LowCtxBits;
    Int32        m_PrevBits[8];

    Uint8 GetCodeword(Int32 nBits);
    Uint8 GolombGetBit(Int32 nCodeSize);
    Uint8 ProbGetBit(Uint8 uContext);
    Uint8 GetBit(Uint8 uCurBitplane);
};

#endif
