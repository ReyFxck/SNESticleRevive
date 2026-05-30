// Bancada de teste host-side para o DSP-1 HLE (sndsp1.cpp).
// Compila o codigo REAL do emulador e dirige o protocolo de
// barramento (WriteData/ReadData) como a CPU do SNES faria.
//
// Objetivo: rodar comandos com entradas conhecidas e inspecionar
// as saidas, sem precisar do PS2 nem de adivinhar matematica.

#include "sndsp1.h"
#include <cstdio>
#include <cstdint>

// ---- helpers do protocolo de barramento ----
// O jogo escreve o opcode (1 byte), depois cada parametro de 16 bits
// como MSB seguido de LSB.  Le os resultados da mesma forma.
static void sendByte(SNDSP1 &d, uint8_t b) { d.WriteData(0, b); }

static void sendWord(SNDSP1 &d, int16_t w) {
    d.WriteData(0, (uint8_t)((uint16_t)w >> 8));   // MSB
    d.WriteData(0, (uint8_t)((uint16_t)w & 0xFF)); // LSB
}

static int16_t readWord(SNDSP1 &d) {
    uint8_t hi = d.ReadData(0);
    uint8_t lo = d.ReadData(0);
    return (int16_t)(((uint16_t)hi << 8) | lo);
}

int main() {
    SNDSP1 dsp;

    // ---------- sanity: Multiply (op 0x00), 2 in, 1 out ----------
    // Esperado: (a * b) >> 15.  0x4000 * 0x4000 >> 15 = 0x2000.
    {
        sendByte(dsp, 0x00);
        sendWord(dsp, 0x4000);
        sendWord(dsp, 0x4000);
        int16_t r = readWord(dsp);
        printf("[Multiply]  0x4000*0x4000>>15 = 0x%04X  (esperado 0x2000)  %s\n",
               (uint16_t)r, (r == 0x2000) ? "OK" : "FALHOU");
    }

    // ---------- Triangle (op 0x04): in=angle,r  out=Y(sin*r),X(cos*r) ----------
    // angle=0x4000 (=90 graus): sin=1.0(0x7fff), cos=0.  r=0x7fff.
    // Y ~= 0x7ffe, X ~= 0.
    {
        sendByte(dsp, 0x04);
        sendWord(dsp, 0x4000); // angle
        sendWord(dsp, 0x7FFF); // r
        int16_t Y = readWord(dsp);
        int16_t X = readWord(dsp);
        printf("[Triangle]  a=90 r=1.0 -> Y=0x%04X X=0x%04X  (Y~0x7ffe X~0)\n",
               (uint16_t)Y, (uint16_t)X);
    }

    // ---------- Rotate (op 0x0C): in=A,X,Y  out=X2,Y2 ----------
    // A=0 (sem rotacao): deve devolver ~ (X, Y).
    {
        sendByte(dsp, 0x0C);
        sendWord(dsp, 0x0000); // A
        sendWord(dsp, 0x1234); // X
        sendWord(dsp, 0x0567); // Y
        int16_t X2 = readWord(dsp);
        int16_t Y2 = readWord(dsp);
        printf("[Rotate]    A=0 (0x1234,0x0567) -> (0x%04X,0x%04X)  (~entrada)\n",
               (uint16_t)X2, (uint16_t)Y2);
    }

    // ---------- Distance (op 0x28): in=X,Y,Z out=sqrt(X^2+Y^2+Z^2) ----------
    {
        sendByte(dsp, 0x28);
        sendWord(dsp, 0x0100);
        sendWord(dsp, 0x0000);
        sendWord(dsp, 0x0000);
        int16_t r = readWord(dsp);
        printf("[Distance]  |(0x100,0,0)| = 0x%04X  (esperado ~0x0100)\n", (uint16_t)r);
    }

    // ---------- Inverse (op 0x10): in=coeff,exp out=iCoeff,iExp ----------
    // coeff=0x4000 (=0.5), exp=0 -> 1/0.5 = 2.0.  iCoeff~0x4000 iExp~2
    // (2.0 = 0x4000 * 2^2 em 1.15 normalizado).
    {
        sendByte(dsp, 0x10);
        sendWord(dsp, 0x4000); // coeff
        sendWord(dsp, 0x0000); // exp
        int16_t iC = readWord(dsp);
        int16_t iE = readWord(dsp);
        printf("[Inverse]   1/0.5 -> C=0x%04X E=%d  (1.0*2^1 = 0x7fff,E=1 ou 0x4000,E=2)\n",
               (uint16_t)iC, iE);
    }

    // ========== Teste de consistencia Project <-> Target ==========
    // Parameter monta a camera; Target(H,V) -> ponto no chao (X,Y);
    // Project(X,Y,0) deve devolver ~ (H,V).  Se nao voltar, ha bug
    // na composicao dos comandos de camera.
    {
        // Parameter (op 0x02): Fx,Fy,Fz,Lfe,Les,Aas,Azs
        sendByte(dsp, 0x02);
        sendWord(dsp, 0x0000); // Fx (foco na origem)
        sendWord(dsp, 0x0000); // Fy
        sendWord(dsp, 0x0000); // Fz
        sendWord(dsp, 0x0600); // Lfe (foco->olho)
        sendWord(dsp, 0x0200); // Les (olho->tela)  (Lfe!=Les: nao degenerada)
        sendWord(dsp, 0x0000); // Aas (azimute 0)
        sendWord(dsp, 0x1000); // Azs (zenite ~22 graus)
        int16_t Vof = readWord(dsp);
        int16_t Vva = readWord(dsp);
        int16_t Cx  = readWord(dsp);
        int16_t Cy  = readWord(dsp);
        printf("\n[Parameter] Vof=0x%04X Vva=0x%04X Cx=0x%04X Cy=0x%04X\n",
               (uint16_t)Vof,(uint16_t)Vva,(uint16_t)Cx,(uint16_t)Cy);

        // round-trip para alguns pontos de tela
        const int16_t Hs[] = { 0, 32, -32, 0 };
        const int16_t Vs[] = { 16, 48, 48, 80 };
        for (int i = 0; i < 4; i++) {
            sendByte(dsp, 0x0E);           // Target: H,V -> X,Y
            sendWord(dsp, Hs[i]);
            sendWord(dsp, Vs[i]);
            int16_t X = readWord(dsp);
            int16_t Y = readWord(dsp);

            sendByte(dsp, 0x06);           // Project: X,Y,Z -> H,V,M
            sendWord(dsp, X);
            sendWord(dsp, Y);
            sendWord(dsp, 0x0000);         // Z=0 (chao)
            int16_t H2 = readWord(dsp);
            int16_t V2 = readWord(dsp);
            int16_t M  = readWord(dsp);
            printf("  (H=%4d,V=%4d) -Target-> (X=%6d,Y=%6d) -Project-> (H=%4d,V=%4d,M=0x%04X)  %s\n",
                   Hs[i], Vs[i], X, Y, H2, V2, (uint16_t)M,
                   (abs(H2-Hs[i])<=2 && abs(V2-Vs[i])<=2) ? "OK" : "<-- DIVERGE");
        }
    }

    return 0;
}
