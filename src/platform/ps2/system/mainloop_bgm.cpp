/* mainloop_bgm.cpp
 *
 * Trilha sonora de fundo do menu (.mod / .xm) via jar_mod / jar_xm.
 *
 * Arquitetura (igual ao resto do projeto): sem thread.  A cada frame de
 * menu, MainLoopRender chama BgmUpdate(), que gera PCM na EE com o
 * player de tracker e empurra para o audsrv via Aud_Enqueue().  Durante
 * o menu o core do SNES/NES nao roda, entao o BGM e' o unico produtor
 * de audio -- nao briga com o AudMixBuffer do jogo.
 *
 * Descoberta de arquivo: procura a 1a faixa .mod/.xm em BGM_PATH (define
 * do Makefile) e em pastas padrao.  Carrega uma faixa e toca em loop.
 *
 * Players de terceiros:
 *   jar_mod.h  - dominio publico (unlicense), Joshua Reisenauer/mackron
 *   jar_xm.h   - WTFPL, Joshua Reisenauer / Romain Dalmaso
 * As unidades de implementacao ficam em src/third_party/jar/jar_*.c.
 * Aqui incluimos APENAS jar_mod.h (header limpo, so declaracoes); para
 * o XM forward-declaramos as funcoes que usamos, porque a parte publica
 * do jar_xm.h define corpos de funcao (geraria simbolo duplicado se
 * incluido em mais de um .o).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "types.h"

extern "C" {
#include "audio.h"
}

/* jar_mod.h: header limpo, da' a struct de contexto e as declaracoes. */
#include "jar_mod.h"

/* jar_xm: tipo opaco + so as funcoes que usamos (ver comentario acima). */
extern "C" {
    struct jar_xm_context_s;
    typedef struct jar_xm_context_s jar_xm_context_t;
    int  jar_xm_create_context_safe(jar_xm_context_t **ctx, const char *moddata,
                                    size_t moddata_length, unsigned int rate);
    void jar_xm_free_context(jar_xm_context_t *ctx);
    void jar_xm_generate_samples(jar_xm_context_t *ctx, float *output,
                                 size_t numsamples);
    void jar_xm_generate_samples_16bit(jar_xm_context_t *ctx, short *output,
                                       size_t numsamples);
    void jar_xm_set_max_loop_count(jar_xm_context_t *ctx, unsigned char loopcnt);
}

#include "mainloop_bgm.h"


/* ---- configuracao ---------------------------------------------------- */

#define BGM_OUT_RATE    48000          /* taxa do audsrv (Aud_*)          */

/* Sintetiza a MEIA-taxa (24 kHz) e faz upsample linear 2x para 48 kHz.
   Isso corta ~metade do custo de CPU do tocador de tracker na EE (era
   o que derrubava o menu de 60 -> 30 fps).  Para chiptune de menu a
   perda de qualidade e' inaudivel. */
#define BGM_SYNTH_RATE  24000

/* Maximo de frames de SAIDA (48 kHz) gerados por chamada de BgmUpdate.
   ~800 frames = 1 frame de video a 60Hz; 1024 da' folga sem gastar CPU
   demais.  BGM_SYNTH_MAX e' a metade (frames sintetizados a 24 kHz). */
#define BGM_OUT_CHUNK   1024
#define BGM_SYNTH_MAX   (BGM_OUT_CHUNK / 2)

/* Pastas tentadas, em ordem.  BGM_PATH (se definido pelo Makefile) vem
   primeiro.  A primeira faixa .mod/.xm encontrada e' tocada. */
static const char *s_dirs[] = {
#ifdef BGM_PATH
    BGM_PATH,
#endif
    "mc0:/SNESticle/bgm",
    "mc1:/SNESticle/bgm",
    "mass:/SNESticle/bgm",
    "mass:/bgm",
    "cdfs:/BGM",
};
#define BGM_NUM_DIRS (sizeof(s_dirs) / sizeof(s_dirs[0]))


/* ---- estado ---------------------------------------------------------- */

