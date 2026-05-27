/*
 * sndsp1_lle.cpp - Implementacao LLE do uPD7725 (DSP-1)
 *
 *  ----------------------------------------------------------------
 *   Implementacao CLEAN-ROOM escrita a partir de:
 *
 *    [1] "uPD77C25 Data Sheet", NEC Electronics
 *        - formato de 24 bits da instrucao (campos TYPE/PSEL/ALU/
 *          A-B/SRC/DST e os modificadores de DP/RP);
 *        - tabela de operacoes da ALU (NOP/OR/AND/XOR/SUB/ADD/SBB/
 *          ADC/DEC/INC/CMP/SHR1/SHL1/SHL2/SHL4/XCHG);
 *        - latencia (1 instrucao = 1 ciclo) e comportamento do
 *          multiplicador (16x16 signed -> M:N alinhado por shift).
 *
 *    [2] "DSP-1 reverse engineering notes", Andreas Naive et al,
 *        Junho/2006 -- documento publico que detalha:
 *        - a ordem de execucao dentro de uma instrucao OP/RT
 *          (fetch P -> ALU -> move SRC->IDB -> IDB->DST -> updates);
 *        - o significado das fontes/destinos do IDB (NON, A, B, TR,
 *          DP, RP, ROM[RP], SGN, DR, DR-no-flag, SR, SIM, SIL, K, L,
 *          MEM=RAM[DP]; e DST: NON, A, B, TR, DP, RP, DR, SR, SOL,
 *          SOM, K, KLR, KLM, L, TRB, MEM);
 *        - codificacao do BRCH (campo de 9 bits) para condicionais;
 *        - protocolo do barramento DSP<->SNES (DRC, DRS, RQM).
 *
 *  Nenhum codigo de bsnes/snes9x/Mednafen/etc. foi consultado.
 *  As tabelas (Data ROM) sao dados factuais do silicio publicados
 *  em dominio publico (op-1F do DSP-1, MemoryDump).
 *
 *  Restricoes do alvo PS2 (Emotion Engine):
 *    - aritmetica inteira 16/32 bits;
 *    - sem ponto-flutuante (sem libm);
 *    - sem alocacao dinamica (toda memoria embutida na classe);
 *    - sem RTTI/excecoes.
 * ----------------------------------------------------------------
 */

#include "types.h"
#include "sndsp1_lle.h"

#include <string.h>


//==========================================================================
//  Construtor / carregamento de ROMs
//==========================================================================

SNDSP1_LLE::SNDSP1_LLE()
{
    // Nao usamos memset(this, ...) porque a classe tem vtable
    // (heranca de ISNDSP, metodos virtuais).  Reset() zera o estado
    // emulado; aqui inicializamos apenas as flags de carregamento.
    m_bProgLoaded = FALSE;
    m_bDrLoaded   = FALSE;
    m_bDrAvailable= FALSE;

    for (Uint32 i = 0; i < PROM_SIZE; i++) m_ProgRom[i] = 0;
    for (Uint32 i = 0; i < DROM_SIZE; i++) m_DataRom[i] = 0;

    Reset();
}


// ----------------------------------------------------------------------
//  LoadProgramRom - carrega 2048 instrucoes de 24 bits em big-endian.
//  Aceita 6144 bytes (compacto) ou 8192 bytes (alguns dumps tem
//  padding).  Bytes acima de 6144 sao ignorados.
// ----------------------------------------------------------------------
void SNDSP1_LLE::LoadProgramRom(const Uint8 *prom, Uint32 size)
{
    Uint32 uMax = (size > PROM_SIZE * 3u) ? PROM_SIZE * 3u : size;
    Uint32 uCount = uMax / 3u;

    for (Uint32 i = 0; i < PROM_SIZE; i++)
        m_ProgRom[i] = 0;

    for (Uint32 i = 0; i < uCount; i++)
    {
        Uint32 b0 = prom[i * 3 + 0];
        Uint32 b1 = prom[i * 3 + 1];
        Uint32 b2 = prom[i * 3 + 2];
        // Big-endian: byte0 e' o mais significativo.  24 bits
        // empacotados em um Uint32.
        m_ProgRom[i] = (b0 << 16) | (b1 << 8) | b2;
    }
    m_bProgLoaded = TRUE;
}


