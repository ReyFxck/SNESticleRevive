#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#define NEWLIB_PORT_AWARE          /* libera fileXio no port newlib (igual main.cpp) */
#include <io_common.h>             /* FIO_MT_RDWR (guard proprio newlib) */
#include <fileXio.h>
#include <fileXio_rpc.h>
#undef NEWLIB_PORT_AWARE

#include "embedded_irx.h"

/* These headers are generated at build time by bin2c from the
   corresponding files in irx/ or directly from $(PS2SDK)/iop/irx/.
   Each one defines:
       unsigned char  <name>_irx[]            __attribute__((aligned(16)));
       unsigned int   size_<name>_irx;
   They are included exactly once in this translation unit so the arrays
   end up as ordinary globals in the ELF. */
#include "audsrv_irx.h"
#include "freesd_irx.h"
#include "sio2man_irx.h"
#include "mcman_irx.h"
#include "mcserv_irx.h"
#include "padman_irx.h"
#include "mtapman_irx.h"
#include "ps2dev9_irx.h"
#include "netman_irx.h"
#include "smap_irx.h"
#include "ps2ip_irx.h"

/* Stack BDM moderna (USB + FAT/exFAT/GPT). */
#include "usbd_irx.h"
#include "bdm_irx.h"
#include "bdmfs_fatfs_irx.h"
#include "usbmass_bd_irx.h"
#include "ps2atad_irx.h"
#include "ps2hdd_irx.h"
#ifdef HAVE_PS2FS
#include "ps2fs_irx.h"
#endif
#ifdef HAVE_MMCEMAN
#include "mmceman_irx.h"
#endif
#ifdef HAVE_MX4SIO
#include "mx4sio_bd_irx.h"
#endif

/* Log visivel no splash de boot (real hardware) -- definido em audio_audsrv.c. */
extern "C" void ScrPrintf(const char *pFormat, ...);
extern "C" void BootImport(const char *pName, int ret);

struct EmbeddedEntry
{
    const char          *name;
    const unsigned char *data;
    unsigned int         size;
};

/* All four iaddis-era custom IRX modules (CDVD.IRX, SJPCM2.IRX,
   MCSAVE.IRX, NETPLAY.IRX) have been retired.  On NetherSX2 -- and on
   any other IOP that isn't a 100% faithful real PS2 -- they would
   load via SifExecModuleBuffer but their RPC entry points never came
   up or came up incompatibly with the rom-resident services already
   bound by main.cpp / the BIOS, and the matching EE-side init function
   (CDVD_Init, Aud_Init, MCSave_Init) would spin forever in
   SifBindRpc and deadlock the boot.  Each one has been replaced with
   a modern PS2SDK-based path:

     - audio   : PS2DEV audsrv.irx, embedded here from
                 $(PS2SDK)/iop/irx/audsrv.irx, plus a freesd.irx
                 fallback (used when the BIOS does not ship
                 rom0:LIBSD).  See src/modules/sjpcm/sjpcm_rpc.c for
                 the EE-side wrapper.
     - cdfs    : init_ps2_filesystem_driver() in app/main.cpp brings
                 up the modern cdfs.irx through ps2_drivers; the
                 browser and ROM loader reach the disc through plain
                 newlib stdio (opendir("cdfs:/"), fopen, ...).
     - memcard : sio2man.irx + mcman.irx + mcserv.irx, embedded here
                 and loaded by MemCardLoadEmbeddedIrx().  All save
                 paths go through newlib stdio onto mcman/mcserv via
                 iomanX -- the synchronous fallback that
                 mainloop_state.cpp::MCSave_Write already had.  The
                 async MCSave_Init() RPC path is gone.
     - netplay : the protocol runs on the EE itself, mirrored from
                 hugorsgarcia/PS2SNESticle/SNESticle/Modules/netplay/
                 Source under src/modules/netplay/protocol/, talking
                 straight to lwIP through PS2SDK's <sys/socket.h>
                 shims.  The lwIP-side stack (ps2dev9 + netman + smap
                 + ps2ip) is embedded here and loaded by
                 NetIfLoadEmbeddedIrx().

   Net effect: no IRX file has to be shipped next to the ELF for any
   subsystem to work; the ELF is fully self-contained. */
