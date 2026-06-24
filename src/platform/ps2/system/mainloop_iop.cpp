#include "mainloop_net.h"
#include "mainloop_load.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "boot_status.h"

#ifndef DEBUG_BOOT_SCREEN
#define DEBUG_BOOT_SCREEN 0
#endif

/* BOOTLOG: route through DLog (defined in modules/sjpcm/sjpcm_rpc.c).
   Plain EE printf never reaches PCSX2/NetherSX2's emulator log in this
   build (the libc->SIF->IOP stdout wiring is broken), but the IOP-side
   loadmodule lines do print.  DLog writes to the EE SIO TX FIFO which
   the emulator captures on the EE_SIO channel, so by funneling BOOTLOG
   through it the boot sequence interleaves correctly with the IOP
   "loadmodule:" / "audsrv:" / "cdfs:" lines. */
extern "C" void DLog(const char *fmt, ...);
#define BOOTLOG(...) DLog(__VA_ARGS__)
#define MENU_STARTDIR ""
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <iopheap.h>
#include <libpad.h>
#include "libxpad.h"
#include "libxmtap.h"
#include <libmc.h>
#include <kernel.h>
#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "types.h"
#include "mainloop.h"
#include "console.h"
#include "input.h"
#include "snes.h"
#include "rendersurface.h"
#include "file.h"
#include "dataio.h"
#include "prof.h"
#include "bmpfile.h"
#if 0
#include "font.h"
#else
#include "font.h"
#endif
#include "poly.h"
#include "texture.h"
#include "mixbuffer.h"
#include "wavfile.h"
#include "snstate.h"
#include "sjpcmbuffer.h"
#include "memcard.h"

#include "pathext.h"
#include "snppucolor.h"
#if 0
#include "version.h"
#endif
#include "emumovie.h"
extern "C" {
#include "cd.h"
#include "ps2dma.h"
#include "sncpu_c.h"
#include "snspc_c.h"
};

//#include "nespal.h"
#include "snes.h"
//#include "nesstate.h"

#include <sifrpc.h>
#include <loadfile.h>

extern "C" {
#include "ps2ip.h"
#include "netplay_ee.h"
#include "mcsave_ee.h"
};

extern "C" {
#include "hw.h"
#include "gs.h"
#include "gpfifo.h"
#include "gpprim.h"
};

extern "C" {
#include "titleman.h"
};

extern "C" {
#include "sjpcm.h"
};

#include "embedded_irx.h"

extern "C" Int32 SNCPUExecute_ASM(SNCpuT *pCpu);


/* MAINLOOP_MEMCARD / NETPORT / STATEPATH / SNESSTATEDEBUG /
   NESSTATEDEBUG / HISTORY / MAXSRAMSIZE now live in
   mainloop_shared.h (already included above). */

#include "uiBrowser.h"
#include "uiNetwork.h"
#include "uiMenu.h"
#include "uiLog.h"
#include "emurom.h"

#include "mainloop_iop.h"
#include "mainloop_ui.h"

static int _LoadMcModule(const char *path, int argc, const char *argv)
{
	void *iop_mem;
	int ret;
	FILE *fp;
	int size;
	struct stat st;

	/* Sized stat() works on every iomanX-registered device, including
	   mc0:/ once init_memcard_driver has run (see app/main.cpp). The
	   previous fioOpen + fioLseek(SEEK_END) round-trip was the legacy
	   rom0:FILEIO path that this refactor replaces. */
	if (stat(path, &st) < 0)
	{
		return -1;
	}
	size = (int)st.st_size;

	/* Verify the path is actually readable before we go reserve IOP
	   heap. fopen() will route to mc0: / cdfs: / mass: etc. through
	   iomanX once init_ps2_filesystem_driver has run. */
	fp = fopen(path, "rb");
	if (!fp)
	{
		return -1;
	}
	fclose(fp);

	printf("LoadMcModule %s (%d)\n", path, size);
	iop_mem = SifAllocIopHeap(size);
	if (iop_mem == NULL) {
		return -2;
	}
	ret = SifLoadIopHeap(path, iop_mem);
	ret=0;
	if (ret < 0) {
		SifFreeIopHeap(iop_mem);
		return -3;
	}

	printf("SifLoadModuleBuffer %08X\n",(Uint32)iop_mem);
	ret = SifLoadModuleBuffer(iop_mem, argc, argv);
	printf("SifLoadModuleBuffer %d\n",ret);
	SifFreeIopHeap(iop_mem);
	return ret;
}

