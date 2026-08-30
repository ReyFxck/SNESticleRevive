/*
 * Copyright (c) 1997-2004-2022 Icer Addis
 * Re-Worked By ReyFxck, Claude Aí, ChatGPT
 *
 * Description:
 *   Exercises gsu test behavior in the superfxtest regression suite.
 */

// Bancada host-side do core SuperFX/GSU (src/snes/core/sngsu.cpp).
//  Parte A: testes de unidade deterministicos (MMIO, GO, prefixos, STOP/IRQ).
//  Parte B: FUZZ ORACLE -- milhares de casos. Para cada opcode de calculo,
//           varre muitos operandos e compara resultado+flags do GSU contra
//           um calculo de referencia em C. Acusa qualquer divergencia.
//  Limite honesto: o oracle espelha a spec (fullsnes), entao ele pega bugs de
//  IMPLEMENTACAO (rota de opcode, mascara, sinal, truncamento, off-by-one).
//  A exatidao fina de flags vs silicio real ainda precisa de validacao em
//  hardware/emulador de referencia -- mas isso varre o grosso dos erros.

#include "sngsu.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

extern "C" void DLog(const char *, ...) {}

static int g_fail = 0;
static uint8_t g_rom[0x10000];
static uint8_t g_ram[0x8000];

static void CHECK(const char *name, long got, long exp)
{
    if (got != exp) { printf("FAIL: %-26s got=0x%lX exp=0x%lX\n", name,
                             (unsigned long)got, (unsigned long)exp); g_fail++; }
    else            { printf("OK  : %-26s = 0x%lX\n", name, (unsigned long)exp); }
}

struct Res { uint16_t r; bool z, cy, s, ov; };

// Roda: [CY=cyIn] FROM R1 ; TO R3 ; <op...> ; STOP   com R1=a, R2=b.
static Res runOp(const uint8_t *op, int oplen, uint16_t a, uint16_t b, bool cyIn)
{
    memset(g_rom, 0x01, sizeof(g_rom));      // NOP de fundo
    int i = 0;
    g_rom[i++] = 0xB1;                        // FROM R1 (Sreg=1)
    g_rom[i++] = 0x13;                        // TO   R3 (Dreg=3)
    for (int k = 0; k < oplen; k++) g_rom[i++] = op[k];
    g_rom[i++] = 0x00;                        // STOP

    SNGSU g;
    g.SetMemory(g_rom, sizeof(g_rom), g_ram, sizeof(g_ram));
    g.Reset();
    g.SetReg(1, a);
    g.SetReg(2, b);
    if (cyIn) g.WriteReg(0x3030, 0x04);       // seta CY antes do GO
    g.WriteReg(0x3034, 0x00);                 // PBR = 0
    g.WriteReg(0x301E, 0x00);                 // R15 lo
    g.WriteReg(0x301F, 0x80);                 // R15 = 0x8000 + GO
    g.Run(64);

    Res o; o.r = g.GetReg(3);
    uint8_t lo = g.ReadReg(0x3030);
    o.z = (lo & 0x02) != 0; o.cy = (lo & 0x04) != 0;
    o.s = (lo & 0x08) != 0; o.ov = (lo & 0x10) != 0;
    return o;
}

// roda um programa arbitrario a partir de rom[0] (PBR=0, R15=0x8000)
static SNGSU runProgram(const uint8_t *prog, int len)
{
    memset(g_rom, 0x01, sizeof(g_rom));
    memset(g_ram, 0x00, sizeof(g_ram));
    memcpy(g_rom, prog, len);
    SNGSU g;
    g.SetMemory(g_rom, sizeof(g_rom), g_ram, sizeof(g_ram));
    g.Reset();
    g.WriteReg(0x3034, 0x00);
    g.WriteReg(0x301E, 0x00);
    g.WriteReg(0x301F, 0x80);
    g.Run(100000);
    return g;
}

// ---- oracle (referencia) ----
static const uint16_t VALS[] = {
    0x0000,0x0001,0x0002,0x0003,0x000F,0x0010,0x007F,0x0080,0x00FF,0x0100,
    0x01FF,0x0200,0x1234,0x4000,0x5555,0x7FFE,0x7FFF,0x8000,0x8001,0xAAAA,
    0xABCD,0xC000,0xF000,0xFFFD,0xFFFE,0xFFFF,0x0055,0x00AA,0x3C00,0x6789,
    0x9ABC,0xDEAD,0xBEEF,0xCAFE,0x0F0F,0xF0F0,0x1357,0x2468,0x9999,0x6666
};
static const int NV = (int)(sizeof(VALS)/sizeof(VALS[0]));

