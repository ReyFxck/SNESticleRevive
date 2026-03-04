
#include <stdio.h>
#include <string.h>
#include <debug.h>
#include <ctype.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <kernel.h>
#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <iopheap.h>
#include <iopcontrol.h>

#include "types.h"
#include "console.h"
#include "mainloop.h"

extern "C" {
#include "excepHandler.h"
#include "cd.h"
#include "hw.h"
};



const char *updateloader = "rom0:UDNL ";
const char *eeloadcnf = "rom0:EELOADCNF";

static char *_Main_pBootPath;
static char _Main_BootDir[256];


char *MainGetBootDir()
{
	return _Main_BootDir;
}

char *MainGetBootPath()
{
	return _Main_pBootPath;
}

void MainSetBootDir(const char *pPath)
{
  if (!pPath) { _Main_BootDir[0] = 0; return; }

  // copia com limite
  strncpy(_Main_BootDir, pPath, sizeof(_Main_BootDir)-1);
  _Main_BootDir[sizeof(_Main_BootDir)-1] = 0;

  // remove ;1 (cdrom) se existir
  char *semi = strchr(_Main_BootDir, ';');
  if (semi) *semi = 0;

  // procura ultima barra / ou barra invertida
  char *s1 = strrchr(_Main_BootDir, '/');
  char *s2 = strrchr(_Main_BootDir, '\\');
  char *sep = s1;
  if (s2 && (!sep || s2 > sep)) sep = s2;

  if (sep) {
    *(sep+1) = 0; // mantém a barra
    return;
  }

  // sem barra: vira "device:"
  char *col = strchr(_Main_BootDir, ':');
  if (col) *(col+1) = 0;
}

/* Reset the IOP and all of its subsystems.  */
int full_reset()
{
	char imgcmd[64];
	int fd;


	/* The CDVD must be initialized here (before shutdown) or else the PS2
	   could hang on reboot.  I'm not sure why this happens.  */
	if (cdvdInit(CDVD_INIT_NOWAIT) < 0)
		return -1;

	/* Here we detect which IOP image we want to reset with.  Older Japanese
	   models don't have EELOADCNF, so we fall back on the default image
	   if necessary.  */
	*imgcmd = '\0';

	if ((fd = fioOpen(eeloadcnf, O_RDONLY)) >= 0) {
		fioClose(fd);

		strcpy(imgcmd, updateloader);
		strcat(imgcmd, eeloadcnf);
	}
//	scr_printf("rebooting with imgcmd '%s'\n", *imgcmd ? imgcmd : "(null)");

	if (cdvdInit(CDVD_EXIT) < 0)
		return -1;

//	scr_printf("Shutting down subsystems.\n");

	cdvdExit();
	fioExit();
	SifExitIopHeap();
	SifLoadFileExit();
	SifExitRpc();

	SifIopReset(imgcmd, 0);
	while (!SifIopSync()) ;

	SifInitRpc(0);
	FlushCache(0);

	// initialize cdvd
//    cdvdInit(CDVD_INIT_NOWAIT);

	return 0;
}










static void MainDebugLogToFile(const char *msg)
{
    if (!msg) return;

    // tenta host: (HostFS do emulador), depois mc0:
    int fd = fioOpen("host:SNESLOG.TXT", O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0)
        fd = fioOpen("mc0:/SNESLOG.TXT", O_WRONLY | O_CREAT | O_APPEND);

    if (fd >= 0) {
        fioWrite(fd, msg, (int)strlen(msg));
        fioClose(fd);
    }
}

/* ---------------- IRX LOADER ----------------
   Carrega IRX do mesmo diretorio do ELF (MainGetBootDir()).
   Use USE_COMPAT_NET=1 se quiser stack antiga (ps2ip/ps2ips/smap-ps2ip). */

#ifndef USE_COMPAT_NET
#define USE_COMPAT_NET 0
#endif

static int g_IopServicesReady = 0;

static void MainEnsureIopServicesReady(void)
{
    if (g_IopServicesReady) return;

    SifInitRpc(0);
    SifLoadFileInit();
    SifInitIopHeap();
    fioInit();

    g_IopServicesReady = 1;
}