enum BgmStateE {
    BGM_UNTRIED = 0,   /* ainda nao tentou carregar (lazy load)           */
    BGM_MOD,           /* tocando um .mod                                 */
    BGM_XM,            /* tocando um .xm                                  */
    BGM_FAILED         /* nenhuma faixa / falha -- nao tenta de novo      */
};

static int  s_state    = BGM_UNTRIED;
static Bool s_enabled  = TRUE;         /* Video Config liga/desliga       */
static Bool s_volSet   = FALSE;        /* ja' forcamos o volume cheio?    */

static jar_mod_context_t  s_mod;       /* contexto do tocador de MOD      */
static jar_xm_context_t  *s_xm  = NULL;/* contexto do tocador de XM       */
static char              *s_xmBuf = NULL; /* buffer do arquivo .xm (vivo) */

/* buffers de geracao (estaticos: evitam pressao de pilha na EE) */
static short s_synth[BGM_SYNTH_MAX * 2] __attribute__((aligned(64))); /* 24k L,R */
static float s_xmf  [BGM_SYNTH_MAX * 2] __attribute__((aligned(64))); /* scratch XM float */
static short s_left [BGM_OUT_CHUNK]     __attribute__((aligned(64))); /* 48k saida L */
static short s_right[BGM_OUT_CHUNK]     __attribute__((aligned(64))); /* 48k saida R */


/* ---- utilitarios ----------------------------------------------------- */

static Bool _HasExt(const char *name, const char *ext)
{
    size_t ln = strlen(name);
    size_t le = strlen(ext);
    size_t i;
    if (ln < le) return FALSE;
    for (i = 0; i < le; i++)
    {
        char a = name[ln - le + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32); /* lower */
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return FALSE;
    }
    return TRUE;
}

/* Acha a 1a faixa .mod/.xm nas pastas candidatas.  Preenche outPath e
   retorna 1 (MOD) / 2 (XM); 0 se nada achado. */
static int _FindTrack(char *outPath, size_t cap)
{
    size_t d;
    for (d = 0; d < BGM_NUM_DIRS; d++)
    {
        DIR *pDir;
        struct dirent *pEnt;

        if (!s_dirs[d] || !s_dirs[d][0]) continue;

        pDir = opendir(s_dirs[d]);
        if (!pDir) continue;

        while ((pEnt = readdir(pDir)) != NULL)
        {
            int kind = 0;
            if (_HasExt(pEnt->d_name, ".mod")) kind = 1;
            else if (_HasExt(pEnt->d_name, ".xm")) kind = 2;
            if (!kind) continue;

            snprintf(outPath, cap, "%s/%s", s_dirs[d], pEnt->d_name);
            closedir(pDir);
            return kind;
        }
        closedir(pDir);
    }
    return 0;
}

/* Le um arquivo inteiro para um buffer malloc'd.  Retorna NULL em erro. */
static char *_LoadFileAlloc(const char *path, long *outLen)
{
    FILE *f;
    long  len;
    char *buf;
    size_t rd;

    f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }

    buf = (char *)malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if ((long)rd != len) { free(buf); return NULL; }

    *outLen = len;
    return buf;
}


/* ---- carga (lazy) ---------------------------------------------------- */

static void _TryLoad(void)
{
    char path[320];
    int  kind;

    kind = _FindTrack(path, sizeof(path));
    if (kind == 0)
    {
        s_state = BGM_FAILED;
        return;
    }

    if (kind == 1) /* MOD */
    {
        jar_mod_init(&s_mod);                 /* defaults: 48000/16/stereo */
        jar_mod_setcfg(&s_mod, BGM_SYNTH_RATE, 16, 1, 1, 1); /* meia-taxa */
        if (jar_mod_load_file(&s_mod, path) != 0)
        {
            s_state = BGM_MOD;                /* jar_mod faz loop sozinho  */
            return;
        }
        jar_mod_unload(&s_mod);
        s_state = BGM_FAILED;
        return;
    }

    /* XM */
    {
        long len = 0;
        s_xmBuf = _LoadFileAlloc(path, &len);
        if (s_xmBuf &&
            jar_xm_create_context_safe(&s_xm, s_xmBuf, (size_t)len, BGM_SYNTH_RATE) == 0)
        {
            jar_xm_set_max_loop_count(s_xm, 0); /* 0 = loop infinito       */
            s_state = BGM_XM;
            return;
        }
        if (s_xmBuf) { free(s_xmBuf); s_xmBuf = NULL; }
        s_state = BGM_FAILED;
    }
}


