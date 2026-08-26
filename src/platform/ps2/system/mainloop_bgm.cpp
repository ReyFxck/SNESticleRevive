/* mainloop_bgm.cpp
 *
 * Trilha sonora de fundo do menu (.mod / .xm) via libxmp-lite.
 *
 * Arquitetura normal: a cada frame de menu, MainLoopRender chama
 * BgmUpdate(), que gera PCM na EE com o player de tracker e empurra para o
 * audsrv via Aud_Enqueue(). Durante uma operacao SINCRONA de filesystem, um
 * helper EE temporariamente mantem apenas esse decoder alimentado; fora desses
 * escopos a thread dorme. Durante o menu o core SNES/NES nao roda, entao o BGM
 * e' o unico produtor de audio -- nao briga com o AudMixBuffer do jogo.
 *
 * Descoberta de arquivo: procura todas as faixas .mod/.xm em BGM_PATH
 * (define do Makefile) e em pastas padrao, indexa-as e toca como uma
 * playlist: ao terminar uma faixa avanca para a proxima. Ao voltar de uma ROM
 * o decoder carregado e' retomado sem reler o dispositivo.
 *
 * O player anterior (jar_mod/jar_xm) implementava apenas parte dos efeitos
 * de tracker e tinha erros em sample loops/pattern loops. libxmp-lite 4.7.2
 * preserva as regras ProTracker/FastTracker e corrige regressões antigas de
 * mute/instrument swap, para que andamento, instrumentos, E6 loops e saltos
 * de pattern sejam reproduzidos como no tracker original.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <kernel.h>
#include <delaythread.h>
#include <libcdvd.h>
#define NEWLIB_PORT_AWARE
#include <fileXio.h>
#include <fileXio_rpc.h>
#include <libhdd.h>
#undef NEWLIB_PORT_AWARE

#include "types.h"

extern "C" {
#include "audio.h"
}

extern "C" {
#include "xmp.h"
}

#include "mainloop_bgm.h"
#include "embedded_irx.h"


/* ---- configuracao ---------------------------------------------------- */

/* Taxa de SINTESE do tracker.  A saida do audsrv e' fixa em 48 kHz; o PCM
   gerado a BGM_RATE e' reamostrado (linear) para 48 kHz em BgmUpdate.  O
   custo de CPU da sintese e' ~proporcional ao numero de AMOSTRAS/frame
   (logo, a' BGM_RATE).  Tipicos: 24000 (leve, garante 60fps), 32000
   (meio-termo, padrao), 48000 (nativo, mais pesado).  Sobrescrevivel pelo
   Makefile:  make BGM_RATE=24000 */
#ifndef BGM_RATE
#define BGM_RATE        24000
#endif

/* Teto de frames de SAIDA (48 kHz) por chamada.  Em regime normal so
   geramos ~800; o teto limita o PICO de sintese (nos frames de recarga do
   ring logo apos um bloqueio de disco/decode) a ~(BGM_OUT_CHUNK*BGM_RATE/
   48000) amostras -- com 3072 e BGM_RATE<=48000 fica <=2048@32k, o mesmo
   pico que segurava 60fps, recarregando o ring (~107ms) em ~3 frames. */
#define BGM_OUT_CHUNK   3072

/* Teto de frames de SAIDA (48 kHz) sintetizados POR CHAMADA de BgmUpdate.
   Em regime normal so' geramos ~800 (um frame @60fps), mas quando o ring
   drena (ex.: bloqueio de disco ao trocar de faixa) avail pode chegar a
   milhares -- sintetizar tudo de uma vez num unico frame faz um PICO de
   CPU que estoura os 16ms (pior com XM de 32 canais) e causa o engasgo.
   Limitar aqui espalha a recarga por varios frames: > 800 para nao ficar
   pra tras do consumo, mas baixo o bastante pra nao dar pico.  ~1200
   permite ~400 frames/recarga sem estourar o orcamento de CPU. */
#define BGM_MAX_OUT_PER_FRAME 1200

/* Duracao (em frames de menu @60fps) do "respiro" de silencio inserido
   ENTRE faixas, ao trocar.  Cobre o bloqueio de disco do carregamento da
   proxima faixa (o ring toca silencio limpo durante a leitura, em vez de
   underrun/estralo) e da' uma pausa agradavel tipo playlist.  ~12 = 200ms. */
#define BGM_GAP_FRAMES 12

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

/* A sondagem abaixo continua feita aos poucos antes do primeiro opendir no
   disco. O cdfs_stream atual tambem possui timeout interno, mas evitar a
   abertura enquanto BIOS/mechacon ainda detectam a midia deixa o boot mais
   rapido e poupa tentativas desnecessarias.

   - grace: deixa BIOS/mechacon/OPL assentarem depois do SifIopReset;
   - poll: evita um RPC de CDVD em todo frame;
   - stable: exige duas respostas prontas consecutivas;
   - giveup: em ELF sem disco, para de mostrar "Searching" apos ~15 s. */
#define BGM_DISC_GRACE_FRAMES    90
#define BGM_DISC_POLL_FRAMES     15
#define BGM_DISC_STABLE_POLLS     2
#define BGM_DISC_GIVEUP_FRAMES  900

/* O Makefile preserva subpastas da origem ao montar BGM/ na ISO. Escanear
   alguns niveis garante que essas faixas tambem sejam encontradas, sem
   permitir recursao ilimitada em dispositivos arbitrarios. */
#define BGM_SCAN_MAX_DEPTH 4

/* Limite de particoes APA/PFS lembradas durante a descoberta do HDD. A
   playlist continua limitada por BGM_INDEX_MAX; esta tabela existe apenas
   para fechar o descritor hdd0: antes de comecar a montar pfs0:. */
#define BGM_HDD_PART_MAX 32

/* Pastas tentadas, em ordem.  BGM_PATH (se definido pelo Makefile) vem
   primeiro.  A primeira faixa .mod/.xm encontrada e' tocada. */
