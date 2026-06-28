#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>

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
#ifdef HAVE_MMCEMAN
#include "mmceman_irx.h"
#endif

/* Log visivel no splash de boot (real hardware) -- definido em audio_audsrv.c. */
extern "C" void ScrPrintf(const char *pFormat, ...);

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
    if (ret < 0)
    {
        printf("MemCardLoadEmbeddedIrx: sio2man.irx failed (%d)\n", ret);
        /* MEMCARD_INIT_STATUS_DEPENDENCY_IRX_ERROR */
        return -4;
    }

    ret = EmbeddedIrxLoad(mcman_irx, sizeof(mcman_irx), 0, NULL);
    if (ret < 0)
    {
        printf("MemCardLoadEmbeddedIrx: mcman.irx failed (%d)\n", ret);
        /* MEMCARD_INIT_STATUS_MCMAN_IRX_ERROR */
        return -2;
    }

    ret = EmbeddedIrxLoad(mcserv_irx, sizeof(mcserv_irx), 0, NULL);
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
    ScrPrintf("usbd.irx = %d\n", ret);
    if (ret < 0) { printf("UsbBdm: usbd.irx failed (%d)\n", ret); return -1; }

    ret = EmbeddedIrxLoad(bdm_irx, sizeof(bdm_irx), 0, NULL);
    ScrPrintf("bdm.irx = %d\n", ret);
    if (ret < 0) { printf("UsbBdm: bdm.irx failed (%d)\n", ret); return -2; }

    ret = EmbeddedIrxLoad(bdmfs_fatfs_irx, sizeof(bdmfs_fatfs_irx), 0, NULL);
    ScrPrintf("bdmfs_fatfs.irx = %d\n", ret);
    if (ret < 0) { printf("UsbBdm: bdmfs_fatfs.irx failed (%d)\n", ret); return -3; }

    ret = EmbeddedIrxLoad(usbmass_bd_irx, sizeof(usbmass_bd_irx), 0, NULL);
    ScrPrintf("usbmass_bd.irx = %d\n", ret);
    if (ret < 0) { printf("UsbBdm: usbmass_bd.irx failed (%d)\n", ret); return -4; }

    /* HD INTERNO formato APA (igual HDD-OSD/OPL): dev9 (barramento) +
       ps2atad (ATA) + ps2hdd (expoe hdd0:).  BEST-EFFORT: em console SEM
       HD interno os modulos so' nao acham hardware -- a gente loga e SEGUE
       (nao aborta o USB, nao trava o boot).  Carregar os modulos NAO
       bloqueia; o que travava era o waitUntilDeviceIsReady() do
       ps2_drivers, que NAO usamos aqui. */
    ret = EmbeddedIrxLoad(ps2dev9_irx, sizeof(ps2dev9_irx), 0, NULL);
    ScrPrintf("dev9 = %d\n", ret);
    ret = EmbeddedIrxLoad(ps2atad_irx, sizeof(ps2atad_irx), 0, NULL);
    ScrPrintf("atad = %d\n", ret);
    ret = EmbeddedIrxLoad(ps2hdd_irx, sizeof(ps2hdd_irx), 0, NULL);
    ScrPrintf("hdd (hdd0:) = %d\n", ret);

#ifdef HAVE_MMCEMAN
    /* Memory cards modificados (MemCard PRO2 / SD2PSX) -> mmce0:/mmce1:.
       So' embutido se o PS2SDK tiver mmceman.irx (ver Makefile). */
    ret = EmbeddedIrxLoad(mmceman_irx, sizeof(mmceman_irx), 0, NULL);
    ScrPrintf("mmceman (mmce0/1) = %d\n", ret);
#endif

    return 0;
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
