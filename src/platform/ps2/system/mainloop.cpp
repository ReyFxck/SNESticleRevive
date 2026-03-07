
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
#include "mainloop_install.h"
#include "mainloop_menu.h"
#include "mainloop_browser.h"
#include "mainloop_load.h"
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

CBrowserScreen *_MainLoop_pBrowserScreen;
CNetworkScreen *_MainLoop_pNetworkScreen;
CMenuScreen *_MainLoop_pMenuScreen;
CLogScreen *_MainLoop_pLogScreen;
CScreen *_MainLoop_pScreen = NULL;

SnesSystem *_pSnes;
SnesRom *_pSnesRom;
#if 0
static NesSystem  *_pNes;
static NesRom	  *_pNesRom;
static NesFDSBios  *_pNesFDSBios;
static NesDisk	  *_pNesFDSDisk;
static Int32 _MainLoop_iDisk=0;
static Bool _MainLoop_bDiskInserted=FALSE;
#endif
Char _RomName[256];

#if MAINLOOP_MEMCARD
Char _SramPath[256] = "mc0:/SNESticle";
static Char  _MainLoop_SaveTitle[] = "SNESticle\nSNESticle";
#else
Char _SramPath[256] = "host0:/cygdrive/d/emu/";
#endif

Emu::System  *_pSystem;

CRenderSurface *_fbTexture[2];
static Uint32 _iframetex=0;

TextureT _OutTex;
#ifdef DEBUG
static CWavFile _WavFile;
#endif

Uint8 _RomData[4 * 1024 * 1024 + 1024] __attribute__((aligned(64))) __attribute__ ((section (".bss")));

SnesStateT		_SnesState;
#if 0
static NesStateT		_NesState;
#endif

Emu::MovieClip *s_pMovieClip;


Uint32 _MainLoop_SRAMChecksum;
Uint32 _MainLoop_SaveCounter = 0;
Uint32 _MainLoop_AutoSaveTime = 8 * 60;
Bool _MainLoop_SRAMUpdated = FALSE;
Bool _bStateSaved = FALSE;
Float32 _MainLoop_fOutputIntensity = 0.8f;


static Uint8 _MainLoop_GfxPipe[0x40000] _ALIGN(128) __attribute__ ((section (".bss")));

//static SJPCMMixBuffer _SJPCMMix(32000, TRUE) _ALIGN(16);
static SJPCMMixBuffer *_SJPCMMix;

//static Char * _pSnesWavFileName = "host0:d:/snesps2.wav";

static Char *_pRomFile = 
//"host:c:/emu/snesrom/mario.smc";
NULL
//"cdfs:\\USA\\SUPER~_U.SMC";
//"cdfs:\\ROMS\\Super Mario World.smc";

// "host:c:/emu/Zombies Ate My Neighbors (U) [!].smc";

// "host:c:/emu/Contra 3.smc";
//"host:c:/emu/Castlevania 4.smc";
//"host:c:/emu/Super Bomberman (U).smc";
//"host:c:/emu/Legend of Zelda, The (U).smc";
//"host:c:/emu/Final Fight (U).smc";

//"cdfs:\\ROMS\\mario.smc";
;


static void _MenuDraw();
 void _MenuEnable(Bool bEnable);
void *_MainLoopNetCallback(NetPlayCallbackE eCallback, char *data, int size);
void MainLoopRender();

static void _MainLoopInputProcess(Uint32);

#if MAINLOOP_HISTORY
void _MainLoopResetHistory();
#endif
void _MainLoopResetInputChecksums();

Bool _bMenu = TRUE;

Char _MainLoop_ModalStr[256];
Int32 _MainLoop_ModalCount=0;

Char _MainLoop_StatusStr[256];
Int32 _MainLoop_StatusCount=0;

static Bool _MainLoop_BlackScreen = FALSE;
static Uint32 _MainLoop_uDebugDisplay = 0;

void _MainLoopSetSampleRate(Uint32 uSampleRate);

extern "C" {
#include "unzip.h"
};

#if 0
static void _MainLoopSetPalette(NesPalE eNesPal)
{
	Color32T BasePal[64];
	Int32 iPal;

	memcpy(BasePal, NesPalGetStockPalette(eNesPal), sizeof(BasePal));
//		NesPalGenerate(BasePal, 334.0f, 0.4f);

	for (iPal=0; iPal < NESPAL_NUMPALETTES; iPal++)
	{
		Color32T Palette[64];

		// get palette
		NesPalComposePalette(iPal, Palette, BasePal, 64);

		// set palette for surface
		_fbTexture[0]->SetPaletteEntries(iPal, Palette, 64);
		_fbTexture[1]->SetPaletteEntries(iPal, Palette, 64);
	}
}
#endif

/* UI_L1R1_CYCLE */
/* UI_CYCLE_L1R1 */
typedef int (*CopyProgressCallBackT)(char *pDestName, char *pSrcName, int Position, int Total);

int InstallFiles(char *pDestPath, char *pSrcPath, char **ppInstallFiles, CopyProgressCallBackT pCallBack);
int CopyFile(char *pDest, char *pSrc, CopyProgressCallBackT pCallBack);

Char _MainLoop_BootDir[256];

static char *_MainLoop_IOPModulePaths[]=
{
	_MainLoop_BootDir,
    (char *)"host:",
    (char *)"cdrom:\\",
    (char *)"rom0:",
    NULL
};