static const EmbeddedEntry s_embedded[] =
{
    { "AUDSRV.IRX",  audsrv_irx,  sizeof(audsrv_irx)  },
    /* freesd is the PS2SDK-supplied SPU2 driver IRX, used as a
       universal fallback when rom0:LIBSD is absent (early Japanese
       models, some emulator setups). audsrv binds to its sceSd*
       exports the same way it would to LIBSD's. */
    { "FREESD.IRX",  freesd_irx,  sizeof(freesd_irx)  },
};

static const char *path_basename(const char *path)
{
    const char *p = path;
    const char *base = path;

    while (*p)
    {
        if (*p == '/' || *p == '\\' || *p == ':')
        {
            base = p + 1;
        }
        p++;
    }

    return base;
}

static int eq_ci(const char *a, const char *b)
{
    while (*a && *b)
    {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

extern "C" int EmbeddedIrxFind(const char *path,
                               const unsigned char **out_data,
                               unsigned int         *out_size)
{
    if (!path) return -1;

    const char *base = path_basename(path);
    if (!*base) return -1;

    for (size_t i = 0; i < sizeof(s_embedded) / sizeof(s_embedded[0]); ++i)
    {
        if (eq_ci(base, s_embedded[i].name))
        {
            if (out_data) *out_data = s_embedded[i].data;
            if (out_size) *out_size = s_embedded[i].size;
            return 0;
        }
    }

    return -1;
}

extern "C" int EmbeddedIrxLoad(const unsigned char *data,
                               unsigned int         size,
                               int                  arg_len,
                               const char          *args)
{
    int result = 0;
    int ret;

    /* SifExecModuleBuffer transfers the IRX from EE RAM to IOP RAM and
       starts it. Returns the module ID on success. */
    ret = SifExecModuleBuffer((void *)data, size, arg_len, args, &result);
    if (ret < 0) return ret;
    return ret;
}

/* Memory-card stack bring-up.
 *
 * Order matches ps2_drivers::init_memcard_driver(true) and every other
 * PS2 homebrew that brings the memcard up from scratch (picodrive,
 * uLaunchELF, OPL, hugorsgarcia/PS2SNESticle):
 *
 *   1. sio2man.irx  - SIO2 transport layer used by the memcard and pad
 *                     subsystems.  Must be loaded first; mcman / mcserv
 *                     and (later) padman / mtapman all depend on it.
 *   2. mcman.irx    - memcard manager; exposes the iomanX `mc0:`/`mc1:`
 *                     device used by newlib fopen / mkdir / opendir.
 *   3. mcserv.irx   - libmc RPC server (0x80000400).  Only required if
 *                     anything calls mcInit / mcOpen / mcSync, but we
 *                     load it anyway for parity with the previous
 *                     init_memcard_driver(true) behaviour so libmc-
 *                     based code paths keep working.
 *
 * We deliberately do NOT pre-check whether sio2man is already resident
 * (e.g. from a re-init) -- SifExecModuleBuffer on an already-running
 * module returns a duplicate-load error code (< 0) which we map to
 * MEMCARD_INIT_STATUS_*.  Callers that need re-entrancy must gate on
 * the static `s_loaded` flag below.
 */
static int s_memcard_loaded = 0;

extern "C" int MemCardLoadEmbeddedIrx(void)
{
    int ret;

    if (s_memcard_loaded) return 0;

    ret = EmbeddedIrxLoad(sio2man_irx, sizeof(sio2man_irx), 0, NULL);
    BootImport("sio2man", ret);
    if (ret < 0)
    {
        printf("MemCardLoadEmbeddedIrx: sio2man.irx failed (%d)\n", ret);
        /* MEMCARD_INIT_STATUS_DEPENDENCY_IRX_ERROR */
        return -4;
    }

    ret = EmbeddedIrxLoad(mcman_irx, sizeof(mcman_irx), 0, NULL);
    BootImport("mcman", ret);
    if (ret < 0)
    {
        printf("MemCardLoadEmbeddedIrx: mcman.irx failed (%d)\n", ret);
        /* MEMCARD_INIT_STATUS_MCMAN_IRX_ERROR */
        return -2;
    }

    ret = EmbeddedIrxLoad(mcserv_irx, sizeof(mcserv_irx), 0, NULL);
    BootImport("mcserv", ret);
    if (ret < 0)
    {
        printf("MemCardLoadEmbeddedIrx: mcserv.irx failed (%d)\n", ret);
        /* MEMCARD_INIT_STATUS_MCSERV_IRX_ERROR */
        return -3;
    }

    s_memcard_loaded = 1;
    return 0;
}

/* USB + BDM stack bring-up (substitui o init_usb_driver() do ps2_drivers).
 *
 * Carrega a stack BDM moderna do PROPRIO PS2SDK -- le FAT16/FAT32/exFAT e
 * tabela de particao MBR/GPT, e enumera cada pendrive/HD-externo como uma
 * unidade massN:.  Ordem padrao (igual OPL / exemplos PS2SDK):
 *
 *   usbd.irx -> bdm.irx -> bdmfs_fatfs.irx -> usbmass_bd.irx
 *
 * NAO usa dev9 (so' USB), entao nao tem o risco de travar boot do HD
 * interno.  Cada passo loga via ScrPrintf, visivel no splash de boot --
 * se der b.o., a ultima linha na tela mostra qual modulo falhou. */
extern "C" int UsbBdmLoadEmbeddedIrx(void)
{
    int ret;

    ret = EmbeddedIrxLoad(usbd_irx, sizeof(usbd_irx), 0, NULL);
    BootImport("usbd", ret);
    if (ret < 0) { printf("UsbBdm: usbd.irx failed (%d)\n", ret); return -1; }

    ret = EmbeddedIrxLoad(bdm_irx, sizeof(bdm_irx), 0, NULL);
    BootImport("bdm", ret);
    if (ret < 0) { printf("UsbBdm: bdm.irx failed (%d)\n", ret); return -2; }

    ret = EmbeddedIrxLoad(bdmfs_fatfs_irx, sizeof(bdmfs_fatfs_irx), 0, NULL);
    BootImport("bdmfs_fatfs", ret);
    if (ret < 0) { printf("UsbBdm: bdmfs_fatfs.irx failed (%d)\n", ret); return -3; }

    ret = EmbeddedIrxLoad(usbmass_bd_irx, sizeof(usbmass_bd_irx), 0, NULL);
    BootImport("usbmass_bd", ret);
    if (ret < 0) { printf("UsbBdm: usbmass_bd.irx failed (%d)\n", ret); return -4; }

#ifdef HAVE_MX4SIO
    /* MX4SIO saiu do boot: agora carrega DEPOIS da config (Mx4sioLoadIfEnabled,
       chamado em mainloop_init), e so' se o suporte a Mass estiver ligado.
       Evita tocar o SIO2 no boot de quem nao usa o adaptador SD. */
#endif

    /* HD INTERNO (APA): dev9 + ps2atad + ps2hdd -- DESABILITADO no boot.
     *
     * REGRESSAO confirmada em hardware real (Adriano): carregar estes 3
     * modulos aqui dava TELA PRETA no boot (testado em FAT32 e exFAT).
     * Motivo: EmbeddedIrxLoad usa SifExecModuleBuffer, que e' SINCRONO;
     * a rotina de init do ps2dev9/ps2hdd fica esperando o hardware DEV9/
     * ATA e, em muitos consoles, NAO retorna -> o boot congela ANTES do
     * video inicializar (por isso nem o splash aparece).  O "best-effort"
     * assumido antes estava errado: o load em si JA bloqueia, nao so' o
     * waitUntilDeviceIsReady() do ps2_drivers.
     *
     * O USB (usbd/bdm/bdmfs_fatfs/usbmass_bd, acima) continua intacto.
     * Suporte a hdd0: vai voltar depois como carga PREGUICOSA -- so' ao
     * entrar em hdd0: no browser -- para nunca tocar o boot.
     *
     *   ret = EmbeddedIrxLoad(ps2dev9_irx, sizeof(ps2dev9_irx), 0, NULL);
     *   ret = EmbeddedIrxLoad(ps2atad_irx, sizeof(ps2atad_irx), 0, NULL);
     *   ret = EmbeddedIrxLoad(ps2hdd_irx,  sizeof(ps2hdd_irx),  0, NULL);
     */

#ifdef HAVE_MMCEMAN
    /* MMCE (MemCard PRO2 / SD2PSX) NAO carrega mais no boot.  Agora e'
       opcional e preguicoso, igual ao HDD (ver MmceLoadEmbeddedIrx):
       evita ocupar RAM do IOP e tocar o SIO2 no boot de quem nao usa. */
#endif

    return 0;
}

/* HD INTERNO (APA) -- carga PREGUICOSA e opcional.
 *
 * Por que separado do UsbBdm: a init do ps2dev9/ps2hdd e' SINCRONA e,
 * em consoles sem HD (ou com DEV9 problematico), NAO retorna -> travava
 * o boot inteiro (tela preta).  Aqui isso so' roda quando o usuario
 * ESCOLHE entrar em hdd0: no browser, e so' se o toggle estiver ligado.
 * No pior caso, trava apenas a entrada no hdd0: (e so' de quem ligou a
 * opcao), nunca o boot.  Mesma filosofia do "HDD device start mode" do
 * OPL: padrao DESLIGADO, quem tem HD liga. */
static int s_hdd_enabled = 0;   /* toggle (persistido no video.cfg) */
static int s_hdd_loaded  = 0;   /* modulos ja carregados nesta sessao */

extern "C" int HddSupportIsEnabled(void)
{
    return s_hdd_enabled;
}

extern "C" void HddSupportSetEnabled(int enabled)
{
    s_hdd_enabled = enabled ? 1 : 0;
}

extern "C" int HddLoadEmbeddedIrx(void)
{
    int ret;

    if (!s_hdd_enabled) return -1;   /* desligado: nem tenta */
    if (s_hdd_loaded)   return 0;    /* ja carregado: no-op */

    /* dev9 -> ps2atad -> ps2hdd (expoe hdd0: no formato APA) -> ps2fs (PFS,
       para montar/ler dentro das particoes via pfs0:).  dev9 pode ja' estar
       carregado (stack de rede); o erro de duplicado e' ignorado.

       IMPORTANTE: ps2hdd e ps2fs PRECISAM de argumentos (-o/-n/-m) para
       alocar as tabelas internas de arquivos/buffers.  Sem eles, o ps2hdd
       nao inicializa direito e o primeiro dopen("hdd0:") (listar particoes)
       crasha.  Os valores batem com o wLaunchELF/OPL. */
    static const char hddarg[] = "-o\0" "4\0" "-n\0" "20";   /* 4 open, 20 buffers */
    static const char pfsarg[] = "-m\0" "4\0" "-o\0" "10\0" "-n\0" "40"; /* 4 mount, 10 open, 40 buf */

    ret = EmbeddedIrxLoad(ps2dev9_irx, sizeof(ps2dev9_irx), 0, NULL);
    printf("HddLoad: dev9 = %d\n", ret);
    ret = EmbeddedIrxLoad(ps2atad_irx, sizeof(ps2atad_irx), 0, NULL);
    printf("HddLoad: atad = %d\n", ret);
    ret = EmbeddedIrxLoad(ps2hdd_irx,  sizeof(ps2hdd_irx),  sizeof(hddarg), hddarg);
    printf("HddLoad: hdd  = %d\n", ret);
#ifdef HAVE_PS2FS
    ret = EmbeddedIrxLoad(ps2fs_irx,   sizeof(ps2fs_irx),   sizeof(pfsarg), pfsarg);
    printf("HddLoad: pfs  = %d\n", ret);
#else
    printf("HddLoad: pfs  = (nao embutido)\n");
#endif

    s_hdd_loaded = 1;
    return 0;
}

/* Traduz um caminho da UI no formato "hdd0:/PARTICAO/resto" para o caminho
 * real do PFS, montando pfs0: na particao (remonta se a particao mudou).
 *   - "hdd0:" ou "hdd0:/"  -> escreve "" em out e retorna 2 (LISTA de
 *     particoes; o chamador deve usar fileXioDopen("hdd0:")).
 *   - "hdd0:/PART/sub"     -> monta pfs0:=hdd0:PART, escreve "pfs0:/sub",
 *     retorna 1.
 *   - falha ao montar      -> retorna -1.
 *   - nao e' caminho hdd0: -> retorna 0 (out inalterado).
 * Mantem uma unica particao montada de cada vez (pfs0:). */
extern "C" int HddMapPath(const char *uiPath, char *out, int outsz)
{
    static char s_mounted[128] = { 0 };

    if (!uiPath || strncmp(uiPath, "hdd0:", 5) != 0)
        return 0;

    const char *p = uiPath + 5;          /* depois de "hdd0:" */
    while (*p == '/') p++;               /* pula barras -> "PART/..." ou "" */
    if (*p == 0)                         /* lista de particoes */
    {
        if (out && outsz) out[0] = 0;
        return 2;
    }

    char part[128];
    int i = 0;
    while (p[i] && p[i] != '/' && i < (int)sizeof(part) - 1) { part[i] = p[i]; i++; }
    part[i] = 0;
    const char *rest = p + i;            /* "/sub/..." ou "" */

    if (strcmp(s_mounted, part) != 0)    /* montar/remontar pfs0: */
    {
        if (s_mounted[0]) { fileXioUmount("pfs0:"); s_mounted[0] = 0; }
        char dev[160];
        snprintf(dev, sizeof(dev), "hdd0:%s", part);
        if (fileXioMount("pfs0:", dev, FIO_MT_RDWR) < 0)
            return -1;
        strncpy(s_mounted, part, sizeof(s_mounted) - 1);
        s_mounted[sizeof(s_mounted) - 1] = 0;
    }

    if (out && outsz)
        snprintf(out, outsz, "pfs0:%s", (*rest) ? rest : "/");
    return 1;
}

/* MMCE (MemCard PRO2 / SD2PSX via mmceman) -- carga PREGUICOSA e opcional,
 * mesma logica do HDD.  So' carrega mmceman.irx quando o usuario entra em
 * mmce0:/mmce1:, e so' se o toggle estiver ligado.  Mantem o IOP/SIO2
 * livres no boot de quem nao usa esses cartoes modificados. */
static int s_mmce_enabled = 0;   /* toggle (persistido no video.cfg) */
static int s_mmce_loaded  = 0;   /* mmceman ja carregado nesta sessao */

extern "C" int MmceSupportIsEnabled(void)
{
    return s_mmce_enabled;
}

extern "C" void MmceSupportSetEnabled(int enabled)
{
    s_mmce_enabled = enabled ? 1 : 0;
}

extern "C" int MmceLoadEmbeddedIrx(void)
{
    if (!s_mmce_enabled) return -1;   /* desligado: nem tenta */
    if (s_mmce_loaded)   return 0;    /* ja carregado: no-op */

#ifdef HAVE_MMCEMAN
    {
        int ret = EmbeddedIrxLoad(mmceman_irx, sizeof(mmceman_irx), 0, NULL);
        printf("MmceLoad: mmceman = %d\n", ret);
        s_mmce_loaded = 1;
        return 0;
    }
#else
    /* mmceman.irx nao foi embutido neste build (PS2SDK sem o modulo). */
    return -2;
#endif
}

/* ------------------------------------------------------------------------
 * Toggles de dispositivos SEM modulo proprio de carga preguicosa:
 *
 *  - Mass (USB): a stack USB (usbd/bdm/bdmfs_fatfs/usbmass_bd) SEMPRE sobe
 *    no boot -- e' o armazenamento principal e seguro, e gatear isso no
 *    boot mexeria no caminho critico que causava a tela preta.  Este flag
 *    so' controla a LISTAGEM de mass0:/mass1: no browser e a carga do
 *    mx4sio (abaixo).  Padrao LIGADO.
 *  - Host (host:): entrada de dev (PC via ps2link/ps2client); nao carrega
 *    modulo, so' aparece/some do browser.  Padrao DESLIGADO.
 * ------------------------------------------------------------------------ */
static int s_mass_enabled = 1;   /* padrao LIGADO */
static int s_host_enabled = 0;   /* padrao DESLIGADO */

extern "C" int  MassStorageIsEnabled(void)   { return s_mass_enabled; }
extern "C" void MassStorageSetEnabled(int e) { s_mass_enabled = e ? 1 : 0; }
extern "C" int  HostIsEnabled(void)          { return s_host_enabled; }
extern "C" void HostSetEnabled(int e)        { s_host_enabled = e ? 1 : 0; }

/* MX4SIO (cartao SD pela porta de memory card / SIO2) -> aparece como um
 * massN: (block device BDM).  Tirado do boot: carrega DEPOIS da config
 * (chamado em mainloop_init), e so' se o suporte a Mass estiver ligado.
 * Carga unica. */
static int s_mx4sio_loaded = 0;
/* MX4SIO num toggle PROPRIO (separado do Mass/USB), padrao DESLIGADO.  O
 * mx4sio_bd.irx fica sondando o SIO2 atras de um cartao SD; quem nao tem o
 * adaptador nao deve carregar (evita o flood de "Trying to init card" e os
 * "Unhandled SIO mode" do emulador).  USB Mass (usbmass_bd) e' independente. */
static int s_mx4sio_enabled = 0;

extern "C" int  Mx4sioIsEnabled(void)        { return s_mx4sio_enabled; }
extern "C" void Mx4sioSetEnabled(int en)     { s_mx4sio_enabled = en ? 1 : 0; }

extern "C" int Mx4sioLoadIfEnabled(void)
{
    if (!s_mx4sio_enabled) return -1; /* toggle MX4SIO desligado (padrao) */
    if (s_mx4sio_loaded) return 0;    /* ja carregado */

#ifdef HAVE_MX4SIO
    {
        int ret = EmbeddedIrxLoad(mx4sio_bd_irx, sizeof(mx4sio_bd_irx), 0, NULL);
        printf("Mx4sioLoad: mx4sio_bd = %d\n", ret);
        s_mx4sio_loaded = 1;
        return 0;
    }
#else
    return -2;   /* mx4sio_bd.irx nao embutido neste build */
#endif
}

/* Network IRX stack bring-up.
 *
 * Mirrors the order used by hugorsgarcia/PS2SNESticle, picodrive,
 * uLaunchELF, OPL and every other modern PS2 homebrew that uses the
 * PS2SDK netman + ps2ip stack:
 *
 *   1. ps2dev9.irx - DEV9 bus driver, required by SMAP.
 *   2. netman.irx  - link-layer abstraction that sits between the NIC
 *                    driver and the TCP/IP stack.  Must come up before
 *                    smap.irx tries to register, since the modern
 *                    smap binary built into PS2SDK
 *                    ($(PS2SDK)/iop/irx/smap.irx) is the netman-aware
 *                    variant.
 *   3. smap.irx    - Sony Multi-Application Player ethernet driver
 *                    (the Network Adapter NIC).
 *   4. ps2ip.irx   - lwIP TCP/IP stack on the IOP side.  Talks to
 *                    smap through netman.  After this one is up the
 *                    EE can call ps2ipInit() / ps2ip_setconfig() and
 *                    open BSD sockets through <sys/socket.h>.
 *
 * The IRX images themselves are bin2c'd into the ELF (see Makefile
 * rules for EMBED_IRX_NAMES) so we do not need ps2dev9.irx /
 * netman.irx / smap.irx / ps2ip.irx to exist on disk next to the ELF.
 *
 * The EE-side NetManInit() call is NOT done here -- it lives in
 * src/platform/ps2/system/mainloop_net.cpp::_MainLoopInitNetwork
 * between the smap and ps2ip loads, because the caller needs to
 * be able to bail out cleanly if any individual step fails (e.g.
 * no Network Adapter installed on a slim PS2).  This function only
 * owns the IRX bring-up itself; consumers can check the return value
 * to decide whether to continue with ps2ipInit() or to skip network
 * features entirely.
 *
 * Returns 0 on success, or a negative value indicating which IRX
 * failed:
 *   -1 ps2dev9.irx
 *   -2 netman.irx
 *   -3 smap.irx
 *   -4 ps2ip.irx
 *
 * Safe to call multiple times -- subsequent calls return the cached
 * result.
 */
static int s_netif_loaded_result = 1; /* 1 = not yet attempted */

extern "C" int NetIfLoadEmbeddedIrx(void)
{
    int ret;

    if (s_netif_loaded_result != 1) return s_netif_loaded_result;

    ret = EmbeddedIrxLoad(ps2dev9_irx, sizeof(ps2dev9_irx), 0, NULL);
    if (ret < 0)
    {
        printf("NetIfLoadEmbeddedIrx: ps2dev9.irx failed (%d)\n", ret);
        s_netif_loaded_result = -1;
        return s_netif_loaded_result;
    }

    ret = EmbeddedIrxLoad(netman_irx, sizeof(netman_irx), 0, NULL);
    if (ret < 0)
    {
        printf("NetIfLoadEmbeddedIrx: netman.irx failed (%d)\n", ret);
        s_netif_loaded_result = -2;
        return s_netif_loaded_result;
    }

    ret = EmbeddedIrxLoad(smap_irx, sizeof(smap_irx), 0, NULL);
    if (ret < 0)
    {
        printf("NetIfLoadEmbeddedIrx: smap.irx failed (%d)\n", ret);
        s_netif_loaded_result = -3;
        return s_netif_loaded_result;
    }

    ret = EmbeddedIrxLoad(ps2ip_irx, sizeof(ps2ip_irx), 0, NULL);
    if (ret < 0)
    {
        printf("NetIfLoadEmbeddedIrx: ps2ip.irx failed (%d)\n", ret);
        s_netif_loaded_result = -4;
        return s_netif_loaded_result;
    }

    s_netif_loaded_result = 0;
    return 0;
}

/* Pad IRX stack bring-up.
 *
 * Modern PS2SDK-based controller stack.  Previously the project relied
 * on the BIOS-resident \"rom0:XSIO2MAN + rom0:XMTAPMAN + rom0:XPADMAN\"
 * trio loaded from src/platform/ps2/system/mainloop_iop.cpp.  That path
 * works under PCSX2 / NetherSX2 because their emulated rom0: modules
 * tolerate co-existing with the modern sio2man.irx we already load for
 * the memory-card stack.  On a real PS2, however, XSIO2MAN tries to
 * register the same SIO2 RPC services that the modern sio2man has
 * already claimed, so XSIO2MAN loads but its RPC server never becomes
 * usable; XPADMAN then opens but cannot talk to its SIO2 transport,
 * and the controller silently never reports any data -- which is the
 * exact symptom users see (\"menu shows up but controller does not
 * respond on retail PS2 only\").
 *
 * The fix is the same pattern picodrive PS2, OPL, uLaunchELF and
 * hugorsgarcia/PS2SNESticle use: stack the PS2SDK padman.irx
 * (and optional mtapman.irx for multitap) on top of the modern
 * sio2man.irx that the memcard bring-up already loaded.  PS2SDK
 * libpad understands this padman natively and is what the EE-side
 * input layer (src/platform/ps2/input/input.cpp) talks to via
 * padInit / padPortOpen / padRead.
 *
 * Order:
 *   1. padman.irx   - SIO2-based controller manager.  REQUIRED.
 *                     sio2man.irx must already be loaded (by
 *                     MemCardLoadEmbeddedIrx).
 *   2. mtapman.irx  - multitap manager.  OPTIONAL.  Lets the player
 *                     plug 3-5 controllers via a multitap on port 1/2.
 *                     If it fails to load we still keep going with
 *                     only the two physical pad slots.
 *
 * Returns 0 on success.  Returns -1 if padman failed (fatal -- caller
 * should not call padInit / InputInit).  mtapman failures map to
 * still-success because they only disable multitap, not the base pad.
 * Safe to call multiple times -- subsequent calls return the cached
 * result.
 */
static int s_pad_loaded_result = 1; /* 1 = not yet attempted */

extern "C" int PadLoadEmbeddedIrx(void)
{
    int ret;

    if (s_pad_loaded_result != 1) return s_pad_loaded_result;

    ret = EmbeddedIrxLoad(padman_irx, sizeof(padman_irx), 0, NULL);
    if (ret < 0)
    {
        printf("PadLoadEmbeddedIrx: padman.irx failed (%d)\n", ret);
        s_pad_loaded_result = -1;
        return s_pad_loaded_result;
    }

    /* mtapman is best-effort. Failure here just means no multitap
       support; the base two-controller path keeps working. */
    ret = EmbeddedIrxLoad(mtapman_irx, sizeof(mtapman_irx), 0, NULL);
    if (ret < 0)
    {
        printf("PadLoadEmbeddedIrx: mtapman.irx failed (%d) "
               "- multitap disabled, base pads still OK\n", ret);
    }

    s_pad_loaded_result = 0;
    return 0;
}
