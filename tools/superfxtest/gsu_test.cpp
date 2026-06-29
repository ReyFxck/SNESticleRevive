// Bancada host-side do core SuperFX/GSU (src/snes/core/sngsu.cpp).
//
// Etapa 1: valida MMIO (latch R0-R15), o disparo de GO via $301F, o loop
// fetch/execute, os prefixos TO/FROM, IWT, ADD/SUB com flags, e STOP/IRQ.
// Determinístico -> "teste que nunca erra": roda igual aqui e no CI.

#include "sngsu.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

static int g_fail = 0;

static void CHECK(const char *name, long got, long exp)
{
    if (got != exp) {
        printf("FAIL: %-28s got=0x%lX exp=0x%lX\n", name, (unsigned long)got, (unsigned long)exp);
        g_fail++;
    } else {
        printf("OK  : %-28s = 0x%lX\n", name, (unsigned long)exp);
    }
}

int main()
{
    // ---- ROM/RAM do "cartucho" de teste ----
    static uint8_t rom[0x10000];
    static uint8_t ram[0x8000];
    memset(rom, 0x01 /*NOP*/, sizeof(rom));   // preenche com NOP por seguranca
    memset(ram, 0, sizeof(ram));

    // Programa GSU em rom[0] (PBR=0, R15=0x8000 -> offset LoROM 0):
    //   IWT R1,#100 ; IWT R2,#3 ; FROM R1 ; TO R3 ; ADD R2   -> R3 = 103
    //   FROM R1 ; TO R4 ; SUB R2                              -> R4 = 97  (CY=1)
    //   IWT R6,#FFFF ; IWT R7,#1 ; FROM R6 ; TO R8 ; ADD R7   -> R8 = 0 (CY=1,Z=1)
    //   STOP
    static const uint8_t prog[] = {
        0xF1, 0x64, 0x00,   // IWT R1,#0x0064 (100)
        0xF2, 0x03, 0x00,   // IWT R2,#0x0003 (3)
        0xB1,               // FROM R1   (Sreg=1)
        0x13,               // TO   R3   (Dreg=3)
        0x52,               // ADD  R2   -> R3 = R1+R2 = 103
        0xB1,               // FROM R1
        0x14,               // TO   R4
        0x62,               // SUB  R2   -> R4 = R1-R2 = 97
        0xF6, 0xFF, 0xFF,   // IWT R6,#0xFFFF
        0xF7, 0x01, 0x00,   // IWT R7,#0x0001
        0xB6,               // FROM R6
        0x18,               // TO   R8
        0x57,               // ADD  R7   -> R8 = 0x0000 (CY=1, Z=1)
        0x00                // STOP
    };
    memcpy(rom, prog, sizeof(prog));

    SNGSU gsu;
    gsu.SetMemory(rom, sizeof(rom), ram, sizeof(ram));
    gsu.Reset();

    // ---- 1) MMIO: latch de escrita/leitura de R5 (addr $300A/$300B) ----
    gsu.WriteReg(0x300A, 0x34);   // LATCH = 0x34
    gsu.WriteReg(0x300B, 0x12);   // R5 = 0x1234
    CHECK("MMIO R5 readback lo", gsu.ReadReg(0x300A), 0x34);
    CHECK("MMIO R5 readback hi", gsu.ReadReg(0x300B), 0x12);
    CHECK("MMIO R5 value",       gsu.GetReg(5),        0x1234);

    // ---- 2) VCR e SFR antes de rodar ----
    CHECK("VCR (versao)",        gsu.ReadReg(0x303B), 0x04);
    CHECK("SFR low (GO=0)",      gsu.ReadReg(0x3030) & 0x20, 0x00);

    // ---- 3) Dispara o programa: PBR=0, R15=0x8000 (a escrita em $301F liga GO) ----
    gsu.WriteReg(0x3034, 0x00);   // PBR = 0
    gsu.WriteReg(0x301E, 0x00);   // LATCH = 0x00 (R15 lo)
    gsu.WriteReg(0x301F, 0x80);   // R15 = 0x8000 + GO=1

    CHECK("GO ligado apos $301F",  gsu.IsRunning() ? 1 : 0, 1);

    gsu.Run(10000);               // roda ate' STOP limpar GO

    // ---- 4) Resultados ----
    CHECK("GO desligado apos STOP", gsu.IsRunning() ? 1 : 0, 0);
    CHECK("R3 = 100 + 3",  gsu.GetReg(3), 103);
    CHECK("R4 = 100 - 3",  gsu.GetReg(4), 97);
    CHECK("R8 = FFFF + 1",  gsu.GetReg(8), 0x0000);

    // flags do ultimo ADD (R8): Z=1, CY=1, S=0, OV=0  -> SFR low = 0x02|0x04 = 0x06
    Uint8 sfrLo = gsu.ReadReg(0x3030);
    CHECK("SFR Z (zero)",  sfrLo & 0x02, 0x02);
    CHECK("SFR CY (carry)", sfrLo & 0x04, 0x04);
    CHECK("SFR S (sign)",  sfrLo & 0x08, 0x00);

    // IRQ setado no STOP; ler $3031 deve mostrar bit7 e depois limpar
    Uint8 sfrHi = gsu.ReadReg(0x3031);
    CHECK("SFR IRQ no STOP", sfrHi & 0x80, 0x80);
    CHECK("IRQ limpa apos leitura", gsu.ReadReg(0x3031) & 0x80, 0x00);

    printf("\n%s (%d falha%s)\n", g_fail ? "FALHOU" : "PASSOU",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