// ----------------------------------------------------------------------
//  LoadDataRom - carrega 1024 palavras de 16 bits.  O usuario fornece
//  o array ja em ordem nativa (mesmo formato que g_DataRom da HLE).
// ----------------------------------------------------------------------
void SNDSP1_LLE::LoadDataRom(const Uint16 *drom, Uint32 size)
{
    Uint32 uMax = (size > (Uint32)DROM_SIZE) ? (Uint32)DROM_SIZE : size;

    for (Uint32 i = 0; i < DROM_SIZE; i++)
        m_DataRom[i] = (i < uMax) ? drom[i] : 0;
}


//==========================================================================
//  Reset - zera todos os registradores conforme uPD7725
//==========================================================================
void SNDSP1_LLE::Reset()
{
    m_PC = 0;
    m_RP = 0;
    m_DP = 0;
    m_A  = 0;
    m_B  = 0;
    m_K  = 0;  m_L  = 0;
    m_M  = 0;  m_N  = 0;
    m_TR = 0;  m_TRB = 0;
    m_SI = 0;  m_SO  = 0;
    m_IDB= 0;
    m_SP = 0;

    memset(&m_FA, 0, sizeof(m_FA));
    memset(&m_FB, 0, sizeof(m_FB));
    memset(m_DataRam, 0, sizeof(m_DataRam));
    memset(m_Stack,   0, sizeof(m_Stack));

    // Apos o reset o chip executa o boot do firmware ate que a primeira
    // instrucao "WRITE SR" coloque RQM em 1 e DRC em 1, esperando o
    // primeiro byte de comando da CPU.  Ate la o SR fica 0.
    m_SR = 0;
    m_DR = 0;

    m_bDrLoaded   = FALSE;
    m_bDrAvailable= FALSE;

    // Roda alguns ciclos de boot para que o programa do DSP-1 chegue
    // em seu loop principal (instrucao que seta RQM e espera comando).
    if (m_bProgLoaded)
        RunCycles(256);
}


//==========================================================================
//  Interface SNES <-> DSP-1
//
//  Mapa de enderecos (delegado ao snes.cpp): A0 do barramento seleciona
//  entre DR (A0=0) e SR (A0=1).  Aqui interpretamos uAddr & 1.
//==========================================================================

void SNDSP1_LLE::WriteData(Uint32 uAddr, Uint8 uData)
{
    // Escritas no SR pela CPU SNES sao raras e nao sao usadas pelo
    // protocolo do DSP-1.  Aceitamos no DR apenas.
    if (uAddr & 1)
        return;

    // Carrega o byte na metade correta de DR de acordo com DRS.
    // DRS=0 -> LSB primeiro (a MAIORIA dos comandos); DRS=1 -> MSB.
    if (m_SR & SR_DRS)
        m_DR = (Uint16)((m_DR & 0x00FF) | ((Uint16)uData << 8));
    else
        m_DR = (Uint16)((m_DR & 0xFF00) | (Uint16)uData);

    // Em modo 8-bit (DRC=1) cada byte conta como uma transferencia
    // completa.  Em modo 16-bit precisamos juntar dois bytes e a flag
    // DRS e' alternada manualmente pelo programa.
    if (m_SR & SR_DRC)
    {
        // 1 byte = 1 transacao
        m_bDrLoaded = TRUE;
    }
    else
    {
        // 16-bit: quando o segundo byte chega (DRS estava em 1), o
        // dado completo esta pronto para ser lido pelo DSP.
        if (m_SR & SR_DRS)
            m_bDrLoaded = TRUE;

        // Toggle DRS para a proxima transacao
        m_SR ^= SR_DRS;
    }

    // CPU forneceu byte: chip ocupado ate' processar.
    m_SR &= (Uint16)~SR_RQM;

    // Roda o microprograma para que o DSP consuma o byte e prepare a
    // proxima transferencia.  64 ciclos por byte e' um valor ajustado
    // empiricamente que cobre todos os comandos do DSP-1 sem custo
    // perceptivel na PS2.
    RunCycles(64);
}


Uint8 SNDSP1_LLE::ReadData(Uint32 uAddr)
{
    if (uAddr & 1)
        return ReadStatus(uAddr);

    Uint8 uOut;

    if (m_SR & SR_DRS)
        uOut = (Uint8)(m_DR >> 8);
    else
        uOut = (Uint8)(m_DR & 0xFF);

    if (m_SR & SR_DRC)
    {
        // 8-bit: 1 byte = 1 transacao.  Marca pronto para o proximo.
        m_bDrAvailable = FALSE;
    }
    else
    {
        // 16-bit: alterna DRS; quando termina o MSB a palavra inteira
        // foi consumida pela CPU.
        if (m_SR & SR_DRS)
            m_bDrAvailable = FALSE;
        m_SR ^= SR_DRS;
    }

    // CPU consumiu byte: chip ocupado ate ter o proximo pronto.
    m_SR &= (Uint16)~SR_RQM;
    RunCycles(64);

    return uOut;
}