static void _MainLoopLoadModules(Char **ppSearchPaths)
{
	Bool bLoadedNetwork;

	#if 0
	if (!EEPuts_Init())
	{
		EEPuts_SetCallback(_MainLoop_Puts);
		IOPLoadModule("EEPUTS.IRX", ppSearchPaths, 0, NULL);
	}
	#endif

//    IOPLoadModule("rom0:SECRMAN", NULL, 0, NULL);

	if (IOPLoadModule("rom0:XSIO2MAN", NULL, 0, NULL) >= 0)
	{
		// use the X version of the iop libs
	    if (IOPLoadModule("rom0:XMTAPMAN", NULL, 0, NULL) >= 0)
		{
			xmtapInit(0);
			xmtapPortOpen(1,0);
		}
	    if (IOPLoadModule("rom0:XPADMAN", NULL, 0, NULL) >= 0)
	    {
	        xpadInit(0);
			InputInit(TRUE);
	    }

	    IOPLoadModule("rom0:XMCMAN", NULL, 0, NULL);
	    if (IOPLoadModule("rom0:XMCSERV", NULL, 0, NULL) >= 0)
		{
			MemCardInit();
			#if MAINLOOP_MEMCARD
			MemCardCreateSave(_SramPath, _MainLoop_SaveTitle, TRUE);
			#endif
		}
	} else
	{
		// use the regular versions
	    IOPLoadModule("rom0:SIO2MAN", NULL, 0, NULL);
	    if (IOPLoadModule("rom0:PADMAN", NULL, 0, NULL) >= 0)
	    {
	        padInit(0);
			InputInit(FALSE);
	    }

	    IOPLoadModule("rom0:MCMAN", NULL, 0, NULL);
	    if (IOPLoadModule("rom0:MCSERV", NULL, 0, NULL) >= 0)
		{
			MemCardInit();
			#if MAINLOOP_MEMCARD
			MemCardCreateSave(_SramPath, _MainLoop_SaveTitle, TRUE);
			#endif
		}
	}

	bLoadedNetwork = _MainLoopInitNetwork(ppSearchPaths);

	// configure network if we started it ourselves
	if (bLoadedNetwork)
	{
		_MainLoopConfigureNetwork(_MainLoop_NetConfigPaths, (char *)"ipconfig.dat");
	}

	// load netplay module
    if (IOPLoadModule("NETPLAY.IRX", ppSearchPaths, 0, NULL) >= 0)
    {
        NetPlayInit((void *)_MainLoopNetCallback);
    }

    if (IOPLoadModule("CDVD.IRX", ppSearchPaths, 0, NULL) >= 0)
    {
        printf("CDVD_Init()\n");
        CDVD_Init();
    }

	if (IOPLoadModule("rom0:LIBSD", NULL, 0, NULL) < 0)
	{
    	IOPLoadModule("LIBSD.IRX", ppSearchPaths, 0, NULL);
	}

    if (IOPLoadModule("SJPCM2.IRX", ppSearchPaths, 0, NULL) >= 0)
    {
        printf("SjPCM_Init()\n");
	    if(SjPCM_Init(0, 960*25, SJPCMMIXBUFFER_MAXENQUEUE) < 0) printf("Could not initialize SjPCM\n");

    //    SjPCM_Setvol(0x3FF);
    //    SjPCM_Setvol(0);
    }

	#if 1
    if (IOPLoadModule("MCSAVE.IRX", ppSearchPaths, 0, NULL) >= 0)
    {
        printf("MCSave_Init()\n");
        MCSave_Init(MAINLOOP_MAXSRAMSIZE);
    }
	#endif

	if (bLoadedNetwork)
	{
		// try to load ps2link so we can have host i/o back
	    IOPLoadModule("PS2LINK.IRX", ppSearchPaths, 0, NULL);
	}
}

int CopyFile(char *pDest, char *pSrc, CopyProgressCallBackT pCallBack)
{
	Uint8	Buffer[32*1024];
	int fdSrc, fdDest;
	int nTotalBytes=0;
	int nBytes;
	int nSrcSize;

	fdSrc = fioOpen(pSrc, O_RDONLY);
	if (fdSrc <= 0)
	{
		printf("Unable to open file %s\n", pSrc);
		return -1;
	}

	// get file size
	nSrcSize = fioLseek(fdSrc, 0, SEEK_END);
	fioLseek(fdSrc, 0, SEEK_SET);

	fdDest = fioOpen(pDest, O_WRONLY | O_CREAT);
	if (fdDest <= 0)
	{
		fioClose(fdSrc);
		printf("Unable to open file %s\n", pDest);
		return -2;
	}

	do
	{
		if (pCallBack)
			pCallBack(pDest, pSrc, nTotalBytes, nSrcSize);

		nBytes = fioRead(fdSrc, Buffer, sizeof(Buffer));
		if (nBytes > 0)
		{
			fioWrite(fdDest, Buffer, nBytes);
			nTotalBytes += nBytes;
		}
	} while (nBytes > 0);

	if (pCallBack)
		pCallBack(pDest, pSrc, nTotalBytes, nSrcSize);

	fioClose(fdSrc);
	fioClose(fdDest);
	printf("Copied %s->%s (%d bytes)\n", pSrc, pDest, nTotalBytes);	

	fdDest = fioOpen(pDest, O_RDONLY);
	if (fdDest > 0)
	{
		fioClose(fdDest);
		return 0;
	} else
	{
		printf("ERROR\n");	
		return -3;
	}
}


static Bool _bTrailingPath(const char *pStr)
{
	int len;
	char cLastChar;
	len = strlen(pStr);
	if (len <= 0)
	{
		return TRUE;
	}
	cLastChar = pStr[len-1];
	return (cLastChar == '/' || cLastChar=='\\' || cLastChar==':');
}

				 //"mc0:/BADATA-SYSTEM"
int InstallFiles(char *pDestPath, char *pSrcPath, char **ppInstallFiles, CopyProgressCallBackT pCallBack)
{
	Bool bTrailingDest;
	Bool bTrailingSrc;

	printf("InstallFiles %s -> %s\n", ppInstallFiles[0], ppInstallFiles[1]);

	bTrailingSrc = _bTrailingPath(pSrcPath);
	bTrailingDest = _bTrailingPath(pDestPath);

	if (fioMkdir(pDestPath) < 0)
	{
		printf("Unable to create directory %s\n", pDestPath);
	} 

	while (*ppInstallFiles)
	{
		char DestPath[256];
		char SrcPath[256];

		printf("Installing %s -> %s\n", ppInstallFiles[0], ppInstallFiles[1]);

		if (bTrailingSrc)
		{
			sprintf(SrcPath,  "%s%s", pSrcPath, ppInstallFiles[1]);
		} else
		{
			sprintf(SrcPath,  "%s/%s", pSrcPath, ppInstallFiles[1]);
		}

		if (bTrailingDest)
		{
			sprintf(DestPath, "%s%s", pDestPath, ppInstallFiles[0]);
		} else
		{
			sprintf(DestPath, "%s/%s", pDestPath, ppInstallFiles[0]);
		}


		if (CopyFile(DestPath, SrcPath, pCallBack) < 0)
		{
			//
		}

		ppInstallFiles+=2;
	} 
	return 0;
}



/*
void InstallSNESticle()
{
	InstallFiles("mc0:/BADATA-SYSTEM", "host0:", _MainLoop_pInstallFiles, NULL);
}

void InstallLoader()
{
	InstallFiles("mc0:/BADATA-SYSTEM", "host0:", _MainLoop_pLoaderFiles, NULL);
}
*/


char *MainGetBootDir();
char *MainGetBootPath();


int dispx, dispy;

/*
#define MAINLOOP_SCREENWIDTH 256
#define MAINLOOP_SCREENHEIGHT 240
#define MAINLOOP_DISPX 65
#define MAINLOOP_DISPY 17
#define FB0     	0x0000
#define FB1     	0x0400
#define Z0      	0x0800
#define TEXADDR 	0x0B00
#define FONT_TEX  	0x2000
*/
/*
#define MAINLOOP_SCREENWIDTH  640
#define MAINLOOP_SCREENHEIGHT 240
#define MAINLOOP_DISPX 160
#define MAINLOOP_DISPY 17
#define FB0     	0x0000
#define FB1     	0x0C00
#define Z0      	0x1800
#define TEXADDR 	0x2400
#define FONT_TEX 	0x3000
*/
/*
#define MAINLOOP_SCREENWIDTH  512
#define MAINLOOP_SCREENHEIGHT 240
#define MAINLOOP_DISPX 160
#define MAINLOOP_DISPY 17
*/

