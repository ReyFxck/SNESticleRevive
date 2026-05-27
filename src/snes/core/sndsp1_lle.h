/*
 * sndsp1_lle.h - DSP-1 Low-Level Emulation (uPD7725 microcontroller)
 *
 * ----------------------------------------------------------------------
 *  Implementacao CLEAN-ROOM do microcontrolador NEC uPD7725 que roda
 *  dentro do chip DSP-1.  Esta nao e' uma traducao das tabelas
 *  matematicas usadas pela HLE; aqui a CPU do DSP-1 e' interpretada
 *  ciclo a ciclo, executando o microcodigo gravado na Program ROM
 *  do chip real.
 *
 *  Fontes publicas usadas (somente):
 *    - "uPD77C25 / uPD7725 Data Sheet", NEC Electronics
 *      (formato de instrucao, ALU, bits do SR, latencias).
 *    - "DSP-1 reverse engineering notes", Andreas Naive et al, Junho/2006
 *      (mapeamento de SRC/DST, fluxo do barramento, encoding do
 *       campo BRCH, dump da Data ROM).
 *
 *  NENHUM trecho de codigo de bsnes, snes9x ou outras emulacoes foi
 *  consultado durante a escrita.  As tabelas de constantes (data ROM)
 *  sao dados factuais do silicio e nao expressao criativa.
 *
 *  Alvo: Emotion Engine (MIPS/PS2).  Apenas inteiros 16/32 bits, sem
 *  ponto-flutuante, sem alocacao dinamica, sem RTTI nem excecoes.
 * ----------------------------------------------------------------------
 */

#ifndef _SNDSP1_LLE_H
#define _SNDSP1_LLE_H

#include "types.h"
#include "sndsp.h"


class SNDSP1_LLE : public ISNDSP
{
public:
    SNDSP1_LLE();

    // -----------------------------------------------------------------
    //  Carregamento da Program ROM e da Data ROM
    //
    //  prom : buffer com instrucoes de 24 bits, big-endian (3 bytes
    //         por instrucao).  Tamanho aceitavel: 6144 (2048 instr.)
    //         ou 8192 bytes (alguns dumps tem padding ate 8KB).
    //  drom : 1024 palavras de 16 bits da Data ROM (formato nativo).
    //
    //  Devem ser chamados ANTES do primeiro Reset/WriteData.
    // -----------------------------------------------------------------
    void  LoadProgramRom(const Uint8 *prom, Uint32 size);
    void  LoadDataRom   (const Uint16 *drom, Uint32 size);

    // ISNDSP interface
    void  Reset();
    void  WriteData (Uint32 uAddr, Uint8 uData);
    Uint8 ReadData  (Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);

private:
    // -----------------------------------------------------------------
    //  Constantes do uPD7725
    // -----------------------------------------------------------------
    enum {
        PROM_SIZE  = 2048,        // 2048 instrucoes de 24 bits
        DROM_SIZE  = 1024,        // 1024 palavras de 16 bits
        DRAM_SIZE  = 256,         // 256 palavras de 16 bits
        STACK_SIZE = 4,           // DSP-1 usa 4 niveis (HW tem ate 16)

        PC_MASK    = 0x07FF,
        RP_MASK    = 0x03FF,
        DP_MASK    = 0x00FF
    };

    // -----------------------------------------------------------------
    //  Bits do Status Register (SR)  --  o SR interno e' 16 bits, mas
    //  a CPU SNES so' enxerga o byte alto (bits 15..8).  Os nomes
    //  abaixo ja se referem ao byte exposto.
    //
    //    bit 7  (0x80)  RQM   request -- chip pronto para troca
    //    bit 6  (0x40)  USF1  user-defined flag 1
    //    bit 5  (0x20)  USF0  user-defined flag 0
    //    bit 4  (0x10)  DRS   data register status (LSB/MSB toggle)
    //    bit 3  (0x08)  DMA   DMA mode
    //    bit 2  (0x04)  DRC   data register control (1=8bit, 0=16bit)
    //    bit 1  (0x02)  SOC   serial-out control
    //    bit 0  (0x01)  SIC   serial-in control
    // -----------------------------------------------------------------
    enum SrBit {
        SR_RQM = 0x80,
        SR_USF1= 0x40,
        SR_USF0= 0x20,
        SR_DRS = 0x10,
        SR_DMA = 0x08,
        SR_DRC = 0x04,
        SR_SOC = 0x02,
        SR_SIC = 0x01
    };