static const char *s_dirs[] = {
#ifdef BGM_PATH
    BGM_PATH,
#endif
    "mc0:/SNESticle/bgm",
    "mc1:/SNESticle/bgm",
    "mmce0:/SNESticle/bgm",
    "mmce0:/bgm",
    "mmce1:/SNESticle/bgm",
    "mmce1:/bgm",
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
static int  s_gapFrames = 0;           /* frames de silencio na troca de faixa */

/* Frequencias de sintese oferecidas no Video Config (Hz).  Mais alta =
   melhor qualidade e mais CPU (48000 pode derrubar o fps).  24000 e' o
   padrao seguro. A saida e' sempre reamostrada para 48 kHz. */
static const int s_rateList[] = { 16000, 22050, 24000, 32000, 38000, 44100, 48000 };
#define BGM_RATE_COUNT ((int)(sizeof(s_rateList) / sizeof(s_rateList[0])))

static xmp_context s_xmp = NULL;       /* um player correto para MOD e XM */

/* Indice (cache) de TODAS as faixas .mod/.xm achadas -- escaneado UMA vez
   (sem reler o disco toda hora).  s_trackIdx aponta a faixa atual; e'
   sorteada no boot para dar variedade sem custo de reload (trocar de
   faixa releria do disco, lento no memory card -> traria a travadinha). */
#define BGM_INDEX_MAX 64
typedef struct { char path[256]; int kind; } BgmTrackT; /* kind 1=mod 2=xm */
static BgmTrackT s_index[BGM_INDEX_MAX];
static int       s_indexCount = -1;    /* -1 = ainda nao escaneado */
static int       s_trackIdx   = 0;     /* faixa atual no indice    */

enum BgmDiscScanE {
    BGM_DISC_PENDING = 0,
    BGM_DISC_DONE
};
static int          s_discScanState   = BGM_DISC_PENDING;
static unsigned int s_discScanFrames  = 0;
static int          s_discStablePolls = 0;
static Bool         s_discEeReady     = FALSE;
static char         s_bootBgm[256];
static Bool         s_massWasReady    = FALSE;
static Bool         s_massScanDone    = FALSE;
static Bool         s_mmceWasEnabled  = FALSE;
static Bool         s_mmceScanDone    = FALSE;
static Bool         s_hddWasEnabled   = FALSE;
static Bool         s_hddScanDone     = FALSE;

/* Synchronous filesystem calls run on the UI thread, but the already-loaded
   tracker can be mixed safely by a small EE helper thread while that thread
   waits for mc/cdfs/mass/mmce/SMB. A binary semaphore gives the helper sole
   ownership of the libxmp context; it never scans or opens files itself. */
#define BGM_IO_THREAD_STACK_BYTES (32 * 1024)
#define BGM_IO_THREAD_PRIORITY    48
#define BGM_IO_TICK_USEC          6000
static int          s_ioLock       = -1;
static int          s_ioThread     = -1;
static volatile int s_ioDepth      = 0;
static Bool         s_menuActive   = FALSE;
static unsigned char s_ioStack[BGM_IO_THREAD_STACK_BYTES]
    __attribute__((aligned(64)));

/* Buffer-fonte na taxa do tracker e estado do reamostrador CONTINUO.
   O codigo antigo reiniciava a fase em zero e descartava 1-2 amostras a
   cada frame de video; isso introduzia jitter de andamento/pitch e pequenas
   descontinuidades. Agora a fracao e as amostras restantes sobrevivem entre
   chamadas de BgmUpdate(). */
static short        s_inter[BGM_OUT_CHUNK * 2] __attribute__((aligned(64)));
static int          s_sourceFrames = 0;
static unsigned int s_resampleFrac = 0; /* fracao exata / 48000 */
static short s_left [BGM_OUT_CHUNK]     __attribute__((aligned(64)));
static short s_right[BGM_OUT_CHUNK]     __attribute__((aligned(64)));


static void _BgmLock(void)
{
    if (s_ioLock > 0)
        WaitSema(s_ioLock);
}

static void _BgmUnlock(void)
{
    if (s_ioLock > 0)
        SignalSema(s_ioLock);
}


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
/* MainGetBootDir(): pasta de onde o ELF foi carregado (definida em
   main.cpp com linkage C++).  Declarada SEM extern "C" para casar com a
   definicao (igual mainloop_init.cpp); usada para achar a pasta "bgm" AO
   LADO do ELF. */
char *MainGetBootDir();

/* Um caminho aponta pro drive de CD/DVD? */
static Bool _IsDiscPath(const char *p)
{
    if (!p) return FALSE;
    return (strncmp(p, "cdfs",  4) == 0 ||
            strncmp(p, "cdrom", 5) == 0) ? TRUE : FALSE;
}

static Bool _IsMassPath(const char *p)
{
    return (p && strncmp(p, "mass", 4) == 0) ? TRUE : FALSE;
}

/* Retorna o slot fisico de um caminho MMCE, ou -1 para outro device.
   Separar esses caminhos impede opendir("mmceN:") antes de mmceman estar
   residente e antes de o cartao real ter respondido ao PING. */
static int _MmcePathSlot(const char *p)
{
    if (!p ||
        p[0] != 'm' || p[1] != 'm' || p[2] != 'c' || p[3] != 'e')
        return -1;
    if ((p[4] != '0' && p[4] != '1') || p[5] != ':')
        return -1;
    return p[4] - '0';
}

static Bool _IsHddPath(const char *p)
{
    return (p && strncmp(p, "hdd0:", 5) == 0) ? TRUE : FALSE;
}

/* Tipos que o cdfs.irx do PS2SDK reconhece como discos com filesystem. */
static Bool _DiscTypeHasFilesystem(int type)
{
    switch (type)
    {
        case SCECdPSCD:
        case SCECdPSCDDA:
        case SCECdPS2CD:
        case SCECdPS2CDDA:
        case SCECdPS2DVD:
        case SCECdDVDV:
            return TRUE;
        default:
            return FALSE;
    }
}

static void _BuildBootBgm(void)
{
    const char *bd = MainGetBootDir();
    int n = 0;

    s_bootBgm[0] = 0;
    if (!bd || !bd[0]) return;

    while (bd[n] && n < (int)sizeof(s_bootBgm) - 6)
    {
        s_bootBgm[n] = (bd[n] == '\\') ? '/' : bd[n];
        n++;
    }
    s_bootBgm[n] = 0;

    /* argv[0] de um ELF na ISO normalmente vem como cdrom0:/...; toda a
       pilha moderna deste projeto usa cdfs:. Converta antes de guardar o
       caminho ao lado do ELF. */
    if (strncmp(s_bootBgm, "cdrom", 5) == 0)
    {
        char converted[sizeof(s_bootBgm)];
        const char *colon = strchr(s_bootBgm, ':');
        const char *tail = colon ? colon + 1 : "";
        snprintf(converted, sizeof(converted), "cdfs:%s", tail);
        strncpy(s_bootBgm, converted, sizeof(s_bootBgm) - 1);
        s_bootBgm[sizeof(s_bootBgm) - 1] = 0;
        n = (int)strlen(s_bootBgm);
    }

    if (n > 0 && s_bootBgm[n - 1] != '/') s_bootBgm[n++] = '/';
    s_bootBgm[n] = 0;
    strncat(s_bootBgm, "bgm",
            sizeof(s_bootBgm) - strlen(s_bootBgm) - 1);
}

static Bool _IndexHasPath(const char *path)
{
    int i;

    for (i = 0; i < s_indexCount; i++)
    {
        const char *a = s_index[i].path;
        const char *b = path;

        if (_IsDiscPath(a) && _IsDiscPath(b))
        {
            while (*a && *b)
            {
                char ca = (*a == '\\') ? '/' : *a;
                char cb = (*b == '\\') ? '/' : *b;
                if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
                if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
                if (ca != cb) break;
                a++;
                b++;
            }
            if (!*a && !*b) return TRUE;
        }
        else if (strcmp(a, b) == 0)
            return TRUE;
    }
    return FALSE;
}

static void _IndexAdd(const char *path, int kind)
{
    size_t len;

    if (!path || !path[0] || !kind ||
        s_indexCount >= BGM_INDEX_MAX || _IndexHasPath(path))
        return;

    len = strlen(path);
    if (len >= sizeof(s_index[s_indexCount].path))
        return;
    memcpy(s_index[s_indexCount].path, path, len + 1);
    s_index[s_indexCount].kind = kind;
    s_indexCount++;
}

static void _IndexRemove(int index)
{
    int i;

    if (index < 0 || index >= s_indexCount) return;
    for (i = index; i + 1 < s_indexCount; i++)
        s_index[i] = s_index[i + 1];
    s_indexCount--;

    if (s_indexCount <= 0)
        s_trackIdx = 0;
    else if (s_trackIdx >= s_indexCount)
        s_trackIdx = 0;
}

/* Retorna 1 se a pasta abriu. scanDir e' o caminho fisico usado pelo I/O;
   indexDir e' o caminho persistido na playlist. Eles normalmente sao iguais,
   mas no HDD scanDir usa pfs0: enquanto indexDir conserva
   hdd0:/PARTICAO/... para que a particao correta possa ser remontada quando
   a faixa for carregada. A recursao e' limitada e so faz stat() quando
   d_type nao informa se a entrada e' diretorio (caso do cdfs). */
static int _ScanDirAs(const char *scanDir, const char *indexDir, int depth)
{
    DIR *pDir;
    struct dirent *pEnt;

    pDir = opendir(scanDir);
    if (!pDir) return 0;

    while ((pEnt = readdir(pDir)) != NULL &&
           s_indexCount < BGM_INDEX_MAX)
    {
        char child[256];
        char indexChild[256];
        const char *scanSep;
        const char *indexSep;
        int kind = 0;
        int scanWritten;
        int indexWritten;

        if (!strcmp(pEnt->d_name, ".") || !strcmp(pEnt->d_name, ".."))
            continue;

        scanSep = (scanDir[0] &&
                   (scanDir[strlen(scanDir) - 1] == '/' ||
                    scanDir[strlen(scanDir) - 1] == '\\')) ? "" : "/";
        indexSep = (indexDir[0] &&
                    (indexDir[strlen(indexDir) - 1] == '/' ||
                     indexDir[strlen(indexDir) - 1] == '\\')) ? "" : "/";
        scanWritten = snprintf(child, sizeof(child), "%s%s%s",
                               scanDir, scanSep, pEnt->d_name);
        indexWritten = snprintf(indexChild, sizeof(indexChild), "%s%s%s",
                                indexDir, indexSep, pEnt->d_name);
        if (scanWritten < 0 || scanWritten >= (int)sizeof(child) ||
            indexWritten < 0 || indexWritten >= (int)sizeof(indexChild))
            continue;

        if (_HasExt(pEnt->d_name, ".mod")) kind = 1;
        else if (_HasExt(pEnt->d_name, ".xm")) kind = 2;
        if (kind)
        {
            _IndexAdd(indexChild, kind);
            continue;
        }

        if (depth < BGM_SCAN_MAX_DEPTH)
        {
            Bool isDir = FALSE;
            Bool typeKnown = FALSE;
#ifdef DT_DIR
            if (pEnt->d_type == DT_DIR)
            {
                isDir = TRUE;
                typeKnown = TRUE;
            }
            else if (pEnt->d_type == DT_REG)
            {
                typeKnown = TRUE;
            }
#endif
            if (!typeKnown)
            {
                struct stat st;
                if (stat(child, &st) == 0)
                    isDir = S_ISDIR(st.st_mode) ? TRUE : FALSE;
            }
            if (isDir)
                _ScanDirAs(child, indexChild, depth + 1);
        }
    }
    closedir(pDir);
    return 1;
}

static int _ScanDir(const char *scanDir, int depth)
{
    return _ScanDirAs(scanDir, scanDir, depth);
}

static void _WakeAfterNewSource(int before)
{
    if (before == 0 && s_indexCount > 0)
    {
        unsigned int seed = (unsigned int)clock();
        s_trackIdx = (int)(seed % (unsigned int)s_indexCount);
        if (s_state == BGM_FAILED)
            s_state = BGM_UNTRIED;
    }
}

/* USB is deliberately absent from the boot-critical path.  Once the browser
   has started the USB BDM stack (or MX4SIO registered a massN: device), scan
   the mass BGM directories exactly once and wake a previously empty player. */
static void _MassScanStep(void)
{
    int ready = UsbBdmIsLoaded() || Mx4sioIsLoaded();
    int enabled = MassStorageIsEnabled() || Mx4sioIsEnabled();
    int before;
    size_t d;

    if (!enabled || !ready)
    {
        s_massWasReady = FALSE;
        s_massScanDone = FALSE;
        return;
    }

    if (!s_massWasReady)
    {
        s_massWasReady = TRUE;
        s_massScanDone = FALSE;
    }
    if (s_massScanDone) return;

    before = s_indexCount;
    if (_IsMassPath(s_bootBgm))
        _ScanDir(s_bootBgm, 0);

    for (d = 0; d < BGM_NUM_DIRS && s_indexCount < BGM_INDEX_MAX; d++)
        if (_IsMassPath(s_dirs[d]))
            _ScanDir(s_dirs[d], 0);

    s_massScanDone = TRUE;
    _WakeAfterNewSource(before);
}

/* MMCE e' carregado sob demanda e, ao contrario de mc:/mass:/, pode ser
   ativado depois que o indice inicial do BGM ja existe. Faz uma unica
   sondagem por ativacao, escaneia somente portas que responderam ao PING e
   reanima o player se ele estava em "No Track". */
static void _MmceScanStep(void)
{
    int enabled = MmceSupportIsEnabled();
    int before;
    int slots;
    int slot;
    size_t d;

    if (!enabled)
    {
        /* Permite uma nova sondagem se o usuario ligar MMCE mais tarde. */
        s_mmceWasEnabled = FALSE;
        s_mmceScanDone = FALSE;
        return;
    }

    if (!s_mmceWasEnabled)
    {
        s_mmceWasEnabled = TRUE;
        s_mmceScanDone = FALSE;
    }
    if (s_mmceScanDone) return;

    /* Se MX4SIO ja possui o SIO2 nesta sessao, a troca so pode acontecer
       apos reiniciar. Nao tente tocar o device oposto enquanto isso. */
    if (MmceNeedsRestart())
    {
        s_mmceScanDone = TRUE;
        return;
    }

    slots = MmceProbeAvailableSlots();
    before = s_indexCount;

    slot = _MmcePathSlot(s_bootBgm);
    if (slot >= 0 && (slots & (1 << slot)))
        _ScanDir(s_bootBgm, 0);

    for (d = 0; d < BGM_NUM_DIRS && s_indexCount < BGM_INDEX_MAX; d++)
    {
        slot = _MmcePathSlot(s_dirs[d]);
        if (slot >= 0 && (slots & (1 << slot)))
            _ScanDir(s_dirs[d], 0);
    }

    s_mmceScanDone = TRUE;
    _WakeAfterNewSource(before);
}

static void _ScanHddLogicalDir(const char *logicalDir)
{
    char mapped[256];

    if (!_IsHddPath(logicalDir)) return;
    if (HddMapPath(logicalDir, mapped, sizeof(mapped)) == 1)
        _ScanDirAs(mapped, logicalDir, 0);
}

/* O HDD interno nao possui uma raiz de arquivos unica: hdd0: enumera
   particoes APA e cada particao PFS precisa ser montada em pfs0:. Quando o
   usuario habilita HDD Support, procura a primeira particao que contenha
   SNESticle/bgm ou bgm. Os nomes guardados no indice continuam no formato
   hdd0:/PARTICAO/..., portanto abrir outra particao no browser nao quebra a
   playlist: _LoadFileAlloc remonta a particao correta antes de cada carga. */
static void _HddScanStep(void)
{
    int enabled = HddSupportIsEnabled();
    int before;
    int hfd;
    int partCount = 0;
    int i;
    size_t d;
    char parts[BGM_HDD_PART_MAX][128];

    if (!enabled)
    {
        s_hddWasEnabled = FALSE;
        s_hddScanDone = FALSE;
        return;
    }

    if (!s_hddWasEnabled)
    {
        s_hddWasEnabled = TRUE;
        s_hddScanDone = FALSE;
    }
    if (s_hddScanDone) return;

    if (s_indexCount >= BGM_INDEX_MAX || HddLoadEmbeddedIrx() < 0)
    {
        s_hddScanDone = TRUE;
        return;
    }

    before = s_indexCount;

    /* Respeita primeiro um caminho explicito vindo do ELF ou BGM_PATH. */
    if (_IsHddPath(s_bootBgm))
        _ScanHddLogicalDir(s_bootBgm);
    for (d = 0; d < BGM_NUM_DIRS && s_indexCount < BGM_INDEX_MAX; d++)
        if (_IsHddPath(s_dirs[d]))
            _ScanHddLogicalDir(s_dirs[d]);

    /* Sem caminho explicito encontrado: coleta as particoes PFS principais.
       Fecha hdd0: antes de montar pfs0:, evitando manter dois comandos APA
       concorrentes durante a varredura. */
    if (s_indexCount == before)
    {
        hfd = fileXioDopen("hdd0:");
        if (hfd >= 0)
        {
            iox_dirent_t entry;
            while (partCount < BGM_HDD_PART_MAX &&
                   fileXioDread(hfd, &entry) > 0)
            {
                if (!entry.name[0] ||
                    entry.stat.attr != ATTR_MAIN_PARTITION ||
                    entry.stat.mode != FS_TYPE_PFS)
                    continue;

                size_t nameLen = strlen(entry.name);
                if (nameLen >= sizeof(parts[partCount]))
                    continue;
                memcpy(parts[partCount], entry.name, nameLen + 1);
                partCount++;
            }
            fileXioDclose(hfd);
        }

        for (i = 0; i < partCount && s_indexCount < BGM_INDEX_MAX; i++)
        {
            char logical[256];
            int beforePart = s_indexCount;
            int written;

            written = snprintf(logical, sizeof(logical),
                               "hdd0:/%s/SNESticle/bgm", parts[i]);
            if (written >= 0 && written < (int)sizeof(logical))
                _ScanHddLogicalDir(logical);

            if (s_indexCount == beforePart)
            {
                written = snprintf(logical, sizeof(logical),
                                   "hdd0:/%s/bgm", parts[i]);
                if (written >= 0 && written < (int)sizeof(logical))
                    _ScanHddLogicalDir(logical);
            }

            /* A primeira particao com tracks vira a fonte HDD desta sessao.
               Isso evita montar e varrer dezenas de particoes sem necessidade. */
            if (s_indexCount > beforePart)
                break;
        }
    }

    s_hddScanDone = TRUE;
    _WakeAfterNewSource(before);
}

static void _BuildIndex(void)
{
    size_t d;

    s_indexCount = 0;
    s_discScanState = BGM_DISC_PENDING;
    s_discScanFrames = 0;
    s_discStablePolls = 0;
    s_discEeReady = FALSE;
    s_massWasReady = FALSE;
    s_massScanDone = FALSE;
    s_mmceWasEnabled = FALSE;
    s_mmceScanDone = FALSE;
    s_hddWasEnabled = FALSE;
    s_hddScanDone = FALSE;
    _BuildBootBgm();

    /* Primeiro escaneia caminhos que nao dependem de CD/DVD, MMCE ou HDD.
       Os dispositivos especiais possuem etapas proprias abaixo. */
    if (s_bootBgm[0] && !_IsDiscPath(s_bootBgm) &&
        !_IsMassPath(s_bootBgm) && _MmcePathSlot(s_bootBgm) < 0 &&
        !_IsHddPath(s_bootBgm))
        _ScanDir(s_bootBgm, 0);

    for (d = 0; d < BGM_NUM_DIRS && s_indexCount < BGM_INDEX_MAX; d++)
        if (!_IsDiscPath(s_dirs[d]) && !_IsMassPath(s_dirs[d]) &&
            _MmcePathSlot(s_dirs[d]) < 0 && !_IsHddPath(s_dirs[d]))
            _ScanDir(s_dirs[d], 0);

    /* Config persistida ja pode ter habilitado MMCE no boot. Esta chamada
       tambem fica barata quando o recurso esta desligado. */
    _MassScanStep();
    _MmceScanStep();
    _HddScanStep();

    /* faixa inicial pseudo-aleatoria (clock varia conforme o tempo de
       boot); se nao houver entropia, cai no indice 0 -- sem problema. */
    if (s_indexCount > 0)
    {
        unsigned int seed = (unsigned int)clock();
        s_trackIdx = (int)(seed % (unsigned int)s_indexCount);
    }
}

/* Um passo curto por frame. Nenhum acesso a cdfs: acontece antes de o
   mechacon responder "pronto" duas vezes. O opendir posterior e' sincrono,
   mas o driver possui prazo e cancelamento para uma leitura emperrada. */
static void _DiscScanStep(void)
{
    int type;
    int ready;
    int before;
    size_t d;
    DIR *root;

    if (s_discScanState == BGM_DISC_DONE) return;

    /* Do not start CDFS merely because menu music is enabled.  The browser's
       explicit cdfs: selection owns that potentially blocking module load.
       This is what keeps direct OPL App/USB boots independent of the drive. */
    if (!CdfsIsLoaded()) return;

    if (!s_discEeReady)
    {
        if (!sceCdInit(SCECdINoD))
        {
            s_discScanState = BGM_DISC_DONE;
            return;
        }
        s_discEeReady = TRUE;
    }

    s_discScanFrames++;
    if (s_discScanFrames < BGM_DISC_GRACE_FRAMES) return;
    if (((s_discScanFrames - BGM_DISC_GRACE_FRAMES) %
         BGM_DISC_POLL_FRAMES) != 0) return;

    type = sceCdGetDiskType();
    if (!_DiscTypeHasFilesystem(type))
    {
        s_discStablePolls = 0;
        if (s_discScanFrames >= BGM_DISC_GIVEUP_FRAMES)
            s_discScanState = BGM_DISC_DONE;
        return;
    }

    /* mode=1: consulta e volta; nunca espera o drive terminar de girar. */
    ready = sceCdDiskReady(1);
    if (ready != SCECdComplete)
    {
        s_discStablePolls = 0;
        return;
    }

    if (++s_discStablePolls < BGM_DISC_STABLE_POLLS) return;

    /* Confirma que o device cdfs: realmente responde. Se o RPC ainda nao
       estiver pronto, mantem PENDING e tenta novamente em outro frame. */
    root = opendir("cdfs:/");
    if (!root)
    {
        s_discStablePolls = 0;
        return;
    }
    closedir(root);

    before = s_indexCount;
    if (s_bootBgm[0] && _IsDiscPath(s_bootBgm))
        _ScanDir(s_bootBgm, 0);

    for (d = 0; d < BGM_NUM_DIRS && s_indexCount < BGM_INDEX_MAX; d++)
        if (_IsDiscPath(s_dirs[d]))
            _ScanDir(s_dirs[d], 0);

    s_discScanState = BGM_DISC_DONE;
    if (before == 0 && s_indexCount > 0)
    {
        unsigned int seed = (unsigned int)clock();
        s_trackIdx = (int)(seed % (unsigned int)s_indexCount);
        if (s_state == BGM_FAILED)
            s_state = BGM_UNTRIED;
    }
}

/* Le um arquivo inteiro para um buffer malloc'd.  Retorna NULL em erro. */
static char *_LoadFileAlloc(const char *path, long *outLen)
{
    FILE *f;
    long  len;
    char *buf;
    size_t rd;
    char mapped[256];
    const char *openPath = path;

    if (_IsHddPath(path))
    {
        if (!HddSupportIsEnabled() || HddLoadEmbeddedIrx() < 0 ||
            HddMapPath(path, mapped, sizeof(mapped)) != 1)
            return NULL;
        openPath = mapped;
    }

    f = fopen(openPath, "rb");
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

static void _ResetResampler(void)
{
    s_sourceFrames = 0;
    s_resampleFrac = 0;
}

static void _TryLoad(void)
{
    if (s_indexCount < 0) _BuildIndex();
    if (s_indexCount <= 0)
    {
        s_state = BGM_FAILED;
        return;
    }

    /* Uma faixa truncada ou incompatível nao deve derrubar a playlist
       inteira. Tenta somente uma entrada por frame: se ela falhar, remove
       do indice e deixa a seguinte para o proximo BgmUpdate, evitando um
       congelamento longo quando ha varios arquivos ruins. */
    {
        const char *path;
        int kind;

        if (s_trackIdx < 0 || s_trackIdx >= s_indexCount) s_trackIdx = 0;
        path = s_index[s_trackIdx].path;
        kind = s_index[s_trackIdx].kind;

        long len = 0;
        char *fileBuf = _LoadFileAlloc(path, &len);
        int loadRet = -XMP_ERROR_SYSTEM;
        int startRet = -XMP_ERROR_STATE;

        s_xmp = xmp_create_context();
        if (fileBuf && s_xmp)
        {
            /* MODs classicos usam separacao 100% L/R. Em TVs/HDMI que fazem
               downmix imperfeito isso soa como canal/instrumento faltando,
               enquanto XM (com pan proprio) costuma parecer normal. O valor
               50 e' o default-pan recomendado pelo libxmp atual e precisa ser
               aplicado ANTES do load para afetar o pan inicial do modulo. */
            xmp_set_player(s_xmp, XMP_PLAYER_DEFPAN, 50);
            loadRet = xmp_load_module_from_memory(s_xmp, fileBuf, len);
            if (loadRet == 0)
            {
                startRet = xmp_start_player(s_xmp, s_rate, 0);
                if (startRet == 0)
                {
                    /* Linear custa bem menos que spline e evita o aliasing
                       forte de nearest. O proprio loader escolhe as regras
                       ProTracker ou FastTracker de acordo com o modulo. */
                    xmp_set_player(s_xmp, XMP_PLAYER_INTERP,
                                   XMP_INTERP_LINEAR);

                    free(fileBuf);
                    _ResetResampler();
                    s_state = (kind == 1) ? BGM_MOD : BGM_XM;
                    return;
                }
            }
        }

        if (fileBuf) free(fileBuf);
        if (s_xmp)
        {
            xmp_free_context(s_xmp);
            s_xmp = NULL;
        }
        _ResetResampler();

        _IndexRemove(s_trackIdx);
    }

    s_state = (s_indexCount > 0) ? BGM_UNTRIED : BGM_FAILED;
}


/* ---- API ------------------------------------------------------------- */

/* Libera APENAS o decoder e o buffer do arquivo, deixando o estado pronto
   para o proximo BgmUpdate recarregar (s_state = UNTRIED).  NAO mexe na
   logica de volume/dreno da sessao de menu (s_volSet/s_drainWait) -- assim
   o auto-advance (trocar de faixa no meio do menu) toca a proxima na hora,
   sem re-esperar o dreno da cauda do jogo. */
static void _BgmFreeDecoder(void)
{
    if (s_xmp)
    {
        xmp_free_context(s_xmp);
        s_xmp = NULL;
    }
    _ResetResampler();
    s_state  = BGM_UNTRIED;
}

/* Libera o decoder e o buffer do arquivo E re-arma a logica de
   volume/dreno.  Chamado quando a trilha e' desligada (BgmSetVolume(0)) ou
   numa troca explicita (BgmNext): nesses casos queremos esperar a cauda de
   audio do jogo drenar antes de soltar o tracker de novo. */
static void _BgmFree(void)
{
    _BgmFreeDecoder();
    s_volSet = FALSE;
    s_drainWait = 0;
    s_gapFrames = 0;
}

/* Avanca para a proxima faixa do indice (sequencial, circular) e libera o
   decoder atual SEM re-armar o dreno -- usado pelo auto-advance quando a
   faixa atual termina uma passada inteira.  Retorna TRUE se trocou; com
   0/1 faixa nao ha "outra": retorna FALSE (o chamador deixa a faixa unica
   seguir em loop normal, sem reload nem hitch). */
static Bool _BgmAdvance(void)
{
    if (s_indexCount <= 1) return FALSE;
    s_trackIdx = (s_trackIdx + 1) % s_indexCount;
    _BgmFreeDecoder();   /* mantem s_volSet: proxima faixa toca na hora */
    return TRUE;
}

void BgmStop(void)
{
    _BgmLock();
    /* Para de alimentar SEM liberar o decoder: a faixa fica carregada,
       entao reabrir o menu e' instantaneo (sem reler do disco -> sem a
       travadinha).  So' re-arma a logica de volume/dreno para a proxima
       entrada no menu (esperar a cauda de audio do jogo drenar antes de
       soltar o tracker).  A liberacao real acontece em BgmSetVolume(0). */
    s_menuActive = FALSE;
    s_volSet = FALSE;
    s_drainWait = 0;
    s_gapFrames = 0;
    _BgmUnlock();
}

void BgmMenuEnter(void)
{
    /* This is intentionally lighter than BgmUpdate(): entering the menu must
       never scan a drive or reload a module before the first frame appears. */
    _BgmLock();
    s_menuActive = TRUE;
    _BgmUnlock();
}

void BgmNext(void)
{
    /* Avanca para a proxima faixa do indice e libera o decoder atual, de
       modo que o proximo BgmUpdate carregue a nova faixa.  Chamado ao
       uma troca explicita de faixa. Releria do disco
       (pode dar um hitch breve no memory card), por isso so' troca quando
       ha 2+ faixas; com 0/1 faixa nao faz nada (sem reload, sem hitch). */
    _BgmLock();
    /* Before the first normal BgmUpdate the index is unknown. Do not turn a
       simple L2+R2 press into a synchronous scan of every storage device. */
    if (s_indexCount <= 1)
    {
        _BgmUnlock();
        return;
    }

    s_trackIdx = (s_trackIdx + 1) % s_indexCount;

    if (s_state == BGM_MOD || s_state == BGM_XM) _BgmFree();
    _BgmUnlock();
}

void BgmSetVolume(int vol)
{
    _BgmLock();
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
    _BgmUnlock();
}

int BgmGetVolume(void)
{
    int result;
    _BgmLock();
    result = s_volume;
    _BgmUnlock();
    return result;
}

int BgmTrackCount(void)
{
    int result;
    /* Draw/getter paths stay memory-only. The next normal BgmUpdate performs
       lazy discovery; merely drawing Video Config must not open devices. */
    _BgmLock();
    result = s_indexCount < 0 ? 0 : s_indexCount;
    _BgmUnlock();
    return result;
}

int BgmIsSearching(void)
{
    int result;
    _BgmLock();
    result = (s_indexCount < 0 ||
              (CdfsIsLoaded() && s_discScanState == BGM_DISC_PENDING)) ? 1 : 0;
    _BgmUnlock();
    return result;
}

int BgmGetRate(void)
{
    int result;
    _BgmLock();
    result = s_rate;
    _BgmUnlock();
    return result;
}

static void _BgmSetRateLocked(int hz)
{
    Bool wasPlaying;

    if (hz < 8000)  hz = 8000;
    if (hz > 48000) hz = 48000;
    if (hz == s_rate) return;
    wasPlaying = (s_xmp && (s_state == BGM_MOD || s_state == BGM_XM))
                 ? TRUE : FALSE;
    s_rate = hz;

    /* Changing synthesis frequency does not require reading the module again.
       Keep it loaded in libxmp, restart only the player at the new rate and
       reset the resampler. The previous code freed the entire context, so one
       Left/Right press caused a synchronous disk reload and an audible stop. */
    if (wasPlaying)
    {
        xmp_end_player(s_xmp);
        if (xmp_start_player(s_xmp, s_rate, 0) == 0)
        {
            xmp_set_player(s_xmp, XMP_PLAYER_INTERP, XMP_INTERP_LINEAR);
            _ResetResampler();
        }
        else
        {
            _BgmFree();
        }
    }
}

void BgmSetRate(int hz)
{
    _BgmLock();
    _BgmSetRateLocked(hz);
    _BgmUnlock();
}

void BgmCycleRate(int dir)
{
    int i, idx = 3; /* fallback ~32000 */
    _BgmLock();
    for (i = 0; i < BGM_RATE_COUNT; i++)
        if (s_rateList[i] == s_rate) { idx = i; break; }
    idx += (dir < 0) ? -1 : 1;
    if (idx < 0)               idx = BGM_RATE_COUNT - 1;
    if (idx >= BGM_RATE_COUNT)  idx = 0;
    _BgmSetRateLocked(s_rateList[idx]);
    _BgmUnlock();
}

static void _BgmUpdateLocked(Bool allowFilesystem)
{
    int avail, n, j;

    if (s_volume <= 0)         return;   /* OFF: nem toca o drive */
    if (s_indexCount < 0)
    {
        if (!allowFilesystem) return;
        _BuildIndex();
    }
    if (allowFilesystem)
    {
        _MassScanStep();
        _MmceScanStep();
        _HddScanStep();
        _DiscScanStep();
    }
    if (!Aud_IsInitialized())  return;

    /* Respiro entre faixas: apos detectar o fim e avancar (decoder ja'
       liberado), tocamos alguns frames de SILENCIO antes de carregar a
       proxima.  Enche o ring de zeros -- quando o _TryLoad bloquear a EE
       lendo o arquivo do disco, o audsrv toca silencio limpo (sem
       underrun/estralo) em vez de repetir lixo do ring.  Da' tambem uma
       pausa curta tipo playlist entre as musicas. */
    if (s_gapFrames > 0)
    {
        int g = Aud_Available();
        s_gapFrames--;
        if (g > 0)
        {
            if (g > BGM_OUT_CHUNK) g = BGM_OUT_CHUNK;
            memset(s_left,  0, (size_t)g * sizeof(s_left[0]));
            memset(s_right, 0, (size_t)g * sizeof(s_right[0]));
            Aud_Enqueue(s_left, s_right, g, 0);
        }
        return;
    }

    if (s_state == BGM_UNTRIED)
    {
        if (!allowFilesystem) return;
        _TryLoad();
    }
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
    if (n > BGM_MAX_OUT_PER_FRAME) n = BGM_MAX_OUT_PER_FRAME; /* anti-pico (bug stutter) */
    if (n > BGM_OUT_CHUNK - 2) n = BGM_OUT_CHUNK - 2;
    if (n < 1) return;

    /* Descobre quantos frames-fonte o reamostrador vai acessar, preservando
       a fase exata como fracao /48000. Nao ha arredondamento cumulativo:
       por exemplo, 44100 gera exatamente 44100 frames-fonte para cada
       48000 frames enviados ao audsrv. */
    {
        unsigned long long lastPhase =
            (unsigned long long)s_resampleFrac +
            (unsigned long long)(n - 1) * (unsigned int)s_rate;
        int needed = (int)(lastPhase / 48000u) + 2;
        int append;

        if (needed > BGM_OUT_CHUNK)
            needed = BGM_OUT_CHUNK; /* defesa para configuracoes futuras */

        append = needed - s_sourceFrames;
        if (append > 0)
        {
            struct xmp_frame_info fi;
            int loopLimit = (s_indexCount > 1) ? 1 : 0;
            int ret;

            memset(&fi, 0, sizeof(fi));
            ret = xmp_play_buffer(
                s_xmp,
                &s_inter[s_sourceFrames * 2],
                append * 2 * (int)sizeof(short),
                loopLimit);
            xmp_get_frame_info(s_xmp, &fi);

            /* Com playlist, libxmp interrompe no primeiro loop completo.
               Assim nao vazam amostras do recomeco da faixa nem um pattern
               preso. Com uma faixa apenas, ela continua em loop infinito. */
            if (s_indexCount > 1 && fi.loop_count > 0)
            {
                if (_BgmAdvance())
                {
                    s_gapFrames = BGM_GAP_FRAMES;
                    return;
                }
            }
            if (ret < 0)
            {
                if (_BgmAdvance())
                    s_gapFrames = BGM_GAP_FRAMES;
                else
                {
                    _BgmFreeDecoder();
                    s_state = BGM_FAILED;
                }
                return;
            }

            s_sourceFrames += append;
        }
    }

    /* Reamostra s_rate -> 48 kHz e desinterleava para L/R. A fase e os
       frames nao consumidos ficam guardados para a proxima chamada. */
    {
        unsigned long long phase = s_resampleFrac;
        int consume;
        int remain;

        for (j = 0; j < n; j++)
        {
            unsigned int i  = (unsigned int)(phase / 48000u);
            unsigned int fr = (unsigned int)(phase % 48000u);
            unsigned int i1 = i + 1;
            int l0, l1, r0, r1;

            if (i1 >= (unsigned int)s_sourceFrames)
                i1 = (unsigned int)(s_sourceFrames - 1);

            l0 = s_inter[i * 2 + 0]; l1 = s_inter[i1 * 2 + 0];
            r0 = s_inter[i * 2 + 1]; r1 = s_inter[i1 * 2 + 1];
            s_left [j] = (short)(l0 +
                (int)(((long long)(l1 - l0) * fr) / 48000));
            s_right[j] = (short)(r0 +
                (int)(((long long)(r1 - r0) * fr) / 48000));
            phase += (unsigned int)s_rate;
        }

        consume = (int)(phase / 48000u);
        s_resampleFrac = (unsigned int)(phase % 48000u);
        if (consume > s_sourceFrames) consume = s_sourceFrames;
        remain = s_sourceFrames - consume;
        if (remain > 0 && consume > 0)
            memmove(s_inter, &s_inter[consume * 2],
                    (size_t)remain * 2 * sizeof(short));
        s_sourceFrames = remain;
    }

    /* garante volume audivel: o menu de pausa muta o audsrv (Aud_Setvol(0))
       para matar o rabo de audio do jogo; o volume cheio ja' foi firmado
       acima, apos a cauda do jogo drenar. */

    Aud_Enqueue(s_left, s_right, n, 0); /* wait=0: best-effort, nao trava */
}

void BgmUpdate(void)
{
    _BgmLock();
    s_menuActive = TRUE;
    /* While a caller holds an I/O scope, only the helper is allowed to own
       filesystem progress. A nested render/modal may still fill PCM. */
    _BgmUpdateLocked(s_ioDepth == 0 ? TRUE : FALSE);
    _BgmUnlock();
}

static void _BgmIOThread(void *argument)
{
    (void)argument;

    for (;;)
    {
        Bool active;

        _BgmLock();
        active = (s_ioDepth > 0 && s_menuActive) ? TRUE : FALSE;
        if (active)
        {
            /* Memory/CPU only: no opendir/fopen, no device probing and no
               module reload. This remains safe even when the UI thread is
               blocked inside fileXio or newlib on another device. */
            _BgmUpdateLocked(FALSE);
        }
        _BgmUnlock();

        if (active)
            DelayThread(BGM_IO_TICK_USEC);
        else
            /* No timer, polling or semaphore churn during gameplay. The
               0->1 I/O-depth transition wakes this thread explicitly. */
            SleepThread();
    }
}

static Bool _BgmEnsureIOThread(void)
{
    ee_sema_t sema;
    ee_thread_t thread;
    int result;

    if (s_ioThread > 0)
        return TRUE;

    if (s_ioLock <= 0)
    {
        memset(&sema, 0, sizeof(sema));
        sema.init_count = 1;
        sema.max_count = 1;
        s_ioLock = CreateSema(&sema);
        if (s_ioLock <= 0)
            return FALSE;
    }

    memset(&thread, 0, sizeof(thread));
    thread.func = (void *)_BgmIOThread;
    thread.stack = s_ioStack;
    thread.stack_size = sizeof(s_ioStack);
    thread.gp_reg = &_gp;
    thread.initial_priority = BGM_IO_THREAD_PRIORITY;

    result = CreateThread(&thread);
    if (result <= 0)
        return FALSE;
    s_ioThread = result;
    if (StartThread(s_ioThread, NULL) < 0)
    {
        DeleteThread(s_ioThread);
        s_ioThread = -1;
        return FALSE;
    }
    return TRUE;
}

void BgmIOBegin(void)
{
    Bool wake;

    if (!_BgmEnsureIOThread())
        return;

    _BgmLock();
    wake = s_ioDepth == 0 ? TRUE : FALSE;
    s_ioDepth++;
    if (s_menuActive)
    {
        /* Fill whatever room exists before the blocking call begins; the
           helper then tops the ring up every few milliseconds. */
        _BgmUpdateLocked(FALSE);
    }
    _BgmUnlock();

    if (wake)
        WakeupThread(s_ioThread);
}

void BgmIOEnd(void)
{
    if (s_ioLock <= 0)
        return;

    _BgmLock();
    if (s_ioDepth > 0)
        s_ioDepth--;
    _BgmUnlock();
}