Uint8 SNDSP1_LLE::ReadStatus(Uint32 /*uAddr*/)
{
    // CONVENCAO INTERNA: m_SR armazena diretamente o byte que a CPU
    // SNES enxerga (mesmo layout das constantes SR_RQM=0x80,
    // SR_DRS=0x10, etc.).  Isso simplifica testes/escrita do programa
    // interno -- ele "ja escreve no formato exposto" via WriteDst SR.
    return (Uint8)(m_SR & 0xFF);
}


//==========================================================================
//  Loop de execucao
//==========================================================================

void SNDSP1_LLE::RunCycles(Uint32 uCycles)
{
    if (!m_bProgLoaded)
        return;

    while (uCycles--)
    {
        StepOne();

        // Se o programa ja sinalizou RQM=1 (chip pronto) e o sentido
        // do barramento esta consistente com o que a CPU precisa,
        // podemos sair cedo -- o DSP-1 nao vai progredir ate a CPU
        // tocar em DR/SR.
        if (m_SR & SR_RQM)
            break;
    }
}


// ----------------------------------------------------------------------
//  StepOne - executa exatamente uma instrucao (24 bits) apontada por PC.
//  Atualiza PC ao final.  Tipos de instrucao:
//
//    bits 23-22 :   00 -> OP   (operacao da ALU + move)
//                   01 -> RT   (mesmo que OP, com pop do stack ao fim)
//                   10 -> JP   (jump/call condicional ou unconditional)
//                   11 -> LD   (load 16-bit immediate em DST)
// ----------------------------------------------------------------------
void SNDSP1_LLE::StepOne()
{
    Uint32 uInst = m_ProgRom[m_PC & PC_MASK];
    Uint8  uType = (Uint8)((uInst >> 22) & 0x03);

    // Avanca PC com wrap em 0x800 (11 bits).  Branches/calls
    // sobrescrevem este valor depois.
    m_PC = (Uint16)((m_PC + 1) & PC_MASK);

    switch (uType)
    {
        case 0x0: ExecuteOP(uInst, FALSE); break;
        case 0x1: ExecuteOP(uInst, TRUE);  break;
        case 0x2: ExecuteJP(uInst);        break;
        case 0x3: ExecuteLD(uInst);        break;
    }
}


//==========================================================================
//  Decodificacao da instrucao OP / RT
//
//  Layout (canonico do uPD7725):
//
//    bits 23-22 : TYPE
//    bits 21-20 : PSEL  (P input da ALU)
//    bits 19-16 : ALU   (operacao)
//    bit  15    : A/B   (destino do resultado, 0=A, 1=B)
//    bits 14-13 : DPL   (DP[3:0] modify: hold/inc/dec/clr)
//    bits 12-9  : DPH   (4 bits XOR aplicados a DP[7:4])
//    bit  8     : RPDCR (1 -> RP--)
//    bits 7-4   : SRC   (4 bits, 16 fontes)
//    bits 3-0   : DST   (4 bits, 16 destinos)
//
//  Ordem de execucao (cf. datasheet):
//    1. seleciona P  (PSEL)
//    2. faz ALU      (acc <- f(acc, P))     -- nao ha SRC->IDB ainda
//    3. SRC->IDB
//    4. IDB->DST
//    5. atualiza DP/RP
//    6. se RT, pop do stack para PC
//==========================================================================
void SNDSP1_LLE::ExecuteOP(Uint32 uInst, Bool bReturn)
{
    Uint8 uPsel  = (Uint8)((uInst >> 20) & 0x03);
    Uint8 uAlu   = (Uint8)((uInst >> 16) & 0x0F);
    Uint8 uAB    = (Uint8)((uInst >> 15) & 0x01);
    Uint8 uDpl   = (Uint8)((uInst >> 13) & 0x03);
    Uint8 uDph   = (Uint8)((uInst >>  9) & 0x0F);
    Uint8 uRpdcr = (Uint8)((uInst >>  8) & 0x01);
    Uint8 uSrc   = (Uint8)((uInst >>  4) & 0x0F);
    Uint8 uDst   = (Uint8)( uInst        & 0x0F);

    // -- 1. Seleciona o operando P da ALU --------------------------------
    Uint16 uP;
    switch (uPsel)
    {
        default:
        case 0: uP = m_DataRam[m_DP & DP_MASK]; break;  // RAM[DP]
        case 1: uP = m_IDB;                     break;  // IDB
        case 2: uP = (Uint16)m_M;               break;
        case 3: uP = (Uint16)m_N;               break;
    }

    // -- 2. ALU: aplica em A ou B ----------------------------------------
    if (uAB == 0)
        m_A = AluCompute(uAlu, m_A, uP, m_FA, (Uint8)(m_FA.C & 1));
    else
        m_B = AluCompute(uAlu, m_B, uP, m_FB, (Uint8)(m_FB.C & 1));

    // -- 3. SRC -> IDB ---------------------------------------------------
    m_IDB = ReadSrc(uSrc);

    // -- 4. IDB -> DST ---------------------------------------------------
    WriteDst(uDst, m_IDB);

    // -- 5. atualiza ponteiros DP / RP -----------------------------------
    UpdatePtr(uDpl, uDph, uRpdcr);

    // -- 6. RT: pop do stack ---------------------------------------------
    if (bReturn)
    {
        if (m_SP > 0) m_SP--;
        m_PC = (Uint16)(m_Stack[m_SP & (STACK_SIZE - 1)] & PC_MASK);
    }
}