#define MAINLOOP_SCREENWIDTH 256
#define MAINLOOP_SCREENHEIGHT 240
#define MAINLOOP_DISPX 65
#define MAINLOOP_DISPY 17
#define FB0     	0x0000
#define FB1     	0x0C00
#define Z0      	0x1800
#define TEXADDR 	0x2400
#define FONT_TEX 	0x3000

#if 0
static SNPPUColorCalibT _ColorCalib =
{
	0.9f,
	15.0f,
	0.2f
};
#else
static SNPPUColorCalibT _ColorCalib =
{
	0.9f,
	20.0f,
	0.2f
};
#endif

Bool MainLoopInit()
{
//    assert(0);
    #if PROF_ENABLED
    ProfInit(128 * 1024);
    #endif

	// initialize GS
	GS_InitGraph(GS_NTSC,GS_NONINTERLACE);
	dispx = MAINLOOP_DISPX;
	dispy = MAINLOOP_DISPY;
	GS_SetDispMode(dispx,dispy, MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT);
	GS_SetEnv(MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT, FB0, FB1, GS_PSMCT32, Z0, GS_PSMZ16S);


	GPFifoInit((Uint128 *)_MainLoop_GfxPipe, sizeof(_MainLoop_GfxPipe));
    PolyInit();
    FontInit(FONT_TEX);

	// setup log screen
	_MainLoop_pLogScreen = new CLogScreen();
	_MainLoop_pLogScreen->SetMsgFunc(_MainLoopLogEvent);
	_MainLoopSetScreen(_MainLoop_pBrowserScreen);
	_bMenu = TRUE;
#if 0
	const VersionInfoT *pVersionInfo = VersionGetInfo();

	ScrPrintf("%s v%d.%d.%d %s %s %s", 
		pVersionInfo->ApplicationName, 
		pVersionInfo->Version[0],
		pVersionInfo->Version[1],
		pVersionInfo->Version[2],
		pVersionInfo->BuildType,
		pVersionInfo->BuildDate, 
		pVersionInfo->BuildTime);
	ScrPrintf("%s",  pVersionInfo->CopyRight);
#endif
	ScrPrintf("BootPath: %s", MainGetBootPath());
	ScrPrintf("BootDir: %s", MainGetBootDir());

	// set boot dir
	strcpy(_MainLoop_BootDir, MainGetBootDir());

    _MainLoopLoadModules(_MainLoop_IOPModulePaths);

	VramInit();

	_SJPCMMix = new SJPCMMixBuffer(32000, TRUE);

	#if CODE_DEBUG
    printf("MainLoopInit\n");
	#endif

	int loop=60 * 2;
	while (loop--)
		WaitForNextVRstart(1);

	// allocate textures
/*	TextureNew(&_frametex[0], 256, 256, TEX_FORMAT_RGB565);
	TextureNew(&_frametex[1], 256, 256, TEX_FORMAT_RGB565);
	_fbTexture[0]->Set((Uint8 *)TextureGetData(&_frametex[0]), _frametex[0].uWidth, _frametex[0].uHeight, _frametex[0].uPitch, PixelFormatGetByEnum(PIXELFORMAT_BGR565));
	_fbTexture[1].Set((Uint8 *)TextureGetData(&_frametex[1]), _frametex[1].uWidth, _frametex[1].uHeight, _frametex[1].uPitch, PixelFormatGetByEnum(PIXELFORMAT_BGR565));
  */
//    _fbTexture[0]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGB555));
//    _fbTexture[1].Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGB555));

    // create textures in main ram
    _fbTexture[0] = new CRenderSurface;
    _fbTexture[1] = new CRenderSurface;

    _fbTexture[0]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
    _fbTexture[1]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
    _fbTexture[0]->Clear();
    _fbTexture[1]->Clear();
//    printf("%08X\n", (Uint32)_fbTexture[0]->GetLinePtr(0));
//    printf("%08X\n", _fbTexture[1].GetLinePtr(0));

//    TextureNew(&_OutTex, 256, 256, GS_PSMCT16);
    // create texture in vram
    TextureNew(&_OutTex, 256, 256, GS_PSMCT32);
    TextureSetAddr(&_OutTex, TEXADDR );

    TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));
#if 0
	_MainLoopSetPalette(NESPAL_FCEU);
#endif
	PathExtAdd(MAINLOOP_ENTRYTYPE_GZ, (char *)"gz");
	PathExtAdd(MAINLOOP_ENTRYTYPE_ZIP, (char *)"zip");


	SNPPUColorCalibrate(&_ColorCalib);

	// create nes machine
	_pSnes = new SnesSystem();
	_pSnes->Reset();

	_pSnesRom = new SnesRom();
	for (Uint32 iExt=0; iExt < _pSnesRom->GetNumExts(); iExt++)
	{
		PathExtAdd(MAINLOOP_ENTRYTYPE_SNESROM, _pSnesRom->GetExtName(iExt));
	}

	PathExtAdd(MAINLOOP_ENTRYTYPE_SNESPALETTE, (char *)"snpal");
#if 0
	_pNes = new NesSystem();
	_pNes->Reset();

	_pNesRom = new NesRom();
	for (Uint32 iExt=0; iExt < _pNesRom->GetNumExts(); iExt++)
	{
		PathExtAdd(MAINLOOP_ENTRYTYPE_NESROM, _pNesRom->GetExtName(iExt));
	}

	_pNesFDSDisk = new NesDisk();
	for (Uint32 iExt=0; iExt < _pNesFDSDisk->GetNumExts(); iExt++)
	{
		PathExtAdd(MAINLOOP_ENTRYTYPE_NESFDSDISK, _pNesFDSDisk->GetExtName(iExt));
	}

	_pNesFDSBios = new NesFDSBios();
	for (Uint32 iExt=0; iExt < _pNesFDSBios->GetNumExts(); iExt++)
	{
		PathExtAdd(MAINLOOP_ENTRYTYPE_NESFDSBIOS, _pNesFDSBios->GetExtName(iExt));
	}
#endif

	s_pMovieClip = new Emu::MovieClip(_pSnes->GetStateSize(), 60 * 60 * 60);

	// init menu
	_MainLoop_pBrowserScreen = new CBrowserScreen(6000);
	_MainLoop_pBrowserScreen->SetMsgFunc(_MainLoopBrowserEvent);
	_MainLoop_pBrowserScreen->SetDir(MENU_STARTDIR);

	_MainLoop_pNetworkScreen = new CNetworkScreen();
	_MainLoop_pNetworkScreen->SetMsgFunc(_MainLoopNetworkEvent);
	_MainLoop_pNetworkScreen->SetPort(MAINLOOP_NETPORT);

	_MainLoop_pMenuScreen = new CMenuScreen();
	_MainLoop_pMenuScreen->SetMsgFunc(_MainLoopMenuEvent);
	_MainLoop_pMenuScreen->SetTitle("Install Menu");
	_MainLoop_pMenuScreen->SetEntries((char **)_MainLoopMenuEntries );


	_MainLoopSetScreen(_MainLoop_pBrowserScreen);
        // espera ~2s (ajuste se quiser)
        _MainLoopSetScreen(_MainLoop_pBrowserScreen);
	_bMenu = FALSE;

