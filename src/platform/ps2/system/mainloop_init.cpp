/* mainloop_init.cpp
 *
 * Hosts MainLoopInit() and the file-static state that is only consumed
 * by the boot path: the screen-dimension / GS-address #defines, the
 * default browser start dir, the GIF FIFO scratch buffer, the SNES
 * colour-calibration block, and the boot-time ROM filename.
 *
 * Extracted from mainloop.cpp during the Batch 3 split. No logic,
 * literal, attribute, or initialisation order has been changed -- only
 * the translation unit a given chunk lives in.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_iop.h"
#include "mainloop_net.h"
#include "mainloop_ui.h"
#include "mainloop_menu.h"
#include "mainloop_browser.h"
#include "mainloop_load.h"
#include "mainloop.h"

#include "types.h"
#include "input.h"
#include "snes.h"
#include "rendersurface.h"
#include "prof.h"
#include "font.h"
#include "poly.h"
#include "texture.h"
#include "sjpcmbuffer.h"
#include "pathext.h"
#include "snppucolor.h"
#include "emumovie.h"

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
#include "gskit_backend.h"
};

extern "C" {
#include "sjpcm.h"
};

#include "uiBrowser.h"
#include "uiNetwork.h"
#include "uiMenu.h"
#include "uiLog.h"


/* BOOTLOG: route boot-phase diagnostics through DLog (defined in
   modules/sjpcm/sjpcm_rpc.c).  Plain printf on the EE never reaches
   PCSX2/NetherSX2's emulator log in this build, so DLog (which writes
   to the EE SIO TX FIFO) is the only practical way to see the boot
   progression from outside the running app. */
extern "C" void DLog(const char *fmt, ...);
#define BOOTLOG(...) DLog(__VA_ARGS__)
#define MENU_STARTDIR _MainLoop_MenuStartDir

/* MAINLOOP_NETPORT lives in mainloop_shared.h (included above). */


/* The chosen GS layout (PAL/NTSC width, FB and texture addresses).
   Three alternative blocks were left commented out in the original
   mainloop.cpp; they are preserved verbatim for reference. */
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


/* Browser starting directory. On real PS2 you typically want "mass:/"
   (USB stick) or a memcard path. On PCSX2/AetherSX2/NetherSX2 the
   "host:" device is not mapped, so an empty string here makes the
   browser screen list nothing - which looks like a "dead" menu even
   though everything is rendering correctly. "mass:/" is the safe
   default that works on real PS2 and on emulators with a USB image
   attached. Override at runtime if needed. */
static Char _MainLoop_MenuStartDir[] = "";

static int dispx, dispy;

static Uint8 _MainLoop_GfxPipe[0x40000] _ALIGN(128) __attribute__ ((section (".bss")));

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


/* MainGetBootDir() / MainGetBootPath() are exported from the app
   entrypoint; forward-declared here exactly as in the original
   mainloop.cpp. */
char *MainGetBootDir();
char *MainGetBootPath();


