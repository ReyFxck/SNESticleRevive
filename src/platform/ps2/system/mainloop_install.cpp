#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MENU_STARTDIR ""
#define NEWLIB_PORT_AWARE
#include <fileio.h>
#include <iopheap.h>
#include <libpad.h>
#include "libxpad.h"
#include "libxmtap.h"
#include <libmc.h>
#include <kernel.h>
#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "mainloop_iop.h"
#include "mainloop_net.h"
#include "mainloop_ui.h"
#include "types.h"
#include "vram.h"
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
#include "../../../Gep/Include/ps2/font.h"
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

#include "zlib.h"
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
#include "cdvd_rpc.h"
};

extern "C" Int32 SNCPUExecute_ASM(SNCpuT *pCpu);


#define MAINLOOP_MEMCARD (CODE_RELEASE || 0)

#define MAINLOOP_NETPORT (6113)


#if CODE_RELEASE
#else
#endif


#if CODE_RELEASE
#define MAINLOOP_STATEPATH "host0:"
#else
#define MAINLOOP_STATEPATH "host0:/cygdrive/d/emu/"
#endif

#define MAINLOOP_SNESSTATEDEBUG (CODE_DEBUG && 0)
#define MAINLOOP_NESSTATEDEBUG (CODE_DEBUG && FALSE)
#define MAINLOOP_HISTORY (CODE_DEBUG && 0)
#define MAINLOOP_MAXSRAMSIZE (64 * 1024)

#include "uiBrowser.h"
#include "uiNetwork.h"
#include "uiMenu.h"
#include "uiLog.h"
#include "emurom.h"

#include "mainloop_install.h"

extern CMenuScreen *_MainLoop_pMenuScreen;
extern void MainLoopRender();

int _MainLoopInstallCallback(char *pDestName, char *pSrcName, int Position, int Total)
{
	char str[256];
	sprintf(str, "Copying %d / %d bytes", Position, Total);
	_MainLoop_pMenuScreen->SetText(0, str);
	_MainLoop_pMenuScreen->SetText(1, pSrcName);
	_MainLoop_pMenuScreen->SetText(2, pDestName);
	MainLoopRender();
	return 1;
}

void _DumpMemory()
{
	int fd;
	fd = fioOpen("host:memdump.bin", O_WRONLY | O_CREAT);
	if (fd >= 0)
	{
		fioWrite(fd, (void *)0x100000, 4 * 1024 * 1024);
		fioClose(fd);
	}
}

void _GetExploitDir(char *pStr)
{
	int fd;
	char code = 'A';
	char romver[16];

	// Determine the PS2's region.  
	fd = fioOpen("rom0:ROMVER", O_RDONLY);
	fioRead(fd, romver, sizeof romver);
	fioClose(fd);
	code  = (romver[4] == 'E' ? 'E' : (romver[4] == 'J' ? 'I' : 'A'));

	sprintf(pStr, "B%cDATA-SYSTEM", code);
}

void _AddTitleDB(char *pPath)
{
	FILE *pFile;
	char str[256];

	CDVD_FlushCache();

	pFile = fopen("cdfs:/SYSTEM.CNF", "rt");
//	pFile = fopen("host:/SYSTEM.CNF", "rt");
	if (!pFile)
	{
		MainLoopModalPrintf(60*3, "Unable to open SYSTEM.CNF on cd.");
		return;
	}
	while (fgets(str, sizeof(str), pFile))
	{
		if (!memcmp(str, "BOOT", 4))
		{
			char *pFileStart = strchr(str, '\\');
			char *pFileEnd = strrchr(str, ';');

			if (pFileStart)
			{
				// skip \ in path
				pFileStart+=1;
				if (pFileEnd) *pFileEnd='\0';
				printf("Found '%s'\n", pFileStart);
				fclose(pFile);

				// add to title.db
				if (add_title_db(pPath, pFileStart)==0)
				{
					MainLoopModalPrintf(60*3, "%s added to mc0:title.db", pFileStart);
				} else
				{
					MainLoopModalPrintf(60*3, "Unable to add to %s", pPath);
				}
				fclose(pFile);
				return;
			}
		}
	}
	fclose(pFile);

	MainLoopModalPrintf(60*3, "Unable to find PSX ELF");
}