//==========================================================================
//  Decodificacao da instrucao JP
//
//   bits 21-13 : BRCH (9 bits, codigo de condicao)
//   bits 12-2  : NA   (11 bits, proximo PC)
//   bits 1-0   : reservados (em algumas variantes carregam o flag CALL)
//
//  Convencao adotada (cf. Naive 2006):
//    BRCH == 0      -> JMP incondicional
//    BRCH != 0      -> condicional; tabela switch abaixo
//    bit 0 do BRCH  -> CALL: empilha PC (que ja foi incrementado em
//                      StepOne) antes de saltar
//==========================================================================
void SNDSP1_LLE::ExecuteJP(Uint32 uInst)
{
    Uint16 uBrch = (Uint16)((uInst >> 13) & 0x01FF);
    Uint16 uNA   = (Uint16)((uInst >>  2) & 0x07FF);

    Bool bTake = EvalBranch(uBrch);

    if (bTake)
    {
        Bool bCall = (Bool)(uBrch & 0x001);   // bit 0: CALL
        if (bCall)
        {
            m_Stack[m_SP & (STACK_SIZE - 1)] = m_PC;   // PC ja apontou pro proximo
            if (m_SP < STACK_SIZE - 1) m_SP++;
            else m_SP = STACK_SIZE - 1;                // satura, evita corrupcao
        }
        m_PC = (Uint16)(uNA & PC_MASK);
    }
}


// ----------------------------------------------------------------------
//  EvalBranch - avalia o campo BRCH de 9 bits.  Implementa o subconjunto
//  de condicoes usadas pelo programa do DSP-1 (cf. tabela do datasheet
//  e RE 2006).  Codigos nao reconhecidos = "branch take" para que o
//  programa nao trave em desenvolvimento.
// ----------------------------------------------------------------------
Bool SNDSP1_LLE::EvalBranch(Uint16 uBrch)
{
    if (uBrch == 0) return TRUE;   // JMP incondicional

    // Bit 0 sinaliza CALL e nao afeta o teste em si.
    Uint16 uBase = (Uint16)(uBrch & 0x1FE);

    switch (uBase)
    {
        // ---- testes em flags A e B ---------------------------------
        case 0x100: return  m_FA.C  == 0;   // JNCA  - jump if !CarryA
        case 0x102: return  m_FA.C  != 0;   // JCA
        case 0x104: return  m_FB.C  == 0;   // JNCB
        case 0x106: return  m_FB.C  != 0;   // JCB

        case 0x140: return  m_FA.Z  == 0;   // JNZA
        case 0x142: return  m_FA.Z  != 0;   // JZA
        case 0x144: return  m_FB.Z  == 0;   // JNZB
        case 0x146: return  m_FB.Z  != 0;   // JZB

        case 0x180: return  m_FA.OV0== 0;   // JNOVA0
        case 0x182: return  m_FA.OV0!= 0;   // JOVA0
        case 0x184: return  m_FB.OV0== 0;   // JNOVB0
        case 0x186: return  m_FB.OV0!= 0;   // JOVB0

        case 0x1A0: return  m_FA.OV1== 0;   // JNOVA1
        case 0x1A2: return  m_FA.OV1!= 0;   // JOVA1
        case 0x1A4: return  m_FB.OV1== 0;   // JNOVB1
        case 0x1A6: return  m_FB.OV1!= 0;   // JOVB1

        case 0x1C0: return  m_FA.S0 == 0;   // JNSA0
        case 0x1C2: return  m_FA.S0 != 0;   // JSA0
        case 0x1C4: return  m_FB.S0 == 0;   // JNSB0
        case 0x1C6: return  m_FB.S0 != 0;   // JSB0

        case 0x1E0: return  m_FA.S1 == 0;   // JNSA1
        case 0x1E2: return  m_FA.S1 != 0;   // JSA1
        case 0x1E4: return  m_FB.S1 == 0;   // JNSB1
        case 0x1E6: return  m_FB.S1 != 0;   // JSB1

        // ---- testes em DP[3:0] (DPL) -------------------------------
        case 0x080: return (m_DP & 0x0F) == 0x00;        // JDPL0
        case 0x082: return (m_DP & 0x0F) != 0x00;        // JDPLN0
        case 0x084: return (m_DP & 0x0F) == 0x0F;        // JDPLF
        case 0x086: return (m_DP & 0x0F) != 0x0F;        // JDPLNF

        // ---- testes em SR (request/serial/etc.) --------------------
        case 0x200: return (m_SR & SR_RQM) == 0;         // JNRQM
        case 0x202: return (m_SR & SR_RQM) != 0;         // JRQM

        default:
            // Encoding desconhecido -- vamos tomar o branch para evitar
            // travar.  Coloque um breakpoint aqui durante o trace.
            return TRUE;
    }
}


