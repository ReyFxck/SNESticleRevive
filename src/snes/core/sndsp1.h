/*
 * sndsp1.h - DSP-1 / DSP-1B coprocessor emulation header
 *
 * Esta implementacao reproduz o comportamento microcoded do DSP-1
 * a partir do trabalho publico de engenharia reversa publicado em
 * Junho/2006 por:
 *
 *     Overload, The Dumper, Neviksti e Andreas Naive
 *
 * Esse trabalho inclui o dump da Data ROM interna do chip (1024
 * palavras de 16 bits), a documentacao do protocolo de barramento
 * (DR/SR, bits DRC/DRS/RQM) e a matematica de cada comando.  As
 * tabelas SinTable/MulTable e a DataRom embutidas no .cpp sao
 * dados factuais do silicio (nao expressao criativa).
 *
 * Documentacao publica:
 *   - https://snes.nesdev.org/wiki/DSP-1
 *   - https://www.sneslab.net/wiki/DSP-1
 *   - "DSP-1 reverse engineering notes", Overload et al, 2006
 *
 * Otimizado para alvo PS2: nada de float, multiplicacao 16x16
 * com shift, sem alocacao dinamica, tabelas estaticas pequenas
 * (~3.5KB total) que cabem em cache.
 */
#ifndef _SNDSP1_H
#define _SNDSP1_H

#include "types.h"
#include "sndsp.h"

class SNDSP1 : public ISNDSP
{
public:
    SNDSP1();

    // ISNDSP
    void  Reset();
    void  WriteData(Uint32 uAddr, Uint8 uData);
    Uint8 ReadData (Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);

    static SNDSP1 *GetInstance();

private:
    // ------------------------------------------------------------------
    // Bits do Status Register (todos referidos ao byte alto, ja que so
    // o byte alto e' visivel pela CPU)
    //
    //   DRC bit 2 (0x04) = 1 -> transferencia de 8 bits (espera opcode)
    //                       0 -> transferencia de 16 bits (data words)
    //   DRS bit 4 (0x10) = toggle MSB/LSB dentro de uma palavra de 16
    //   RQM bit 7 (0x80) = chip pronto para troca de byte com a CPU
    // ------------------------------------------------------------------
    enum SrFlag { SR_DRC = 0x04, SR_DRS = 0x10, SR_RQM = 0x80 };

    enum FsmState { FSM_WAIT_CMD = 0, FSM_READ_DATA = 1, FSM_WRITE_DATA = 2 };

    // -------- estado do barramento --------
    Uint8   m_uSR;             // byte alto do status register
    Uint16  m_uDR;             // data register interno (16-bit)
    Uint8   m_uFsmState;
    Uint8   m_uCommand;        // opcode atual em execucao
    Uint16  m_uDataCounter;    // indice da palavra atual no buffer
    Uint8   m_bFreeze;         // op1A/2A/3A: trava o chip

    // -------- buffers de palavras --------
    // 7 entradas e' o max usado por qualquer comando do DSP-1
    // (Parameter).  Saidas vao ate 1024 palavras (MemoryDump op1F).
    Int16   m_InWords [8];
    Int16   m_OutWords[1024];

    // ------------------------------------------------------------------
    // Estado matematico compartilhado entre comandos (replica o RAM
    // interno do chip).  Nomes seguem o documento de RE publico.
    // ------------------------------------------------------------------
    Int16   m_MatA[3][3];
    Int16   m_MatB[3][3];
    Int16   m_MatC[3][3];

    Int16   m_CentreX, m_CentreY, m_CentreZ;
    Int16   m_CentreZ_C, m_CentreZ_E;
    Int16   m_VOffset;
    Int16   m_Les, m_C_Les, m_E_Les;
    Int16   m_SinAas, m_CosAas;
    Int16   m_SinAzs, m_CosAzs;
    Int16   m_SinAZS, m_CosAZS;
    Int16   m_SecAZS_C1, m_SecAZS_E1;
    Int16   m_SecAZS_C2, m_SecAZS_E2;
    Int16   m_Nx, m_Ny, m_Nz;
    Int16   m_Gx, m_Gy, m_Gz;
    Int16   m_Hx, m_Hy;
    Int16   m_Vx, m_Vy, m_Vz;

    // ------------------------------------------------------------------
    // FSM
    // ------------------------------------------------------------------
    void FsmStep(bool bRead, Uint8 &rData);
    void Execute(Uint8 uCmd);
};

#endif