//	while (1);

	// load snes palette
	printf("[DBG] palette load desativado para teste\n");
        //_MainLoopLoadSnesPalette("mc0:/SNESticle/default.snpal");

	// load rom
	_MainLoopExecuteFile(_pRomFile, TRUE);
	_bMenu = TRUE;

	SjPCM_Clearbuff();
	SjPCM_Play();
//   SjPCM_Setvol(0xF);

/*
    if (!_WavFile.Open(_pSnesWavFileName, 32000, 16, 2))
    {
         printf("WavOut Open\n");
    }
  */

	InputPoll();

#if 0
	while (1)
	{
		MainLoopRender();
		_MainLoop_pBrowserScreen->SetDir("cdfs:/ROMS/SNES");
		MainLoopRender();
		_MainLoop_pBrowserScreen->SetDir("cdfs:/ROMS/SNES/USA");
	}
#endif

    return TRUE;
}


static Uint16 _MainLoopSnesInput(Uint32 cond)
{
	Uint32 pad = 0;

	if (cond & PAD_LEFT)    pad|= (SNESIO_JOY_LEFT);
	if (cond & PAD_RIGHT)   pad|= (SNESIO_JOY_RIGHT);
	if (cond & PAD_UP)      pad|= (SNESIO_JOY_UP);
	if (cond & PAD_DOWN)    pad|= (SNESIO_JOY_DOWN);

	if (cond & PAD_SQUARE)   pad|= (SNESIO_JOY_Y);
	if (cond & PAD_TRIANGLE) pad|= (SNESIO_JOY_X);
	if (cond & PAD_CROSS)    pad|= (SNESIO_JOY_B);
	if (cond & PAD_CIRCLE)   pad|= (SNESIO_JOY_A);

	if (cond & PAD_L1)   pad|= (SNESIO_JOY_L);
	if (cond & PAD_R1)   pad|= (SNESIO_JOY_R);

	if (cond & PAD_SELECT)  pad|= (SNESIO_JOY_SELECT);
	if (cond & PAD_START)   pad|= (SNESIO_JOY_START);
	return pad;
}

#if 0
static Uint16 _MainLoopNesInput(Uint32 cond)
{
	Uint8 pad = 0;

	if ((cond & PAD_LEFT) ) pad|= (1<<NESIO_BIT_LEFT);
	if ((cond & PAD_RIGHT)) pad|= (1<<NESIO_BIT_RIGHT);
	if ((cond & PAD_UP)   ) pad|= (1<<NESIO_BIT_UP);
	if ((cond & PAD_DOWN) ) pad|= (1<<NESIO_BIT_DOWN);
	if ((cond & PAD_SQUARE)    ) pad|= (1<<NESIO_BIT_B);
	if ((cond & PAD_CROSS)    ) pad|= (1<<NESIO_BIT_A);

	if ((cond & PAD_TRIANGLE)    ) pad|= (1<<NESIO_BIT_B);
	if ((cond & PAD_CIRCLE)    ) pad|= (1<<NESIO_BIT_A);

	if ((cond & PAD_SELECT)    ) pad|= (1<<NESIO_BIT_SELECT);
	if ((cond & PAD_START)) pad|= (1<<NESIO_BIT_START);
	return pad;
}
#endif
void _MainLoopSetSampleRate(Uint32 uSampleRate)
{
    _SJPCMMix->SetSampleRate(uSampleRate);
}


#if MAINLOOP_SNESSTATEDEBUG
static SnesStateT _TestState[3];
#endif

#if MAINLOOP_NESSTATEDEBUG
static NesStateT _NesTestState[3];
#endif

//extern Bool bStateDebug;

#if MAINLOOP_HISTORY
Uint32 _History[16384 * 2];
Uint32 _nHistory = 0;
#endif

#if MAINLOOP_HISTORY
void _MainLoopResetHistory()
{
    _nHistory = 0;
}
#endif

#if MAINLOOP_HISTORY

void _MainLoopSaveHistory()
{
    FileWriteMem("host:game.hst", _History, _nHistory * sizeof(Uint32));
    printf("History written\n");
}
#endif


static Uint32 _uVblankCycle;
static Uint32 _uInputFrame;
static Uint32 _uInputChecksum[5];

void _MainLoopResetInputChecksums()
{
	_uInputFrame =0;
	memset(_uInputChecksum, 0, sizeof(_uInputChecksum));
}




#if 1
static Bool _ExecuteSnes(CRenderSurface *pSurface, CMixBuffer *pMixBuffer, Emu::SysInputT *pInput, Emu::System::ModeE eMode)
{

        #if !TESTASM  

            #if !MAINLOOP_SNESSTATEDEBUG
//            pMixBuffer=NULL;
//            SNCPUSetExecuteFunc(SNCPUExecute_C);
            SNCPUSetExecuteFunc(SNCPUExecute_ASM);
            SNSPCSetExecuteFunc(SNSPCExecute_C);

		    PROF_ENTER("SnesExecuteFrame");
  		    _pSystem->ExecuteFrame(pInput, pSurface, pMixBuffer, eMode);
		    PROF_LEAVE("SnesExecuteFrame");
            #else

			if (_pSnes->GetFrame() > 50*60)
			{
            	SNCPUSetExecuteFunc(SNCPUExecute_C);
				_pSnes->SaveState(&_TestState[0]);
				_pSnes->ExecuteFrame(pInput, pSurface, NULL);
				_pSnes->SaveState(&_TestState[1]);


            	SNCPUSetExecuteFunc(SNCPUExecute_ASM);
				_pSnes->RestoreState(&_TestState[0]);
				_pSnes->ExecuteFrame(pInput, pSurface, NULL);
				_pSnes->SaveState(&_TestState[2]);

				if (memcmp(&_TestState[1], &_TestState[2],sizeof(SnesStateT)))
				{
					printf("State fault (frame= %d)\n", _pSnes->GetFrame());
            	    SNStateCompare(&_TestState[1],&_TestState[2]);

            	    FileWriteMem("host0:c:/emu/fault.sns", &_TestState[0], sizeof(_TestState[0]));


            	   
					printf("Resuming frame...\n");

//          	      bStateDebug = TRUE;

            	    SNCPUSetExecuteFunc(SNCPUExecute_C);
    				_pSnes->RestoreState(&_TestState[0]);
				    _pSnes->ExecuteFrame(pInput, pSurface, NULL);

            	    SNCPUSetExecuteFunc(SNCPUExecute_ASM);
				    _pSnes->RestoreState(&_TestState[0]);
				    _pSnes->ExecuteFrame(pInput, pSurface, NULL);

            	    while (1);

				}
			} else
			{
	            SNCPUSetExecuteFunc(SNCPUExecute_C);
	            SNSPCSetExecuteFunc(SNSPCExecute_C);

			    PROF_ENTER("SnesExecuteFrame");
	  		    _pSystem->ExecuteFrame(pInput, pSurface, pMixBuffer);
			    PROF_LEAVE("SnesExecuteFrame");
			}
            #endif


//  		    _pSnes->ExecuteFrame(&Input, NULL, &_SJPCMMix);
//  		    _pSnes->ExecuteFrame(&Input, pSurface, NULL);
        #endif

    return TRUE;
}