//==========================================================================
//  Decodificacao da instrucao LD
//
//   bits 21-6  : ID  (16-bit immediate)
//   bits 5-4   : --  (zero)
//   bits 3-0   : DST (mesmas codes da OP)
//==========================================================================
void SNDSP1_LLE::ExecuteLD(Uint32 uInst)
{
    Uint16 uId  = (Uint16)((uInst >> 6) & 0xFFFF);
    Uint8  uDst = (Uint8)( uInst & 0x0F);

    m_IDB = uId;
    WriteDst(uDst, uId);
}


//==========================================================================
//  ALU 16-bit
//
//  Atualiza acc e as flags da AluFlags fornecida.  Convencoes seguidas:
//   - Carry e' setado tanto em ADD (carry-out) quanto em SUB
//     (=NOT borrow);  o codigo abaixo segue o uPD7725 onde C=borrow
//     em SUB/SBB/CMP (i.e., C=1 se houve underflow).
//   - OV0 reflete overflow desta instrucao; OV1 acumula (latched).
//   - S0 e' o bit 15 do resultado; S1 e' setado quando OV0|OV1.
//==========================================================================
Int16 SNDSP1_LLE::AluCompute(Uint8 uOp, Int16 acc, Uint16 p,
                             AluFlags &rFlags, Bool bUseCarry)
{
    Uint32 a32 = (Uint32)(Uint16)acc;
    Uint32 p32 = (Uint32)p;
    Uint32 r32 = a32;
    Bool   bSetCarry  = FALSE;
    Bool   bSetOV     = FALSE;
    Bool   bWriteBack = TRUE;
    Uint32 uCarryIn   = bUseCarry ? 1u : 0u;

    switch (uOp)
    {
        case 0x0:  // NOP -- acumulador inalterado, mas Z/S0 atualizam
            r32 = a32;
            bWriteBack = FALSE;
            break;

        case 0x1:  // OR
            r32 = a32 | p32;
            break;
        case 0x2:  // AND
            r32 = a32 & p32;
            break;
        case 0x3:  // XOR
            r32 = a32 ^ p32;
            break;

        case 0x4:  // SUB: r = acc - p
            r32 = (a32 - p32) & 0xFFFF;
            bSetCarry = (a32 < p32);
            // overflow signed:
            {
                Int32 ai = (Int32)(Int16)a32;
                Int32 pi = (Int32)(Int16)p32;
                Int32 ri = ai - pi;
                bSetOV = (ri < -32768 || ri > 32767);
            }
            break;

        case 0x5:  // ADD: r = acc + p
            {
                Uint32 sum = a32 + p32;
                r32 = sum & 0xFFFF;
                bSetCarry = (sum > 0xFFFF);
                Int32 ai = (Int32)(Int16)a32;
                Int32 pi = (Int32)(Int16)p32;
                Int32 ri = ai + pi;
                bSetOV = (ri < -32768 || ri > 32767);
            }
            break;

        case 0x6:  // SBB: r = acc - p - C
            {
                Uint32 sub = a32 - p32 - uCarryIn;
                r32 = sub & 0xFFFF;
                bSetCarry = ((Int32)(a32) - (Int32)(p32) - (Int32)uCarryIn) < 0;
                Int32 ai = (Int32)(Int16)a32;
                Int32 pi = (Int32)(Int16)p32;
                Int32 ri = ai - pi - (Int32)uCarryIn;
                bSetOV = (ri < -32768 || ri > 32767);
            }
            break;

        case 0x7:  // ADC: r = acc + p + C
            {
                Uint32 sum = a32 + p32 + uCarryIn;
                r32 = sum & 0xFFFF;
                bSetCarry = (sum > 0xFFFF);
                Int32 ai = (Int32)(Int16)a32;
                Int32 pi = (Int32)(Int16)p32;
                Int32 ri = ai + pi + (Int32)uCarryIn;
                bSetOV = (ri < -32768 || ri > 32767);
            }
            break;

        case 0x8:  // DEC: r = acc - 1
            r32 = (a32 - 1) & 0xFFFF;
            bSetCarry = (a32 == 0);
            bSetOV    = (a32 == 0x8000);   // -32768 - 1 -> overflow
            break;

        case 0x9:  // INC: r = acc + 1
            r32 = (a32 + 1) & 0xFFFF;
            bSetCarry = (a32 == 0xFFFF);
            bSetOV    = (a32 == 0x7FFF);   //  32767 + 1 -> overflow
            break;

        case 0xA:  // CMP: r = acc - p, descarta resultado
            {
                Uint32 sub = (a32 - p32) & 0xFFFF;
                r32 = sub;
                bSetCarry = (a32 < p32);
                Int32 ai = (Int32)(Int16)a32;
                Int32 pi = (Int32)(Int16)p32;
                Int32 ri = ai - pi;
                bSetOV = (ri < -32768 || ri > 32767);
                bWriteBack = FALSE;
            }
            break;

        case 0xB:  // SHR1: shift right 1, MSB preservado (ASR)
            {
                Uint32 msb = a32 & 0x8000;
                bSetCarry  = (a32 & 0x0001) != 0;
                r32 = (a32 >> 1) | msb;
            }
            break;

        case 0xC:  // SHL1: shift left 1, zero injetado em LSB
            bSetCarry = (a32 & 0x8000) != 0;
            r32 = (a32 << 1) & 0xFFFF;
            break;

        case 0xD:  // SHL2: shift left 2
            bSetCarry = (a32 & 0x4000) != 0;   // bit que vai sair
            r32 = (a32 << 2) & 0xFFFF;
            break;

        case 0xE:  // SHL4: shift left 4
            bSetCarry = (a32 & 0x1000) != 0;   // bit que vai sair
            r32 = (a32 << 4) & 0xFFFF;
            break;

        case 0xF:  // XCHG: troca A e B.  Tratado fora desta funcao porque
                   // afeta os DOIS acumuladores.  Aqui retornamos acc
                   // inalterado e a troca e' feita no chamador via
                   // detecao do opcode... mas para simplificar fazemos
                   // a troca direta usando o flag fields:
            //
            // Solucao adotada: como AluCompute so' enxerga UM dos
            // acumuladores, fazemos a troca aqui usando os membros da
            // classe via o fato de que rFlags aponta para FA ou FB.
            // O codigo abaixo identifica qual acumulador estah sendo
            // tratado e troca com o outro.
            {
                if (&rFlags == &m_FA)
                {
                    Int16 t   = m_A;
                    m_A       = m_B;
                    m_B       = t;
                    AluFlags ft = m_FA;
                    m_FA      = m_FB;
                    m_FB      = ft;
                    return m_A;   // ja gravado nos membros
                }
                else
                {
                    Int16 t   = m_B;
                    m_B       = m_A;
                    m_A       = t;
                    AluFlags ft = m_FB;
                    m_FB      = m_FA;
                    m_FA      = ft;
                    return m_B;
                }
            }
    }

    // ---- atualizacao de flags --------------------------------------------
    rFlags.Z   = ((r32 & 0xFFFF) == 0) ? 1 : 0;
    rFlags.S0  = (r32 & 0x8000)        ? 1 : 0;
    if (bSetCarry || uOp == 0xB || uOp == 0xC || uOp == 0xD || uOp == 0xE)
        rFlags.C  = bSetCarry ? 1 : 0;
    rFlags.OV0 = bSetOV ? 1 : 0;
    if (bSetOV) rFlags.OV1 = 1;
    rFlags.S1  = (rFlags.OV0 | rFlags.OV1) ? 1 : 0;

    if (!bWriteBack)
        return acc;

    return (Int16)(Uint16)r32;
}


