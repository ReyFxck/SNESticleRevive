// Bancada de teste host-side para o nucleo uPD7725 LLE
// (src/snes/core/sndsp1_lle.cpp), usado pelos chips DSP-1/2/3/4.
//
// Compila o codigo REAL do emulador num binario de PC, carrega um
// firmware combinado (dsp3.rom / dsp4.rom / dsp1.rom ...) e dirige o
// protocolo de barramento (WriteData / ReadData / ReadStatus) como a
// CPU do SNES faria.  Serve para:
//   - confirmar que o firmware "boota" (chip fica pronto: RQM=1);
//   - mandar uma sequencia de bytes de comando e inspecionar a saida;
//   - validar o nucleo LLE contra o comportamento do chip real, antes
//     de commitar (workflow "verify bit-exact" do projeto).
//
// Uso:
//   ./dsplle_test <firmware.rom> [byteHex ...] [-r N]
//
//   <firmware.rom>  imagem de 8192 bytes (6144 program + 2048 data).
//   byteHex         bytes a escrever no Data Register, em hex (ex.: 00 40).
//   -r N            quantos bytes ler de volta apos os writes (default 4).
//
// Exemplos:
//   ./dsplle_test dsp4.rom              # so' smoke test (boot + status)
//   ./dsplle_test dsp4.rom 00 40 00 -r 4

#include "sndsp1_lle.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Le o arquivo de firmware inteiro para um buffer.
static long readFile(const char *path, unsigned char *buf, long max) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERRO: nao abriu '%s'\n", path); return -1; }
    long n = (long)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}

// Helpers do protocolo: a CPU SNES espera RQM=1 (bit 7 do status) antes
// de tocar no Data Register.  Aqui exibimos o status para diagnostico.
static bool ready(SNDSP1_LLE &d) { return (d.ReadStatus(1) & 0x80) != 0; }

static void writeByte(SNDSP1_LLE &d, unsigned char b) {
    d.WriteData(0, b);   // A0=0 -> Data Register
}
static unsigned char readByte(SNDSP1_LLE &d) {
    return d.ReadData(0);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "uso: %s <firmware.rom> [byteHex ...] [-r N]\n", argv[0]);
        return 2;
    }

    unsigned char img[0x4000];
    long n = readFile(argv[1], img, sizeof(img));
    if (n < 0) return 1;
    printf("[fw] '%s' = %ld bytes (esperado 8192: %u prog + %u data)\n",
           argv[1], n, 2048u * 3u, 1024u * 2u);

    SNDSP1_LLE dsp;
    if (!dsp.LoadFirmware(img, (Uint32)n)) {
        fprintf(stderr, "ERRO: LoadFirmware falhou (imagem pequena demais?)\n");
        return 1;
    }

    // LoadFirmware ja chama Reset(), que roda o boot do microcodigo.
    printf("[boot] status apos reset = 0x%02X  (RQM/pronto: %s)\n",
           dsp.ReadStatus(1), ready(dsp) ? "SIM" : "NAO");

    // Coleta bytes de comando + flag -r N.
    unsigned char cmd[64]; int nCmd = 0;
    int nRead = 4;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc) {
            nRead = atoi(argv[++i]);
        } else if (nCmd < (int)sizeof(cmd)) {
            cmd[nCmd++] = (unsigned char)strtol(argv[i], 0, 16);
        }
    }

    if (nCmd > 0) {
        printf("[tx] escrevendo %d byte(s):", nCmd);
        for (int i = 0; i < nCmd; i++) printf(" %02X", cmd[i]);
        printf("\n");
        for (int i = 0; i < nCmd; i++) {
            writeByte(dsp, cmd[i]);
            printf("     apos write %02X -> status 0x%02X\n",
                   cmd[i], dsp.ReadStatus(1));
        }

        if (nRead < 0) nRead = 0;
        if (nRead > 64) nRead = 64;
        printf("[rx] lendo %d byte(s):", nRead);
        for (int i = 0; i < nRead; i++) printf(" %02X", readByte(dsp));
        printf("\n");
    } else {
        printf("[info] nenhum byte de comando dado; passe-os em hex.\n");
        printf("       ex.: %s %s 00 40 00 -r 4\n", argv[0], argv[1]);
    }

    return 0;
}