extern "C" {
//#include "ncpu_c.h"
};
#if 0
static Bool _ExecuteNes(CRenderSurface *pSurface, CMixBuffer *pMixBuffer, EmuSysInputT *pInput, EmuSysModeE eMode)
{

#if !MAINLOOP_NESSTATEDEBUG
    N6502SetExecuteFunc(NCPUExecute_C);

	PROF_ENTER("NesExecuteFrame");
  	_pSystem->ExecuteFrame(pInput, pSurface, pMixBuffer, eMode);
	PROF_LEAVE("NesExecuteFrame");
    #else

	_pNes->SaveState(&_NesTestState[0]);
	_pNes->ExecuteFrame(pInput, pSurface, NULL);
	_pNes->SaveState(&_NesTestState[1]);

	_pNes->RestoreState(&_NesTestState[0]);
	_pNes->ExecuteFrame(pInput, pSurface, NULL);
	_pNes->SaveState(&_NesTestState[2]);

	if (memcmp(&_NesTestState[1], &_NesTestState[2],sizeof(NesStateT)))
	{
		printf("State fault\n");
	}

    #endif
    return TRUE;
}
#endif
#endif



Uint16 _MainLoopInput(Uint32 pad)
{
	if (pad & (PAD_R2|PAD_L2))
    {
        return 0;
    }
#if 0
    if (_pSystem==_pSnes)
    {
        return _MainLoopSnesInput(pad);
    } else
    if (_pSystem==_pNes)
    {
        return _MainLoopNesInput(pad);
    }
	   return 0;
#else
 	return _MainLoopSnesInput(pad);
#endif
 
}

#include "snppublend_gs.h"



#include "common/debug/dbgterm.h"

void MainLoopRender()
{
    static int __dbg_render_calls = 0;
    __dbg_render_calls++;
    if (__dbg_render_calls <= 20)
    {
        DbgLog("MainLoopRender #%d bMenu=%d black=%d screen=0x%08x system=0x%08x",
               __dbg_render_calls,
               (int)_bMenu,
               (int)_MainLoop_BlackScreen,
               (unsigned int)_MainLoop_pScreen,
               (unsigned int)_pSystem);
    }

	static Uint32 _iFrame=0;
	static int whichdrawbuf = 0;

    // render frame
    GPPrimDisableZBuf();

	if (!_MainLoop_BlackScreen)
	{
//		Float32 fDestColor = (_bMenu || _MainLoop_ModalCount) ? 0.10f : 0.80f;
		Float32 fDestColor = 0.10f;
		
		if  (!_bMenu && !_MainLoop_ModalCount)
		{
			fDestColor = _MainLoop_fOutputIntensity;
		}

		static Float32 fColor=0.0f;
		Float32 dx = 0.0f;
		Float32 dy = 8.0f;

		if (fColor < fDestColor) 
		{
			fColor+=0.06f;
			if (fColor > fDestColor) 
			{
				fColor = fDestColor;
			}
		} 

		if (fColor > fDestColor) 
		{
			fColor-=0.06f;
			if (fColor < fDestColor) 
			{
				fColor = fDestColor;
			}
		}


        PolyBlend(FALSE);
        PolyTexture(&_OutTex);
//        PolyUV(0,0,256,240);
        PolyUV(0,0,256,240);
		PolyColor4f(fColor, fColor, fColor, 1.0f);
//		PolyColor4f(0.50f, 0.50f, 0.50f, 1.0f);


        PolyRect(dx,dy,MAINLOOP_SCREENWIDTH,MAINLOOP_SCREENHEIGHT);

        PolyBlend(TRUE);
        //PolyTexture(NULL);
        //PolyRect(dx,dy,128,120);
    }


    if (!_bMenu)
    {	
	
		if (s_pMovieClip->IsPlaying())
		{
	        FontSelect(2);
	        FontColor4f(0.5, 0.5f, 0.5f, 1.0f);
	        FontPrintf(240,220, ">");
		}

		if (s_pMovieClip->IsRecording())
		{
	        FontSelect(2);
	        FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
	        FontPrintf(240,220, "O");
		}


		switch (_MainLoop_uDebugDisplay)
        {
		case 0:
/*	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,170, "%08X", InputGetPadData(0));
  */

//		        FontSelect(2);
//		        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
//		        FontPrintf(40,190, "%3d", xpadGetFrameCount(0,0));
			break;
		case 1:
		/*
	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,190, "%3d %3d", NetInput.InputSize[0], NetInput.OutputSize[0]);
	        FontPrintf(40,200, "%3d %3d", NetInput.InputSize[1], NetInput.OutputSize[1]);
	        FontPrintf(40,210, "%3d %3d", NetInput.InputSize[2], NetInput.OutputSize[2]);
	        FontPrintf(40,220, "%3d %3d", NetInput.InputSize[3], NetInput.OutputSize[3]);
			*/
			break;
		case 2:
	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,170, "%08X", _uInputFrame);
	        FontPrintf(40,180, "%08X", _uInputChecksum[0]);
	        FontPrintf(40,190, "%08X", _uInputChecksum[1]);
	        FontPrintf(40,200, "%08X", _uInputChecksum[2]);
	        FontPrintf(40,210, "%08X", _uInputChecksum[3]);
	        FontPrintf(40,220, "%08X", _uInputChecksum[4]);
			break;
		case 3:
			FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
			FontPrintf(195, 210, "%8d", _uVblankCycle / 1024);
			break;
        }

        FontSelect(2);
		FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
		{

/*
		FontPrintf(15, 180, "%08X %08X Y", (Int32)(_ColorCalib.y_mul * 0x10000), (Int32)(_ColorCalib.y_add * 0x10000));
		FontPrintf(15, 190, "%08X %08X I", (Int32)(_ColorCalib.i_mul * 0x10000), (Int32)(_ColorCalib.i_add * 0x10000));
		FontPrintf(15, 200, "%08X %08X Q", (Int32)(_ColorCalib.q_mul * 0x10000), (Int32)(_ColorCalib.q_add * 0x10000));
  */

		/*
		FontPrintf(195, 180, "%6.3f %6.3f", _ColorCalib.y_mul, _ColorCalib.y_add);
		FontPrintf(195, 190, "%6.3f %6.3f", _ColorCalib.i_mul, _ColorCalib.i_add);
		FontPrintf(195, 200, "%6.3f %6.3f", _ColorCalib.q_mul, _ColorCalib.q_add);
		*/
		}

//			FontPrintf(195, 210, "%8d", _SJPCMMix.GetLastOutput());
    }


	if (_MainLoop_ModalCount > 0)
	{
		FontSelect(0);
		FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
		FontPrintf(128 - FontGetStrWidth(_MainLoop_ModalStr) / 2,100, _MainLoop_ModalStr);

		_MainLoop_ModalCount--;
	} else
	{

		if (_MainLoop_StatusCount > 0)
		{
			FontSelect(0);
			FontColor4f(0.0, 0.8f, 0.8f, 1.0f);
			FontPrintf(20, 200, _MainLoop_StatusStr);

			_MainLoop_StatusCount--;
		} 


		if (_bMenu)
		{
			_MenuDraw();
		} 
	}


	#if CODE_DEBUG
	if (MCSave_WriteSync(FALSE, NULL))
	{
		FontSelect(1);
		FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
		if (_iFrame & 4)
			FontPrintf(235,216, "#");
	}
	#endif