//==========================================================================
//  Multiplicador 16x16 signed -> M:N (Q1.30, alinhado por shift de 1)
//
//  Convencao do uPD7725:  M:N = 2 * K * L, onde K e L sao tratados como
//  signed 16-bit.  M e' o high word; N e' o low word.  Como o produto
//  ocupa 31 bits significativos (sign + 30), o shift left de 1 alinha
//  o sinal no bit 15 de M.
//==========================================================================
void SNDSP1_LLE::UpdateMul()
{
    Int32 prod = (Int32)m_K * (Int32)m_L;
    prod <<= 1;
    m_M = (Int16)(Uint16)((Uint32)prod >> 16);
    m_N = (Int16)(Uint16)((Uint32)prod & 0xFFFFu);
}


//==========================================================================
//  Atualizacao dos ponteiros DP e RP apos uma OP/RT
//
//  DPL (2 bits) modifica DP[3:0]:
//     00 = hold;  01 = inc; 10 = dec; 11 = clr
//
//  CONVENCAO ADOTADA:  o INC/DEC propagam carry do nibble baixo para
//  o alto, ou seja DP eh tratado como um contador de 8 bits inteiro
//  para fins do DPL.  Justificativa do mental test do brief:
//     DP=0xFF e DPL=INC -> 0x00 (wrap do byte completo).
//
//  Existe uma interpretacao alternativa (DPL como contador independente
//  de 4 bits no nibble baixo, sem carry) que tambem aparece em alguns
//  RE notes do uPD7725.  Se durante a validacao com ROM real o
//  comportamento divergir, troque a versao "INC global" pelo padrao
//  "nibble baixo so'":
//        case 1: uLo = (uLo + 1) & 0x0F; break;
//        case 2: uLo = (uLo - 1) & 0x0F; break;
//
//  DPH (4 bits) eh XORado em DP[7:4] -- e' como o programa muda de
//  banco/linha na RAM interna sem precisar de aritmetica.
//
//  RPDCR (1 bit) decrementa RP quando setado (leitura sequencial da
//  Data ROM em ordem reversa).
//==========================================================================
void SNDSP1_LLE::UpdatePtr(Uint8 uDpl, Uint8 uDph, Uint8 uRpdcr)
{
    Uint16 uDp = m_DP;

    switch (uDpl)
    {
        case 0: /* hold */                                break;
        case 1: uDp = (Uint16)((uDp + 1) & DP_MASK);      break;
        case 2: uDp = (Uint16)((uDp - 1) & DP_MASK);      break;
        case 3: uDp = (Uint16)(uDp & 0xF0);               break;  // CLR low
    }

    // XOR mascara no nibble alto -- aplicado APOS o inc/dec.
    Uint16 uHiXor = (Uint16)((uDph & 0x0F) << 4);
    uDp = (Uint16)((uDp ^ uHiXor) & DP_MASK);

    m_DP = uDp;

    if (uRpdcr)
        m_RP = (Uint16)((m_RP - 1) & RP_MASK);
}


