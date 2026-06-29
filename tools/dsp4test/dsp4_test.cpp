// Bancada host-side do DSP-4 HLE (src/snes/core/sndsp4.cpp).
//
// Valida:
//   - PROTOCOLO: status 0x80, transfers 16-bit LSB-first, versao 0x14
//     -> 0x0400, terminador 0xFFFF.
//   - COMANDOS ARITMETICOS verificaveis a mao:
//       0x00 (multiplicacao), 0x11 (mapeamento horizontal), 0x0A (lookup).
//   - SMOKE TEST dos comandos de PROJECAO (0x01): alimenta um quadro
//     plausivel + terminador e confere que ele gera saida, nao trava e
//     deixa a FSM sincronizada para o proximo comando.
//
// A matematica dos comandos de projecao nao tem spec publica para
// comparar valor-a-valor; o smoke test cobre o fluxo/FSM (a parte que
// dessincroniza e trava o jogo quando esta errada).

#include "sndsp4.h"
#include <cstdio>
#include <cstdint>
#include <cstdarg>

extern "C" void DLog(const char *fmt, ...)
{
    (void)fmt;
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

static int g_fail = 0;
static void check(const char *name, uint16_t got, uint16_t exp)
{
    if (got != exp) { printf("FAIL: %-28s got=0x%04X esperado=0x%04X\n", name, got, exp); g_fail++; }
    else            { printf("OK  : %-28s = 0x%04X\n", name, got); }
}

int main()
{
    SNDSP4 dsp;

    //--------------------------------------------------------------
    // Protocolo
    //--------------------------------------------------------------
    if (dsp.ReadStatus(0) != 0x80) { printf("FAIL: status != 0x80\n"); g_fail++; }
    else                            printf("OK  : status=0x80 (RQM pronto)\n");

    sendWord(dsp, 0x0014);
    check("version 0x14", readWord(dsp), 0x0400);
    check("terminador pos-comando", readWord(dsp), 0xFFFF);

    //--------------------------------------------------------------
    // 0x00 - multiplicacao 16x16 (write product low, product>>16)
    //--------------------------------------------------------------
    // 19 * 0x35 = 1007 = 0x03EF  (igual a captura do jogo: idx*0x35)
    sendWord(dsp, 0x0000);
    sendWord(dsp, 0x0013);   // multiplier = 19
    sendWord(dsp, 0x0035);   // multiplicand = 0x35
    check("mul 19*0x35 low",  readWord(dsp), 0x03EF);
    check("mul 19*0x35 high", readWord(dsp), 0x0000);

    // 0x4000 * 4 = 0x10000 -> low=0x0000, high=0x0001
    sendWord(dsp, 0x0000);
    sendWord(dsp, 0x4000);
    sendWord(dsp, 0x0004);
    check("mul 0x4000*4 low",  readWord(dsp), 0x0000);
    check("mul 0x4000*4 high", readWord(dsp), 0x0001);

    //--------------------------------------------------------------
    // 0x11 - mapeamento horizontal (le d,c,b,a -> M)
    // captura: d=FF18 c=00E8 b=FF18 a=00E8 -> M=0x4B4B (calculado a mao)
    //--------------------------------------------------------------
    sendWord(dsp, 0x0011);
    sendWord(dsp, 0xFF18);   // d
    sendWord(dsp, 0x00E8);   // c
    sendWord(dsp, 0xFF18);   // b
    sendWord(dsp, 0x00E8);   // a
    check("op11 mapa horizontal", readWord(dsp), 0x4B4B);

    //--------------------------------------------------------------
    // 0x0A - lookup de 4 nibbles.  n2=0x1234 (palavra do meio).
    //   saida esperada (ordem o1,o2,o3,o4) = 0x0060,0x0030,0x00C0,0x0090
    //--------------------------------------------------------------
    sendWord(dsp, 0x000A);
    sendWord(dsp, 0x0000);   // palavra 0 (ignorada)
    sendWord(dsp, 0x1234);   // palavra 1 = n2
    sendWord(dsp, 0x0000);   // palavra 2 (ignorada)
    check("op0A o1", readWord(dsp), 0x0060);
    check("op0A o2", readWord(dsp), 0x0030);
    check("op0A o3", readWord(dsp), 0x00C0);
    check("op0A o4", readWord(dsp), 0x0090);

    //--------------------------------------------------------------
    // FSM ainda sincronizada apos os comandos aritmeticos?
    //--------------------------------------------------------------
    sendWord(dsp, 0x0014);
    check("version pos-aritmetica", readWord(dsp), 0x0400);
    readWord(dsp); // drena terminador

    //--------------------------------------------------------------
    // 0x01 - SMOKE TEST da projecao de pista (1 jogador).
    // Alimenta 44 bytes (22 words) de um quadro plausivel, le o primeiro
    // bloco de saida, manda o terminador -0x8000 e confere que a FSM
    // volta a aceitar comando (versao funciona depois).
    //--------------------------------------------------------------
    {
        // 22 words: world_y(2) bottom top cx1 vpbottom world_x(2) cx0 ptr
        //           yofs world_dy(2) world_dx(2) distance 0x0000
        //           world_xenv(2) ddy ddx yofsenv
        sendWord(dsp, 0x0001);              // comando
        // world_y = 0x00200000 (dword: low, high)
        sendWord(dsp, 0x0000); sendWord(dsp, 0x0020);
        sendWord(dsp, 0x00E0);              // poly_bottom = 224
        sendWord(dsp, 0x0000);              // poly_top = 0
        sendWord(dsp, 0x0000);              // poly_cx[1][0]
        sendWord(dsp, 0x00E0);              // viewport_bottom = 224
        // world_x = 0x00000000
        sendWord(dsp, 0x0000); sendWord(dsp, 0x0000);
        sendWord(dsp, 0x0080);              // poly_cx[0][0] = 128
        sendWord(dsp, 0x7E00);              // poly_ptr (HDMA)
        sendWord(dsp, 0x0000);              // world_yofs
        // world_dy = 0x00010000
        sendWord(dsp, 0x0000); sendWord(dsp, 0x0001);
        // world_dx = 0x00000000
        sendWord(dsp, 0x0000); sendWord(dsp, 0x0000);
        sendWord(dsp, 0x4000);              // distance (escala)
        sendWord(dsp, 0x0000);              // 0x0000
        // world_xenv = 0x00000000
        sendWord(dsp, 0x0000); sendWord(dsp, 0x0000);
        sendWord(dsp, 0x0000);              // world_ddy
        sendWord(dsp, 0x0000);              // world_ddx
        sendWord(dsp, 0x0000);              // view_yofsenv

        // le o primeiro bloco de saida: 4 words de posicao + 1 word
        // segments(N) + N*3 words de raster.  Tem que drenar TUDO antes
        // de mandar o proximo comando (senao o handshake "come" o byte).
        (void)readWord(dsp);  // world_x
        (void)readWord(dsp);  // view_x2
        (void)readWord(dsp);  // world_y
        (void)readWord(dsp);  // view_y2
        uint16_t segs = readWord(dsp);
        for (int i = 0; i < segs * 3; i++) (void)readWord(dsp);

        // proximo comando do stream = terminador
        sendWord(dsp, 0x8000);              // -0x8000 encerra a projecao

        // FSM deve aceitar comando de novo
        sendWord(dsp, 0x0014);
        uint16_t v = readWord(dsp);
        if (v == 0x0400) printf("OK  : op01 smoke (FSM ok, segments=%d)\n", segs);
        else { printf("FAIL: op01 smoke -> FSM dessincronizada (version=0x%04X)\n", v); g_fail++; }
    }

    //--------------------------------------------------------------
    // 0x08 - SMOKE TEST do poligono solido (90 bytes / 45 words).
    // Resume points 1-2.  Confere que nao trava e resincroniza.
    //--------------------------------------------------------------
    {
        sendWord(dsp, 0x0008);
        for (int i = 0; i < 45; i++) sendWord(dsp, 0x0000);  // params (smoke)
        (void)readWord(dsp);          // win_left/win_right inicial
        sendWord(dsp, 0x8000);        // terminador
        (void)readWord(dsp);          // WriteWord(0) final
        sendWord(dsp, 0x0014);
        uint16_t v = readWord(dsp);
        if (v == 0x0400) printf("OK  : op08 smoke (FSM ok)\n");
        else { printf("FAIL: op08 smoke -> FSM dessincronizada (version=0x%04X)\n", v); g_fail++; }
        readWord(dsp); // drena terminador
    }

    //--------------------------------------------------------------
    // 0x09 - SMOKE TEST da projecao de sprites (14 bytes / 7 words).
    // Resume points 1-6 (o mais complexo).  Termina logo no resume1.
    //--------------------------------------------------------------
    {
        sendWord(dsp, 0x0009);
        for (int i = 0; i < 7; i++) sendWord(dsp, 0x0000);   // viewport (smoke)
        sendWord(dsp, 0x0000);        // raster
        sendWord(dsp, 0x8000);        // distance = -0x8000 -> terminate
        sendWord(dsp, 0x0014);
        uint16_t v = readWord(dsp);
        if (v == 0x0400) printf("OK  : op09 smoke (FSM ok)\n");
        else { printf("FAIL: op09 smoke -> FSM dessincronizada (version=0x%04X)\n", v); g_fail++; }
    }

    printf("\n%s (%d falha%s)\n", g_fail ? "FALHOU" : "PASSOU",
           g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