//	FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
//	FontPuts(100, 100, _VersionStr);

//    PolyTexture(NULL);
//	PolyColor4f(0.0f, 1.0f, 0.0f, 1.0f);
//	PolyRect(0, 0, 100, 50);

    PROF_ENTER("GPFlush");
    GPFifoFlush();
    PROF_LEAVE("GPFlush");

    PROF_ENTER("WaitVBlank");

    if ( (_iFrame&15)==0)   _uVblankCycle = ProfCtrGetCycle();
	WaitForNextVRstart(1);
    if ( (_iFrame&15)==0)   _uVblankCycle = ProfCtrGetCycle() - _uVblankCycle;

    PROF_LEAVE("WaitVBlank");

    PROF_ENTER("GSSetCrt");
    GS_SetCrtFB(0);
    GS_SetDrawFB(0);
    PROF_LEAVE("GSSetCrt");

    _iFrame++;
}


Bool MainLoopProcess()
{
    NetPlayRPCInputT NetInput;

    PROF_ENTER("Frame");

    PROF_ENTER("NetPlayRPCProcess");
    NetPlayRPCProcess();
    PROF_LEAVE("NetPlayRPCProcess");

    PROF_ENTER("InputProcess");
    InputPoll();
    /* UI_L1R1_PROCESS */
    {
        static Uint32 _prev = 0;
        Uint32 now = InputGetPadData(0);
        Uint32 pressed = now & ~_prev;
        _prev = now;
        if (pressed & PAD_L1) _UICycle(-1);
        if (pressed & PAD_R1) _UICycle(+1);
    }

    /* UI_CYCLE_L1R1_PROCESS */
    {
        static Uint32 s_prev = 0;
        Uint32 now = InputGetPadData(0);
        Uint32 pressed = now & ~s_prev;
        s_prev = now;

        if (pressed & PAD_L1) _MainLoopCycleScreen(-1);
        if (pressed & PAD_R1) _MainLoopCycleScreen(+1);
    }
    PROF_LEAVE("InputProcess");


	_MainLoopInputProcess(InputGetPadData(0) | InputGetPadData(1) | InputGetPadData(2) | InputGetPadData(3));

//	_MainLoopInputProcess(InputGetPadData(0));
//	_MainLoopInputProcess(InputGetPadData(1));

    if (!_bMenu && _pSystem && !_MainLoop_BlackScreen)
    {
        CRenderSurface *pSurface;
        CMixBuffer *pMixBuffer = NULL;
        pSurface = _fbTexture[_iframetex];
	
		Emu::SysInputT Input;
	
		Int32 iPad;

        /*
        if (_WavFile.IsOpen())
        {
            pMixBuffer = &_WavFile;
        } else
        {
            pMixBuffer = &_SJPCMMix;
        } 
        */                        
        pMixBuffer = _SJPCMMix;
//                pMixBuffer = NULL;

		// read inputs
		for (iPad=0; iPad < 5; iPad++)
		{
			if (InputIsPadConnected(iPad))
			{
				Input.uPad[iPad] = _MainLoopInput(InputGetPadData(iPad));
			} else
			{
				Input.uPad[iPad] = EMUSYS_DEVICE_DISCONNECTED;
			}
		}

		// send controller 1 + 2 inputs combined to 32-bits
		NetInput.InputSend = ((Uint32)Input.uPad[0]) | (((Uint32)Input.uPad[1])<<16);

        PROF_ENTER("NetPlayClientInput");
        NetPlayClientInput(&NetInput);
        PROF_LEAVE("NetPlayClientInput");

        if (NetInput.eGameState == NETPLAY_GAMESTATE_PLAY)
        {
            if ((_pSystem->GetFrame()+1) != NetInput.uFrame)
            {
				#if CODE_DEBUG
                printf("Not executing frame %d %d\n", NetInput.uFrame, _pSystem->GetFrame());
				#endif
                NetInput.eGameState = NETPLAY_GAMESTATE_PAUSE;
            }

			// we are connected, retrieve input data
	        Input.uPad[0] = (Uint16)NetInput.InputRecv[0];
	        Input.uPad[1] = (Uint16)NetInput.InputRecv[1];
	        Input.uPad[2] = (Uint16)NetInput.InputRecv[2];
	        Input.uPad[3] = (Uint16)NetInput.InputRecv[3];
			Input.uPad[4] = EMUSYS_DEVICE_DISCONNECTED;

			if (Input.uPad[2] == EMUSYS_DEVICE_DISCONNECTED)
			{
				// if controller 3 is disconnected, use controller 2 of first peer
				Input.uPad[2] = (Uint16)(NetInput.InputRecv[0]>>16);
			}

			if (Input.uPad[3] == EMUSYS_DEVICE_DISCONNECTED)
			{
				// if controller 4 is disconnected, use controller 2 of second peer
				Input.uPad[3] = (Uint16)(NetInput.InputRecv[1]>>16);
			}
		
        }  
		else
		{
		
            if (s_pMovieClip->IsPlaying())
            {
                if (!s_pMovieClip->PlayFrame(Input))
                {
                    s_pMovieClip->PlayEnd();
                    ConPrint("Movie: Play End\n");
                }
            }
	
		}

        if (NetInput.eGameState != NETPLAY_GAMESTATE_PAUSE)
        {
			Emu::System::ModeE eMode;

            #if MAINLOOP_HISTORY
            if (_nHistory < 16384 * 2)
            {
                _History[_nHistory++] = Input.uPad[0];
                _History[_nHistory++] = Input.uPad[1];
                _History[_nHistory++] = Input.uPad[2];
                _History[_nHistory++] = Input.uPad[3];
            }
            #endif

			_uInputFrame    = NetInput.uFrame;
			_uInputChecksum[0] += Input.uPad[0];
			_uInputChecksum[1] += Input.uPad[1];
			_uInputChecksum[2] += Input.uPad[2];
			_uInputChecksum[3] += Input.uPad[3];
			_uInputChecksum[4] += Input.uPad[4];

			eMode = (NetInput.eGameState == NETPLAY_GAMESTATE_IDLE) ? Emu::System::MODE_ACCURATENONDETERMINISTIC : Emu::System::MODE_INACCURATEDETERMINISTIC;

            if (s_pMovieClip->IsRecording())
            {
                if (!s_pMovieClip->RecordFrame(Input))
                {
                    s_pMovieClip->RecordEnd();
                    ConPrint("Movie: Reached end of record buffer!\n");
                }
            }

#if 0
            if (_pSystem==_pSnes)
            {
//                _ExecuteSnes(NULL, NULL, &Input, eMode);
 			    GPPrimDisableZBuf();
                _ExecuteSnes(pSurface, pMixBuffer, &Input, eMode);
//                TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
            } else
            if (_pSystem==_pNes)
            {
                _ExecuteNes(pSurface, pMixBuffer, &Input, eMode);

                TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
            } 
#else
 			    GPPrimDisableZBuf();
                _ExecuteSnes(pSurface, pMixBuffer, &Input, eMode);
#endif
		    _iframetex^=1;
        }

        SjPCM_BufferedAsyncStart();
    }

    _MainLoopCheckSRAM();

	MainLoopRender();

    PROF_LEAVE("Frame");

    #if PROF_ENABLED
    ProfProcess();
    #endif

    return TRUE;
}