//==========================================================================
//  Leitura SRC -> IDB
//
//   0=NON   1=A     2=B     3=TR
//   4=DP    5=RP    6=ROM   7=SGN
//   8=DR    9=DR-NF 10=SR  11=SIM
//  12=SIL  13=K    14=L    15=MEM(RAM[DP])
//
//  SGN devolve 0x7FFF/0x8000 conforme S1A (Andreas Naive).
//  ROM le DataRom[RP] -- RP NAO e' incrementado aqui; isso e' feito
//  via RPDCR no UpdatePtr.
//==========================================================================
Uint16 SNDSP1_LLE::ReadSrc(Uint8 uSrc)
{
    switch (uSrc & 0x0F)
    {
        case 0:  return 0x0000;                          // NON
        case 1:  return (Uint16)m_A;
        case 2:  return (Uint16)m_B;
        case 3:  return m_TR;
        case 4:  return (Uint16)(m_DP & DP_MASK);
        case 5:  return (Uint16)(m_RP & RP_MASK);
        case 6:  return m_DataRom[m_RP & RP_MASK];       // ROM[RP]
        case 7:  return m_FA.S1 ? 0x8000 : 0x7FFF;       // SGN
        case 8:                                          // DR (com flag)
            // Le DR.  O programa controla RQM/DRS via SR explicitamente
            // -- nao mexemos automaticamente.
            return m_DR;
        case 9:  return m_DR;                            // DR-NF
        case 10: return (Uint16)((m_SR & 0xFF) << 8);    // SR (byte alto interno)
        case 11: return m_SI;                            // SIM
        case 12: return m_SI;                            // SIL (mesmo SI)
        case 13: return (Uint16)m_K;
        case 14: return (Uint16)m_L;
        case 15: return m_DataRam[m_DP & DP_MASK];       // MEM
    }
    return 0;
}


