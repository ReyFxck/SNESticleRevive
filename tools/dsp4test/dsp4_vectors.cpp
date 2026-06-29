// dsp4_vectors.cpp - runner de vetores clean-room para o DSP-4 HLE.
//
// Motor de TDD para reconstruir a matematica do DSP-4 SEM ler/portar
// codigo de outro emulador.  A ideia (clean-room observacional):
//
//   1. Capture os INPUTS reais: rode o Top Gear 3000 aqui no SNESticle
//      com a HLE compilada com -DDSP4_TRACE.  Cada palavra escrita pela
//      CPU vira uma linha "W xxxx" no log.
//   2. Capture os OUTPUTS corretos observando hardware real (test ROM no
//      flashcart) ou um emulador de referencia tratado como CAIXA-PRETA
//      (instrumentando so' a fronteira do barramento -- nunca lendo o
//      codigo da matematica dele).  Cada palavra lida vira "R xxxx".
//   3. Junte input+output num arquivo .vec e rode aqui.  Implemente a
//      matematica em sndsp4.cpp ate' TODOS os vetores passarem.
//
// Como nada aqui deriva de codigo licenciado de terceiros (so' de I/O
// observado, que e' fato nao-protegivel), o resultado fica limpo para a
// licenca MIT do projeto.
//
// ------------------------------------------------------------------------
// Formato do arquivo .vec (texto):
//   W xxxx        escreve a palavra de 16 bits xxxx (hex) no Data Reg
//   R xxxx        le' uma palavra e exige que seja igual a xxxx (hex)
//   S xx          le' o Status e exige que seja igual a xx (hex)
//   RESET         reseta o chip (recomeca uma nova transacao)
//   NAME texto    rotulo do bloco corrente (so' para o relatorio)
//   # ...         comentario (linha inteira ignorada)
//   (linha vazia) ignorada
//
// Uso:  ./dsp4_vectors arquivo1.vec [arquivo2.vec ...]
//       (sem args, roda todos os *.vec em ./vectors/)

#include "sndsp4.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <dirent.h>
#include <string>
#include <vector>

// Stub de DLog: no PS2 ele escreve no SIO; no bench host so' descartamos
// (ou ecoa em modo verboso). A captura DSP4_CAPTURE do sndsp4.cpp referencia
// DLog sempre, entao precisamos satisfazer o linker aqui tambem.
extern "C" void DLog(const char * /*fmt*/, ...) { }

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

// roda um arquivo .vec; devolve numero de falhas e soma os contadores.
static int runFile(const char *path, int &outChecks, int &outPass)
{
    FILE *f = fopen(path, "r");
    if (!f) { printf("ERRO: nao abriu '%s'\n", path); return 1; }

    printf("\n=== %s ===\n", path);

    SNDSP4 dsp;
    char   line[256];
    int    lineno = 0;
    int    fail   = 0;
    std::string name = "(sem nome)";

    while (fgets(line, sizeof(line), f))
    {
        lineno++;
        // trim inicial
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        char op[16] = {0};
        unsigned val = 0;

        if (sscanf(p, "%15s", op) != 1) continue;

        if (!strcasecmp(op, "RESET"))
        {
            dsp.Reset();
            continue;
        }
        if (!strcasecmp(op, "NAME"))
        {
            char *rest = p + strlen(op);
            while (*rest && isspace((unsigned char)*rest)) rest++;
            // remove newline final
            size_t n = strlen(rest);
            while (n && (rest[n-1] == '\n' || rest[n-1] == '\r')) rest[--n] = 0;
            name = rest;
            continue;
        }

        if (sscanf(p, "%15s %x", op, &val) != 2)
        {
            printf("  L%-4d  linha invalida: %s", lineno, line);
            fail++;
            continue;
        }

        if (!strcasecmp(op, "W"))
        {
            sendWord(dsp, (uint16_t)val);
            outChecks++; outPass++;   // escrita nao "falha", mas conta
        }
        else if (!strcasecmp(op, "R"))
        {
            uint16_t got = readWord(dsp);
            outChecks++;
            if (got != (uint16_t)val)
            {
                printf("  L%-4d  FAIL [%s] R esperado=%04X obtido=%04X\n",
                       lineno, name.c_str(), val & 0xFFFF, got);
                fail++;
            }
            else outPass++;
        }
        else if (!strcasecmp(op, "S"))
        {
            uint8_t got = dsp.ReadStatus(0);
            outChecks++;
            if (got != (uint8_t)val)
            {
                printf("  L%-4d  FAIL [%s] S esperado=%02X obtido=%02X\n",
                       lineno, name.c_str(), val & 0xFF, got);
                fail++;
            }
            else outPass++;
        }
        else
        {
            printf("  L%-4d  op desconhecido: %s\n", lineno, op);
            fail++;
        }
    }
    fclose(f);

    printf("  -> %s\n", fail ? "FALHOU" : "ok");
    return fail;
}

int main(int argc, char **argv)
{
    std::vector<std::string> files;

    if (argc > 1)
    {
        for (int i = 1; i < argc; i++) files.push_back(argv[i]);
    }
    else
    {
        // sem args: roda todos os ./vectors/*.vec
        DIR *d = opendir("vectors");
        if (d)
        {
            struct dirent *e;
            while ((e = readdir(d)))
            {
                std::string n = e->d_name;
                if (n.size() > 4 && n.substr(n.size() - 4) == ".vec")
                    files.push_back("vectors/" + n);
            }
            closedir(d);
        }
    }

    if (files.empty())
    {
        printf("nenhum vetor. uso: ./dsp4_vectors arq.vec ...  "
               "(ou popule ./vectors/*.vec)\n");
        return 0;
    }

    int totalFail = 0, checks = 0, pass = 0;
    for (auto &fn : files) totalFail += runFile(fn.c_str(), checks, pass);

    printf("\n================ RESUMO ================\n");
    printf("arquivos: %zu   checagens: %d   passou: %d   falhou: %d\n",
           files.size(), checks, pass, totalFail);
    printf("%s\n", totalFail ? "FALHOU" : "PASSOU");
    return totalFail ? 1 : 0;
}
