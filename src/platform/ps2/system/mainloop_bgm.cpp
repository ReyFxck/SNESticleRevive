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
#include <time.h>
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

/* Taxa de SINTESE do tracker.  A saida do audsrv e' fixa em 48 kHz; o PCM
   gerado a BGM_RATE e' reamostrado (linear) para 48 kHz em BgmUpdate.  O
   custo de CPU da sintese e' ~proporcional ao numero de AMOSTRAS/frame
   (logo, a' BGM_RATE).  Tipicos: 24000 (leve, garante 60fps), 32000
   (meio-termo, padrao), 48000 (nativo, mais pesado).  Sobrescrevivel pelo
   Makefile:  make BGM_RATE=24000 */
#ifndef BGM_RATE
#define BGM_RATE        32000
#endif

/* Teto de frames de SAIDA (48 kHz) por chamada.  Em regime normal so
   geramos ~800; o teto limita o PICO de sintese (nos frames de recarga do
   ring logo apos um bloqueio de disco/decode) a ~(BGM_OUT_CHUNK*BGM_RATE/
   48000) amostras -- com 3072 e BGM_RATE<=48000 fica <=2048@32k, o mesmo
   pico que segurava 60fps, recarregando o ring (~107ms) em ~3 frames. */
#define BGM_OUT_CHUNK   3072

/* Limiar (em frames stereo) abaixo do qual consideramos que a cauda de
   audio do jogo ja' drenou do ring do audsrv.  Ao abrir o menu, o
   _MenuEnable muta o audsrv; esperamos o ring cair abaixo disso antes de
   soltar o tracker, para nao ouvir SNES/NES junto com a trilha. ~5ms. */
#define BGM_DRAIN_THRESH 256

/* Maximo de frames esperando a cauda do jogo drenar antes de soltar o
   tracker.  No boot o audsrv_queued() reporta uma ocupacao inicial
   "fantasma" que nunca drena; sem este timeout a musica so' comecava
   depois de entrar num jogo e voltar.  ~12 frames (~200ms) cobrem a cauda
   real (~107ms) e destravam o caso do boot. */
#define BGM_DRAIN_MAXFRAMES 12

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
static int  s_volume   = 100;          /* 0 = off; 1..100 (Video Config)  */
static int  s_rate     = BGM_RATE;     /* taxa de sintese (Hz), Video Config */
static Bool s_volSet   = FALSE;        /* ja' firmamos o volume p/ tocar? */
static int  s_drainWait = 0;           /* frames esperando dreno da cauda */

/* Frequencias de sintese oferecidas no Video Config (Hz).  Mais alta =
   melhor qualidade e mais CPU (48000 pode derrubar o fps).  32000 e' o
   padrao recomendado.  A saida e' sempre reamostrada para 48 kHz. */
static const int s_rateList[] = { 16000, 22050, 24000, 32000, 38000, 44100, 48000 };
#define BGM_RATE_COUNT ((int)(sizeof(s_rateList) / sizeof(s_rateList[0])))

static jar_mod_context_t  s_mod;       /* contexto do tocador de MOD      */
static jar_xm_context_t  *s_xm  = NULL;/* contexto do tocador de XM       */
static char              *s_xmBuf = NULL; /* buffer do arquivo .xm (vivo) */

/* Indice (cache) de TODAS as faixas .mod/.xm achadas -- escaneado UMA vez
   (sem reler o disco toda hora).  s_trackIdx aponta a faixa atual; e'
   sorteada no boot para dar variedade sem custo de reload (trocar de
   faixa releria do disco, lento no memory card -> traria a travadinha). */
#define BGM_INDEX_MAX 64
typedef struct { char path[256]; int kind; } BgmTrackT; /* kind 1=mod 2=xm */
static BgmTrackT s_index[BGM_INDEX_MAX];
static int       s_indexCount = -1;    /* -1 = ainda nao escaneado */
static int       s_trackIdx   = 0;     /* faixa atual no indice    */

/* buffers de geracao (estaticos: evitam pressao de pilha na EE) */
static short s_inter[BGM_OUT_CHUNK * 2] __attribute__((aligned(64))); /* 48k L,R,L,R */
static float s_xmf  [BGM_OUT_CHUNK * 2] __attribute__((aligned(64))); /* scratch XM float */
static short s_left [BGM_OUT_CHUNK]     __attribute__((aligned(64)));
static short s_right[BGM_OUT_CHUNK]     __attribute__((aligned(64)));


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

/* Escaneia as pastas candidatas UMA vez e indexa todas as faixas
   .mod/.xm achadas (so' os nomes/caminhos -- barato).  Sorteia uma faixa
   inicial (variedade por boot, sem custo de reload). */