static int g_fuzzCases = 0, g_fuzzFail = 0;

static void cmp(const char *tag, uint16_t a, uint16_t b, const Res &got,
                uint16_t er, int ez, int ecy, int es, int eov, bool checkCYOV)
{
    g_fuzzCases++;
    bool bad = (got.r != er) || (got.z != (bool)ez) || (got.s != (bool)es)
            || (checkCYOV && (got.cy != (bool)ecy || got.ov != (bool)eov));
    if (bad) {
        if (g_fuzzFail < 12)
            printf("  FUZZ FAIL %s a=%04X b=%04X  r=%04X(exp %04X) "
                   "z=%d/%d cy=%d/%d s=%d/%d ov=%d/%d\n", tag, a, b,
                   got.r, er, got.z, ez, got.cy, ecy, got.s, es, got.ov, eov);
        g_fuzzFail++;
    }
}

static void fuzz()
{
    const uint8_t ADD[]={0x52}, ADC[]={0x3D,0x52}, SUB[]={0x62}, SBC[]={0x3D,0x62};
    const uint8_t AND[]={0x72}, OR[]={0xC2}, XOR[]={0x3D,0xC2};
    const uint8_t MUL[]={0x82}, UMUL[]={0x3D,0x82};
    const uint8_t LSR[]={0x03}, ASR[]={0x96}, ROL[]={0x04}, ROR[]={0x97};
    const uint8_t NOTo[]={0x4F}, SWAP[]={0x4D}, SEX[]={0x95};

    for (int ia = 0; ia < NV; ia++) {
        uint16_t a = VALS[ia];

        // ---- unarios (usam so 'a') ----
        { Res g=runOp(LSR,1,a,0,false);  uint16_t r=a>>1;        cmp("LSR",a,0,g,r,r==0,a&1,(r>>15)&1,0,true); }
        { Res g=runOp(ASR,1,a,0,false);  uint16_t r=(uint16_t)(((int16_t)a)>>1); cmp("ASR",a,0,g,r,r==0,a&1,(r>>15)&1,0,true); }
        { Res g=runOp(NOTo,1,a,0,false); uint16_t r=(uint16_t)~a; cmp("NOT",a,0,g,r,r==0,0,(r>>15)&1,0,false); }
        { Res g=runOp(SWAP,1,a,0,false); uint16_t r=(uint16_t)((a>>8)|(a<<8)); cmp("SWAP",a,0,g,r,r==0,0,(r>>15)&1,0,false); }
        { Res g=runOp(SEX,1,a,0,false);  uint16_t r=(uint16_t)(int16_t)(int8_t)(a&0xFF); cmp("SEX",a,0,g,r,r==0,0,(r>>15)&1,0,false); }
        for (int cy=0; cy<2; cy++) {
            { Res g=runOp(ROL,1,a,0,cy!=0); uint16_t r=(uint16_t)((a<<1)|cy); cmp("ROL",a,0,g,r,r==0,(a>>15)&1,(r>>15)&1,0,true); }
            { Res g=runOp(ROR,1,a,0,cy!=0); uint16_t r=(uint16_t)((cy<<15)|(a>>1)); cmp("ROR",a,0,g,r,r==0,a&1,(r>>15)&1,0,true); }
        }

        // ---- binarios (usam 'a' e 'b') ----
        for (int ib = 0; ib < NV; ib++) {
            uint16_t b = VALS[ib];

            { Res g=runOp(ADD,1,a,b,false); uint32_t t=(uint32_t)a+b; uint16_t r=(uint16_t)t;
              int ov=((~(a^b))&(a^r)&0x8000)!=0; cmp("ADD",a,b,g,r,r==0,t>0xFFFF,(r>>15)&1,ov,true); }
            { Res g=runOp(SUB,1,a,b,false); uint32_t t=(uint32_t)a+((~b)&0xFFFF)+1; uint16_t r=(uint16_t)t;
              int ov=((a^b)&(a^r)&0x8000)!=0; cmp("SUB",a,b,g,r,r==0,t>0xFFFF,(r>>15)&1,ov,true); }
            { Res g=runOp(AND,1,a,b,false); uint16_t r=a&b; cmp("AND",a,b,g,r,r==0,0,(r>>15)&1,0,false); }
            { Res g=runOp(OR ,1,a,b,false); uint16_t r=a|b; cmp("OR" ,a,b,g,r,r==0,0,(r>>15)&1,0,false); }
            { Res g=runOp(XOR,(int)sizeof(XOR),a,b,false); uint16_t r=a^b; cmp("XOR",a,b,g,r,r==0,0,(r>>15)&1,0,false); }
            { Res g=runOp(MUL,(int)sizeof(MUL),a,b,false); uint16_t r=(uint16_t)((int8_t)a*(int8_t)b); cmp("MULT",a,b,g,r,r==0,0,(r>>15)&1,0,false); }
            { Res g=runOp(UMUL,(int)sizeof(UMUL),a,b,false);uint16_t r=(uint16_t)((uint8_t)a*(uint8_t)b); cmp("UMULT",a,b,g,r,r==0,0,(r>>15)&1,0,false); }

            for (int cy=0; cy<2; cy++) {
                { Res g=runOp(ADC,(int)sizeof(ADC),a,b,cy!=0); uint32_t t=(uint32_t)a+b+cy; uint16_t r=(uint16_t)t;
                  int ov=((~(a^b))&(a^r)&0x8000)!=0; cmp("ADC",a,b,g,r,r==0,t>0xFFFF,(r>>15)&1,ov,true); }
                { Res g=runOp(SBC,(int)sizeof(SBC),a,b,cy!=0); uint32_t t=(uint32_t)a+((~b)&0xFFFF)+cy; uint16_t r=(uint16_t)t;
                  int ov=((a^b)&(a^r)&0x8000)!=0; cmp("SBC",a,b,g,r,r==0,t>0xFFFF,(r>>15)&1,ov,true); }
            }
        }
    }
}

