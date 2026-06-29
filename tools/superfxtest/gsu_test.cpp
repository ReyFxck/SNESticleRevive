// Bancada host-side do core SuperFX/GSU (src/snes/core/sngsu.cpp).
//
//  Parte A: testes de unidade deterministicos (MMIO, GO, prefixos, STOP/IRQ).
//  Parte B: FUZZ ORACLE -- milhares de casos. Para cada opcode de calculo,
//           varre muitos operandos e compara resultado+flags do GSU contra
//           um calculo de referencia em C. Acusa qualquer divergencia.
//
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
            { Res g=runOp(MUL,(int)sizeof(MUL),a,b,false); uint16_t r=(uint16_t)((int16_t)a*(int16_t)b); cmp("MULT",a,b,g,r,r==0,0,(r>>15)&1,0,false); }
            { Res g=runOp(UMUL,(int)sizeof(UMUL),a,b,false);uint16_t r=(uint16_t)((uint32_t)a*(uint32_t)b); cmp("UMULT",a,b,g,r,r==0,0,(r>>15)&1,0,false); }

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

    // ===== Parte B: fuzz oracle =====
    printf("\n--- fuzz oracle (milhares de casos) ---\n");
    fuzz();
    printf("fuzz: %d casos, %d falhas\n", g_fuzzCases, g_fuzzFail);
    g_fail += g_fuzzFail;

    printf("\n%s (%d falha%s no total)\n", g_fail ? "FALHOU" : "PASSOU",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
