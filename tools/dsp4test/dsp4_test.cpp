// Bancada host-side do DSP-4 HLE (src/snes/core/sndsp4.cpp).
//
// Valida o PROTOCOLO de barramento (o que e' documentado e testavel):
//   - Status sempre pronto (0x80 = RQM).
//   - Transfers de 16 bits LSB-first.
//   - Comando 0x14 (Test ROM Version) -> 0x0400 (identifica o DSP-4).
//   - Terminador 0xFFFF ao ler alem do fim de um comando.
//   - Comando funcional desconhecido nao trava e mantem a FSM sincronizada.
//
// NAO valida a matematica de renderizacao da pista: ela nao tem spec
// publica e ainda nao foi implementada (ver sndsp4.h).

#include "sndsp4.h"
#include <cstdio>
#include <cstdint>
#include <cstdarg>

// Stub de DLog: no PS2 ele escreve no SIO; no bench host so' descartamos
// (ou imprime, se quiser ver a captura no terminal).  Necessario porque a
// captura do sndsp4.cpp agora referencia DLog sempre.
extern "C" void DLog(const char *fmt, ...)
{
    (void)fmt;
    // descomente para ver a captura no terminal:
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

    // comando funcional desconhecido (0x08): inerte, nao trava, da 0xFFFF
    sendWord(dsp, 0x0008);
    uint16_t r = readWord(dsp);
    if (r != 0xFFFF) { printf("FAIL: cmd 0x08 deu 0x%04X (esperado 0xFFFF)\n", r); fail++; }
    else             printf("OK  : cmd 0x08 inerte -> 0xFFFF (sem crash)\n");

    // re-testar versao depois do desconhecido (FSM ainda sincronizada?)
    sendWord(dsp, 0x0014);
    ver = readWord(dsp);
    if (ver != 0x0400) { printf("FAIL: version pos-unknown=0x%04X\n", ver); fail++; }
    else               printf("OK  : FSM sincronizada apos comando desconhecido\n");

    printf("\n%s (%d falha%s)\n", fail ? "FALHOU" : "PASSOU",
           fail, fail == 1 ? "" : "s");
    return fail ? 1 : 0;
}