static void _BuildIndex(void)
{
    size_t d;

    s_indexCount = 0;
    for (d = 0; d < BGM_NUM_DIRS && s_indexCount < BGM_INDEX_MAX; d++)
    {
        DIR *pDir;
        struct dirent *pEnt;

        if (!s_dirs[d] || !s_dirs[d][0]) continue;

        pDir = opendir(s_dirs[d]);
        if (!pDir) continue;

        while ((pEnt = readdir(pDir)) != NULL && s_indexCount < BGM_INDEX_MAX)
        {
            int kind = 0;
            if (_HasExt(pEnt->d_name, ".mod")) kind = 1;
            else if (_HasExt(pEnt->d_name, ".xm")) kind = 2;
            if (!kind) continue;

            snprintf(s_index[s_indexCount].path, sizeof(s_index[0].path),
                     "%s/%s", s_dirs[d], pEnt->d_name);
            s_index[s_indexCount].kind = kind;
            s_indexCount++;
        }
        closedir(pDir);
    }

    /* faixa inicial pseudo-aleatoria (clock varia conforme o tempo de
       boot); se nao houver entropia, cai no indice 0 -- sem problema. */
    if (s_indexCount > 0)
    {
        unsigned int seed = (unsigned int)clock();
        s_trackIdx = (int)(seed % (unsigned int)s_indexCount);
    }
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
    const char *path;
    int  kind;

    if (s_indexCount < 0) _BuildIndex();
    if (s_indexCount == 0) { s_state = BGM_FAILED; return; }
    if (s_trackIdx < 0 || s_trackIdx >= s_indexCount) s_trackIdx = 0;

    path = s_index[s_trackIdx].path;
    kind = s_index[s_trackIdx].kind;

    if (kind == 1) /* MOD */
    {
        jar_mod_init(&s_mod);                 /* defaults: 48000/16/stereo */
        jar_mod_setcfg(&s_mod, s_rate, 16, 1, 1, 1); /* taxa de sintese */
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
            jar_xm_create_context_safe(&s_xm, s_xmBuf, (size_t)len, s_rate) == 0)
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

/* Libera o decoder e o buffer do arquivo.  Chamado SO' quando a trilha e'
   desligada (BgmSetVolume(0)) -- nao no fluxo de abrir jogo, para o menu
   reabrir sem reler do disco. */
static void _BgmFree(void)
{
    if (s_state == BGM_MOD) jar_mod_unload(&s_mod);
    if (s_state == BGM_XM && s_xm) { jar_xm_free_context(s_xm); s_xm = NULL; }
    if (s_xmBuf) { free(s_xmBuf); s_xmBuf = NULL; }
    s_state  = BGM_UNTRIED;
    s_volSet = FALSE;
    s_drainWait = 0;
}

void BgmStop(void)
{
    /* Para de alimentar SEM liberar o decoder: a faixa fica carregada,
       entao reabrir o menu e' instantaneo (sem reler do disco -> sem a
       travadinha).  So' re-arma a logica de volume/dreno para a proxima
       entrada no menu (esperar a cauda de audio do jogo drenar antes de
       soltar o tracker).  A liberacao real acontece em BgmSetVolume(0). */
    s_volSet = FALSE;
    s_drainWait = 0;
}

void BgmNext(void)
{
    /* Avanca para a proxima faixa do indice e libera o decoder atual, de
       modo que o proximo BgmUpdate carregue a nova faixa.  Chamado ao
       ABRIR o menu (sair do jogo) para dar variedade.  Releria do disco
       (pode dar um hitch breve no memory card), por isso so' troca quando
       ha 2+ faixas; com 0/1 faixa nao faz nada (sem reload, sem hitch). */
    if (s_indexCount < 0) _BuildIndex();
    if (s_indexCount <= 1) return;

    s_trackIdx = (s_trackIdx + 1) % s_indexCount;

    if (s_state == BGM_MOD || s_state == BGM_XM) _BgmFree();
}

void BgmSetVolume(int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;

    if (vol == 0)
    {
        /* OFF: libera o decoder/buffer (nao consome RAM) e silencia o que
           ainda estiver no ring. */
        _BgmFree();
        if (Aud_IsInitialized()) Aud_Setvol(0);
    }
    else if (s_volSet && Aud_IsInitialized())
    {
        /* ja' tocando: ajusta o volume ao vivo */
        Aud_Setvol((unsigned int)(vol * 0x3FFF / 100));
    }

    s_volume = vol;
}

int BgmGetVolume(void)
{
    return s_volume;
}

int BgmTrackCount(void)
{
    /* escaneia o indice na 1a chamada (cacheado depois) para o menu poder
       mostrar "No Track" quando nao ha arquivos. */
    if (s_indexCount < 0) _BuildIndex();
    return s_indexCount;
}

int BgmGetRate(void)
{
    return s_rate;
}

void BgmSetRate(int hz)
{
    if (hz < 8000)  hz = 8000;
    if (hz > 48000) hz = 48000;
    if (hz == s_rate) return;
    s_rate = hz;
    /* recarrega o decoder na nova taxa: _BgmFree zera o estado e o proximo
       BgmUpdate recarrega em s_rate. */
    if (s_state == BGM_MOD || s_state == BGM_XM) _BgmFree();
}

void BgmCycleRate(int dir)
{
    int i, idx = 3; /* fallback ~32000 */
    for (i = 0; i < BGM_RATE_COUNT; i++)
        if (s_rateList[i] == s_rate) { idx = i; break; }
    idx += (dir < 0) ? -1 : 1;
    if (idx < 0)               idx = BGM_RATE_COUNT - 1;
    if (idx >= BGM_RATE_COUNT)  idx = 0;
    BgmSetRate(s_rateList[idx]);
}

void BgmUpdate(void)
{
    int avail, n, synthN, j;

    if (s_volume <= 0)         return;   /* OFF: nem carrega, nem usa RAM */
    if (!Aud_IsInitialized())  return;

    if (s_state == BGM_UNTRIED) _TryLoad();
    if (s_state != BGM_MOD && s_state != BGM_XM) return; /* FAILED/nada */

    /* Espera a cauda de audio do jogo (mutada pelo _MenuEnable ao abrir o
       menu) drenar do ring ANTES de soltar o tracker: evita ouvir SNES/NES
       junto com a trilha, e da' um inicio limpo (nao instantaneo).  So'
       enquanto ainda nao firmamos o volume desta sessao de menu. */
    if (!s_volSet)
    {
        /* Espera a cauda do jogo drenar, mas com TIMEOUT: no boot o
           audsrv_queued() reporta uma ocupacao inicial "fantasma" que
           nunca drena -- sem o timeout a musica so' comecava depois de
           entrar num jogo e voltar.  O timeout cobre a cauda real (~107ms)
           e destrava o caso do boot. */
        if (Aud_Buffered() > BGM_DRAIN_THRESH && s_drainWait < BGM_DRAIN_MAXFRAMES)
        {
            s_drainWait++;
            return;
        }
        s_drainWait = 0;
        Aud_Setvol((unsigned int)(s_volume * 0x3FFF / 100)); /* volume do menu */
        s_volSet = TRUE;
    }

    /* frames de SAIDA (48 kHz) que cabem no ring do audsrv agora */
    avail = Aud_Available();
    if (avail <= 0) return;
    n = avail;
    if (n > BGM_OUT_CHUNK - 2) n = BGM_OUT_CHUNK - 2;
    if (n < 1) return;

    /* frames a sintetizar na taxa s_rate p/ render n frames @48k (+guarda) */
    synthN = (int)(((unsigned int)n * (unsigned int)s_rate) / 48000u) + 2;
    if (synthN > BGM_OUT_CHUNK) synthN = BGM_OUT_CHUNK;
    if (synthN < 2) synthN = 2;

    /* gera synthN frames interleaved (L,R,...) a s_rate em s_inter */
    if (s_state == BGM_MOD)
    {
        jar_mod_fillbuffer(&s_mod, s_inter, (unsigned long)synthN, NULL);
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
            s_inter[k] = (short)f;
        }
    }

    /* reamostra s_rate -> 48 kHz (linear, ponto fixo 16.16) e
       desinterleava para L/R (Aud_Enqueue reinterleava). */
    {
        unsigned int step = (unsigned int)(((unsigned int)s_rate << 16) / 48000u);
        unsigned int pos  = 0;
        for (j = 0; j < n; j++)
        {
            unsigned int i  = pos >> 16;
            unsigned int fr = pos & 0xFFFF;
            unsigned int i1 = i + 1;
            int l0, l1, r0, r1;

            if (i1 >= (unsigned int)synthN) i1 = (unsigned int)(synthN - 1);

            l0 = s_inter[i * 2 + 0]; l1 = s_inter[i1 * 2 + 0];
            r0 = s_inter[i * 2 + 1]; r1 = s_inter[i1 * 2 + 1];
            s_left [j] = (short)(l0 + (int)(((long long)(l1 - l0) * fr) >> 16));
            s_right[j] = (short)(r0 + (int)(((long long)(r1 - r0) * fr) >> 16));
            pos += step;
        }
    }

    /* garante volume audivel: o menu de pausa muta o audsrv (Aud_Setvol(0))
       para matar o rabo de audio do jogo; o volume cheio ja' foi firmado
       acima, apos a cauda do jogo drenar. */

    Aud_Enqueue(s_left, s_right, n, 0); /* wait=0: best-effort, nao trava */
}