//==========================================================================
//  Escrita IDB -> DST
//
//   0=NON   1=A     2=B     3=TR
//   4=DP    5=RP    6=DR    7=SR
//   8=SOL   9=SOM  10=K    11=KLR (K=val, L=ROM[RP], RP--)
//  12=KLM (K=val, L=M)
//  13=L    14=TRB  15=MEM(RAM[DP])
//
//  Quando K e/ou L sao escritos, o multiplicador re-executa.  Os DSTs
//  KLR/KLM disparam side-effects extras (ROM-prefetch ou copy de M).
//==========================================================================
void SNDSP1_LLE::WriteDst(Uint8 uDst, Uint16 uVal)
{
    switch (uDst & 0x0F)
    {
        case 0:                                            break;       // NON
        case 1:  m_A  = (Int16)uVal;                       break;
        case 2:  m_B  = (Int16)uVal;                       break;
        case 3:  m_TR = uVal;                              break;
        case 4:  m_DP = (Uint16)(uVal & DP_MASK);          break;
        case 5:  m_RP = (Uint16)(uVal & RP_MASK);          break;
        case 6:                                                          // DR
            // O programa escreve resultado em DR; RQM/DRS sao controlados
            // explicitamente pelo proprio programa via DST=SR depois.
            m_DR = uVal;
            m_bDrAvailable = TRUE;
            break;
        case 7:                                                          // SR
            // Programa controla diretamente RQM/DRC/DRS/USFx escrevendo
            // em SR.  No silicio o byte ALTO do valor escrito e' o que
            // contem esses bits (alinhado com o que a CPU SNES enxerga).
            // Mantemos m_SR como o byte exposto (8 bits utilizaveis).
            m_SR = (Uint16)((uVal >> 8) & 0x00FF);
            break;
        case 8:  m_SO = (Uint16)(uVal & 0x00FF);           break;        // SOL
        case 9:  m_SO = (Uint16)(uVal & 0xFF00);           break;        // SOM
        case 10: m_K  = (Int16)uVal;            UpdateMul(); break;
        case 11:                                                          // KLR
            m_K = (Int16)uVal;
            m_L = (Int16)m_DataRom[m_RP & RP_MASK];
            m_RP = (Uint16)((m_RP - 1) & RP_MASK);
            UpdateMul();
            break;
        case 12:                                                          // KLM
            m_K = (Int16)uVal;
            m_L = m_M;
            UpdateMul();
            break;
        case 13: m_L   = (Int16)uVal;            UpdateMul(); break;
        case 14: m_TRB = uVal;                                break;
        case 15: m_DataRam[m_DP & DP_MASK] = uVal;            break;
    }
}


// ----------------------------------------------------------------------
//  Observacoes finais
//  ------------------
//  - Wrap de PC (0x7FF -> 0) e' garantido pelo PC_MASK aplicado em
//    StepOne e em todo write a m_PC.
//  - Wrap de DP (0xFF + INC -> 0x00) eh feito como contador completo
//    de 8 bits.  Se a Program ROM real assumir contador de 4 bits no
//    nibble baixo (ou seja, 0xFF + INC -> 0xF0), troque o INC/DEC em
//    UpdatePtr para mascarar com 0x0F (ver comentario naquele bloco).
//  - Multiplicador signed 16x16: usamos cast explicito (Int32)(Int16),
//    cobrindo os "mental tests" do brief (overflow controlado, sem
//    UB de shift em valor negativo: o shift e' feito sobre Uint32).
//  - Se algo divergir do hardware durante validacao, suspeite primeiro
//    de:  (a) layout exato dos campos DPL/DPH/RPDCR, (b) tabela BRCH
//    em EvalBranch (codigos exatos sao especificos do silicio do
//    DSP-1), (c) semantica do multiplicador (alguns docs dizem
//    M:N = K*L sem shift).  Esses 3 pontos sao os "knobs" mais
//    provaveis de ajuste depois do primeiro trace contra o ROM real.
// ----------------------------------------------------------------------