static void _str_to_lower(char *dst, int cap, const char *src)
{
    int i = 0;
    if (cap <= 0) return;
    for (; i < cap-1 && src[i]; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static void _str_to_upper(char *dst, int cap, const char *src)
{
    int i = 0;
    if (cap <= 0) return;
    for (; i < cap-1 && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = 0;
}

static int MainExecModuleFile(const char *path)
{
    int mod_res = 0;
    int ret = SifExecModuleFile(path, 0, NULL, &mod_res);
    if (ret < 0) {
        printf("[IRX] FAIL %s (ret=%d)\n", path, ret);
    } else {
        printf("[IRX] OK   %s (id=%d, start=%d)\n", path, ret, mod_res);
    }
    return ret;
}

static int MainLoadIrxFromBootDirOne(const char *name)
{
    const char *dir = MainGetBootDir();
    char path[512], low[256], up[256];

    if (!dir || !*dir) return -1;

    // helper: tenta sem ;1 e com ;1
    auto try_exec = [&](const char *p)->int {
        if (MainExecModuleFile(p) >= 0) return 0;
        char p1[520];
        snprintf(p1, sizeof(p1), "%s;1", p);
        if (MainExecModuleFile(p1) >= 0) return 0;
        return -1;
    };

    // 1) como veio
    snprintf(path, sizeof(path), "%s%s", dir, name);
    if (try_exec(path) == 0) return 0;

    // 2) lower
    _str_to_lower(low, sizeof(low), name);
    if (low[0]) {
        snprintf(path, sizeof(path), "%s%s", dir, low);
        if (try_exec(path) == 0) return 0;
    }

    // 3) UPPER
    _str_to_upper(up, sizeof(up), name);
    if (up[0]) {
        snprintf(path, sizeof(path), "%s%s", dir, up);
        if (try_exec(path) == 0) return 0;
    }

    return -1;
}

static void MainLoadIrxFromBootDir(void)
{
    const char *dir = MainGetBootDir();

    MainEnsureIopServicesReady();
    printf("[IRX] BootDir: %s\n", dir ? dir : "(null)");

    // base / util
    MainLoadIrxFromBootDirOne("ioptrap.irx");
    MainLoadIrxFromBootDirOne("poweroff.irx");

    // pad (opcional, mas ajuda)
    MainLoadIrxFromBootDirOne("sio2man.irx");
    MainLoadIrxFromBootDirOne("padman.irx");

    // memory card
    MainLoadIrxFromBootDirOne("mcman.irx");
    MainLoadIrxFromBootDirOne("mcserv.irx");

#if USE_COMPAT_NET
    // stack antiga
    MainLoadIrxFromBootDirOne("ps2ip.irx");
    MainLoadIrxFromBootDirOne("ps2ips.irx");
    MainLoadIrxFromBootDirOne("smap-ps2ip.irx");
#else
    // stack netman (moderna)
    MainLoadIrxFromBootDirOne("ps2dev9.irx");
    MainLoadIrxFromBootDirOne("netman.irx");
    MainLoadIrxFromBootDirOne("ps2ip-nm.irx");
    MainLoadIrxFromBootDirOne("smap.irx");
#endif

    // custom (do Wolf3s)
    MainLoadIrxFromBootDirOne("NETPLAY.IRX");
    MainLoadIrxFromBootDirOne("MCSAVE.IRX");
    MainLoadIrxFromBootDirOne("SJPCM2.IRX");

    FlushCache(0);
}
/* -------------- /IRX LOADER -------------- */


/* Your program's main entry point */
int main(int argc, char **argv) 
{
          init_scr();
  scr_printf("\n\n*** STAGE 1 ***\n");
  scr_printf("Entrou no main\n");
  scr_printf("argv0=%s\n", (argc>0 && argv && argv[0]) ? argv[0] : "(null)");

  scr_printf("before MainSetBootDir\n");
  if (argc > 0 && argv && argv[0])
    MainSetBootDir(argv[0]);
  else
    MainSetBootDir("host:");
  scr_printf("after MainSetBootDir\n");
  scr_printf("bootdir=%s\n", MainGetBootDir() ? MainGetBootDir() : "(null)");

  scr_printf("\n*** STAGE 2 ***\n");
  scr_printf("before ConInit\n");
  ConInit();

  init_scr();
  scr_printf("\n\n*** STAGE 1 RECAP ***\n");
  scr_printf("Entrou no main\n");
  scr_printf("argv0=%s\n", (argc>0 && argv && argv[0]) ? argv[0] : "(null)");
  scr_printf("after MainSetBootDir\n");
  scr_printf("bootdir=%s\n", MainGetBootDir() ? MainGetBootDir() : "(null)");

  scr_printf("\n*** STAGE 2 POST-CONINIT ***\n");
  scr_printf("after ConInit\n");

  scr_printf("\n*** STAGE 3 ***\n");
  SifInitRpc(0);
  SifLoadFileInit();
  fioInit();

  char p1[512];
  snprintf(p1, sizeof(p1), "%sioptrap.irx", MainGetBootDir() ? MainGetBootDir() : "");
  int fd = fioOpen(p1, O_RDONLY);
  scr_printf("fioOpen(bootdir+ioptrap)=%d\n", fd);
  if (fd >= 0) fioClose(fd);

  fd = fioOpen("host:ioptrap.irx", O_RDONLY);
  scr_printf("fioOpen(host:ioptrap)=%d\n", fd);
  if (fd >= 0) fioClose(fd);

  scr_printf("try load ioptrap...\n");
  int r = MainLoadIrxFromBootDirOne("ioptrap.irx");
  scr_printf("load ioptrap ret=%d\n", r);

  scr_printf("try load poweroff...\n");
  r = MainLoadIrxFromBootDirOne("poweroff.irx");
  scr_printf("load poweroff ret=%d\n", r);
  scr_printf("\n*** STAGE 4 (ROM0 + MC) ***\n");

  int ret;
  ret = SifLoadModule("rom0:XSIO2MAN", 0, NULL);
  if (ret < 0) ret = SifLoadModule("rom0:SIO2MAN", 0, NULL);
  scr_printf("SIO2MAN ret=%d\n", ret);

  ret = SifLoadModule("rom0:XPADMAN", 0, NULL);
  if (ret < 0) ret = SifLoadModule("rom0:PADMAN", 0, NULL);
  scr_printf("PADMAN ret=%d\n", ret);

  ret = SifLoadModule("rom0:XMCMAN", 0, NULL);
  if (ret < 0) ret = SifLoadModule("rom0:MCMAN", 0, NULL);
  scr_printf("MCMAN ret=%d\n", ret);

  ret = SifLoadModule("rom0:XMCSERV", 0, NULL);
  if (ret < 0) ret = SifLoadModule("rom0:MCSERV", 0, NULL);
  scr_printf("MCSERV ret=%d\n", ret);

  int dh = fioDopen("mc0:/");
  scr_printf("fioDopen(mc0:/)=%d\n", dh);
  if (dh >= 0) fioDclose(dh);


  for (;;) { SleepThread(); }

  // --- file log probe ---
  SifInitRpc(0);
  SifLoadFileInit();
  fioInit();
  MainDebugLogToFile("SNESLOG: entered main\n");
  // trava aqui pra garantir que o log foi escrito
  while (1) { SleepThread(); }
  // --- /file log probe ---

  init_scr();
  scr_printf("\n\n\n*** PROBE: ENTROU NO MAIN ***\n");
  scr_printf("Se voce esta vendo isso, o ELF iniciou.\n");
  for (;;) { }

    int iArg;
//    init_scr();

	if (argc>=1)
	{
		_Main_pBootPath = argv[0];
	}

	MainSetBootDir(_Main_pBootPath);

	SifInitRpc(0);

	if (_Main_pBootPath[0]=='m' && _Main_pBootPath[1]=='c')
	{
//		installExceptionHandlers();

		// reset if loaded from memory card
		full_reset();
	}

	// initialize cdvd
    cdvdInit(CDVD_INIT_NOWAIT);

    for (iArg=0; iArg < argc; iArg++)
    {
        printf("%d: %s\n", iArg, argv[iArg]);
    }

	DmaReset();

    install_VRstart_handler();

	ConInit();

  scr_printf("before IRX load\n");
  MainLoadIrxFromBootDir();
  scr_printf("after IRX load\n");


	if (MainLoopInit())
	{
		// do stuff here
		while (MainLoopProcess())
		{
		}

		MainLoopShutdown();
	}

	ConShutdown();

	return 0;
}