Int32 IOPLoadModule(const Char *pModuleName, Char **ppSearchPaths, int arglen, const char *pArgs)
{
	int ret = -1;
	char ModulePath[256];

	/* If we have a copy of this module embedded in the ELF, prefer it
	   unconditionally. The host:/cdrom: search paths used by ppSearchPaths
	   are not usable on emulators (NetherSX2 etc.) or on a stripped-down
	   PS2, so falling through to SifLoadModule there is guaranteed to
	   fail with -203 ("module not found"). */
	{
		const unsigned char *embed_data = NULL;
		unsigned int         embed_size = 0;

		if (EmbeddedIrxFind(pModuleName, &embed_data, &embed_size) == 0)
		{
			ret = EmbeddedIrxLoad(embed_data, embed_size, arglen, pArgs);
			if (ret >= 0)
			{
				ScrPrintf("IOP Load (embed): %s\n", pModuleName);
				return ret;
			}
			/* fall through to disk-based load if the embedded copy
			   somehow refused to start. */
		}
	}

	if (ppSearchPaths)
	{
		// iterate through search paths
		while (*ppSearchPaths)
		{
			if (strlen(*ppSearchPaths) > 0)
			{
				strcpy(ModulePath, *ppSearchPaths);
				strcat(ModulePath, pModuleName);
				if (ModulePath[0] == 'm' && ModulePath[1]=='c')
				{
					ret = _LoadMcModule(ModulePath, arglen, pArgs);
				} else
				{
					ret = SifLoadModule(ModulePath, arglen, pArgs);
				}

				if (ret >= 0)
				{
					// success!
					break;
				}
			}

			ppSearchPaths++;
		}
	} else
	{
		strcpy(ModulePath, pModuleName);
		ret = SifLoadModule(ModulePath, arglen, pArgs);
	}


	if (ret >= 0)
	{
		// success!
		ScrPrintf("IOP Load: %s\n", ModulePath);
		return ret;
	} else
	{
		ScrPrintf("IOP Fail: %s %d\n", pModuleName, ret);
		printf("IOP: Failed to load module '%s'\n", pModuleName);

		// module not loaded
		return -1;
	}
}

Char _MainLoop_BootDir[256];

Char *_MainLoop_IOPModulePaths[]=
{
	_MainLoop_BootDir,
	(char *)"host:",
	(char *)"cdrom:\\",
	(char *)"rom0:",
	NULL
};

Bool _MainLoop_bSjPCMReady = FALSE;
Bool _MainLoop_bMCSaveReady = FALSE;