void MainLoopShutdown()
{
    FontShutdown();
    PolyShutdown();
}






//
//
//



/*
	Menu stuff

*/

void _MenuEnable(Bool bEnable)
{
	if (bEnable!=_bMenu)
	{
		// if menu is enabled, then attempt to save sram immediately
		if (bEnable)
		{
            #if 1 
			if (_MainLoopHasSRAM() && _MainLoop_SRAMUpdated)
			{
			   	MainLoopModalPrintf(10, "Saving SRAM...");

			    if (_MainLoopHasSRAM())
			    {
					#if MAINLOOP_MEMCARD
					MCSave_WriteSync(1, NULL);

					if (MemCardCheckNewCard())
					{
						printf("New memcard detected\n");
						if (MemCardCreateSave(_SramPath, _MainLoop_SaveTitle, FALSE))
						{
							MemCardCreateSave(_SramPath, _MainLoop_SaveTitle, FALSE);
						}
					}
					#endif

				    if (_MainLoopSaveSRAM(TRUE))
				    {
					    MainLoopModalPrintf(60, "SRAM saved.\n");
				    } else
				    {
					    MainLoopModalPrintf(60 * 1 + 30, "Error Saving SRAM!\n");
				    }
			    }
			}
            #endif
		}

		_bMenu = bEnable;

//    	SjPCM_Clearbuff();
	}
}

#define MENU_REPEAT (16)

//#define MENU_REPEATBUTTONS (PAD_UP|PAD_DOWN|PAD_SQUARE|PAD_CIRCLE)
#define MENU_REPEATBUTTONS (PAD_UP|PAD_DOWN|PAD_SQUARE|PAD_CIRCLE|PAD_CROSS|PAD_TRIANGLE|PAD_LEFT|PAD_RIGHT)