int main()
{
    memset(g_ram, 0, sizeof(g_ram));

    // ===== Parte A: unidade =====
    {
        static const uint8_t prog[] = {
            0xF1,0x64,0x00, 0xF2,0x03,0x00, 0xB1,0x13,0x52,   // R3 = 100+3
            0xB1,0x14,0x62,                                    // R4 = 100-3
            0xF6,0xFF,0xFF, 0xF7,0x01,0x00, 0xB6,0x18,0x57,   // R8 = FFFF+1
            0x00 };
        memset(g_rom, 0x01, sizeof(g_rom));
        memcpy(g_rom, prog, sizeof(prog));
        SNGSU g; g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        CHECK("VCR", g.ReadReg(0x303B), 0x04);
        g.WriteReg(0x3034,0x00); g.WriteReg(0x301E,0x00); g.WriteReg(0x301F,0x80);
        CHECK("GO apos $301F", g.IsRunning()?1:0, 1);
        g.Run(10000);
        CHECK("GO=0 apos STOP", g.IsRunning()?1:0, 0);
        CHECK("R3 = 100+3", g.GetReg(3), 103);
        CHECK("R4 = 100-3", g.GetReg(4), 97);
        CHECK("R8 = FFFF+1", g.GetReg(8), 0x0000);
        CHECK("IRQ no STOP", g.ReadReg(0x3031)&0x80, 0x80);
    }
    {
        // A revisao e' configuracao do cartucho e deve sobreviver ao reset.
        SNGSU g;
        g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram));
        g.SetVersion(0x01); g.Reset();
        CHECK("VCR Mario Chip 1 persiste", g.ReadReg(0x303B), 0x01);
        g.SetVersion(0x04); g.Reset();
        CHECK("VCR GSU2 persiste", g.ReadReg(0x303B), 0x04);
    }
    {
        // Cada metade de R0-R15 preserva a outra metade do MESMO registro.
        // Escritas intercaladas nao compartilham um latch global.
        SNGSU g;
        g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        g.WriteReg(0x3000, 0x34);             // R0 low
        g.WriteReg(0x3002, 0x78);             // R1 low (intercalado)
        g.WriteReg(0x3001, 0x12);             // R0 high
        CHECK("MMIO R0 bytes independentes", g.GetReg(0), 0x1234);
        CHECK("MMIO R1 low preservado",      g.GetReg(1), 0x0078);

        g.WriteReg(0x3031, 0x13);             // ALT1|ALT2|B
        CHECK("SFR high gravavel", g.ReadReg(0x3031) & 0x1F, 0x13);
    }

    // ===== Parte C: controle de fluxo + memoria =====
    printf("\n--- controle de fluxo + memoria ---\n");
    {
        // round-trip STW/LDW: [0x100]=0xBEEF via R5, le de volta em R6
        static const uint8_t p1[] = {
            0xF1,0x00,0x01,   // IWT R1,#0x0100 (endereco)
            0xF5,0xEF,0xBE,   // IWT R5,#0xBEEF (valor)
            0xB5,             // FROM R5  (Sreg=5)
            0x31,             // STW (R1) -> [0x100] = R5
            0x16,             // TO R6    (Dreg=6)
            0x41,             // LDW (R1) -> R6 = [0x100]
            0x00 };
        SNGSU g = runProgram(p1, sizeof(p1));
        CHECK("STW/LDW round-trip R6", g.GetReg(6), 0xBEEF);
    }
    {
        // SM/LM: [0x200]=0x1234 via R7, le em R8
        static const uint8_t p2[] = {
            0xF7,0x34,0x12,         // IWT R7,#0x1234
            0x3E,0xF7,0x00,0x02,    // SM (0x0200),R7   (ALT2 + F7)
            0x3D,0xF8,0x00,0x02,    // LM R8,(0x0200)   (ALT1 + F8)
            0x00 };
        SNGSU g = runProgram(p2, sizeof(p2));
        CHECK("SM/LM round-trip R8", g.GetReg(8), 0x1234);
    }
    {
        // BRA com delay-slot: o NOP apos o BRA executa; o IWT R4 e' PULADO;
        // o IWT R5 (alvo) executa.  R3=1, R4=0(pulado), R5=0xBB.
        static const uint8_t p3[] = {
            /*0*/ 0xF3,0x01,0x00,   // IWT R3,#1
            /*3*/ 0x05,0x04,        // BRA +4  (alvo = 0x8005+4 = 0x8009)
            /*5*/ 0x01,             // NOP  (DELAY SLOT - executa)
            /*6*/ 0xF4,0xAA,0x00,   // IWT R4,#0xAA  (PULADO)
            /*9*/ 0xF5,0xBB,0x00,   // IWT R5,#0xBB  (ALVO)
            /*12*/0x00 };
        SNGSU g = runProgram(p3, sizeof(p3));
        CHECK("BRA: R3 setado",      g.GetReg(3), 1);
        CHECK("BRA: R4 pulado",      g.GetReg(4), 0);
        CHECK("BRA: R5 alvo",        g.GetReg(5), 0xBB);
    }
    {
        // LOOP: corpo (INC R1) roda 3x via R12=contador, R13=alvo
        static const uint8_t p4[] = {
            /*0*/ 0xFC,0x03,0x00,   // IWT R12,#3
            /*3*/ 0xFD,0x06,0x80,   // IWT R13,#0x8006 (inicio do corpo)
            /*6*/ 0xD1,             // INC R1   (corpo)
            /*7*/ 0x3C,             // LOOP     (R12--, se !=0 salta R13)
            /*8*/ 0x01,             // NOP      (delay slot)
            /*9*/ 0x00 };           // STOP
        SNGSU g = runProgram(p4, sizeof(p4));
        CHECK("LOOP: R1 = 3 iteracoes", g.GetReg(1), 3);
    }
    {
        // LMULT (3D 9F): R9 x R6 com sinal; alto->R8, baixo->R4.
        // Exemplo oficial: R9=B556 x R6=DAAB => R8=0AE3, R4=5C72.
        static const uint8_t p5[] = {
            0xF6,0xAB,0xDA,   // IWT R6,#DAAB
            0xF9,0x56,0xB5,   // IWT R9,#B556
            0xB9,             // FROM R9 (Sreg=9)
            0x18,             // TO   R8 (Dreg=8)
            0x3D,             // ALT1
            0x9F,             // LMULT
            0x00 };
        SNGSU g = runProgram(p5, sizeof(p5));
        CHECK("LMULT R8 (alto)", g.GetReg(8), 0x0AE3);
        CHECK("LMULT R4 (baixo)", g.GetReg(4), 0x5C72);
    }
    {
        // FMULT (9F): R5 x R6 com sinal; alto->R2.  R4 nao e' tocado.
        // Exemplo oficial: R5=4AAA x R6=DAAB => R2=F51C.
        static const uint8_t p6[] = {
            0xF6,0xAB,0xDA,   // IWT R6,#DAAB
            0xF5,0xAA,0x4A,   // IWT R5,#4AAA
            0xB5,             // FROM R5 (Sreg=5)
            0x12,             // TO   R2 (Dreg=2)
            0x9F,             // FMULT
            0x00 };
        SNGSU g = runProgram(p6, sizeof(p6));
        CHECK("FMULT R2 (alto)", g.GetReg(2), 0xF51C);
    }
    {
        // DREG=R4 continua sendo um destino valido para os 16 bits altos.
        static const uint8_t p6r4[] = {
            0xF6,0xAB,0xDA, 0xF5,0xAA,0x4A,
            0xB5,0x14,0x9F,0x00 };
        SNGSU g = runProgram(p6r4, sizeof(p6r4));
        CHECK("FMULT permite destino R4", g.GetReg(4), 0xF51C);
    }
    {
        // Prefixos sobrevivem ao proprio branch. O byte no delay slot usa
        // FROM R1 / TO R3 que foram selecionados antes do BRA.
        static const uint8_t p7[] = {
            0xF1,0x05,0x00,       // IWT R1,#5
            0xF2,0x07,0x00,       // IWT R2,#7
            0xB1,0x13,            // FROM R1 ; TO R3
            0x05,0x01,            // BRA +1 -> STOP
            0x52,                 // delay slot: ADD R2 -> R3=12
            0x00 };
        SNGSU g = runProgram(p7, sizeof(p7));
        CHECK("branch preserva prefixos", g.GetReg(3), 12);
    }
    {
        // A tabela de despacho real e' 06=BGE e 07=BLT (os nomes de duas
        // funcoes/comentarios em referencias antigas aparecem trocados).
        // Estes testes validam a condicao executada, nao apenas o rotulo.
        static const uint8_t pbge[] = {
            0xF1,0xFF,0xFF,       // R1=-1
            0xF2,0x01,0x00,       // R2=+1
            0xF3,0x11,0x11,       // marcador preservado se BGE saltar
            0xB2,0x3F,0x61,       // FROM R2; ALT3; CMP R1 -> +1 >= -1
            0x06,0x04,0x01,       // BGE +4; delay NOP; alvo=STOP
            0xF3,0xAD,0xDE,       // executado somente se 06 estiver errado
            0x00 };
        SNGSU bge = runProgram(pbge, sizeof(pbge));
        CHECK("opcode 06 = BGE", bge.GetReg(3), 0x1111);

        static const uint8_t pblt[] = {
            0xF1,0xFF,0xFF,       // R1=-1
            0xF2,0x01,0x00,       // R2=+1
            0xF4,0x22,0x22,       // marcador preservado se BLT saltar
            0xB1,0x3F,0x62,       // FROM R1; ALT3; CMP R2 -> -1 < +1
            0x07,0x04,0x01,       // BLT +4; delay NOP; alvo=STOP
            0xF4,0xAD,0xDE,       // executado somente se 07 estiver errado
            0x00 };
        SNGSU blt = runProgram(pblt, sizeof(pblt));
        CHECK("opcode 07 = BLT", blt.GetReg(4), 0x2222);
    }
    {
        // Qualquer opcode que escreve no destino R15 conserva o byte ja
        // buscado no pipeline. Ele e' executado antes do novo PC.
        static const uint8_t p8[] = {
            0xF1,0x08,0x80,       // IWT R1,#$8008 (alvo)
            0xB1,0x1F,            // FROM R1 ; TO R15
            0x50,                 // ADD R0 -> escreve o alvo em R15
            0xD3,                 // delay slot: INC R3
            0x01,                 // padding
            0x00 };               // $8008: STOP
        SNGSU g = runProgram(p8, sizeof(p8));
        CHECK("destino R15 tem pipeline", g.GetReg(3), 1);
    }
    {
        // Em um delay slot multi-byte, so o OPCODE vem da origem. Depois
        // desse primeiro byte o PC ja aponta ao destino, de onde saem os
        // operandos imediatos (comportamento usado pelo Doom).
        static const uint8_t p9[] = {
            0xF8,0x10,0x80,       // R8=$8010
            0x98,                 // JMP R8
            0xF3,                 // delay: IWT R3,#imm16 (so este byte aqui)
            0xAA,0xBB,            // NAO podem virar o imediato
            0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
            0x34,0x12,            // $8010: imediato no destino
            0x00 };               // $8012: STOP
        SNGSU g = runProgram(p9, sizeof(p9));
        CHECK("pipeline divide opcode imediato", g.GetReg(3), 0x1234);
    }
    {
        // LJMP tambem so pode trocar PBR/CBR depois de buscar o opcode do
        // delay slot. Se o cache for realinhado cedo, ele e' preenchido com
        // o banco antigo e os operandos no destino saem do lugar errado.
        memset(g_rom, 0x01, sizeof(g_rom));
        memset(g_ram, 0x00, sizeof(g_ram));
        static const uint8_t origin[] = {
            0xF1,0x00,0x80,       // R1=$8000 (PC alvo)
            0xFA,0x01,0x00,       // R10=$0001 (PBR alvo)
            0xB1,0x3D,0x9A,       // FROM R1; ALT1; LJMP R10
            0xF3,                 // delay: IWT R3 (opcode no banco 0)
            0xAA,0xBB };
        memcpy(g_rom, origin, sizeof(origin));
        g_rom[0x8000] = 0x34;      // operandos/STOP no banco 1:$8000
        g_rom[0x8001] = 0x12;
        g_rom[0x8002] = 0x00;
        SNGSU g; g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        g.WriteReg(0x3034,0); g.WriteReg(0x301E,0); g.WriteReg(0x301F,0x80);
        g.Run(100000);
        CHECK("LJMP troca cache apos delay", g.GetReg(3), 0x1234);
        CHECK("LJMP troca PBR", g.ReadReg(0x3034), 0x01);
    }
    {
        // ALT1/2/3 cancelam o B de WITH. Assim, o FROM seguinte volta a ser
        // prefixo em vez de ser interpretado como MOVES.
        static const uint8_t p10[] = {
            0xF1,0x34,0x12,       // R1=$1234
            0xF2,0x01,0x00,       // R2=1
            0x21,0x3D,            // WITH R1 ; ALT1 (deve limpar B)
            0xB2,0x13,0x4F,       // FROM R2 ; TO R3 ; NOT
            0x00 };
        SNGSU g = runProgram(p10, sizeof(p10));
        CHECK("ALT limpa B (R1 intacto)", g.GetReg(1), 0x1234);
        CHECK("ALT limpa B (FROM ativo)", g.GetReg(3), 0xFFFE);
    }
    {
        // GETBH/GETBL preservam a metade do registrador SOURCE, nao o valor
        // antigo do destino.
        static const uint8_t getbh[] = {
            0xF1,0x34,0x12, 0xF2,0xCD,0xAB, 0xFE,0x00,0x90,
            0xB1,0x12,0x3D,0xEF,0x00 };
        memset(g_rom, 0x01, sizeof(g_rom));
        memset(g_ram, 0x00, sizeof(g_ram));
        memcpy(g_rom, getbh, sizeof(getbh));
        g_rom[0x1000] = 0x5A;     // ROMBR=0, R14=$9000
        SNGSU g; g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        g.WriteReg(0x3034,0); g.WriteReg(0x301E,0); g.WriteReg(0x301F,0x80);
        g.Run(100000);
        CHECK("GETBH preserva source low", g.GetReg(2), 0x5A34);

        static const uint8_t getbl[] = {
            0xF1,0x34,0x12, 0xF2,0xCD,0xAB, 0xFE,0x00,0x90,
            0xB1,0x12,0x3E,0xEF,0x00 };
        memset(g_rom, 0x01, sizeof(g_rom)); memcpy(g_rom, getbl, sizeof(getbl));
        g_rom[0x1000] = 0x5A;
        g.Reset(); g.WriteReg(0x3034,0); g.WriteReg(0x301E,0); g.WriteReg(0x301F,0x80);
        g.Run(100000);
        CHECK("GETBL preserva source high", g.GetReg(2), 0x125A);
    }
    {
        // MERGE tem flags especiais, baseadas em mascaras de ambos os bytes.
        static const uint8_t pm[] = {
            0xF7,0x00,0xA5, 0xF8,0x00,0x5A, 0x13,0x70,0x00 };
        SNGSU g = runProgram(pm, sizeof(pm));
        CHECK("MERGE resultado", g.GetReg(3), 0xA55A);
        uint8_t sfr = g.ReadReg(0x3030);
        CHECK("MERGE flag Z",  sfr & 0x02, 0x02);
        CHECK("MERGE flag CY", sfr & 0x04, 0x04);
        CHECK("MERGE flag S",  sfr & 0x08, 0x08);
        CHECK("MERGE flag OV", sfr & 0x10, 0x10);
    }
    {
        // Uma linha escrita por completo em $3100-$310F fica valida e deve
        // sobrepor ROM/RAM quando PC estiver na janela CBR..CBR+511.
        memset(g_rom, 0x00, sizeof(g_rom));
        memset(g_ram, 0x00, sizeof(g_ram));
        uint8_t cacheLine[16]; memset(cacheLine, 0x01, sizeof(cacheLine));
        cacheLine[0]=0xF3; cacheLine[1]=0x34; cacheLine[2]=0x12; cacheLine[3]=0x00;
        SNGSU g; g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        g.WriteReg(0x3034,0);
        for (int i=0; i<16; i++) g.WriteReg((uint16_t)(0x3100+i),cacheLine[i]);
        g.WriteReg(0x301E,0); g.WriteReg(0x301F,0);
        g.Run(100000);
        CHECK("fetch usa code cache", g.GetReg(3), 0x1234);
    }
    {
        // Com CBR=$C3A0, o inicio logico do cache aparece para a CPU em
        // $3160: ($060 + $1A0) & $1FF = 0. STOP e escritas com GO ja zero
        // preservam o CBR; somente a transicao GO=1 -> GO=0 o limpa.
        memset(g_rom, 0x01, sizeof(g_rom));
        memset(g_ram, 0x00, sizeof(g_ram));
        g_rom[0x43A4] = 0x02;                 // CACHE em PBR=0:$C3A4
        SNGSU g; g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        g.WriteReg(0x3034,0);
        g.WriteReg(0x301E,0xA4); g.WriteReg(0x301F,0xC3);
        // O pipeline do GSU inicia com um NOP: primeiro ciclo prebusca CACHE,
        // segundo ciclo efetivamente o executa.
        g.Run(2);
        CHECK("CACHE define CBR low",  g.ReadReg(0x303E), 0xA0);
        CHECK("CACHE define CBR high", g.ReadReg(0x303F), 0xC3);

        uint8_t line[16]; memset(line, 0x01, sizeof(line));
        line[0]=0xF3; line[1]=0xEF; line[2]=0xBE; line[3]=0x00;
        for (int i=0; i<16; i++) g.WriteReg((uint16_t)(0x3160+i),line[i]);
        g.WriteReg(0x301E,0xA0); g.WriteReg(0x301F,0xC3);
        g.Run(100000);
        CHECK("cache rotacionado pelo CBR", g.GetReg(3), 0xBEEF);
        CHECK("STOP preserva CBR", g.ReadReg(0x303E), 0xA0);
        g.WriteReg(0x3030,0x00);
        CHECK("GO ja zero preserva CBR", g.ReadReg(0x303E), 0xA0);
        g.WriteReg(0x3030,0x20);
        g.WriteReg(0x3030,0x00);
        CHECK("transicao GO limpa CBR low",  g.ReadReg(0x303E), 0x00);
        CHECK("transicao GO limpa CBR high", g.ReadReg(0x303F), 0x00);
    }
    {
        // PBR $60-$7F executa da Game Pak RAM. No Mario Chip 1 esta janela
        // e' usada diretamente; limitar o core a $70/$71 deixa Star Fox sem
        // executar os blocos enviados pelo 65816.
        memset(g_rom, 0x01, sizeof(g_rom));
        memset(g_ram, 0x01, sizeof(g_ram));
        static const uint8_t ramProgram[] = { 0xF3,0x34,0x12,0x00 };
        memcpy(g_ram, ramProgram, sizeof(ramProgram));
        SNGSU g; g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        g.WriteReg(0x3034,0x60);
        g.WriteReg(0x301E,0x00); g.WriteReg(0x301F,0x80);
        g.Run(1000);
        CHECK("PBR $60 executa Game Pak RAM", g.GetReg(3), 0x1234);
    }

    // ===== Parte D: graficos (PLOT / pixel cache / RPIX) =====
    printf("\n--- graficos (PLOT/RPIX/bitplanes) ---\n");
    {
        // 16 cores (4bpp), altura 128, SCBR=0.  Plota cor 5 em (0,0), le de
        // volta com RPIX e confere os bytes de bitplane gerados na RAM.
        static const uint8_t pg[] = {
            0xF5,0x05,0x00,   // IWT R5,#5
            0xB5,             // FROM R5
            0x4E,             // COLOR  -> color = 5
            0xF1,0x00,0x00,   // IWT R1,#0  (X)
            0xF2,0x00,0x00,   // IWT R2,#0  (Y)
            0x4C,             // PLOT (0,0)=5 ; R1->1
            0xF1,0x00,0x00,   // IWT R1,#0  (reset X)
            0x16,             // TO R6
            0x3D,0x4C,        // RPIX -> R6 = pixel(0,0) (flush)
            0x00 };
        memset(g_rom, 0x01, sizeof(g_rom));
        memset(g_ram, 0x00, sizeof(g_ram));
        memcpy(g_rom, pg, sizeof(pg));
        SNGSU g;
        g.SetMemory(g_rom, sizeof(g_rom), g_ram, sizeof(g_ram));
        g.Reset();
        g.WriteReg(0x303A, 0x19);   // SCMR: MD=1 (16c), RAN+RON, height 128
        g.WriteReg(0x3038, 0x00);   // SCBR = 0
        g.WriteReg(0x3034, 0x00);
        g.WriteReg(0x301E, 0x00);
        g.WriteReg(0x301F, 0x80);
        g.Run(100000);
        // cor 5 = 0101b: plano0=1 (byte[0] bit7), plano2=1 (byte[16] bit7)
        CHECK("PLOT/RPIX round-trip R6", g.GetReg(6), 5);
        CHECK("bitplane0 byte[0]",  g_ram[0],  0x80);
        CHECK("bitplane1 byte[1]",  g_ram[1],  0x00);
        CHECK("bitplane2 byte[16]", g_ram[16], 0x80);
        CHECK("bitplane3 byte[17]", g_ram[17], 0x00);
    }
    {
        // POR.OBJ força layout 256x256 mesmo quando SCMR.HT seleciona 128.
        // Em 4bpp, pixel (0,128) pertence ao tile $200 -> offset $4000.
        static const uint8_t pobj[] = {
            0xF5,0x10,0x00, 0xB5,0x3D,0x4E, // CMODE OBJ
            0xF5,0x01,0x00, 0xB5,0x4E,      // COLOR=1
            0xF1,0x00,0x00, 0xF2,0x80,0x00,
            0x4C,                            // PLOT (0,128)
            0xF1,0x00,0x00, 0x3D,0x4C,      // RPIX: flush
            0x00 };
        memset(g_rom, 0x01, sizeof(g_rom)); memset(g_ram,0,sizeof(g_ram));
        memcpy(g_rom,pobj,sizeof(pobj));
        SNGSU g; g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        g.WriteReg(0x303A,0x19); g.WriteReg(0x3034,0);
        g.WriteReg(0x301E,0); g.WriteReg(0x301F,0x80); g.Run(100000);
        CHECK("CMODE OBJ endereco tile", g_ram[0x4000], 0x80);
        CHECK("CMODE OBJ nao usa 128px", g_ram[0x0200], 0x00);
    }
    {
        // Alterna A->B->A para exercitar os dois buffers reais. O secundario
        // precisa preservar B enquanto o primario volta a receber A.
        static const uint8_t ptwo[] = {
            0xF5,0x01,0x00, 0xB5,0x4E,      // COLOR=1
            0xF1,0x00,0x00, 0xF2,0x00,0x00,0x4C, // A: (0,0)=1
            0xF5,0x02,0x00, 0xB5,0x4E,      // COLOR=2
            0xF1,0x08,0x00, 0x4C,           // B: (8,0)=2
            0xF5,0x03,0x00, 0xB5,0x4E,      // COLOR=3
            0xF1,0x01,0x00, 0x4C,           // A: (1,0)=3
            0xF1,0x00,0x00, 0x3D,0x4C,      // RPIX força ambos
            0x00 };
        memset(g_rom,0x01,sizeof(g_rom)); memset(g_ram,0,sizeof(g_ram));
        memcpy(g_rom,ptwo,sizeof(ptwo));
        SNGSU g; g.SetMemory(g_rom,sizeof(g_rom),g_ram,sizeof(g_ram)); g.Reset();
        g.WriteReg(0x303A,0x19); g.WriteReg(0x3034,0);
        g.WriteReg(0x301E,0); g.WriteReg(0x301F,0x80); g.Run(100000);
        CHECK("pixel cache A plano 0", g_ram[0], 0xC0);
        CHECK("pixel cache A plano 1", g_ram[1], 0x40);
        CHECK("pixel cache B plano 0", g_ram[0x200], 0x00);
        CHECK("pixel cache B plano 1", g_ram[0x201], 0x80);
    }

    // ===== Parte B: fuzz oracle =====
    printf("\n--- fuzz oracle (milhares de casos) ---\n");
    fuzz();
    printf("fuzz: %d casos, %d falhas\n", g_fuzzCases, g_fuzzFail);
    g_fail += g_fuzzFail;

    printf("\n%s (%d falha%s no total)\n", g_fail ? "FALHOU" : "PASSOU",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