Bool MainLoopInit()
{
//    assert(0);
    #if PROF_ENABLED
    ProfInit(128 * 1024);
    #endif
	BOOTLOG("[boot] GS_InitGraph()\n");
	GS_InitGraph(GS_NTSC,GS_NONINTERLACE);
dispx = MAINLOOP_DISPX;
	dispy = MAINLOOP_DISPY;
	BOOTLOG("[boot] GS_SetDispMode()\n");
	GS_SetDispMode(dispx,dispy, MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT);
BOOTLOG("[boot] GS_SetEnv()\n");
	GS_SetEnv(MAINLOOP_SCREENWIDTH, MAINLOOP_SCREENHEIGHT, FB0, FB1, GS_PSMCT32, Z0, GS_PSMZ16S);

	/* Use the legacy hard-coded VRAM layout that matches the iaddis
	   original SNESticle ELF (which renders correctly on real PS2 and
	   on NetherSX2). The previous gsKit-allocator path was the only
	   non-cosmetic divergence between iaddis's source and HEAD in the
	   PS2 render pipeline: gsKit_vram_alloc returns addresses *after*
	   its internal FB0/FB1/Z allocations, with its own padding and
	   alignment rules, so the returned TBPs would not match the
	   pre-Fase-1A layout (TEXADDR=0x2400, blender=0x3C00) that the
	   SNESticle blender was originally tuned against, and on the
	   PS2's GS the blender per-scanline writes would land in regions
	   that happened to alias FB0/FB1/Z0/font memory depending on the
	   gsKit version - producing the vertical-stripe tile corruption
	   visible on the SMW title screen since the very first successful
	   boot. Hard-coding to TEXADDR/0x3C00 reproduces iaddis's exact
	   working layout. */
	_MainLoop_uOutTexTBP  = TEXADDR;
	_MainLoop_uBlenderTBP = 0x3C00;
	printf("[boot] _OutTex TBP=0x%04X, Blender TBP=0x%04X (legacy layout)\n",
		(unsigned)_MainLoop_uOutTexTBP, (unsigned)_MainLoop_uBlenderTBP);

GPFifoInit((Uint128 *)_MainLoop_GfxPipe, sizeof(_MainLoop_GfxPipe));
    PolyInit();
    FontInit(FONT_TEX);

	// setup log screen
	_MainLoop_pLogScreen = new CLogScreen();
	_MainLoop_pLogScreen->SetMsgFunc(_MainLoopLogEvent);
	_MainLoopSetScreen(_MainLoop_pLogScreen);
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

	/* Boot banner: original SNESticlePS2 title + iaddis copyright
	   (replaces the #if 0 block above which depended on VersionGetInfo,
	   itself wrapped in #if 0 inside version.cpp), followed by the
	   ReyFxck fork credit. */
	ScrPrintf("SNESticlePS2 v0.3.4   %s  %s", __DATE__, __TIME__);
	ScrPrintf("Copyright (c) 1997-2004 Icer Addis");
	ScrPrintf("Forked By ReyFxck ~ Thomas R. (2026)");

	ScrPrintf("BootPath: %s", MainGetBootPath());
	ScrPrintf("BootDir: %s", MainGetBootDir());

	// set boot dir
	strcpy(_MainLoop_BootDir, MainGetBootDir());
    _MainLoopLoadModules(_MainLoop_IOPModulePaths);
	/* The legacy VramInit() bumped a software VRAM watermark used by
	   the now-deleted VramAlloc helper. gsKit owns the GS-side VRAM
	   allocator (gskit_backend.c::GSK_VramAllocTBP) so there is
	   nothing left to initialise here. */
_SJPCMMix = new SJPCMMixBuffer(32000, TRUE);
	#if CODE_DEBUG
    printf("MainLoopInit\n");
	#endif

	/* The original code does 120 * WaitForNextVRstart(1) here to let
	   the GS settle. On NetherSX2 VBlank interrupts may or may not
	   fire depending on what state the GS is in - if they don't, the
	   120-iter loop becomes an infinite hang. Reduce to 1 iter and
	   probe before/after so we can tell which side it died on. */
BOOTLOG("[boot] WaitForNextVRstart begin (120 iters)\n");
	{
		int loop = 60 * 2;
		while (loop--)
			WaitForNextVRstart(1);
	}
// create textures in main ram
    _fbTexture[0] = new CRenderSurface;
    _fbTexture[1] = new CRenderSurface;

    _fbTexture[0]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
    _fbTexture[1]->Alloc(256, 256,  PixelFormatGetByEnum(PIXELFORMAT_RGBA8));
    _fbTexture[0]->Clear();
    _fbTexture[1]->Clear();
	BOOTLOG("[boot] TextureNew(_OutTex)\n");
    TextureNew(&_OutTex, 256, 256, GS_PSMCT32);
    TextureSetAddr(&_OutTex, _MainLoop_uOutTexTBP);
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

	/* Phase 2 of the NES integration: instantiate the NES core +
	   register .nes / .fds / disksys.rom with the existing browser.
	   PathExtAdd feeds the same CBrowserScreen that already lists
	   .smc / .sfc, so the user gets one unified ROM picker.
	   The NesSystem::ExecuteFrame() body is still a STUB - selecting
	   a .nes paints a diagnostic test pattern. Real InfoNES wiring
	   comes in Phase 3. */
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
	_bMenu = FALSE;

//	while (1);
	// load snes palette
        _MainLoopLoadSnesPalette("mc0:/SNESticle/default.snpal");
	// load rom
	_MainLoopExecuteFile(_pRomFile, TRUE);
        _bMenu = _pSystem ? FALSE : TRUE;
        if (_MainLoop_bSjPCMReady)
        {
            SjPCM_Clearbuff();
            SjPCM_Play();
        }
	BOOTLOG("[boot] MainLoopInit: leave (bMenu=%d, sjpcm=%d, mcsave=%d)\n",
		(int)_bMenu, (int)_MainLoop_bSjPCMReady, (int)_MainLoop_bMCSaveReady);

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
