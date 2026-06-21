// Teste do comando Raster (0x0A) do DSP-1 - gera a matriz Mode-7
// (An,Bn,Cn,Dn) por scanline.  E' o que desenha a pista do Mario Kart
// e o chao do Pilotwings.  NAO e' coberto pela bancada principal.
//
// Dirige o protocolo de barramento exatamente como o jogo: escreve
// Parameter (monta a camera), depois Raster com Vs inicial e LE em
// streaming continuo (o DSP auto-incrementa Vs e entrega 4 words por
// scanline ate a CPU quebrar o stream com uma nova escrita).

#include "sndsp1.h"
#include <cstdio>
#include <cstdint>

static void sendByte(SNDSP1 &d, uint8_t b) { d.WriteData(0, b); }
static void sendWord(SNDSP1 &d, int16_t w) {
    d.WriteData(0, (uint8_t)((uint16_t)w >> 8));
    d.WriteData(0, (uint8_t)((uint16_t)w & 0xFF));
}
static int16_t readWord(SNDSP1 &d) {
    uint8_t hi = d.ReadData(0);
    uint8_t lo = d.ReadData(0);
    return (int16_t)(((uint16_t)hi << 8) | lo);
}

int main() {
    SNDSP1 dsp;

    // Camera tipo "corrida": foco na origem, olho atras e acima,
    // tela a frente, leve inclinacao (zenite).
    sendByte(dsp, 0x02);   // Parameter
    sendWord(dsp, 0x0000); // Fx
    sendWord(dsp, 0x0000); // Fy
    sendWord(dsp, 0x0000); // Fz
    sendWord(dsp, 0x0600); // Lfe
    sendWord(dsp, 0x0200); // Les
    sendWord(dsp, 0x0000); // Aas
    sendWord(dsp, 0x0800); // Azs (~11 graus)
    int16_t Vof = readWord(dsp);
    int16_t Vva = readWord(dsp);
    int16_t Cx  = readWord(dsp);
    int16_t Cy  = readWord(dsp);
    printf("[Parameter] Vof=0x%04X Vva=0x%04X Cx=0x%04X Cy=0x%04X\n\n",
           (uint16_t)Vof,(uint16_t)Vva,(uint16_t)Cx,(uint16_t)Cy);

    // Raster em streaming: escreve Vs inicial, le 4 words por scanline.
    int16_t Vs0 = Vof; // jogos costumam comecar do Vof
    printf("[Raster] streaming a partir de Vs=0x%04X (%d):\n", (uint16_t)Vs0, Vs0);
    sendByte(dsp, 0x0A);
    sendWord(dsp, Vs0);

    int prevA = 0;
    bool monotonic = true, allzero = true, sat = false;
    for (int line = 0; line < 12; line++) {
        int16_t An = readWord(dsp);
        int16_t Bn = readWord(dsp);
        int16_t Cn = readWord(dsp);
        int16_t Dn = readWord(dsp);
        printf("  line %2d: An=%6d Bn=%6d Cn=%6d Dn=%6d\n", line, An, Bn, Cn, Dn);
        if (An||Bn||Cn||Dn) allzero = false;
        if (An==32767||An==-32767||Dn==32767||Dn==-32767) sat = true;
        if (line>0 && abs(An) > abs(prevA)) monotonic = false; // perspectiva: |An| deve diminuir com a distancia
        prevA = An;
    }
    printf("\n  allzero=%s  saturado=%s  An_decrescente=%s\n",
           allzero?"SIM(BUG)":"nao", sat?"SIM(suspeito)":"nao",
           monotonic?"sim(ok perspectiva)":"NAO(suspeito)");
    return 0;
}