void _MainLoopLoadModules(Char **ppSearchPaths)
{
	Bool bLoadedNetwork;

	#if 0
	if (!EEPuts_Init())
	{
		EEPuts_SetCallback(_MainLoop_Puts);
		IOPLoadModule("EEPUTS.IRX", ppSearchPaths, 0, NULL);
	}
	#endif

	/* SIO2MAN + MCMAN + MCSERV are loaded by MemCardLoadEmbeddedIrx()
	   in app/main.cpp before we ever get here. The modern PS2SDK
	   copies register with iomanX so newlib stdio fopen("mc0:/...")
	   routes through them. */
	/* Controller bring-up.
	 *
	 * The PS2SDK padman.irx is embedded in the ELF and stacked on top
	 * of the modern sio2man.irx that MemCardLoadEmbeddedIrx() already
	 * loaded -- the SAME, single sio2man serves both the memory card
	 * and the pad.  This is exactly the recipe Open-PS2-Loader uses
	 * (src/system.c loads embedded sio2man -> mcman -> mcserv ->
	 * padman, then src/opl.c calls padInit(0); no mtapman, no
	 * mtapInit), and OPL boots the controller on every retail PS2.
	 *
	 * An earlier revision switched this to rom0:PADMAN, but the OPL /
	 * picodrive evidence shows the embedded path is correct on real
	 * hardware -- the dead-controller symptom traced to the EE-side
	 * analog handling (see input.cpp: the left stick was read even on
	 * a digital pad, injecting a phantom UP+LEFT), not to the module
	 * source.  rom0:PADMAN is kept only as a last-resort fallback for
	 * the rare IOP image whose embedded padman refuses to start.
	 * Every step is mirrored to the on-screen log via ScrPrintf so it
	 * can be diagnosed on a real console without an EE SIO cable. */
	{
		int pad_ok = 0;

		// BOOTLOG("[boot] pad: load embedded padman.irx\n");
		ScrPrintf("PAD: loading embedded padman.irx\n");
		if (PadLoadEmbeddedIrx() == 0)
		{
			ScrPrintf("PAD: embedded padman.irx loaded\n");
			pad_ok = 1;
		}
		else
		{
			ScrPrintf("PAD: embedded padman.irx FAILED -> rom0:PADMAN\n");
			if (IOPLoadModule("rom0:PADMAN", NULL, 0, NULL) >= 0)
			{
				ScrPrintf("PAD: rom0:PADMAN loaded\n");
				pad_ok = 1;
			}
			else
			{
				ScrPrintf("PAD: rom0:PADMAN FAILED\n");
			}
		}

		if (pad_ok)
		{
			int pi = padInit(0);
			// BOOTLOG("[boot] padInit=%d (expect 1)\n", pi);
			ScrPrintf("PAD: padInit=%d (expect 1)\n", pi);
			if (pi == 1)
			{
				InputInit(FALSE);
				ScrPrintf("PAD: InputInit done\n");
				// BOOTLOG("[boot] padInit/InputInit done\n");
			}
			else
			{
				ScrPrintf("PAD: padInit FAILED - controller unavailable\n");
				// BOOTLOG("[boot] padInit failed -- controller unavailable\n");
			}
		}
		else
		{
			ScrPrintf("PAD: no controller module - controller unavailable\n");
			// BOOTLOG("[boot] no controller module loaded\n");
		}
	}

	/* libmc finalise. mcInit() picks up whichever MCMAN/MCSERV pair
	   is currently loaded - the modern PS2DEV ones registered by
	   init_memcard_driver() in main.cpp are detected automatically. */
	// BOOTLOG("[boot] MemCardInit (ps2_drivers mcman/mcserv)\n");
	MemCardInit();
	// BOOTLOG("[boot] MemCardInit done\n");
	#if MAINLOOP_MEMCARD
	MemCardCreateSave(_SramPath, _MainLoop_SaveTitle, TRUE);
	#endif

	// BOOTLOG("[boot] InitNetwork: enter\n");
	bLoadedNetwork = _MainLoopInitNetwork(ppSearchPaths);
	// BOOTLOG("[boot] InitNetwork: leave (loaded=%d)\n", (int)bLoadedNetwork);

	// configure network if we started it ourselves
	if (bLoadedNetwork)
	{
		_MainLoopConfigureNetwork(_MainLoop_NetConfigPaths, (char *)"ipconfig.dat");
	}

	/* Initialize the EE-side netplay protocol when the network IRX
	   stack came up.  The protocol used to live in NETPLAY.IRX
	   (a custom iaddis IOP module) and was driven from the EE via
	   SifRpcCall; that IRX has been retired in favour of running the
	   whole NetServer / NetClient state machine on the EE itself,
	   talking straight to lwIP through PS2SDK's <sys/socket.h>
	   shims.  See src/modules/netplay/netplay_ee.c and
	   src/modules/netplay/protocol/, mirrored from
	   hugorsgarcia/PS2SNESticle/SNESticle/Modules/netplay/Source.
	   No IRX load is required here. */
	if (bLoadedNetwork)
	{
		NetPlayInit((void *)_MainLoopNetCallback);
	}

	/* The custom CDVD.IRX (and CDVD_Init / CDVD_FlushCache RPC) was
	   the iaddis project's legacy cdfs replacement. It is no longer
	   loaded here because init_ps2_filesystem_driver() in app/main.cpp
	   has already brought up the modern cdfs.irx, which registers the
	   "cdfs:" device with iomanX. The browser and ROM loader now
	   reach the disc through plain newlib stdio (opendir("cdfs:/"),
	   fopen("cdfs:/ROMS/foo.sfc", "rb"), ...) instead of the bespoke
	   RPC. CDVD_FlushCache call-sites have been replaced with no-ops
	   or fileXioSync()/cdfs_FlushCache() where appropriate. */

	/* Audio: load audsrv.irx (modern PS2DEV audio service, replaces
	   the legacy SjPCM stack). audsrv.irx depends on the SPU2 driver
	   (sceSd*), which is provided either by rom0:LIBSD (retail BIOS)
	   or by PS2SDK's freesd.irx (embedded here from
	   $(PS2SDK)/iop/irx/freesd.irx).

	   Prefer FREESD over rom0:LIBSD.  A real PS2 retail user (Adriano)
	   reported boot hangs right after audsrv.irx loaded successfully,
	   which traced to audsrv_init() blocking inside SifBindRpc while
	   waiting for audsrv to register its RPC server.  That registration
	   happens at the very end of audsrv's _start() in IOP code, after
	   sceSdInit() returns; on some retail BIOS revisions sceSdInit()
	   hangs or never returns when called from rom0:LIBSD, so audsrv's
	   _start() never reaches SifRegisterRpc and the EE waits forever.
	   freesd.irx is PS2SDK-vetted, deterministic across BIOS revisions,
	   and is what OPL / uLaunchELF / picodrive PS2 use in production.

	   Keep rom0:LIBSD as a last-resort fallback (for the rare case
	   where freesd somehow refuses to load -- in practice the embedded
	   copy always loads). */
	// BOOTLOG("[boot] FREESD/LIBSD: try load\n");
	ScrPrintf("FREESD: try load\n");
	if (IOPLoadModule("FREESD.IRX", ppSearchPaths, 0, NULL) < 0)
	{
		ScrPrintf("FREESD failed - falling back to rom0:LIBSD\n");
		if (IOPLoadModule("rom0:LIBSD", NULL, 0, NULL) < 0)
		{
			ScrPrintf("FREESD/LIBSD: both failed - audio will be silent\n");
			// BOOTLOG("[boot] FREESD/LIBSD: both failed - audio will be silent\n");
		}
	}
	// BOOTLOG("[boot] FREESD/LIBSD done\n");

	// BOOTLOG("[boot] AUDSRV.IRX: try load\n");
	if (IOPLoadModule("AUDSRV.IRX", ppSearchPaths, 0, NULL) >= 0)
	{
		/* Mirror to screen so the user can see progression even when
		   SIO is not connected.  If the next ScrPrintf does not appear,
		   the hang is inside SjPCM_Init -> audsrv_init -> SifBindRpc
		   (audsrv RPC server never registered, likely sceSd init bug
		   in whichever SPU2 driver we ended up with). */
		ScrPrintf("AUDSRV: SjPCM_Init starting...\n");
		// BOOTLOG("[boot] SjPCM_Init() (audsrv backend)\n");
		if (SjPCM_Init(0, 960*25, SJPCMMIXBUFFER_MAXENQUEUE) >= 0)
		{
			_MainLoop_bSjPCMReady = TRUE;
			// BOOTLOG("[boot] SjPCM_Init done\n");
		}
		else
		{
			// BOOTLOG("[boot] SjPCM_Init failed\n");
		}
	}
	else
	{
		// BOOTLOG("[boot] AUDSRV.IRX skipped (not available)\n");
		ScrPrintf("AUDSRV: skipped (no IRX)\n");
	}

	/* The iaddis-era custom MCSAVE.IRX (async memory-card writer) is
	   no longer loaded.  The IRX is not shipped alongside the ELF or
	   embedded in it, so this used to print "IOP Fail: MCSAVE.IRX
	   -203" on every boot of an ISO/emulator setup where the file is
	   absent.  We embed the modern mcman.irx / mcserv.irx stack
	   directly into the ELF (see embedded_irx.cpp), and
	   mainloop_state.cpp::MCSave_Write already has a synchronous
	   fallback that goes through newlib stdio
	   (fopen("mc0:.../filename","wb")/fwrite/fclose) which routes
	   through iomanX onto mcman/mcserv.  Real-PS2 boots take the
	   same sync path as emulator boots now, which removes the IRX
	   version skew risk completely and matches Hugo's design in
	   hugorsgarcia/PS2SNESticle.  _MainLoop_bMCSaveReady stays FALSE
	   and the sync branch is always taken. */

	if (bLoadedNetwork)
	{
		// try to load ps2link so we can have host i/o back
		IOPLoadModule("PS2LINK.IRX", ppSearchPaths, 0, NULL);
	}
}
