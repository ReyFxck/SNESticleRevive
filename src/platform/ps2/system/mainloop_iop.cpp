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

#include "mainloop_iop.h"
extern void ScrPrintf(const char *fmt, ...);

static int _LoadMcModule(const char *path, int argc, const char *argv)
{
    void *iop_mem;
    int ret;
	int fd;
	int size;

	fd= fioOpen(path, O_RDONLY);
	if (fd < 0)
	{
		return -1;
	}
	size = fioLseek(fd, 0, SEEK_END);
	fioClose(fd);

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