    // -----------------------------------------------------------------
    //  Estrutura das flags da ALU  (uma copia para cada acumulador)
    // -----------------------------------------------------------------
    struct AluFlags {
        Uint8 S0;   // sign      (bit 15 do resultado)
        Uint8 S1;   // overflow latched
        Uint8 C;    // carry/borrow
        Uint8 Z;    // zero
        Uint8 OV0;  // overflow desta operacao
        Uint8 OV1;  // overflow latched (carry forward)
    };

    // -----------------------------------------------------------------
    //  Estado completo do uPD7725
    // -----------------------------------------------------------------
    Uint16   m_PC;                     // 11-bit program counter
    Uint16   m_RP;                     // 10-bit ROM pointer (Data ROM)
    Uint16   m_DP;                     //  8-bit Data RAM pointer

    Int16    m_A;                      // 16-bit accumulator A
    Int16    m_B;                      // 16-bit accumulator B
    AluFlags m_FA;                     // flags do acumulador A
    AluFlags m_FB;                     // flags do acumulador B

    Int16    m_K, m_L;                 // entradas do multiplicador
    Int16    m_M, m_N;                 // saidas do multiplicador

    Uint16   m_TR, m_TRB;              // registradores temporarios

    Uint16   m_SR;                     // status register (16-bit interno)
    Uint16   m_DR;                     // data register   (16-bit interno)
    Uint16   m_SI, m_SO;               // serial in/out (nao usados pelo DSP-1)

    Uint16   m_Stack[STACK_SIZE];      // stack de retorno (PCs)
    Uint8    m_SP;                     // stack pointer

    Uint16   m_DataRam[DRAM_SIZE];     // RAM interna 256 x 16
    Uint16   m_DataRom[DROM_SIZE];     // Data ROM 1024 x 16
    Uint32   m_ProgRom[PROM_SIZE];     // Program ROM 2048 x 24

    // Controle do barramento DSP<->SNES.  O 'IDB' (internal data bus)
    // e' a fonte/destino do canal SRC/DST de cada instrucao.
    Uint16   m_IDB;

    Bool     m_bProgLoaded;            // valida se a Program ROM existe
    Bool     m_bDrLoaded;              // ha byte recem-escrito do SNES?
    Bool     m_bDrAvailable;           // ha palavra esperando leitura?

    // -----------------------------------------------------------------
    //  Execucao
    // -----------------------------------------------------------------
    void  RunCycles (Uint32 uCycles);  // executa N instrucoes ou ate RQM=1
    void  StepOne   ();                // uma instrucao (24 bits)

    void  ExecuteOP (Uint32 uInst, Bool bReturn);
    void  ExecuteJP (Uint32 uInst);
    void  ExecuteLD (Uint32 uInst);

    Uint16 ReadSrc  (Uint8 uSrc);
    void   WriteDst (Uint8 uDst, Uint16 uVal);

    Int16  AluCompute(Uint8 uOp, Int16 acc, Uint16 p, AluFlags &rFlags,
                      Bool bUseCarry);

    void   UpdateMul ();               // recalcula M:N apos K ou L mudar
    void   UpdatePtr (Uint8 uDpl, Uint8 uDph, Uint8 uRpdcr);

    Bool   EvalBranch(Uint16 uBrch);   // avalia condicao do JP

    // pequenos helpers
    static _INLINE Int16 SignExtend(Uint16 uVal) { return (Int16)uVal; }
};


#endif  // _SNDSP1_LLE_H