/* ---- API ------------------------------------------------------------- */

void BgmStop(void)
{
    if (s_state == BGM_MOD)
    {
        jar_mod_unload(&s_mod);
    }
    else if (s_state == BGM_XM)
    {
        if (s_xm) { jar_xm_free_context(s_xm); s_xm = NULL; }
    }
    if (s_xmBuf) { free(s_xmBuf); s_xmBuf = NULL; }

    /* volta a UNTRIED para recarregar do disco na proxima vez que o
       menu reabrir (ex.: pausa durante o jogo). */
    s_state  = BGM_UNTRIED;
    s_volSet = FALSE;
}

void BgmSetEnabled(Bool bEnable)
{
    if (!bEnable && (s_state == BGM_MOD || s_state == BGM_XM))
    {
        BgmStop();
    }
    s_enabled = bEnable;
}

Bool BgmIsEnabled(void)
{
    return s_enabled;
}

void BgmUpdate(void)
{
    int avail, out, synthN, i;

    if (!s_enabled)            return;
    if (!Aud_IsInitialized())  return;

    if (s_state == BGM_UNTRIED) _TryLoad();
    if (s_state != BGM_MOD && s_state != BGM_XM) return; /* FAILED/nada */

    /* frames de SAIDA (48 kHz) que cabem no ring do audsrv agora */
    avail = Aud_Available();
    if (avail <= 0) return;
    out = avail;
    if (out > BGM_OUT_CHUNK) out = BGM_OUT_CHUNK;
    out &= ~1;                 /* par: upsample 2x exato */
    if (out < 2) return;

    synthN = out / 2;          /* sintetiza a meia-taxa (24 kHz) */

    /* gera PCM interleaved (L,R,...) a 24 kHz em s_synth */
    if (s_state == BGM_MOD)
    {
        jar_mod_fillbuffer(&s_mod, s_synth, (unsigned long)synthN, NULL);
    }
    else
    {
        /* float -> int16 na mao: evita o malloc/free por chamada que
           jar_xm_generate_samples_16bit faz internamente. */
        int k;
        jar_xm_generate_samples(s_xm, s_xmf, (size_t)synthN);
        for (k = 0; k < synthN * 2; k++)
        {
            float f = s_xmf[k] * 32767.0f;
            if (f >  32767.0f) f =  32767.0f;
            if (f < -32768.0f) f = -32768.0f;
            s_synth[k] = (short)f;
        }
    }

    /* upsample linear 2x (24k -> 48k) + desinterleave para L/R */
    for (i = 0; i < synthN; i++)
    {
        int l0 = s_synth[i * 2 + 0];
        int r0 = s_synth[i * 2 + 1];
        int l1 = (i + 1 < synthN) ? s_synth[i * 2 + 2] : l0;
        int r1 = (i + 1 < synthN) ? s_synth[i * 2 + 3] : r0;
        s_left [i * 2 + 0] = (short)l0;
        s_left [i * 2 + 1] = (short)((l0 + l1) / 2);
        s_right[i * 2 + 0] = (short)r0;
        s_right[i * 2 + 1] = (short)((r0 + r1) / 2);
    }

    /* garante volume audivel: o menu de pausa muta o audsrv (Aud_Setvol(0))
       para matar o rabo de audio do jogo; forcamos o volume cheio uma vez
       quando a trilha comeca a tocar. */
    if (!s_volSet)
    {
        Aud_Setvol(0x3FFF);
        s_volSet = TRUE;
    }

    Aud_Enqueue(s_left, s_right, out, 0); /* out = 2*synthN frames, wait=0 */
}