static void _MainLoopInputProcess(Uint32 buttons)
{
	static Uint32 lastbuttons= ~0;
	static Uint32 repeat=0;
	Uint32 trigger;


	if (!(buttons& MENU_REPEATBUTTONS))
	{
		repeat=0;
	}

	// 
	repeat++;
	if (repeat > MENU_REPEAT)
	{
		repeat -= 1;
		lastbuttons &= ~MENU_REPEATBUTTONS;
	}

	trigger = ((buttons ^ lastbuttons) & buttons);
	lastbuttons = buttons;

#if 1
	if (trigger & PAD_R3)
	{
        #if PROF_ENABLED
		ProfStartProfile(1);
        #endif
//		BMPWriteFile("/pc/mnt/c/out.bmp", &_fbTexture[0]);
	}





    #ifdef DEBUG // CODE_DEBUG
	if (trigger & PAD_L2)
	{
        if (_WavFile.IsOpen())
        {
            _WavFile.Close();
            printf("WavOut Closed\n");
        } else
        {
        /*
            if (!_WavFile.Open(_pSnesWavFileName, 32000, 16, 2))
            {
                 printf("WavOut Open\n");
            }
            */
            if (!_WavFile.Open(_pSnesWavFileName, 48000, 16, 2))
            {
                 printf("WavOut Open\n");
            }

        }
//		BMPWriteFile("/pc/mnt/c/out.bmp", &_fbTexture[0]);
	}
    #endif


    #ifdef DEBUG // CODE_DEBUG
	if (buttons & PAD_L2)
	{
        if (trigger&PAD_CROSS)
            _MainLoopSaveState();
        if (trigger&PAD_CIRCLE)
            _MainLoopLoadState();
        if (trigger&PAD_TRIANGLE)
		{
            _MainLoop_uDebugDisplay++;
            _MainLoop_uDebugDisplay&=3;
		}

        if (trigger&PAD_L3)
		{
			
	        // stop recording if we are recording
	        if (s_pMovieClip->IsRecording())
	        {
	            printf("Movie: Record End\n");
	            s_pMovieClip->RecordEnd();
	        } else
	        // stop playing if we are playing
	        if (s_pMovieClip->IsPlaying())
	        {
	            printf("Movie: Play End\n");
	            s_pMovieClip->PlayEnd();
	        } else

	        if (_pSystem)
	        {
	            s_pMovieClip->RecordBegin(_pSystem);
	            printf("Movie: Record Begin\n");
	        }
		}

        if (trigger&PAD_R3)
		{
	        // stop recording if we are recording
	        if (s_pMovieClip->IsRecording())
	        {
	            printf("Movie: Record End\n");
	            s_pMovieClip->RecordEnd();
	        } 

	        // stop playing if we are playing
	        if (s_pMovieClip->IsPlaying())
	        {
	            printf("Movie: Play End\n");
	            s_pMovieClip->PlayEnd();
	        } 
	        if (_pSystem)
	        {
	            s_pMovieClip->PlayBegin(_pSystem);
	            printf("Movie: Play Begin\n");
	        }
		}

    }

			/*
	if (buttons & PAD_R2)
	{
		if (buttons & PAD_SQUARE)
		{
        if (trigger&PAD_LEFT)
			_ColorCalib.i_mul-=0.1f;
        if (trigger&PAD_RIGHT)
			_ColorCalib.i_mul+=0.1f;
        if (trigger&PAD_UP)
			_ColorCalib.i_add+=0.005f;
        if (trigger&PAD_DOWN)
			_ColorCalib.i_add-=0.005f;
		}

		if (buttons & PAD_CROSS)
		{
        if (trigger&PAD_LEFT)
			_ColorCalib.q_mul-=0.01f;
        if (trigger&PAD_RIGHT)
			_ColorCalib.q_mul+=0.01f;
        if (trigger&PAD_UP)
			_ColorCalib.q_add+=0.005f;
        if (trigger&PAD_DOWN)
			_ColorCalib.q_add-=0.005f;
		}

		if (buttons & PAD_CIRCLE)
		{
        if (trigger&PAD_LEFT)
			_ColorCalib.y_mul-=0.01f;
        if (trigger&PAD_RIGHT)
			_ColorCalib.y_mul+=0.01f;
        if (trigger&PAD_UP)
			_ColorCalib.y_add+=0.005f;
        if (trigger&PAD_DOWN)
			_ColorCalib.y_add-=0.005f;
		}

		SNPPUBlendGS::ColorCalibrate(&_ColorCalib);
    }

			  */



    #endif


	#if 0
	if (
	 	((trigger & PAD_R2) && (buttons & PAD_L2)) ||
	 	((trigger & PAD_L2) && (buttons & PAD_R2)) 
	   )
	{
		// toggle menu
		 _MenuEnable(!_bMenu);
		 return;
	}
	#endif

	static int _MenuTriggerTimeout[2] = {0,0};

	if (trigger & PAD_L2)
	{
		_MenuTriggerTimeout[0]=5;
	}

	if (trigger & PAD_R2)
	{
		_MenuTriggerTimeout[1]=5;
	}


	if (_MenuTriggerTimeout[0] > 0)
	{
		if (trigger & PAD_R2)
		{
			_MenuTriggerTimeout[0] = 0;
			 // toggle menu
			 _MenuEnable(!_bMenu);
			 return;
		}
		_MenuTriggerTimeout[0]--;
	}

	if (_MenuTriggerTimeout[1] > 0)
	{
		if (trigger & PAD_L2)
		{
			_MenuTriggerTimeout[1] = 0;
			 // toggle menu
			 _MenuEnable(!_bMenu);
			 return;
		}
		_MenuTriggerTimeout[1]--;
	}


	if (_bMenu)
	{
		if (_MainLoop_pScreen)
		{
		    if (trigger & PAD_R1)
		    {
				if (_MainLoop_pScreen == _MainLoop_pBrowserScreen)
					_MainLoopSetScreen(_MainLoop_pNetworkScreen);
				 else
				if (_MainLoop_pScreen == _MainLoop_pNetworkScreen)
					_MainLoopSetScreen(_MainLoop_pMenuScreen);
				 else
				if (_MainLoop_pScreen == _MainLoop_pMenuScreen)
					_MainLoopSetScreen(_MainLoop_pBrowserScreen);
				else
					_MainLoopSetScreen(_MainLoop_pBrowserScreen);
		    } else

		    if (trigger & PAD_L1)
		    {
				if (_MainLoop_pScreen == _MainLoop_pBrowserScreen)
					_MainLoopSetScreen(_MainLoop_pBrowserScreen);
				 else
				if (_MainLoop_pScreen == _MainLoop_pNetworkScreen)
					_MainLoopSetScreen(_MainLoop_pBrowserScreen);
				 else
				if (_MainLoop_pScreen == _MainLoop_pMenuScreen)
					_MainLoopSetScreen(_MainLoop_pNetworkScreen);
				else
				if (_MainLoop_pScreen == _MainLoop_pLogScreen)
					_MainLoopSetScreen(_MainLoop_pMenuScreen);
		    } else
			{
				_MainLoop_pScreen->Input(buttons, trigger);
			}
		}

	}
	else
	{

		if (buttons & PAD_L2)
		{
			if (trigger & PAD_SELECT)
			{
				_MainLoop_BlackScreen^=1;
			}
		}

#if 0
		// perform cheesy non-deterministic disk switching
		if (trigger & (PAD_R1|PAD_L1) )
		{
			if (_pNesFDSDisk->IsLoaded())
			{
				if (_MainLoop_bDiskInserted)
				{
					// eject disk!
					_MainLoop_bDiskInserted = FALSE;
					_pNes->GetMMU()->InsertDisk(-1);

					MainLoopStatusPrintf(60, "NESFDS Disk Ejected");

					// pick next disk
					if (trigger & PAD_R1)
						_MainLoop_iDisk++;
					else
						_MainLoop_iDisk--;
				} else
				{
					// wrap the number of disks
					if (_MainLoop_iDisk < 0)
					{
						_MainLoop_iDisk = _pNesFDSDisk->GetNumDisks()-1;
					}

					if (_MainLoop_iDisk >= _pNesFDSDisk->GetNumDisks())
					{
						_MainLoop_iDisk = 0;
					}
					// insert disk
					_pNes->GetMMU()->InsertDisk(_MainLoop_iDisk);
					_MainLoop_bDiskInserted = TRUE;


					MainLoopStatusPrintf(60, "NESFDS Disk %d Inserted", _MainLoop_iDisk);
				}
			}
		}
#endif

	}
#endif
}







#if MAINLOOP_HISTORY
    if (trigger & PAD_L3)
    {
         _MainLoopSaveHistory();
   }
#endif






static void _MenuDraw()
{
	FontSelect(0);

	PolyTexture(NULL);
    PolyBlend(TRUE);


    t_ip_info config;
    memset(&config, 0, sizeof(config));

	// draw current screen
	if (_MainLoop_pScreen)
	{
		_MainLoop_pScreen->Draw();
	}

	int vy = 215;

	FontSelect(2);
//	FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
//	FontColor4f(1.0, 0.5f, 0.5f, 1.0f);
	FontColor4f(0.2, 0.6f, 0.2f, 1.0f);

#if 0
	const VersionInfoT *pVersionInfo = VersionGetInfo();

	char VersionStr[256];
	
	sprintf(VersionStr, "%s v%d.%d.%d %s", 
		pVersionInfo->ApplicationName, 
		pVersionInfo->Version[0],
		pVersionInfo->Version[1],
		pVersionInfo->Version[2],
		pVersionInfo->BuildType
		);

	FontPuts(256 - 16 - FontGetStrWidth(VersionStr), vy, VersionStr);

//	FontPrintf(8, vy-16, "%d", CDVD_DiskReady(1));





	FontPrintf(8, vy, "%s%d.%d", 
		pVersionInfo->Compiler, 
		pVersionInfo->CompilerVersion[0],  
		pVersionInfo->CompilerVersion[1]
		);
#endif	
    FontPrintf(48,vy,"IP: %d.%d.%d.%d", 
            (config.ipaddr.s_addr >> 0) & 0xFF,
            (config.ipaddr.s_addr >> 8) & 0xFF,
            (config.ipaddr.s_addr >>16) & 0xFF,
            (config.ipaddr.s_addr >>24) & 0xFF
                    );



	FontSelect(0);
}





#if 1
void *operator new(unsigned x)
{
	void *ptr = malloc(x);
	#if CODE_DEBUG
	printf("new %d %08X\n", x, (unsigned)ptr);
	#endif
	return ptr;
}

void operator delete(void *ptr)
{
	#if CODE_DEBUG
	printf("delete %08X\n", (unsigned)ptr);
	#endif
	free(ptr);
}



void *operator new[](unsigned x)
{
//	printf("new %d\n", x);
	return malloc(x);
}

void operator delete[](void *ptr)
{
//	printf("delete %08X\n", (unsigned)ptr);
	free(ptr);
}

#endif







