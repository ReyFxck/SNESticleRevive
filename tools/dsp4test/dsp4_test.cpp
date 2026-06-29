// Bancada host-side do DSP-4 HLE (src/snes/core/sndsp4.cpp + dsp4emu.cpp).
//
// Agora que o HLE do ZSNES (GPLv2) esta portado, validamos:
//   - Status sempre pronto (0x80 = RQM).
//   - Transfers de 16 bits LSB-first.
//   - Comando 0x14 (Test ROM Version) -> 0x0400 (identifica o DSP-4).
//   - Comando 0x00 (multiplicacao 16-bit) -> resultado correto (FUNCIONAL).
//   - Terminador 0xFFFF ao ler alem do fim de um comando.
//   - Opcode realmente desconhecido nao trava e mantem a FSM sincronizada.

#include "sndsp4.h"
#include <cstdio>
#include <cstdint>
#include <cstdarg>

// Stub de DLog: no PS2 ele escreve no SIO; no bench host so' descartamos.
extern "C" void DLog(const char *fmt, ...)
{
    (void)fmt;
    // descomente para ver a captura .vec no terminal:
    // va_list ap; va_start(ap, fmt); vprintf(fmt, ap); printf("\n"); va_end(ap);
}

static void sendWord(SNDSP4 &d, uint16_t w)
{
    d.WriteData(0, (uint8_t)(w & 0xFF));   // LSB primeiro (igual SNES real)
    d.WriteData(0, (uint8_t)(w >> 8));     // MSB
}

static uint16_t readWord(SNDSP4 &d)
{
    uint8_t lo = d.ReadData(0);
    uint8_t hi = d.ReadData(0);
    return (uint16_t)(lo | ((uint16_t)hi << 8));
}

int main()
{
    SNDSP4 dsp;
    int fail = 0;

    // status sempre pronto
    if (dsp.ReadStatus(0) != 0x80) { printf("FAIL: status != 0x80\n"); fail++; }
    else                            printf("OK  : status=0x80 (RQM pronto)\n");

    // comando de versao 0x14 -> 0x0400
    sendWord(dsp, 0x0014);
    uint16_t ver = readWord(dsp);
    if (ver != 0x0400) { printf("FAIL: version=0x%04X (esperado 0x0400)\n", ver); fail++; }
    else               printf("OK  : version=0x0400 (DSP-4)\n");

    // ler alem do fim -> terminador 0xFFFF
    uint16_t term = readWord(dsp);
    if (term != 0xFFFF) { printf("FAIL: terminador=0x%04X (esperado 0xFFFF)\n", term); fail++; }
    else                printf("OK  : terminador=0xFFFF\n");

    // comando 0x00: multiplicacao 16-bit  (multiplier=100, multiplicand=3 -> 300)
    sendWord(dsp, 0x0000);
    sendWord(dsp, 100);   // multiplier
    sendWord(dsp, 3);     // multiplicand
    uint16_t prod_lo = readWord(dsp);
    uint16_t prod_hi = readWord(dsp);
    uint32_t prod = prod_lo | ((uint32_t)prod_hi << 16);
    if (prod != 300) { printf("FAIL: multiply 100*3 = %u (esperado 300)\n", prod); fail++; }
    else             printf("OK  : multiply 100*3 = 300 (HLE funcional!)\n");

    // opcode realmente desconhecido (0x12): inerte, nao trava, da 0xFFFF
    sendWord(dsp, 0x0012);
    uint16_t r = readWord(dsp);
    if (r != 0xFFFF) { printf("FAIL: cmd 0x12 deu 0x%04X (esperado 0xFFFF)\n", r); fail++; }
    else             printf("OK  : cmd 0x12 desconhecido -> 0xFFFF (sem crash)\n");

    // re-testar versao depois do desconhecido (FSM ainda sincronizada?)
    sendWord(dsp, 0x0014);
    ver = readWord(dsp);
    if (ver != 0x0400) { printf("FAIL: version pos-unknown=0x%04X\n", ver); fail++; }
    else               printf("OK  : FSM sincronizada apos comando desconhecido\n");

    printf("\n%s (%d falha%s)\n", fail ? "FALHOU" : "PASSOU",
           fail, fail == 1 ? "" : "s");
    return fail ? 1 : 0;
}
